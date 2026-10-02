# JavaScriptCore on Phoenix-RTOS (browser track C, milestone B3)

WTF + JavaScriptCore from **WPE WebKit 2.54.0**, WebKit's `JSCOnly` CMake port, cross-built as one
static aarch64-phoenix ELF: the `jsc` shell. B3 is the go/no-go gate for the WebKit browser
([docs/browser/PLAN.md](../../../docs/browser/PLAN.md)). Developed outside the ports framework
(PLAN decision 3): `build.sh` + patches here, all output in a scratch directory.

Status 2026-10-02: **B3 GO on the Pi** (see "Pi result" below). The x86-64 Linux build of the same patched
tree with the same options (`--host-jsc`) runs everything below; its numbers are the reference.
**B9 (the JIT)**: `--jit` builds Baseline + DFG + FTL for Phoenix and the host; host results and the
pre-registered Pi check are in "JIT (browser B9)" at the end. Pi result pending.

## Build

```
tools/browser/jsc/build.sh --out <scratch>/out --dl <cache> -j12 \
    [--icu-prefix <ICU 78.3 install>] [--host-jsc] [--jit]
tools/browser/jsc/bench/fetch-bench.sh <scratch>/jsc-bench --dl <cache> --build-out <scratch>/out
```

`--out` must be outside the repository (~3 GB). Outputs: `<out>/jsc` (unstripped, for addr2line),
`<out>/jsc-stripped` (stage this), `<out>/mallocrate`, `<out>/mallocrate-mimalloc`, and with
`--host-jsc` `<out>/jsc-host`. With `--jit` (B9) the same names carry a `-jit` suffix (`<out>/jsc-jit`,
`<out>/jsc-jit-stripped`, `<out>/jsc-host-jit`; trees `src/webkit-jit`, `webkit-build-jit`), so both builds
can share one `--out`. Reads the tree sysroot and toolchain only; never writes into
`.buildroot`, `sources/` or the repo.

| Pinned input | Version | sha256 |
|---|---|---|
| WebKit | `wpewebkit-2.54.0.tar.xz` (wpewebkit.org; JSC is identical in the WebKitGTK tarball) | `efa9bcc3cb891c2d88f50eec710d9ccee71cbdf1040420361eb98c17355eb452` |
| mimalloc | 3.2.8, vendored in that tarball (`Source/bmalloc/mimalloc`) | (part of the tarball) |
| ICU4C | 78.3 (same as track A1's `icu` port) | `3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0` |
| host ruby (WebKit's generators, offlineasm) | 3.4.7, built because the host has none | `23815a6d095696f7919090fdc3e2f9459b2c83d57224b2e446ce1f5f7333ef36` |
| host libyaml (ruby's psych) | 0.2.5 | `c642ae9b75fee120b2d96c712538bd2cf283228d2337df2cf2988e3c02678ef4` |
| test262 | `7a096c205fd422ecba49a407d5ac4d1b3f842296` (the revision WebKit 2.54 imports) | `54088b23164536010a7b5cdfab8b0036e0b0220c110d37a1895fab9fc61b9486` |
| SunSpider | 1.0.2 at WebKit tag `webkitgtk-2.54.0` | per file, in `bench/fetch-bench.sh` |

ICU: without `--icu-prefix` the script builds a private ICU 78.3 with the **full** 33 MB data
(`patches/icu/`). Point `--icu-prefix` at track A1's install (its data is filtered to 11 MB for
WebKit) to get the binary the browser will ship; never at the tree's `_build/<target>` prefix,
whose `include/` holds every port's headers.

## Configuration (and why)

| Option | Value | Why |
|---|---|---|
| `PORT` | `JSCOnly`, `ENABLE_STATIC_JSC=ON` | JSC without WebCore; static archives into one ELF |
| interpreter | **asm LLInt**: `ENABLE_JIT=OFF`, `ENABLE_C_LOOP=OFF` | On ARM64 `ENABLE_C_LOOP` is 0 whenever `CPU(ARM64)` (WTF `PlatformEnable.h`), and the asm LLInt with the JIT compiled out needs no executable memory. It is the faster of the two interpreters (see the host CLoop comparison below) |
| DFG/FTL/WASM/YARR JIT | off | no executable memory (PLAN decision 7 / B9) |
| `ENABLE_SAMPLING_PROFILER`, remote inspector, API tests | off | sampling needs thread suspension with `ucontext` |
| allocator | **`USE_MIMALLOC=ON`** (WebKit's vendored mimalloc 3.2.8), `USE_SYSTEM_MALLOC=OFF`; on Phoenix the same mimalloc also **overrides malloc/free/new** | libpas/bmalloc needs `madvise` and reserve-then-commit with `PROT_NONE` (impossible on Phoenix, below). `USE_SYSTEM_MALLOC` alone would send every `fastMalloc` through libphoenix's allocator (one kernel mutex per call once threaded: P24). `USE_MIMALLOC` is WebKit's own supported mimalloc mode (default on 32-bit ARM, RISC-V and 64 KiB-page builds), so `fastMalloc` calls `mi_malloc` directly and the Structure heap is a mimalloc arena; the override makes ICU, libstdc++ and libc allocations use the same allocator. This is PLAN decision 4's "mimalloc linked as the system malloc", one instance instead of two |
| concurrent GC | **off by default on `OS(PHOENIX)`** (`Options.cpp`); `JSC_useConcurrentGC=true` turns it on | no `ucontext` in signal handlers yet (B4) |
| VM traps | polling (`ENABLE(SIGNAL_BASED_VM_TRAPS)` is off without the JIT) | no signal-based traps |
| main-thread stack | 8 MiB (`-z stack-size`, PT_GNU_STACK; aarch64 default `SIZE_USTACK` is 1 MiB) | JSC's recursion limits |
| WTF thread stacks | 1 MiB (libphoenix default is 256 KiB) | as WTF does for musl |
| flags | `-mcpu=cortex-a72 -mno-outline-atomics`, **no** `-mstrict-align` | EL0 alignment checking is off (`SCTLR_EL1.A=0`, kernel `hal/aarch64/_init.S`); the LLInt does unaligned bytecode loads regardless |

**The Phoenix memory model drives several choices.** On an MMU build every anonymous `mmap()` is
populated at once (`process->lazy` is 0, kernel `vm/map.c` `_vm_mmap` → `_map_force` per page), and
`mprotect()` can never add a permission the mapping did not have at `mmap` time (`map_checkProt`
against `protOrig`, `vm/map.c:1153`). There is no "virtual reserve". So:
- WTF's `OSAllocatorPOSIX` already reserves read-write on non-Linux systems (no `PROT_NONE`
  reserve-then-commit); nothing to change, but every "uncommitted" reservation is resident.
- The Structure heap reservation is 4 GiB upstream; Phoenix gets **64 MiB**
  (`JSC_structureHeapSizeInKB` overrides). Its aligned reservation maps twice that for a moment.
  64 MiB is the floor: mimalloc's arena uses whole 32 MiB chunks, and 32 MiB minus the first
  block leaves none, so 32 MiB aborts in `JSC::initialize()` (found on the Pi 2026-10-02;
  reproduced on the host with `JSC_structureHeapSizeInKB=32768`). Run the `--host-jsc` reference
  with `JSC_structureHeapSizeInKB=65536` so that it exercises the Phoenix heap size.
- mimalloc is told there is no virtual reserve and no overcommit; it maps everything read-write;
  commit/decommit/reset are no-ops (no `madvise`), and arenas grow in **32 MiB** steps instead of
  1 GiB. Freed memory goes back to the kernel only when a whole arena could be unmapped (never, in
  practice): RSS is a high-water mark.

## Patches

`patches/webkit/` (applied in order by `build.sh`, one commit each in `<out>/src/webkit`):

| Patch | What |
|---|---|
| 0001-wtf-os-phoenix | `OS(PHOENIX)` from `__phoenix__`, part of `OS(UNIX)`; CMake `CMAKE_SYSTEM_NAME=Phoenix`; ELF symbol syntax (`InlineASM.h`); `sysconf` core count; `HAVE(BACKTRACE)` (libphoenix `<execinfo.h>`), `HAVE(INT128_T)`; no `HAVE(STACK_BOUNDS_FOR_NEW_THREAD)` |
| 0002-wtf-threads-phoenix | `StackBounds` via `pthread_getattr_np()` of the calling thread; 1 MiB default thread stacks; `pthread_key_t` is a pointer (invalid key = `nullptr`); no `<sys/ucontext.h>` (`PlatformRegisters` = stack pointer) |
| 0003-wtf-memory-footprint-phoenix | `WTF::memoryFootprint()` = anonymous pages of the process's map entries (`meminfo()`); used by FastMalloc statistics and the shell's `MemoryFootprint()` / `--footprint` |
| 0004-jsc-phoenix | 64 MiB Structure heap; concurrent GC off by default; LLInt `globaladdr` ELF GOT form + opcode debug labels as Linux (`offlineasm/arm64.rb`, `LowLevelInterpreter.cpp`); no `mincore` / `dl_iterate_phdr` paths; `ARM64Assembler::cacheFlush` case |
| 0005-mimalloc-phoenix | mimalloc unix prim on Phoenix (no virtual reserve/overcommit/madvise, RW mappings, 39-bit VA, weak random seed, short `struct rusage`); WebKit's mimalloc wrapper: override malloc on Phoenix, no `-march=armv8.1-a`, 32 MiB arenas |

`patches/webkit-jit/` (B9, applied after `patches/webkit/` only by `build.sh --jit`; a separate series so
that the WPE build, which applies `patches/webkit/*`, is unchanged until it takes the JIT on purpose):

| Patch | What |
|---|---|
| 0001-wtf-jsc-machine-context-phoenix | `HAVE(MACHINE_CONTEXT)` on `OS(PHOENIX) && CPU(ARM64)`; `PlatformRegisters` = the Linux-layout `mcontext_t` from `<sys/ucontext.h>` (drops 0002's "no ucontext" case); `OS(PHOENIX)` on the six Linux branches of `MachineContext.h`; concurrent GC back to the upstream default (drops 0004's override) |
| 0002-jsc-jit-phoenix | DFG on by default for ARM64 Phoenix (as Linux/FreeBSD); **32 MiB** executable pool instead of ARM64's 512 MiB (resident on Phoenix); `ARM64Assembler::cacheFlush` takes the Linux path (`__builtin___clear_cache`, replacing 0004's case and its stale UCI comment); `useWasm` defaults to false on Phoenix |
| 0003-jsc-inline-cache-compiler-jump-types | upstream bug, not Phoenix-specific: `InlineCacheCompiler.h` names `CCallHelpers::Jump` with `CCallHelpers` only forward-declared; `LLIntOffsetsExtractor.cpp` with the JIT on (GCC 16) fails. Declared through `MacroAssembler::Jump`, the same type |

`patches/icu/0001` (private ICU only): `LC_MESSAGES` fallback in `putil.cpp` (A1's port carries its
own equivalent, `02-phoenix-lc-messages.patch`).

## Local shims (`compat/`) — each one is a libphoenix gap, listed for its track

`build.sh` probes the sysroot's `libphoenix.a` symbols and headers and compiles/installs **only the
shims still needed** (it logs `shim <NAME>` and the compat header list), so the same tree builds
before and after libphoenix gains them. Against libphoenix `b20-merge` (B1/B2/P24, next
full-clean build) the probes drop: `pthread_getattr_np` (b20's works for any live thread, so
`HAVE(STACK_BOUNDS_FOR_NEW_THREAD)` can then be re-enabled in patch 0001/0002), `sem_*` +
`<semaphore.h>`, `madvise` (b20 refuses `MADV_DONTNEED` with EINVAL; every WebKit caller loops
only on EAGAIN and mimalloc's Phoenix prim never calls it, so that is fine), `<fenv.h>`,
`<uchar.h>`, `LC_MESSAGES`. Still needed with b20: `msync`, `MAP_FILE`, the `stdint.h` fix (b20
still has `UINT8_MAX (0xffU)`), and the `_malloc_init` hook.

| Shim | Track | Notes |
|---|---|---|
| `pthread_getattr_np()` | **B1** | calling thread only: finds the map entry holding a local variable via `meminfo()` (every stack is its own anonymous entry). Once libphoenix has it, drop the `extern "C"` declaration in `StackBounds.cpp` (patch 0002); if it works for **other** threads too, `HAVE(STACK_BOUNDS_FOR_NEW_THREAD)` can come back |
| `<semaphore.h>`, `sem_*()` | **B1** | mutex + condvar; `sem_post` is NOT async-signal-safe. Only `Thread::suspend()` uses it (from a signal handler); unreachable with concurrent GC and the sampling profiler off |
| `madvise()` | **B1** | accepts NORMAL/RANDOM/SEQUENTIAL/WILLNEED/DONTNEED, does nothing (bmalloc headers and WTF call it; mimalloc's Phoenix prim does not) |
| `msync()` | **B1** | `ENOSYS`. P21 (no shared file mappings) means WTF's `FileSystem::mapToFile()` (JSC bytecode disk cache, WebKit's network cache) cannot work as written: a B6 item |
| `<fenv.h>` | **B1** | libphoenix ships libmcs's `#error` stub; this is a full header-only AArch64 implementation (FPCR/FPSR). WTF's SIMDe includes it |
| `<uchar.h>` (types only) | **B1** | without it WTF's bundled simdutf C header `#define`s `char16_t` and breaks the C++ keyword |
| `<locale.h>` `LC_MESSAGES` | **B1** | libphoenix's `setlocale()` returns NULL for it, which WTF handles |
| `<sys/mman.h>` `MAP_FILE` | **B1** | 0, as on Linux/BSD |
| `<stdint.h>` `UINT8_MAX`/`UINT16_MAX` | **B1 (libphoenix bug)** | libphoenix defines them `0xffU`/`0xffffU` (unsigned int); C11 7.20.2 requires int. Signed comparisons against them silently become unsigned (`int16_t t = -5; t > UINT8_MAX` is true): WTF's SIMDe saturation code and JSC's `PropertyTable::canFitInCompact()` have exactly that shape; GCC 16 flagged 397 such comparisons here before the fix. Fix it in libphoenix; every port is exposed |
| `_malloc_init()` (weak, empty) | not a gap | keeps libphoenix's `malloc_dl.o` (and its duplicate `malloc`) out of the link; `build.sh` fails if `malloc_common` shows up in `jsc` |

The B2 `ucontext_t` (build 20) and the JIT are consumed by `patches/webkit-jit/` (section "JIT
(browser B9)"). The non-JIT build above still has 0002's stack-pointer-only `PlatformRegisters` and
concurrent GC off.

Link hazard from A1 (libstdc++.a's own `hypotf` vs libphoenix libm): not hit by this link.

## Results

Build host: 16 threads, 29 GiB, `-j12`.

| | Value |
|---|---|
| clean WebKit build (configure + `ninja jsc`, 2670 steps) | **396–502 s** at `-j12` (3 clean runs); + ~150 s once for the host ruby/libyaml; + ~4 min for the private ICU when no `--icu-prefix` |
| `jsc` stripped / unstripped, with A1's filtered ICU (11 MB data) | **28,858,928 B** / 33,117,544 B (`text` ≈ 28.5 MB of it) |
| `jsc` stripped with the private full-data ICU (33 MB data) | 51,022,256 B |
| ELF | static, 2 PT_LOAD (4 KiB aligned), PT_GNU_STACK 8 MiB, 0x18-byte TLS segment |
| build warnings | 171, all GCC 16's new `-Wsfinae-incomplete` in upstream WTF/JSC headers (none from the Phoenix patches; the host GCC 15 build has none). Before `compat/include/stdint.h` there were also 397 `-Wsign-compare`, all from libphoenix's unsigned `UINT8_MAX` (see shims) |
| allocator check | `build.sh` fails if `malloc_common` (libphoenix `malloc_dl.o`) is linked into `jsc` or `mallocrate-mimalloc`; `malloc` == `mi_malloc` in the final ELF |

Host reference (`--host-jsc`: same tree, same options, x86-64 Linux, GCC 15, system ICU 78.2,
AMD Ryzen 7 PRO 250; `TZ` does not change any result):

| Run | Host result |
|---|---|
| `jsc -e 'print(1+1)'` | `2` |
| test262 subset (7808 runs of 4847 files, 779 files skipped) | **pass=7791 fail=17**, 11–20 s. The 17 are `bench/test262-host-reference.txt`: 9 `eval-code` "no global arguments binding" (the shell's own global `arguments`), 4 `Function.prototype.arguments/caller` (JSC legacy accessors), 2 `Proxy/apply/null-handler-realm`, `tco-non-eval-with` (stack), `for-in/identifier-let-allowed...` |
| SunSpider 1.0.2, mean of 3 passes after a warm-up | **283–431 ms** per pass across sessions (frequency scaling; 345 ms in the first run, regexp-dna 76 ms the largest test) |
| `micro.js` | geomean **29.2 ms**, total 476 ms; checksums object-churn 560003, property-access 0, string-build 355600, json 20000, regexp 8020, array-sort 494582, closures 49997, map-set 682832, typed-array 349946 |
| `mallocInALoop` (fastMalloc → mimalloc, 2 KiB) | 35 ns per malloc+free pair |
| footprint (Linux RSS, lazy) | 121 MB current, 144 MB peak |

Interpreter choice, measured on the host (same tree and options, `-DENABLE_C_LOOP=ON` vs the asm
LLInt, both pinned to one core, alternated, 10 SunSpider passes each): **asm LLInt 283–314 ms vs
CLoop 385–434 ms per SunSpider pass (~28 % less time), micro geomean 25.5–27.0 vs 31.3–33.4 ms
(~20 %)**; both produce the same checksums. The CLoop was not built for Phoenix (no need: the asm
LLInt built and needs no executable memory either). Host timings vary ±15 % with frequency
scaling; compare only runs from one session.


## Pi check (pre-registered, B3 gate)

Stage the stripped binary and the bundle (built by `bench/fetch-bench.sh`; both are in the B3
scratch dir, paths in the hand-off) on the netboot NFS root, e.g. `/usr/bin/jsc` and
`/usr/share/jsc-bench/`. psh does **not** strip quotes and has no `;`, pipes or loops, so every
command is one line with no quotes (`jsc -e 'print(1+1)'` would evaluate a string literal and print
nothing). One boot, in this order; capture the UART for the whole sequence (≈ 6–10 min of
user-space; split across cycles if needed, keeping the order).

| # | Command at `(psh)%` | Expected | Measures |
|---|---|---|---|
| 1 | `/usr/bin/jsc -e print(1+1)` (if psh mangles the parentheses: `/usr/bin/jsc /usr/share/jsc-bench/hello.js`) | `2`, back to the prompt | the shell starts: WTF/JSC init, StackBounds via meminfo, mimalloc override, ICU, Structure heap, LLInt |
| 2 | `/usr/bin/jsc --footprint /usr/share/jsc-bench/micro.js` | 9 kernel lines whose **checksums equal the host's** (table above), `MICRO ... geomean-ms=`, `MALLOC fastMalloc ns/pair=`, `FOOTPRINT current= peak= gc-heap=`, then `Memory Footprint:` (current/peak) | interpreter correctness + speed, fastMalloc rate, footprint (RSS) |
| 3 | `/usr/bin/jsc /usr/share/jsc-bench/sunspider-run.js -- /usr/share/jsc-bench/sunspider 3` | 26 test lines and `SUNSPIDER runs=3 total-ms=...` | SunSpider score |
| 4 | `/usr/bin/jsc /usr/share/jsc-bench/test262-run.js -- /usr/share/jsc-bench/test262-subset.json /usr/share/jsc-bench/t262-fail.txt` | `TEST262 rev=7a096c205fd4 runs=7808 pass=7791 fail=17 ...`; the file lists the failures | conformance; on the host diff only the `FAIL` lines (`grep ^FAIL`) of `t262-fail.txt` and `bench/test262-host-reference.txt` (both end with a summary line) |
| 5 | `/usr/share/jsc-bench/mallocrate 4 200000` | 12 `MALLOCRATE impl=libphoenix ...` lines | system-malloc cost before/after P24 |
| 6 | `/usr/share/jsc-bench/mallocrate-mimalloc 4 200000` | 12 `MALLOCRATE impl=mimalloc ...` lines | the allocator jsc uses, same workload |

**GO** (WebKit stages B4+ proceed) needs all of:
- G1: step 1 prints `2` and returns to the prompt.
- G2: every step completes; **zero** exception dumps (`Exception #`, EL0/EL1 fault lines) in the UART
  log for the whole sequence; no hang (no output for 5 min inside a step).
- G3: all 9 `micro.js` checksums equal the host values.
- G4: test262 `pass >= 7713` (host 7791 minus 1 % of the runs), and every failure not in the
  host list is listed and triaged in the B3 doc. Expected: identical to the host. Candidates for a
  difference: stack-depth tests (`RangeError` with 8 MiB main stack), anything touching `Date`/time
  zone or `Math` precision (libphoenix libm).
- G5: SunSpider completes all 26 tests.

**Recorded for the decision, no pass threshold** (the owner's go/no-go on speed and on P24):
SunSpider ms per pass (the host's ~300–430 ms is a different machine; an A72 interpreter is expected
around 5–8× slower, so ≈ 2–3 s; > 10 s per pass is the "too slow to be usable for B5" flag), the
`micro.js` geomean, `FOOTPRINT current/peak` (expected 80–200 MB: the 64 MiB Structure heap, 32 MiB
mimalloc arenas, an 8 MiB main stack and 1 MiB per WTF thread are all resident from the start on
Phoenix; > 400 MB is a flag), `MALLOC fastMalloc ns/pair` (mimalloc; expect tens of ns), and the
`mallocrate` pairs: libphoenix `phase=single-threaded-process` and `phase=4-threads` against
mimalloc's. That ratio is the measured cost P24 removes for everything that still uses the system
allocator (GLib, libsoup3, Mesa in the browser processes); jsc itself no longer pays it.

**NO-GO** (stop and fix before B4): any fault or hang; a checksum mismatch; test262 below the bar;
or a failure of step 1 (the port's platform layer is wrong). A slow SunSpider alone is not a
NO-GO for B4 (headless WebCore), but it is the input for B9 (JIT) priority.

Triage aids: `<out>/jsc` is unstripped (`addr2line -e <out>/jsc <pc>`; LLInt opcodes have
symbol names); `JSC_useConcurrentGC=false` is already the default; `JSC_structureHeapSizeInKB=`
and `WTF_numberOfProcessorCores=1` (one GC marker thread) are the two knobs to try first if
memory or threading misbehaves.


## Pi result (2026-10-02, build 20 netboot, log `rpi4b-uart-20261002-*-b3-jsc2.log`): **GO**

The first Pi run aborted in `JSC::initialize()` on every VM start (SIGABRT, no output). The cause was the
32 MiB Structure heap, which is smaller than one mimalloc arena chunk (see "Platform layer"). With the
64 MiB heap, all five conditions hold in one boot:

| Gate | Result |
|---|---|
| G1 | `jsc hello.js` prints `2`, rc 0 |
| G2 | 0 exception dumps / `PHX-ABORT` lines in the whole boot; every step rc 0 |
| G3 | all 9 `micro.js` checksums equal the host's |
| G4 | `TEST262 runs=7808 pass=7791 fail=17 seconds=187.8`; the 17 `FAIL` lines are identical to `bench/test262-host-reference.txt` |
| G5 | SunSpider: all 26 tests complete |

Recorded for the decision:

| Measure | Pi 4 (A72, LLInt) | Host | Ratio |
|---|---|---|---|
| SunSpider ms per pass (3 passes) | 4857, 4757, 3575 (mean 4397) | 283–431 | ~12× |
| `micro.js` geomean / total | 345.9 / 4594 ms | 29.2 / 476 ms | ~12× |
| test262 subset wall time | 187.8 s | 11–20 s | ~12× |
| `MALLOC fastMalloc ns/pair` (2 KiB, mimalloc) | 533.8 | 35 | — |
| footprint (anonymous pages) | ≈ 286 MB (the line read 8 874 676 222: two "no anon map" entries counted as 4 GiB each, fixed in patch 0003 after this run) | — | — |

`mallocrate` (ns per malloc+free pair, 16 / 64 / 256 / 2048 B):

| | single-threaded process | 4 threads |
|---|---|---|
| libphoenix (P24 in) | 699 / 1140 / 2294 / 23171 | 5728 / 5286 / 6483 / 105148 |
| mimalloc | 24 / 26 / 26 / 274 | 40 / 35 / 45 / 1988 |

Reading:
- The interpreter is ~12× the host on every measure. That makes B9 (JIT) the main speed lever, but it is not a B4 blocker.
- The system allocator is still 30–150× slower than mimalloc, and up to 100 µs per 2 KiB pair under 4 threads. Everything in the browser processes that still calls libphoenix `malloc` (GLib, libsoup3, Mesa) pays that. See KNOWN-ISSUES P26 and the allocator follow-up.
- fastMalloc inside `jsc` measured 534 ns per 2 KiB pair, against 274 ns for the same mimalloc in `mallocrate`. Not chased yet.



## JIT (browser B9)

`build.sh --jit` builds the Baseline JIT, the DFG, the FTL (B3/Air, no LLVM) and the YARR regexp
JIT, with the same allocator, ICU and flags as B3, and applies `patches/webkit-jit/` on top of
`patches/webkit/`. One binary covers both sides of the comparison: `--useJIT=false` turns every JIT
off at run time (LLInt only, polling traps, no regexp JIT), which is the B3 configuration.

How each OS dependency is met:

| Need | On Phoenix |
|---|---|
| executable memory | WTF's `OSAllocatorPOSIX` already reserves the pool `PROT_READ\|PROT_WRITE\|PROT_EXEC`, `MAP_PRIVATE\|MAP_ANON`, in one `mmap` at JSC initialization on non-Linux systems; the two guard pages are `MAP_FIXED PROT_NONE` remaps (kernel `_vm_mmap` unmaps and maps). No `SEPARATED_WX_HEAP`, no `mprotect` (Phoenix's cannot add `PROT_EXEC`, kernel `vm/map.c` `map_checkProt`). `commit`/`decommit` are no-ops (no `MADV_*` on this OS in WTF), so freed JIT memory stays resident and is reused by the pool's own allocator |
| pool size | **32 MiB** (patch 0002), resident from JSC initialization; ARM64's default is 512 MiB. 25 % of it is held back by JSC; when it fills, JSC stops compiling (it does not fail). Host check: SunSpider and the test262 subset pass and run as fast with a 2 MiB pool. `--jitMemoryReservationSize=<bytes>` overrides. With one region the jump islands collapse to nothing |
| instruction cache | `__builtin___clear_cache` → libgcc `__aarch64_sync_cache_range`: `mrs ctr_el0`, `dc cvau` per line, `dsb ish`, `ic ivau` per line, `dsb ish`, `isb` (disassembled from `jsc-jit`). Legal at EL0: the kernel sets `SCTLR_EL1.UCI` (bit 26) and `UCT` (bit 15) on every core (`hal/aarch64/_init.S`, the M\|C\|I write in `el1_entry`, before secondaries park; `HCR_EL2` traps nothing but RW). The README's earlier "UCI=0" note read only the pre-MMU baseline value. lwip's genet driver already does `dc civac` at EL0 on every frame, and Quake III's JIT uses the same builtin. `IC IVAU` is broadcast to the inner shareable domain, so code written on a compiler thread is visible on the mutator's core; JSC issues `isb` (`crossModifyingCodeFence`) where it needs one. No kernel change |
| registers of a suspended thread | `HAVE(MACHINE_CONTEXT)` + the Linux `MachineContext.h` branches (patch 0001): `uc_mcontext.regs[]/sp/pc` (B2 layout). `Thread::suspend()` = `pthread_kill(SIGUSR1)` (thread-directed `sys_tkill`) → handler `sem_post` (libphoenix: one lock-free `write()`, async-signal-safe) → `sigsuspend` (atomic mask+sleep under the scheduler lock) → second `SIGUSR1` resumes. Both halves are covered by `phoenix-rtos-tests` `test-libc-signal`: `siginfo.pthread_kill_target_context` (a sleeping thread) and `siginfo.pthread_kill_running_thread` (new: a thread spinning in user space, as JIT code does; it must stand still while parked and run on after) |
| concurrent GC | on (upstream default; patch 0001 drops B3's override). See risks: `SA_RESTART` |
| VM traps | `SIGNAL_BASED_VM_TRAPS` (on with DFG + machine context): only for asynchronous termination/watchdog/debugger requests, none of which the benchmarks make. The halt is a `dc zva` to address 0 → caught `SIGSEGV` with ucontext (kernel prints one `vm: SIGSEGV caught by pid ...` line per fault, no register dump). `--usePollingTraps=true` is the fallback |
| WebAssembly | compiled in (WebKit 2.54's B3 and FTL do not build without it: B3 `Wasm*Value`, FTL `compileCallWasm`), **off** at run time on Phoenix (patch 0002): wasm memories reserve address space to commit later. `--useWasm=true` for experiments only |
| CPU features | no `AT_HWCAP` on Phoenix: `collectCPUFeatures()` falls back to the compile-time `-mcpu=cortex-a72` answer (no LSE, JSCVT, FP16, FRINT, SHA3), which is right for the A72. `x18` stays reserved (the non-Linux register set) |
| thread stacks | 1 MiB for every WTF thread including the JIT compiler threads (Darwin's are 512 KiB) |

Sizes: `jsc-jit` stripped 47,105,464 B / 55,093,488 B unstripped (B3 `jsc`: 28.9 MB; the JIT, B3/Air and
the dormant WebAssembly tiers add ~18 MB of text). Clean cross build from the patch files 674 s at `-j8` (sources identical to the development tree the host numbers below come from). Build warnings:
175, all GCC 16's upstream `-Wsfinae-incomplete`.

### Host reference (x86-64 `jsc-host-jit`, same tree and options)

Run with `JSC_structureHeapSizeInKB=65536 ... --forceRAMSize=4000000000`: with the 64 MiB Structure heap
the host needs a Pi-like RAM size too, otherwise its heap-growth policy (29 GiB of RAM) lets the
Structure heap fill and test262 aborts with `MemoryExhaustion` — the LLInt-only `jsc-host` of B3 does the
same. That is a margin to watch on the Pi as well (risks).

| Run | `--useJIT=false` (LLInt) | JIT (default options) | ratio |
|---|---|---|---|
| SunSpider ms/pass (mean of 3) | 291.3 | 67.3 (66.1 with `--useConcurrentGC=false`) | 4.3× |
| `micro.js` geomean / total ms | 22.3 / 380.5 | 5.4 / 75.5 | 4.1× |
| test262 subset | 7791/17, 7.9 s | 7791/17, 4.9 s | — |

`micro.js` checksums are the B3 values in every mode. test262's 17 `FAIL` lines equal
`bench/test262-host-reference.txt` in all five modes run: default, `--useJIT=false`,
`--useConcurrentGC=false`, `--forceEagerCompilation=true` (14.1 s), `--collectContinuously=true`
(10.4 s). `jit-check.js` (new, below): `JITTIER loop=546383 poly=153593 regexp=224975` in every mode;
with the JIT `dfg-compiles=1 poly-dfg-compiles=3` (the OSR exits recompile `poly` twice), with
`--useJIT=false` both read 1000000 (the shell's "pretend compiled").

### Pi check (pre-registered, B9 gate)

Stage `<out>/jsc-jit-stripped` as `/usr/bin/jsc-jit` (next to B3's `/usr/bin/jsc`, which stays) and
`bench/jit-check.js` into the B3 bundle, `/usr/share/jsc-bench/jit-check.js` (the rest of the bundle is
unchanged). One boot, in this order; psh rules as for B3 (no quotes; options are `--name=value`
arguments, so no environment is needed). Expected user-space time ≈ 10–15 min; split across boots
only between whole steps.

| # | Command at `(psh)%` | Expected | Measures |
|---|---|---|---|
| 1 | `/usr/bin/jsc-jit /usr/share/jsc-bench/hello.js` | `2` | starts: the 32 MiB RWX pool is mapped |
| 2 | `/usr/bin/jsc-jit /usr/share/jsc-bench/jit-check.js` | `JITCHECK useJIT=1 baseline=1 dfg=1 ftl=1 regexp=1 concurrentJIT=1 concurrentGC=1 pollingTraps=0 pool=0`, `JITTIER loop=546383 dfg-compiles=<1..999999> poly=153593 poly-dfg-compiles=<n> regexp=224975`, `JITCHECK result=PASS` | the JIT is really on and reaches the DFG; OSR exits |
| 3 | `/usr/bin/jsc-jit --useJIT=false --footprint /usr/share/jsc-bench/micro.js` | the 9 B3 checksums, `MICRO ...`, `FOOTPRINT ...` | LLInt baseline, this boot |
| 4 | `/usr/bin/jsc-jit --footprint /usr/share/jsc-bench/micro.js` | the same 9 checksums, `MICRO ...` | JIT speed, footprint with the pool |
| 5 | `/usr/bin/jsc-jit --useJIT=false /usr/share/jsc-bench/sunspider-run.js -- /usr/share/jsc-bench/sunspider 3` | 26 test lines, `SUNSPIDER runs=3 total-ms=...` | LLInt baseline |
| 6 | `/usr/bin/jsc-jit /usr/share/jsc-bench/sunspider-run.js -- /usr/share/jsc-bench/sunspider 3` | 26 test lines, `SUNSPIDER runs=3 total-ms=...` | JIT speed |
| 7 | `/usr/bin/jsc-jit /usr/share/jsc-bench/test262-run.js -- /usr/share/jsc-bench/test262-subset.json /usr/share/jsc-bench/t262-jit-fail.txt` | `TEST262 rev=7a096c205fd4 runs=7808 pass=7791 fail=17 ...` | conformance with all tiers + concurrent JIT + concurrent GC |
| 8 (stress) | `/usr/bin/jsc-jit --forceEagerCompilation=true /usr/share/jsc-bench/micro.js` | the 9 checksums | every function tiers up after ~10–20 calls: Baseline → DFG → FTL, OSR entry/exit |
| 9 (stress) | `/usr/bin/jsc-jit --forceEagerCompilation=true /usr/share/jsc-bench/jit-check.js` | step 2's checksums, `result=PASS` | same, on the OSR-exit kernel |
| 10 (stress) | `/usr/bin/jsc-jit --collectContinuously=true /usr/share/jsc-bench/test262-run.js -- /usr/share/jsc-bench/test262-subset.json /usr/share/jsc-bench/t262-cgc-fail.txt` | `TEST262 ... pass=7791 fail=17` | the collector thread runs back to back, suspending and resuming the mutator (signals + ucontext) while JIT code runs |
| 11 (if the image has `phoenix-rtos-tests` branch `b9-jit-tests` with `--with-tests`) | `/bin/test-libc-jit` then `/bin/test-libc-signal` | `5 Tests 0 Failures 0 Ignored` with the line `without a flush N of 1000 rewrites ran the old instruction` (N > 0 on the A72); signal: 0 failures | EL0 cache maintenance; suspend of a spinning thread |

**PASS** needs all of:
- J1: steps 1–10 each return to the prompt; **zero** `Exception #` dumps, `PHX-ABORT` lines, `vm: SIGSEGV caught` lines and `ASSERTION FAILED`/`MemoryExhaustion` lines in the boot; no step silent for 5 min.
- J2: step 2 prints the `JITCHECK useJIT=1 ... dfg=1 ftl=1` line and `JITCHECK result=PASS`; the three `JITTIER` checksums equal the host's (steps 2 and 9).
- J3: all 9 `micro.js` checksums equal the B3/host values in steps 3, 4 and 8.
- J4: test262 `pass >= 7713` in steps 7 and 10, and their `FAIL` lines equal `bench/test262-host-reference.txt` (any extra one is triaged in the B9 doc).
- J5 (the speed gate, PLAN B9 "≥ 3× B3", SunSpider and `micro.js` as the JetStream-lite proxy): step 6's `total-ms` ≤ step 5's / 3, and step 4's `geomean-ms` ≤ step 3's / 3. For orientation, B3's 4.4 s per SunSpider pass would mean ≤ 1.47 s.
- J6: step 11, when run: 0 failures, and N > 0 (otherwise the flush checks prove nothing on this board).

Recorded, no threshold: SunSpider and micro ratios, test262 seconds (B3: 187.8 s), `FOOTPRINT` of steps 3/4
(B3 ≈ 286 MB; expect ≈ +32 MiB pool + JIT data + compiler-thread stacks), step 10's seconds.

NO-GO, and the first thing to try: a hang or `vm: SIGSEGV caught` in JIT code → rerun the step with
`--usePollingTraps=true`; a hang or wrong result in steps 7/10 only → `--useConcurrentGC=false`
(then it is the suspend path: check step 11); a crash in compiled code → `--useFTLJIT=false`, then
`--useDFGJIT=false` to find the tier; `MemoryExhaustion` → rerun with `--structureHeapSizeInKB=131072`
(the option behind `JSC_structureHeapSizeInKB`; checked on the host).
`addr2line -e <out>/jsc-jit <pc>` for any pc outside the pool; a pc inside the pool (the JIT region is
logged with `--verboseExecutablePoolAllocation=true`) is generated code.

Risks:
- **`SA_RESTART` is not implemented** by the kernel (`<phoenix/signal.h>`: FIXME). A suspend signal that
  lands in a system call other than a futex/condvar wait (which treat `EINTR` as a spurious wake) can
  return `EINTR` where Linux would restart. libphoenix already retries `EINTR` in its read/write/lookup
  helpers. In `jsc` the collector suspends the mutator while it is parked at a safepoint, so this is not
  expected here; in the browser, a WebProcess main thread idle in `poll()` (GLib retries) is the case to
  watch. If concurrent GC misbehaves, `--useConcurrentGC=false` restores the B3 behaviour.
- **FP/SIMD registers are not in the `mcontext_t`** (Linux's `__reserved` area is not provided), so the
  conservative scan of a suspended thread sees x0–x30/sp/pc but not d8–d15. A pointer that GCC spilled
  to a SIMD register of a suspended thread would be missed; rare for C++, impossible for JSValues in JIT
  code (Air keeps them in GPRs). Fix if it ever shows: deliver the FP state in the ucontext (kernel).
- **Signal latency**: a thread running JIT code takes the suspend signal at its next preemption (the
  scheduler tick), not at once: GC pause time grows by up to one tick per suspended thread.
- **RWX code memory** (owner-approved): no W^X; a memory-corruption bug can write code.
- **Resident pool**: 32 MiB per JIT-enabled process from start-up, used or not.
- **Structure heap margin**: 64 MiB passes test262 on the Pi (B3) and on the host only with a Pi-like
  RAM size; JIT tiers keep more structures alive. Watch J1's `MemoryExhaustion`.
- **EL0 `brk`** (JIT `abortWithReason`, B3 `Oops`) is killed by the kernel with a `BRK (AA64)` exception
  dump instead of a `SIGTRAP`; only matters for crash reporting.
- The WPE build does not have any of this until it applies `patches/webkit-jit/` and the `--jit` CMake
  options (and rebuilds); this check is the gate for doing so.

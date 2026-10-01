# JavaScriptCore on Phoenix-RTOS (browser track C, milestone B3)

WTF + JavaScriptCore from **WPE WebKit 2.54.0**, WebKit's `JSCOnly` CMake port, cross-built as one
static aarch64-phoenix ELF: the `jsc` shell. B3 is the go/no-go gate for the WebKit browser
([docs/browser/PLAN.md](../../../docs/browser/PLAN.md)). Developed outside the ports framework
(PLAN decision 3): `build.sh` + patches here, all output in a scratch directory.

Status 2026-10-01: **builds; not yet run on the Pi.** The x86-64 Linux build of the same patched
tree with the same options (`--host-jsc`) runs everything below; its numbers are the reference.

## Build

```
tools/browser/jsc/build.sh --out <scratch>/out --dl <cache> -j12 \
    [--icu-prefix <ICU 78.3 install>] [--host-jsc]
tools/browser/jsc/bench/fetch-bench.sh <scratch>/jsc-bench --dl <cache> --build-out <scratch>/out
```

`--out` must be outside the repository (~3 GB). Outputs: `<out>/jsc` (unstripped, for addr2line),
`<out>/jsc-stripped` (stage this), `<out>/mallocrate`, `<out>/mallocrate-mimalloc`, and with
`--host-jsc` `<out>/jsc-host`. Reads the tree sysroot and toolchain only; never writes into
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
- The Structure heap reservation is 4 GiB upstream; Phoenix gets **32 MiB** (~250k Structures;
  `JSC_structureHeapSizeInKB` overrides). Its aligned reservation maps twice that for a moment.
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
| 0004-jsc-phoenix | 32 MiB Structure heap; concurrent GC off by default; LLInt `globaladdr` ELF GOT form + opcode debug labels as Linux (`offlineasm/arm64.rb`, `LowLevelInterpreter.cpp`); no `mincore` / `dl_iterate_phdr` paths; `ARM64Assembler::cacheFlush` case |
| 0005-mimalloc-phoenix | mimalloc unix prim on Phoenix (no virtual reserve/overcommit/madvise, RW mappings, 39-bit VA, weak random seed, short `struct rusage`); WebKit's mimalloc wrapper: override malloc on Phoenix, no `-march=armv8.1-a`, 32 MiB arenas |

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

For **B4 (SA_SIGINFO/ucontext)**: when `ucontext_t` lands with Linux's aarch64 `uc_mcontext`
layout (`fault_address`, `regs[31]`, `sp`, `pc`, `pstate`), add `OS(PHOENIX)` to
`HAVE(MACHINE_CONTEXT)` (`PlatformHave.h`), to the `OS(LINUX)` branches of
`Source/JavaScriptCore/runtime/MachineContext.h`, and drop the `OS(PHOENIX)` case in
`PlatformRegisters.h`; `Thread::suspend()` then reads real registers, and concurrent GC can be
tested with `JSC_useConcurrentGC=true` before flipping the default. `Thread::suspend` already uses
`pthread_kill(SIGUSR1)` + `sigsuspend`, which libphoenix has; the handler is installed with
`SA_SIGINFO` today (the kernel accepts the flag and calls it with the signal number only).

For **B9 (JIT)**: besides the RWX `ExecutableAllocator` region (PLAN decision 7), the kernel runs EL0
with `SCTLR_EL1.UCI=0` (`hal/aarch64/_init.S` baseline), so the `DC CVAU`/`IC IVAU` that
`__builtin___clear_cache` emits would trap; JIT code needs `UCI=1` or a cache-maintenance call.

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
| 4 | `/usr/bin/jsc /usr/share/jsc-bench/test262-run.js -- /usr/share/jsc-bench/test262-subset.json /usr/share/jsc-bench/t262-fail.txt` | `TEST262 rev=7a096c205fd4 runs=7808 pass=7791 fail=17 ...`; the file lists the failures | conformance; diff `t262-fail.txt` against `bench/test262-host-reference.txt` on the host |
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
`micro.js` geomean, `FOOTPRINT current/peak` (expected 80–200 MB: the 32 MiB Structure heap, 32 MiB
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

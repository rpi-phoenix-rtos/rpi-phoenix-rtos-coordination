# What the Raspberry Pi 4 fork changed in Phoenix-RTOS

**Audience:** Phoenix-RTOS maintainers. **Purpose:** so you can see, without reading 1 500
commits, what this fork contains and which parts might be worth your attention.

This is a downstream fork that ports Phoenix-RTOS to the Raspberry Pi 4 (BCM2711, Cortex-A72,
aarch64). It pulls from canonical `phoenix-rtos` and is maintained separately; nothing here is
submitted upstream. The work falls into five kinds, and the distinction matters to you:

1. **Fixes to defects in your code** that are not Pi-specific and would bite any target. These
   are marked **★** throughout, and the shortlist is directly below.
2. **Bring-up changes to existing code** so it runs on this SoC (aarch64 HAL, pmap, timers, SMP,
   boot).
3. **New drivers** for Pi 4 on-board hardware — new code in new directories, of interest mainly
   if you care about this board.
4. **Application and game ports**, whose carried patches are evidence of platform gaps.
5. **Programs built on Phoenix rather than changes to it** — hardware experiments that live in
   the coordination repo, not in your repositories. They are in here because of what they say
   about Phoenix's userspace driver model, and they are kept in their own section at the end.

## Scope, honestly

Raw diff against `origin/master` is ~**1 540 commits / ~890 files / ~150 000 insertions** across
14 repos. **That number overstates the authored work and should not be quoted.** Two large
blocks are not ours:

| excluded | insertions | what it is |
|---|---|---|
| `libphoenix/math/` + `libm/` | +28 472 | upstream's own vendored libmcs and math reorg, carried ahead of master |
| `vkquake/glue/vkquake_shaders.c` | +30 506 | machine-generated SPIR-V bytecode as C arrays |

Hand-written surface is therefore roughly **90 000 insertions**. Fork-authored libphoenix alone
is **93 files / +5 874**, not the 338 / +32 886 the raw stat suggests.

| repo | commits | authored surface |
|---|---|---|
| phoenix-rtos-devices | 400 | +36 863 — almost all new Pi 4 drivers |
| phoenix-rtos-project | 222 | +3 412 — new board/target integration |
| phoenix-rtos-kernel | 287 | +5 754 |
| phoenix-rtos-ports | 174 | ~+24 700 excl. generated shaders |
| libphoenix | 121 | +5 874 excl. vendored libm |
| phoenix-rtos-tests | 63 | +3 906 |
| phoenix-rtos-filesystems | 54 | +2 939 — includes a new NFSv4 client |
| plo | 79 | +2 275 |
| phoenix-rtos-utils | 50 | +1 594 |
| phoenix-rtos-usb | 37 | +478 |
| phoenix-rtos-lwip | 15 | +3 992 — includes the GENET driver |
| phoenix-rtos-build | 35 | +957 |
| corelibs / posixsrv | 3 / 2 | +231 / +47 |

Those 14 are the *Phoenix* repositories, and they are not the whole build. The GPU stack
additionally depends on **upstream Mesa at the immutable tag `mesa-26.2.0` plus a 981-line
fork-authored patch** (`patches/mesa/phoenix-rpi4-v3d.patch`, in the coordination repo), fetched
and applied by `scripts/bootstrap-linux-host.sh`. Deliberately *not* a fork — pinning a frozen
tag means upstream drift cannot break it — but it is load-bearing, and two of its hunks are
generic upstream Mesa defects rather than Phoenix plumbing (see the V3D material in the drivers
section). Anyone reproducing the GPU results needs it and cannot get there from the 14 repos
alone.

## ★ Start here: fixes to Phoenix that are not Pi-specific

If you read nothing else, read these. Each is a defect in code you ship, found because this port
exercised it hard, and each is described with its root cause in the section that follows.

| # | defect | where | why it matters to you |
|---|---|---|---|
| 1 | **Stale cache lines survive into uncached DMA mappings** | `hal/aarch64/pmap.c` `5d8645f6` + `usb/mem.c` `12c4fe8` | Nothing retires dirty lines when a previously-cached page becomes uncached DMA memory, so a later eviction silently overwrites live DMA data. Hit SD, Ethernet, USB and WiFi. Measured ~12 corrupt GPU control lists per 8 boots → 0. **Three independent instances of one class** (the third, in the V3D winsys, was a driver-local `dc civac` retired once the kernel guaranteed it). |
| 2 | **`open()`/socket construction race** | `posix/posix.c` `e88c8b75` | A half-built `open_file_t` is published in the fd slot before init and is reachable across blocking IPCs; a racing `close()` frees it under its own constructor and double-frees onto the zone free list. `accept4` blocks *inside* that window. |
| 3 | **`semaphoreUp` lost wakeup** | libphoenix `e75c4fe` | Signals the condvar only on a 0→1 transition, so N units with N waiters wakes exactly one — deadlocking any multi-consumer pool. Introduced by the RTOS-1250 rewrite. |
| 4 | **Heap overflow amplified by `free()`** | libphoenix `839b24b`, `aae70f0` | `vasprintf` overflowed a fixed 1 KiB buffer; `free()` then derived a *second, unbounded* write from the corrupted chunk header, so the fault always surfaced far from its cause. |
| 5 | **pthread defaults are unusable** | libphoenix `02ab4e0`, `bad2009` | Default attrs had `guardsize = 0` and `PTHREAD_STACK_MIN` (one page) as the stack size, so ordinary library code overran silently into a neighbour's stack or the heap. Fixed without affecting NOMMU targets. |
| 6 | **AF_UNIX socket-id recycling** | `posix/unix.c` `69d9a448` | Lowest-free-id reuse let a stale bound pathname resolve to a *different live* socket, and `send()` delivered the payload there. |
| 7 | **Demand pager ignored short reads** | `vm/object.c` `f145658f` | One `proc_read()` per page with no short-read loop leaves stale allocator bytes in the page tail — silent corruption of demand-paged text on any backing store that legally short-reads. |
| 8 | **vfork kernel-stack / borrowed-map lifetime** | `proc/*` `20802d61`, `6d8f40a5` | Async death in the vfork window freed the parent's *running* kernel stack. The same audit found `thread_t.execkstack` uninitialised, which had **silently disabled the kernel-stack canary** for every non-vforking thread. |
| 9 | **USB enumeration robustness** | `usb/hub.c` `47eede9`, `03bd903` | Missing USB 2.0 §7.1.7.5 TRSTRCY delay before SET_ADDRESS, plus an unbounded re-enumeration loop that rebooted the board. Applies to existing EHCI targets unchanged. |
| 10 | **ext2 allocators unlocked** | `463aec1` | Block/inode allocators mutate fs-global bitmaps and superblock counts with no lock; any multi-worker fs server corrupts them. |
| 11 | **`FIONREAD` failed on every tty** | libtty `b247643` | Returned `-EINVAL`, breaking readline/bash and any input-availability check. |
| 12 | **`select(…, NULL)` never blocked** | libphoenix `033ee1f` | Became a 0 ms poll, so every readline-based program aborted at its first prompt. |
| 13 | **`fcntl` record locks were a stub** | kernel `3844d204` | Returned `EOK` without locking, so two processes could both "acquire" the same lock. |
| 14 | **`std::atomic<int>` in `pthread_mutex_t`** | libphoenix `94df683` | Made the type non-copyable, so modern libstdc++ would not compile against libphoenix at all. |
| 15 | **A kernel diagnostic that self-deadlocks the scheduler** | `proc/threads.c`, `vm/map.c` `ce445804` | `lib_printf` called from inside `_threads_schedule` re-enters `threads_common.spinlock` via `log_write` → `proc_lockSet`, on a HAL whose spinlock is non-recursive — so the **first character hangs that CPU** with the global scheduler lock held, and the other cores wedge at their next scheduler entry. Any target with a non-recursive spinlock and a mutex-taking log path has this. It is why a "corrupt process pointer" line had never once appeared in a log despite the condition being detected, and why a UART log stopped **mid-word**. Compounding it, the fallback then asserted with the pointer it had just nulled, and `LIB_ASSERT_ALWAYS` → `hal_cpuReboot()` on **BCM2711 is `for(;;) hal_cpuHalt()`** — a permanent halt, no reboot. Together they turned one corrupt pointer into a dead board on ~1 in 6 X11 desktop exits; now 20/20 clean. |
| 16 | **`process_getName` faults the kernel on a corrupt `argv`** | `proc/process.c` `3c8009b7` | Three defects in one function, all reachable from an ordinary `top`: `argv` entries were dereferenced unvalidated (they are one kernel `vm_kmalloc` block, so a single garbage slot faults **EL1** inside `hal_strlen`), the loop had **no bound** on `argc` (a missing NULL terminator walks off the array), and `*(sbuf - 1) = '\0'` ran unconditionally — writing **one byte before the caller's buffer** when `argv[0]` was NULL. Observed as **2982 identical EL1 aborts in a single run**, because `top` re-enumerates every process on each refresh. |

## Platform gaps that every future port will hit

Found by porting 41 packages; each cost real debugging time and will recur.

- **`gettime`/`settime` exported in the global namespace**, colliding with gnulib's own — patched
  identically in coreutils and nano, and will recur for every GNU port.
- **No wide-character output at all** — no `swprintf`/`vswprintf`/`fwprintf` in libphoenix and no
  wide iostreams in libstdc++; forced a hand-written wide-printf shim.
- **`<fenv.h>` is a poison-pill header** that `#error`s on include, and `aligned_alloc` /
  `posix_memalign` are absent — all C11-mandatory surface.
- **`<stdio.h>` exposes no `__freadahead`/`__freading`/`__fseterr`**, so gnulib needs
  per-platform branches poking directly at `FILE` internals; without the `fseterr` branch, nano
  does not build.
- **`mprotect` cannot escalate past a mapping's original protection**, so every JIT and bytecode
  engine must ask for RWX at `mmap` time. `map_checkProt(baseProt, newProt)` is
  `(baseProt | newProt) ^ baseProt` (`vm/map.c:725`) and `vm_mprotect` returns **`-EACCES`** for
  any bit `e->protOrig` lacks (`:976`), where `protOrig` is stamped at map time and never widened
  — a deliberate W^X policy, worth knowing rather than rediscovering. Quake III's aarch64 JIT
  could not add `PROT_EXEC` after the fact and now allocates the code buffer RWX up front; it is
  also why the dl loader maps text/RO file-backed at *final* protection (the doc states that
  consequence in the dl entry without stating the cause).
- **A server has no way to learn that a client died** unless the client held an fd on it. The
  kernel's only client-death signal is a synthesized `mtClose`, delivered solely to servers the
  dead process held an fd on (`process_destroy` -> `posix_sweepFds` -> `proc_close`); there is no
  death notification to port owners. So any server handing out resources over bare `msgSend` —
  no `open()`, no fd — leaks everything a killed client held. Found the expensive way, as a real
  per-session GPU BO leak (see the drivers section). Sharp edge for anyone taking the fd-based
  route instead: do **not** key the death `mtClose` on `msg.pid`, which arrives as pid 1.

## How to read the rest

Four sections follow, one per area, each written to stand alone. **★** marks anything valuable
beyond the Pi 4. Completeness is stated honestly — where a driver is read-only, or a fix is a
workaround, or a defect is still open, it says so.

---

## Kernel, bootloader (plo), posixsrv

This fork brings up Phoenix-RTOS on the Raspberry Pi 4B (BCM2711, 4x Cortex-A72) and, in the
process, rewrites a good deal of the shared `hal/aarch64` layer into a **DTB-driven "generic"
aarch64 platform** rather than a second board hardcode next to `zynqmp`. Roughly half the kernel
diff is that bring-up (new `hal/aarch64/generic/`, +632 lines of DTB parsing, an architectural-timer
backend, GIC/PPI/SMP work, MMU and cache-maintenance corrections, EL2->EL1 boot recipe). The other
half is **not Pi-specific at all**: a long hunt for an intermittent kernel-heap corruption and an
AF_UNIX teardown crash produced a chain of individually-verified fixes in `posix/`, `vm/` and
`proc/` — an open()/socket construction-window race, AF_UNIX socket-id reuse, a vfork kernel-stack
lifetime bug, a missing short-read loop in the demand pager — that would bite any multithreaded
POSIX target. Those are the parts most worth cherry-picking. posixsrv contributes only two substantive
commits, both listed below.

Caveat on reading the history: of ~287 kernel commits, the oldest ~140 are bring-up churn
(single-character UART markers, probes, experiment/revert pairs from the cache-enable and SMP
phases). They are summarised here as single entries; only the surviving state is described. Where a
commit message claims more than its diff delivers, this section says so.

### 1. Pi 4 / BCM2711 bring-up

The board is not a new `hal/aarch64/<board>` copy: `hal/aarch64/Makefile` now includes
`hal/aarch64/$(TARGET_SUBFAMILY)/Makefile` generically, and the timer implementation is selected
through a documented per-platform override hook.

| change | where | what/why |
|---|---|---|
| ★ Generic aarch64 platform | `hal/aarch64/generic/{generic,console}.c`, `include/arch/aarch64/generic/` (new, ~1050 lines) | A board whose peripherals come from the DTB rather than `config.h`. Gives aarch64 a second, non-Xilinx reference platform. |
| ★ DTB discovery expanded | `hal/aarch64/dtb.c` (+632), `dtb.h` | Now parses GIC reg tuples (variable `#address/#size-cells`), `/chosen` `stdout-path` console, `/soc` ranges translation, timer `interrupts` (phys-secure / phys-non-secure / virt / hyp) with source selection, `/reserved-memory`, and `/soc/dma-ranges` with a `dtb_armToBus()` CPU-PA -> bus-PA translator. |
| ★ Architectural-timer HAL | `hal/aarch64/gtimer.c`, `gtimer_backend.c`, `gtimer_timer.c` (new); `hal/timer.h` gains `hal_timerIrq()` | CNTV/CNTP driver split into helpers + backend + `hal_timer*` implementation. `interrupts_gicv2.c` stops hardcoding `TIMER_IRQ_ID` and asks `hal_timerIrq()` instead. |
| GICv2 non-secure EL1 arrival | `hal/aarch64/interrupts_gicv2.c` | Adds per-IRQ Group assignment (timer PPI must be Group 1 when the kernel arrives in NS-EL1), per-IRQ trigger configuration on registration via a platform `_interrupts_gicv2_classify()` hook, targeted `hal_cpuSendIPI()`, and pending-state readers. `interrupts_setCPU()` is now applied only to SPIs (it is meaningless for PPIs/SGIs). |
| ★ EL2 -> EL1 boot recipe | plo `hal/aarch64/generic/_init.S`, kernel `hal/aarch64/_init.S` | The single largest fix in the port. Previously plo enabled MMU/caches at EL2; now armstub(EL3) -> plo at EL2h -> minimal EL2 setup (`CNTVOFF_EL2`, `CNTHCTL_EL2`, `CPTR_EL2`, `HCR_EL2.RW`, `VMPIDR/VPIDR`, SCTLR RES1 baselines) -> `eret` to EL1, where MMU and caches are enabled. This matches Linux/U-Boot/NetBSD/Circle and is what unblocked boot to userspace. |
| Cache maintenance corrected | plo `hal/aarch64/{cache,mmu}.c`, `generic/hal.c`; kernel `hal/aarch64/_init.S` | Dropped the set/way (`dc isw`) invalidate-all from the boot path (ARM ARM B2.2.6: set/way is for power-down, not coherency, and cannot reach the BCM2711 VPU-side system L2 that `start4.elf` primes). Adopted Linux's SCTLR write ritual (`msr; isb; ic iallu; dsb nsh; isb`) and per-VA maintenance. Handoff teardown uses `dc ivac` (discard) not `dc civac`, because cleaning stale firmware/VC4 L2 lines would write them back over the kernel image plo had just staged. Note the apparent contradiction with `54bf7c3` below: that one switches plo's *final* full invalidate back to set/way precisely because a 4 GB per-VA `dc ivac` sweep walks Device-mapped GPU reserve, where `dc ivac` is CONSTRAINED UNPREDICTABLE. Different jobs. |
| Two kernel TTL3 tables | `hal/aarch64/pmap.c` | `PMAP_KERNEL_TTL3_TABLES 2U` — the kernel image outgrew the single 2 MiB early window. Fixed bound guarded by `LIB_ASSERT`, not a general fix. |
| Reserved-memory honoured | `hal/aarch64/pmap.c`, `dtb.c` | `pmap_getPage()` marks pages inside a `/reserved-memory` region `PAGE_OWNER_BOOT` instead of `PAGE_FREE`, keeping the CMA pool, framebuffer and armstub spin-table out of the allocator. Truncated to 16 regions. |
| Firmware DTB handoff | plo `hal/aarch64/generic/hal.c`; kernel `hal.c` | The Pi 4 firmware patches the DTB at runtime to add the high memory bank, but the pointer arrives at plo as `x0 == 0` (chain not root-caused). plo reads `dtb_ptr32` directly from armstub PA `0xf8` as a fallback; the kernel prefers the firmware DTB over the initramfs one. This unlocked the full 3.94 GB (plo `SIZE_DDR` 948 MB -> 3.94 GB). Workaround, by the author's own note. |
| ★★ SMP: 4-core scheduling | `hal/aarch64/generic/config.h` (`NUM_CPUS 4U`), `_init.S`, `interrupts_gicv2.c`, `gtimer_timer.c`, `proc/threads.c`, `main.c` | ~40 commits of phased experiments, most reverted. Surviving mechanism: firmware releases secondaries into the armstub spin-table; each secondary waits on `hal_smpPrimaryReady` (published by the primary after vm/proc/threads init), then does its own `_hal_interruptsInitPerCPU` / `_hal_cpuInit` / `_hal_timerInitPerCPU`, enables its own **banked** GICD_ISENABLER0 PPI bit, arms its own CNTV, and re-arms CNTV in `threads_timeintr` so the level-triggered PPI does not stick asserted. All four cores run the scheduler. Residual: no PSCI/memory-poke fallback, so a core the firmware never releases stays down. |
| SError handler (dormant) | `hal/aarch64/exceptions.c`, `cpu.c` | `exceptions_serrorHandler` dumps ESR/ELR/FAR and halts (never reboots). **SError stays masked** — `NO_SERR` is now set in the `cpu.c` PSR templates — because unmasking exposes a live external abort in BCM2711 PCIe/VL805 bring-up (`esr=0xbf000002`, imprecise; isolation-proven absent with USB disabled). Infrastructure, not live handling. |
| EL0 counter + cache-maintenance enable | `hal/aarch64/_init.S` | `CNTKCTL_EL1.EL0VCTEN|EL0PCTEN` (reset value is architecturally UNKNOWN, so `mrs cntvct_el0` from userspace was trapping and killing binaries) and `SCTLR_EL1.UCI=1` for EL0 `dc civac`/`ic ivau`, needed for userspace streaming-DMA drivers on this non-coherent SoC. Both match Linux; both are generally useful on aarch64. |
| Fork-local divergences to be aware of | `hal/aarch64/spinlock.c`, `pmap.c`, `proc/name.c`, `syscalls.c` | (a) `hal_spinlockSet/Clear` take a DAIF-only fake-lock path under `#if NUM_CPUS == 1`, and `hal_spinlockCreate` skips the registry lock before `hal_started()`. (b) `_pmap_preinit` does `if (nBanks == 0) while (1) wfe;` — a silent hard hang where upstream would want an error path (also space-indented amid tabs). (c) `proc/name.c` adds a cached-`devfs_oid` fast path in `proc_portLookup` (TD-14) to dodge a Pi 4 cold-boot race against devfs's own `portRegister` that produced 20-second IPC hangs; a fork-local workaround, not an optimisation. (d) The syscall table is byte-identical to upstream again except an appended `sys_fdpath`, but restoring upstream order shifted ~93 syscall numbers — anyone cherry-picking must rebuild every binary. |

**Same class, 2026-09-09: upstream widened the priority space 8 → 64.** A 64-bit ready bitmask
replaced the linear ready-queue scan, and `priority()` was split into `setPriority()`/`getPriority()`
across kernel/devices/usb/utils/corelibs — a coordinated API break. Two details worth carrying:
the syscall was **renamed in place** (`ID(priority)` → `ID(sys_priority)`, same slot 54 of 109), so
nothing was renumbered and existing binaries still invoke correct ordinals; but the syscall **gained
a second argument** (an out-pointer), so an old caller passes one argument and the kernel reads the
second from a garbage stack slot. That is unreachable in practice here — 0 undefined references
across all 229 built programs, and `objdump` finds 0 call sites in every demo binary, because
`priority()` is dead linkage pulled in from `libphoenix.a`. And when adapting local code to the
bitmask: **dropping a corrupt ready queue must clear its bitmask bit**, or `_readyMinPrioIdx()`
returns the same index forever and hangs the scheduler on the exact path the guard exists to survive.

**libphoenix allocator, further hardening (2026-09-09).** Beyond the coalesce-neighbour validation
noted above: `free()` now checks that a chunk's `heap` pointer lies inside the window of heaps the
allocator has actually `mmap`'d, and free-bin links are **value-checked before `LIST_REMOVE`
dereferences them** — on garbage the bin is abandoned rather than followed, which is the userspace
twin of the kernel fix in shortlist row 15. Two cautions from doing it: the first version required
`sizeof(chunk_t)` to fit below the window top, which **rejects a legitimate chunk at a heap's end**
and, because the failure action abandons the bin, silently **leaked live memory** — it passed both
libc suites and a desktop session, and only a real application exercised a boundary chunk. And do
not assume low addresses are invalid: a real process reports `heapLo=0x2000`, so the **first heap
lives at page 2** on this port.

### 2. New hardware support

Little new *device* code lands in these three repos — the Pi 4 drivers (genet, V3D/Mesa, EMMC2/SD,
USB/xHCI, WiFi, audio, HWRNG, thermal, GPIO, framebuffer) live in `phoenix-rtos-devices` and other
repos outside this section's scope. What is here:

| change | where | what/why |
|---|---|---|
| VideoCore mailbox framebuffer | plo `hal/aarch64/generic/video.c` (new, +376) | Mailbox property interface: query/set physical mode, read framebuffer base and pitch, render the 3-stage boot-progress panel. Later extended to request a **triple-height virtual framebuffer in one allocation** so a GPU renderer can page-flip via `SET_VIRTUAL_OFFSET` (2 buffers still race the vsync latch); degrades to 2x then 1x if the firmware cannot grant the memory. The physical size is reported onward, so fbcon/graphmode see one 1080p buffer. Worth stating *why* three, since it is not a capability but a fix: GPU render wedges were contention between the GPU writing and the HVS display engine reading the live scanout buffer, and the measured progression was render-to-scanout ~40 % of boots wedged → blit-resolve to a DRAM target (rare) → double-buffer (~1/boot) → **triple-buffer, 0 wedges over 95 FPS samples / ~190 s**. Triple buffering is what guarantees the GPU never writes the buffer being scanned out. |
| Graphmode handoff | plo `syspage.c`; kernel `syspage.c`, `include/arch/aarch64/generic/syspage.h`, `generic.c` | Framebuffer geometry + base travel plo -> syspage -> `platformctl(pctl_graphmode)`, gated on `HAS_GRAPHICS`. |
| PL011 + GIC in plo | kernel `hal/aarch64/pl011.{c,h}` (new); plo `generic/{console,interrupts,timer}.c` (new) | Reusable PL011 helper split out of the console; plo gains its own GICv2 Group-1 init, timer and console for the generic board. |

### 3. General bug fixes (not Pi-specific)

This is the section upstream should read. All of these are defects in shared Phoenix code.

| change | where | root cause in one sentence |
|---|---|---|
| ★★★ Stale dirty lines survive into a NON-CACHED mapping | `hal/aarch64/pmap.c` (`5d8645f6`) | A page mapped Normal-NC may still carry dirty cache lines from a previous owner that had it mapped cached: `_pmap_destroy()` releases a process's pages with no cache maintenance, and the only callers of `_pmap_cacheOpBeforeChange()` are `_pmap_enter()` and the unmap path, so nothing retires them — they can be evicted at any later moment, landing on top of whatever was written through the new uncached mapping. Every uncached DMA mapping in the system has this exposure (SD, genet, USB, WiFi), so intermittent address-dependent faults in those subsystems are the same bug class; the fix clean+invalidates on the `MAIR_IDX_NONCACHED` transition only (Device mappings hold no lines). The location is aarch64-specific, the defect class is not. Measured: GPU control-list corruption at 64-byte granularity went from ~12 corrupt lists per 8 boots to 0, and a dependent rendering bug from 0/15 to 7/7. The V3D driver had independently worked around the *same* class with its own `dc civac` on every new BO (devices `9bac0f7`, "stale dirty lines were eating control lists") and could delete it once this landed (`6cdf597`) — so the shortlist's "three instances" is three real subsystems, not three guesses. |
| ★ 32 KiB main-thread user stack + double-fault on signal push | `hal/aarch64/generic/config.h` (`SIZE_USTACK`, `8ae20864`) | Phoenix has no auto-stack-growth, so the 8-page main-thread stack is a hard ceiling that a real POSIX userland exceeds — coreutils `cksum` and `od` overflow it and SIGSEGV. Worse, the kernel then **double-faults pushing the signal frame below the exhausted SP** (`hal_cpuPushSignal` -> `hal_memcpy` on an unmapped user stack), which also corrupts the crash dump, so the overflow does not even report itself. Raised to 256 pages here (demand-paged, so only touched pages cost anything); the 1 MiB value is Pi-tuned but the ceiling and the double-fault are general. The double-fault itself is *not* fixed. |
| ★★★ open()/socket construction window | `posix/posix.c` (`e88c8b75`, `19dcbd5d`, `c3f60f1b`, `d3862861`, `7ac1bd10`, `39135453`) | `posix_open`/`posix_newFile` publish `p->fds[fd].file` *before* the file is initialised (the slot is how the fd is reserved) and then drop `p->lock` across several blocking IPCs, so a half-built `open_file_t` — uninitialised `refs`, `oid`, `type` — is reachable by every other thread; a concurrent `close()` or exec CLOEXEC sweep decrements that garbage refcount, frees the file under its own constructor, and the error paths then free it a second time, putting one block on the zone free list twice. Fixed by a construction reference (`refs = 2`, one for the slot, one for the caller), an `ftConstructing` file type so a half-built file is never mistaken for a regular file, a sentinel oid that cannot resolve to a live port, and slot-still-ours re-checks before writing flags or returning the descriptor. `unix_accept4`/`inet_accept4` *block* inside this window, so it is wide. |
| ★★★ AF_UNIX socket-id recycling | `posix/unix.c` (`69d9a448`) | `unixsock_alloc()` handed out the lowest free id while `unix_bind()` publishes a filesystem name keyed on `{US_PORT, id}` that POSIX keeps until `unlink()`, so a stale pathname resolved to a *different, live* socket and `send()` wrote its payload into that socket's buffer where an unrelated reader consumed it. Allocating ids monotonically reproduces Linux's behaviour (stale name -> `ECONNREFUSED`); `send()` also stopped reporting `ENOTSOCK` (which describes the caller's fd) for a dead destination. Removes the lowest-free rbtree augment machinery, net -19 lines. |
| ★★★ vfork kernel-stack / map lifetime | `proc/{process,threads}.c` (`20802d61`, `b98d7b9a`, `6d8f40a5`) | In the vfork window the child's kstack *is* the parent's live kernel stack and its `mapp`/`pmapp` point into the parent's `process_t`; an asynchronous death on either side skipped all three cooperative unwind paths, so `thread_destroy` freed a running kernel stack and `process_destroy` ran `vm_mapDestroy` on the borrowed map while the parent's TTBR0 still walked those page tables. Because every kernel stack is its own kmalloc zone, the freed block recycled straight into a zone of `thread_t`s and an ordinary write to an ordinary stack local (`char pstack[16]` in `proc_portLookup`) landed on a live thread — *not* a buffer overrun. Also in the same audit: `thread_t.execkstack` was never initialised in `proc_threadCreate`, which **silently disabled the kernel-stack canary** for every thread that never vforked. |
| ★★ Demand pager ignored short reads | `vm/object.c` (`f145658f`, `31045b7f`) | `object_fetch()` issued one `proc_read()` per page and treated only a negative return as failure; a legally short read (NFS does this) left the page tail holding whatever `vm_pageAlloc` last had, corrupting demand-paged text/data — silently, because the failure happens inside the already-vfork'd child. Now loops to EOF and zero-fills the tail. The cluster path additionally did not clamp a byte count the backing store over-reported (that clamp has not fired on hardware). |
| ★★ `PROT_USER` derived from the wrong source | `vm/map.c` (`043cde80`) | `map_pageFault` took `PROT_USER` from `hal_exceptionsFaultType`, which only sets it for EL0 aborts, so a kernel-mode user-copy fault (an AF_UNIX `recv` memcpy into a just-forked COW buffer) installed EL1-RW/EL0-none; the process's own EL0 read then re-mapped it RO and the next kernel write faulted again — a non-converging EL1 fault storm (~119 dumps in one test) that could fail the transfer. Now keyed off whether the fault targets the current process's user map. COW is preserved (a read still maps RO). |
| ★★ `recvmsg` never reported control length | `posix/unix.c` (`381152c6`) | Only `fdpass_unpack()` wrote `*controllen`, and only on the received-and-not-peeking path, so on EOF / `EWOULDBLOCK` / `ENOTCONN` / `MSG_PEEK` the caller kept its own *input* length describing a buffer the kernel never touched; a conforming caller then walks it, `CMSG_FIRSTHDR` returns a `cmsghdr` made of uninitialised stack, and its `cmsg_len` drives an arbitrary-length memcpy into the caller's fd array. |
| ★★ `accept4` use-after-free | `posix/unix.c` (`9c60b783`) | A socket queued on a listener's `connecting` list was owned by nobody: the non-blocking `EINPROGRESS` path returned to userspace with it still linked, so closing it freed the block in place, and `unix_accept4` then wrote `r->state`/`r->remote` and woke `&r->queue` with no reference and no lock. Now the queued socket records its listener so `unixsock_put` can unlink itself from any path (and drains the list when a *listener* dies, waking blocked connectors with `US_PEER_CLOSED`), and `accept4` holds `unix_common.lock` across the unlink. |
| ★★ `vm/zone` free-list hardening | `vm/zone.c`, `vm/kmalloc.c` (`b516c8a4`, `8522b1db`, `521320e9`, `1ff99ec4`, `ec29e159`) | The free list is threaded through the blocks themselves, so a misaligned free or a write-after-free replaces a link and the allocator faults at some later, innocent call site. Now: `_vm_zfree` rejects a misaligned pointer and *returns a status* so `_kmalloc_free` stops moving accounting for a rejected block; `_vm_zalloc` validates the link before adopting it and quarantines the zone (`used = blocks`) rather than truncating its list. A genuine latent bug fell out of the review: the header-zone replenish trigger was an equality test (`== 1U`), so one quarantine would have meant `vm_kmalloc` returning NULL for every new size forever — now `<= 1U`. |
| ★★ `p->lock` held across blocking IPC | `posix/posix.c` (`2c489b75`) | Process exit and exec held `p->lock` across `posix_fileDeref`, whose last-reference branch sends a blocking `proc_close`, so a wedged server stalled every other thread's open/close/read behind it. Factored into `posix_sweepFds()`, which clears the slot under the lock (whoever clears it owns the reference) and derefs outside it. |
| ★★ Uninitialised kernel memory to userspace | `posix/posix.c`, `log/log.c`, `vm/object.c` (`31045b7f`, `e5c5f833`) | `posix_ioctl`'s and `_log_msgRespond`'s kernel-stack `msg_t` were un-zeroed while `o.raw` is copied back to the caller for any `IOC_OUT` request — `log_devctl` answers `TCGETS` (what `isatty()` sends) without writing `o.raw`, returning the calling thread's own kernel stack. Separately, `posix_pipe()` and the `pp == NULL` branch of `posix_clone()` never initialised `f->path`, which `posix_fileDeref` frees unconditionally (latent today, but exactly the shape of double-free the audit started from). Now zeroed at every site. |
| ★ Phantom `-ENOMEM` from backing store | `vm/object.c` (`0620f2e9`) | `vm_objectPage` swallowed a real fetch error (`got = 0`, `*page = NULL`, return EOK) and `_map_force` then returned a generic `-ENOMEM`, so a transient read failure surfaced at exec as out-of-memory — which sent a multi-session investigation chasing allocation failures that were all measurably silent. Now propagates the real error. |
| ★ `/dev/urandom` never advanced its destination | posixsrv `special.c` (`ef6e39b`) | The fill loop `memcpy`'d into `r->msg.o.data` every iteration without advancing `dst`, so any read larger than 64 bytes left the tail uninitialised. (Same commit sources entropy from `/dev/hwrng` when present — see §7; it still falls back to `rand()`, so this is not "urandom is now secure".) |
| `tmpfile()` after a root swap | posixsrv `tmpfile.c` (`8a44ce8`) | `tmpfile_init()` creates `/var/tmp` once at startup; if `/` is mounted or swapped later (netboot NFS takeover here, but the defect is general) every backing-file open hits ENOENT and `tmpfile()` returns NULL forever. Recreate the directory on ENOENT and retry once. |
| Sign-compare in memory enumeration | plo `hal/aarch64/generic/hal.c` (`98418d1`) | `hal_memoryGetNextEntry` compared a signed index against an unsigned count. |

Honest status of the corruption hunt these came out of: several of the commits state plainly that
they did *not* fix the failure they were written for — `381152c6` ("the fatal events still occur at
the same rate, 1 of 4 runs"), `9c60b783` (8 of 9 runs clean, "one residual HANG remains… this is
not the whole story"), `1ff99ec4` explicitly retracting `521320e9`'s claim. The chain converged at
`6d8f40a5` (the reproducing EL1 Data Abort on X-session teardown is gone) with a named, less severe
EL0 residual still open. Working notes:
`docs/misc/2026-09-02-kernel-heap-corruption-workorder.md`.

### 4. Performance

| change | where | measured |
|---|---|---|
| ★★ Read-ahead clustering for file-backed faults | `vm/object.c` (`8834eaf3`) | A file-backed fault fetched one 4 KB page and paid three synchronous server round-trips (`proc_open` + `proc_read` + `proc_close`) for it. `object_fetchCluster()` fills a bounded 16-page / 64 KB window in one open+read+close and installs the read-ahead pages in the object's cache. A 24 MB static binary exec'd from ext2-on-SD reached `main()` at **T+68.0 s -> T+5.5 s (~12x)**. Bounded, clamped to the object's backing pages, zero-fills past EOF, degrades to one page under heap pressure. **Honest boundary, from the controlled follow-up:** raising `OBJECT_READAHEAD_PAGES` 16 → 64 (256 KiB clusters) measured 7.686 s against the cluster-16 baseline's 7.669 s — a 4x larger cluster with the total unchanged, which *refutes* the per-cluster-round-trip model for cold exec paging over NFS (that path is per-page-fault and message-copy bound). Reverted; manifest `2026-08-12-readahead-cluster-64`. |
| ★★ Demand-zero the anonymous `.bss` | `proc/process.c` (`b446114f`) | `process_load{32,64}` `hal_memset` the entire `.bss` at exec under `map->lock` — ~14k pages for a 26 MB game binary. But the bulk `.bss` past the last file page is an anonymous mapping the VM already demand-zeroes per fault, so only the tail sharing the last file-backed page needs zeroing (it is COW'd from the file). Matches Linux. Turned a 26 MB-`.bss` exec from "hangs over flaky NFS" into fast and robust. |
| ★★ Demand-page the ELF header map | `proc/process.c` (`432f537b`) | `process_load` mapped the whole ELF image into the kernel map, which on an MMU target is eagerly populated — so exec of a large binary pulled the entire file in one page-read per page even though only phdrs, shdrs and `.shstrtab` are dereferenced. Now mapped lazily with only the metadata ranges explicitly forced (strided by `sizeof()` so a malformed `e_*entsize` cannot leave a touched page unforced); other ELF classes keep the old behaviour. A force failure propagates its real code instead of being masked as `-ENOEXEC`. |
| ★★ Readiness-woken `poll()`/`select()` | `posix/posix.c`, `posix/unix.c` (`01715f09`, `7a52147c`, `9a6d4743`) | `posix_poll` was a poll-and-sleep loop with a 100 ms re-check, so every X client round trip (libxcb waits each reply in `poll(-1)`) cost up to 100 ms. AF_UNIX fds now block on a global unix poll queue broadcast by every socket state change; the timed loop survives only as the fallback for remote-server fds and the safety-net timeout, back at 20 ms. Lost-wakeup safe via the existing `wakeupPending` sentinel; a missed broadcast degrades to <=20 ms, never a stall. **Note the inet half is narrower than it reads**: for a poll on *exactly one* `ftInetSocket` fd, the block timeout is packed into the high bits of the `atPollStatus` attr value above the 16-bit event mask, and only the lwip socket server decodes it — everything else keeps the legacy path. |
| ★ ~100 s of every boot was a diagnostic dump | `usb/xhci/bcm2711-pcie.c` (devices `3f82638`), kernel `6cdf217e` | A "Pi-firmware bridge state" dump read 10 RC / MEM_WIN registers *before* `bcm2711PrepareHostBridge` clears `SERDES_IDDQ`; each read took the PCIe external-abort recovery path at **~10.8 s** and returned `0x0`. Deleting it took the boot span **165.9–177.5 s -> 66–74 s**. Its own hypothesis ("the firmware leaves the bridge working") was disproved by its all-zero output. Worth stating because the follow-on claim was withdrawn: the post-fix study also credited it with "USB enumeration 3/10 -> 10/10", and a controlled 8+8 A/B — with the build identity gated by `strings loader.disk`, i.e. verifying the *measurement* — gave 6/8 with the dump against 8/8 without, **Fisher p ≈ 0.47, not significant**. Enumeration was intermittently flaky either way. |
| ★ AF_UNIX rings are ONE PAGE, so bulk IPC pays a round-trip per 4 kB | `posix/unix.c` (`137ec58f`) | `US_DEF_BUFFER_SIZE` is `SIZE_PAGE`, and a `SOCK_STREAM` write copies only what currently fits before blocking — so a large transfer costs one blocking round-trip per refill, and a ring hands over only about *half* its capacity per refill (writer blocks when full, reader drains it, so the reader finds it half-full). An X client pushing a 1.2 MB `XPutImage` took **~512 round-trips = 355.2 ms of a 395.9 ms frame** at the one-page ring. Raising the receive ring took the frame to **112.6 ms (3.5x)**; what remains after that is **~38 round-trips at ~2.79 ms each = 68.6 ms of a 105.8 ms frame**. `SO_RCVBUF` is honoured and resizes the socket's *own* ring — the one the peer writes into — so the receiving end is the knob; its ceiling was raised 64 kB → 256 kB. **Honest scope, because the obvious conclusion is wrong:** at 256 kB the round-trips fell 2.8× exactly as predicted *and the per-round-trip cost rose 2.5×*, leaving throughput flat at ~12 MB/s and buying only +12% (9.46 → 10.62 fps) — so beyond 64 kB the ring is **not** the constraint, though at the original 4 kB it certainly was (3.5x): `rpi4-ipcprobe` reaches **213 MB/s cross-process** (`fork` + `socketpair`) on the *default 4 kB* ring with no `poll()` in the loop — 17× faster than the X path — so the cost is the per-operation overhead of that path (server does `poll`→`read`→`poll`→`read`; `io.c:402` returns to the dispatch loop on every incomplete request), not buffering. Kept because it is measured net-positive and stops being marginal if that overhead is fixed. |
| plo `dc ivac` sweep -> set/way invalidate-all | plo `hal/aarch64/generic/hal.c` (`54bf7c3`) | `hal_dcacheInval(0, 0xfc000000)` walked 4 GB with ~65M `dc ivac` through the EL2 MMU, including the 76 MB GPU reserve mapped as Device (where `dc ivac` is CONSTRAINED UNPREDICTABLE). Replaced with a set/way invalidate-all, which is the right tool for a full invalidate and does not go through the MMU. |

### 5. Stability / robustness

Most of the lifetime and race work is in §3. What is specific to robustness here:

- ★ **Bounded, backed-off re-drive of the exec-path open** (`vm/object.c`, `c25ed0cb`, `7e6cbe37`,
  `b74db0da`). `object_fetchCluster`'s `proc_open` had none of the read path's resilience and
  hard-coded `-EIO`, discarding the real errno, so one transient blip aborted a ~17 MB exec. Now
  propagates the real return and retries with ramped backoff to a ~10 s deadline, exiting the
  instant the window clears. Honest scope: the ~10 s deadline exists because the specific failure
  is an NFSv4 OPEN-establishment window in the client; it is a recovery loop, not a fix for the
  underlying flake, and it is inert on non-remote roots.
- **Dead spawn-cap removed** (`main.c`, `1594a550`). A 32-iteration cap on the syspage program loop
  was a fork-era band-aid; a direct state dump proved the list is fully circular, and the cap had
  become a silent-truncation hazard (sized for 9 programs, 17 shipped).
- **Cross-target build correctness** (`c93298a9`). `main.c`'s SMP-primary-ready block and
  `_init.S`'s early-exception facility referenced aarch64-only / rpi4-only symbols from shared
  sources, breaking the link for sparcv8leon and aarch64a53 targets. Fixed by construction only —
  those targets' SMP/boot behaviour is unverified on hardware in this fork.

### 6. POSIX / compatibility gaps filled

| change | where | what/why |
|---|---|---|
| ★★ `fcntl` POSIX record locks | `posix/posix.c`, `include/posix-fcntl.h` (`3844d204`) | `F_GETLK`/`F_SETLK`/`F_SETLKW` were a no-op stub returning EOK, so two processes could both "acquire" the same lock — worse than unimplemented for anything relying on advisory locking (SQLite's rollback journal). Now a real oid-keyed, pid-owned lock table: ranges normalised from `l_whence`/`l_len` (0 = to EOF), conflict = overlap by a different pid where either side is a write lock, same-owner requests replace/split in place (unlocking a middle correctly yields two records), released on any close of an fd to the file and on process exit. pid-keyed ownership gives fork-no-inherit and exec-preserve for free. **Limitation stated in-commit: `F_SETLKW` is a bounded 20 ms poll, not a wait queue, and does not yet return EINTR.** |
| ★★ `fchdir` / `*at` support | `posix/posix.c`, `include/syscalls.h` (`9a0593d0`) | Phoenix's cwd is a userspace path string with no fd->path map, so `fchdir()` was a silent no-op that broke gnulib's `unlinkat` emulation and `rm -r`/fts. The canonical path is now stored in the refcounted `open_file_t` at open time (shared across `dup()` by construction) and read back through an appended `sys_fdpath(fd, buf, size)`. Best-effort: NULL for path-less fds. |
| ★ `F_*` as macros | `include/posix-fcntl.h` (`f70e7332`) | The fcntl operations were enum constants only, so `#ifdef F_GETFL` feature probes (autoconf, gnulib, bfd) could not see them and aborted with "please port fcntl to your platform". Self-referential `#define F_GETFL F_GETFL` after the enum — values and behaviour unchanged. |
| `platformctl(pctl_cpucount)` | `include/arch/aarch64/generic/generic.h`, `generic.c` (`78a42efb`) | Backs `sysconf(_SC_NPROCESSORS_ONLN/CONF)`; `nproc` and `os.cpu_count()` were wrong on a 4-core board. Additive: the appended union member is smaller than the existing one, so the packed ABI size is unchanged. |
| `/dev/urandom` entropy source | posixsrv `special.c` (`ef6e39b`) | Was `rand()` seeded from `srand(time(NULL))` — guessable bytes for any crypto/uuid/session use. Reads `/dev/hwrng` when present (fd opened lazily and cached, because posixsrv starts before the RNG driver registers), falling back to `rand()` otherwise. |

### 7. Diagnostics / debuggability

| change | where | what/why |
|---|---|---|
| ★★ Kernel call-stack backtrace on exception | `hal/aarch64/exceptions.c`, `Makefile` (`d8baae66`) | Fault handlers now print `pc`, `lr`, then the AAPCS64 `x29` chain. Printing `lr` explicitly is what makes a crash in a *leaf* traceable (a leaf has no frame, so walking x29 alone silently skips it). Deliberately a separate step run *after* the register dump has already reached the console, so a nested fault while chasing a corrupt frame pointer costs only the backtrace. The walk validates 16-byte alignment, monotonic ascent, and a 16 KiB window anchored on the first fp; depth capped at 16. Requires kernel-only `-fno-omit-frame-pointer` (aarch64 targets), which the Makefile now forces. |
| ★★ Self-hosted data watchpoint | `hal/aarch64/cpu.c`, `exceptions.c`, `generic.c` (`a67f6dac`, `5160cd8d`) | A hardware store watchpoint (DBGWVR0/DBGWCR0, EL0+EL1) armable from userspace via `platformctl(pctl_watchpoint)` with no JTAG, reporting through the existing UART dump. The handler halts rather than reboots even under NDEBUG, so the culprit stays readable while the preemptive scheduler keeps the rest of the system alive. The second commit adds a **value-trap** mode: given `[trapLo, trapHi)` it decodes the faulting `str Xt,[Xn{,#imm}]`, halts only if the stored value is in range, and otherwise emulates the store and steps past it — so a wild code-pointer write can be caught without stopping on thousands of legitimate stores. Single comparator; the store-form decoder is deliberately narrow (an unrecognised form halts). |
| ★ Fault stack window | `hal/aarch64/exceptions.c` (`ebf4f157`, `76e0adbc`) | A crash from stack corruption prints `pc == lr == far == the corrupt value`, which says a `ret` went somewhere invalid and nothing about what put it there. The two EL0 aborts and PC/SP-alignment now also dump 384 bytes spanning both sides of `sp` (the overwriting data is still sitting there and its extent identifies the array), clipped to `sp`'s page so a read starting on a mapped page cannot leave it. Deliberately a flat range read, not a frame walk — this crash shape has already proved x29 untrustworthy. |
| ★ Fault dump gating | `vm/map.c` (`b49268e5`, `3dd747e0`, `7fb8a317`) | `map_pageFault` dumped a full register set for *any* kernel-PC fault before attempting to resolve it, so a passing test printed kernel exception dumps (two per `test-libc-unix-socket` run) and made a healthy boot indistinguishable at a glance from real corruption. The up-front dump is now gated on the faulting *address* via `pmap_belongs()` — a pure inline range check that dereferences nothing, which matters because the up-front dump exists precisely for a kernel whose thread/scheduler structures may themselves be the corruption victim. The lost canary is restored as a milestone counter (first at 64, then every 1024); the threshold comes from measurement — an instrumented kernel resolved exactly *one* page fault across a three-suite run. Author notes the report path itself could not be exercised on hardware. |
| ★ Zone poisoning + alloc trace ring | `vm/zone.c` (`3e913c24`, `521320e9`) | Both opt-in and zero-cost when off. `-DVM_ZONE_POISON=1` stamps freed blocks with 0xa5 plus the freeing site and verifies at the next allocation, reporting the block, the freeing site, the first bad offset and a hex+ASCII dump — so a write-after-free is caught at the next allocation of that block rather than at an unbounded later time, and the ASCII usually names the writer. Blocks are poisoned at zone creation too, without which every block's first allocation trips the check. `-DVM_ZONE_TRACE=1` is the low-perturbation alternative (two words per op): poisoning's memset shifts allocation timing enough that the race it was built for stopped reproducing, and may mask it. |
| Object magic + checked derefs | `proc/{process,threads}.[ch]`, `vm/map.c` (`20802d61`, `b98d7b9a`) | `thread_t` and `process_t` are stamped and validated where they are dereferenced without any way to tell a live object from a wild value: `_proc_threadWakeup`/`_proc_threadBroadcast` (which take a caller-supplied `thread_t **` and trust it — several wait-queue heads are kernel-stack locals or share a kmalloc zone with `process_t`), the scheduler *before* `LIST_REMOVE` (whose `->next`/`->prev` deref is how this turns into a second, more confusing fault), and `map_pageFault`. `process_t`'s magic is its last field so no existing offset moves. |
| Console + klog observability | `log/log.c`, `hal/aarch64/generic/console.c` (`6cdf217e`, `90ce5766`) | Every klog byte is mirrored to the UART unconditionally so the UART is the kernel's own complete boot log independent of userspace libklog (ring readers drain to HDMI fbcon only, so the two never double up); `hal_consolePrint` takes the console lock for the whole attr+string+reset sequence so multi-core prints and userspace `debug()` no longer interleave char-by-char. Verified as 520 identical lines across 10 boots. The normal-path mirror is gated behind `RPI4_LOG_TO_FILE` (default 0) for builds that capture to a file instead; the panic path is never gated. |
| Per-thread last-CPU | `proc/threads.c`, `include/sysinfo.h` (`a839db02`) | `thread_t.cpuId` set in `_threads_schedule`, surfaced in `threadinfo_t` so `psh top` can show a per-core view. Behaviour-neutral. |

A known limitation in the generic console: only `hal_consolePutch` uses the DTB-discovered PL011
base. `hal_consolePrint` always writes through the fixed VA alias `0xffffffffffe00000` (documented
in-file as review item B5), which happens to *be* the discovered base on the Pi 4B. Any other
aarch64 board must conditionalise that path.
## libphoenix, corelibs, test suite

This fork's userspace-library work is overwhelmingly *libc completeness and defect repair* rather than
Pi-specific plumbing: bringing up X11, bash, CPython, coreutils, SQLite, Redis and five 3D games on
Phoenix forced roughly 90 previously-missing POSIX/C99 interfaces into libphoenix, and each port that
crashed instead of failing to link exposed a real libc bug underneath. **Read the headline diffstat with
care**: of libphoenix's `338 files / +32886`, about 27k lines are *upstream's own* vendored libmcs v1.3.0
plus the `math/` → `libm/{phoenix,libmcs}` reorganisation (`2480901`, `1fe50cb`, `d0a2884`, Mikolaj
Matalowski, 2025-09-30, also on `origin/Darchiv/libm-failsafe`), carried here only because the fork
needed it ahead of `master`; `f6b49ad` is likewise upstream (julianuziemblo). The fork's own libphoenix
surface is **93 files, +5874/-813** across 108 commits, and its math additions live in `libm/phoenix/`
(`c99extra.c`, `erf.c`, `gammaextra.c`, `longdouble.c`, `compatibility.c`). `phoenix-rtos-corelibs` gains
one new library and one documented constant; `phoenix-rtos-tests` gains ~3.9k lines that both cover the
new interfaces and, in three cases, fix tests that were quietly testing nothing. No `###` Performance
section appears below: none of these commits carries a measured number, and the throughput work in this
port lives elsewhere.

### ★ General bug fixes

The largest and most transferable bucket. Every entry below is target-independent unless noted, and in
most cases the failure was *silent or misattributed* — the fault surfaced far from its cause.

**Allocator and stdio**

| change | where | root cause |
| --- | --- | --- |
| ★ `839b24b` vasprintf heap overflow | `stdio/asprintf.c` | `malloc(1024)` + unbounded `vsprintf` — any `asprintf`/`g_strdup_printf` over 1 KB smashed the next chunk's metadata and crashed a *later* `malloc`. Now sized by `vsnprintf(NULL, 0, ...)`. |
| ★ `aae70f0` `free()` amplified an overflow | `stdlib/malloc_dl.c` | `free()` derived `malloc_chunkSetFooter`'s write address from the chunk header, so one caller overflow became a second, unbounded write at an arbitrary address. Added `malloc_chunkValid()` (page-aligned heap, chunk in range, size 8-aligned ≥ `CHUNK_MIN_SIZE`, no run past heap end); on failure it reports the *smashed* block via `debug()` and leaks it. The diagnostic deliberately avoids `printf` (re-entering malloc under its own lock). |
| ★ `6465a4a` `malloc(0)` returned NULL | `stdlib/malloc_dl.c` | Legal per C, but glibc/BSD/dlmalloc all return a unique freeable pointer and portable code relies on it (jq's `jv_mem_calloc` mis-reported OOM on every empty collection). Size 0 is now treated as 1. |
| ★ `5fa3847` double `fclose()` was a NULL write | `sys/list.c`, `stdio/file.c` | Fixed at both layers: `lib_listRemove()` treated a node with NULL links as still linked and did `t->prev->next = ...` through NULL; and `fclose()` now *unlinks first* (`file_unlink`/`file_release` split, membership by walking the open list) and returns `EOF`/`EBADF` for an already-closed FILE instead of re-flushing freed memory, closing a recycled fd and double-freeing. Found as a `far=0x10` EL0 Data Abort in quake3e. |
| ★ `9029813` `fflush` re-transmitted an already-written prefix | `stdio/file.c` | `__fflush_one()` treated a short write from `full_write()` as "keep the entire buffer": it set `F_ERROR` and left `bufpos` spanning everything, *including the bytes already gone out*. But `full_write()` deliberately returns a short count on `EAGAIN` (`return (errno == EAGAIN) ? total : -1;`) — which is what a tty does whenever a program prints faster than the UART drains. The next flush restarted from `stream->buffer` with the same `bufpos`, so the written prefix went out twice and the unwritten remainder was never resumed; `F_ERROR` also stuck permanently on a stream that had only met backpressure. `write_buffer()` — the flush path taken when the buffer *fills*, ten lines below in the same file — already did `bufpos -= err` and memmoved the remainder down, so the two paths disagreed about what a short write means. Evidence: a vkQuake run printing at frame rate produced **234 113 identical copies of one 31-byte buffer tail** on the UART (7 MB in ~200 s), perfectly contiguous. Ships with a regression test. Stated in-commit: the observed symptom is a repeated *tail* while this defect explains a repeated *prefix*, so the fix is correct but not proven to be the whole story. |
| `5674368` printf output shredded into 15-char lines | `stdio/fprintf.c` | `format_feed()` flushed every 15 chars; on an unbuffered stream or raw fd each chunk became its own `write()`. Buffer 16 → 256. |
| `01f74b0` scanf returned EOF on a matching failure | `stdio/scanf.c` | POSIX distinguishes matching failure (return items assigned) from input failure (EOF); `sscanf("!@#", "%d", &d)` returned −1 instead of 0. |
| `eb60be1`, `cbe4946` `fopen` mode parsing | `stdio/file.c` | `string2mode("")` read past the terminator; the modifier scan accepted `b` in one fixed slot only and rejected `t`, so `fopen(..., "rt")` failed (X11's libXfont2 had a downstream workaround). Rewritten to scan modifiers in any order; `x`→`O_EXCL`, `e`→`O_CLOEXEC`. |

**pthread defaults** — the mechanism is generic; only the 256 KiB value is per-arch.

* ★ `02ab4e0` — the default attr set `guardsize = 0`, so every default-attr thread stack was a bare
  `mmap` with live memory immediately below it. An overrun wrote silently into a neighbouring thread's
  stack or the heap and surfaced as a garbage return address or a fault inside `malloc`. Default is now
  one page; the `mprotect` was made **best-effort** so NOMMU targets (no `mprotect`) keep working
  exactly as they did, and an explicit `guardsize` of 0 is still honoured.
* ★ `bad2009` — the default `stacksize` came from `PTHREAD_STACK_MIN` (256, one page once aligned).
  That is a POSIX *floor*, not a usable default: libjpeg's Huffman setup overflows it doing nothing
  unusual. Introduces `PTHREAD_STACK_DEFAULT`, `#ifndef`-defaulted to `PTHREAD_STACK_MIN` so only
  arches that opt in change; aarch64 sets 256 KiB in `include/arch/aarch64/limits.h`.

**Path resolution and filesystem conformance**

* ★ `22b2c5a`, `fdfbfff` — the `*at` wrappers and `fchdir()` used `PATH_MAX` **stack** arrays; coreutils'
  `fts` calls them in deep chains during `rm -r`, and the accumulated frames overflowed the user stack.
  Moved to the heap. The class is "PATH_MAX array in a recursive wrapper", not an arch issue.
* ★ `1d86e24` → `2e46a0a` — `fchdir()` was a stub *returning 0 without changing the cwd*, so gnulib's
  `save_cwd`/`fchdir` emulation of `unlinkat()` unlinked in the wrong directory. First made to fail
  loudly (`ENOSYS`), then implemented for real once the kernel recorded each fd's canonical path.
* `c6cec41` — `umask()` stored the mask but nothing consulted it, so `open(O_CREAT)`/`creat`/`mkdir`/
  `mkfifo`/`mknod` created files more permissive than requested (0666 instead of 0644). POSIX and
  security relevant on every target.
* `ae9801e` — `unlink("dir")` succeeded, and `remove()` inherited it, because Phoenix's fs servers share
  `mtUnlink` with no dir check. Enforced client-side with an `lstat()` (see limitations).
* `c01f7a7` — `rename()` (emulated as `link`+`unlink`) failed `EEXIST` onto an existing destination,
  breaking the universal save-via-temp-file idiom. Now drops the destination and retries, gated strictly
  on `EEXIST`.
* `2b8dff5` — GNU `basename()` unconditionally wrote `*(last+1) = '\0'`, faulting on a read-only string
  literal (the common case, since `last+1` is already the terminator). `xedit` crashed at startup.
* `f09da71` — `fstatvfs(-1)` returned garbage (−1 collides with the "use the path" sentinel) instead of
  `EBADF`; `statvfs("file/")` returned success instead of `ENOTDIR`.
* `4c94672` — `sysconf(_SC_OPEN_MAX)` returned 512 against the kernel's real `MAX_FD_COUNT` of 1024, so
  anything sizing fd arrays from it was capped at half the limit.

**Signals, startup, concurrency**

| change | where | root cause |
| --- | --- | --- |
| ★ `e75c4fe` counting-semaphore lost wakeup | `sys/semaphore.c` | The RTOS-1250 rewrite (`74852cb`) made `semaphoreUp` signal the condvar only on a 0→1 transition. With N units released and N waiters parked, only one wakes and the rest sleep forever with `v > 0` — deadlocks **any** multi-consumer pool built on `semaphore_t` (found via vkQuake's 4-thread task system). Signals on every up again, keeping the rewrite's signal-outside-mutex optimisation. |
| ★ `033ee1f` `select(..., NULL)` never blocked | `sys/socket.c` | The NULL (infinite) timeout became `-1`, was clamped to `0`, and `poll()` got a 0 ms non-blocking call. This is the whole reason interactive bash exited at its first prompt: readline's `rl_getc` does a blocking `select(...,NULL)` and treats 0 as a timeout → `_rl_abort_internal`. The `n == 0` path was separately broken (`usleep` mis-scaling ms as µs). |
| ★ `da69de7` NULL signal handler was branch-called | `signal/signal.c` | The trampoline tail-calls `sightab[sig]` unconditionally. Portable code does `memset(&act, 0, ...)` relying on Linux's `SIG_DFL == 0`, but libphoenix's `SIG_DFL` is `(sighandler_t)-2`, so a zero `sa_handler` was stored verbatim → Instruction Abort at pc=0. Now resolves NULL/`SIG_DFL`/`SIG_IGN` before invoking. Same for any `SA_SIGINFO`-only install. |
| ★ `a59c800` `main()` got garbage `envp` | `crt0-common.c` | `_startc` called `main(argc, argv)`, so the POSIX three-parameter form received garbage. GNU bash read it as `shell_environment` and faulted. Passing an extra argument to a two-parameter `main` is ABI-safe. |
| `e643fa5`, `8dc40bb` atexit index ran off its arrays | `stdlib/atexit.c` | `_atexit_register` tested `idx == ATEXIT_MAX` — an equality test matches once, so an idx that ever exceeded the bound wrote past the node forever; and `__cxa_finalize` did `idx--` unguarded, wrapping 0 to `UINT_MAX` (reachable, since destructors may register new handlers and reset idx). See limitations: this is escalation hardening, not the root fix. |

**Headers and toolchain**

* ★ `94df683` — the C++ view of `pthread_mutex_t`/`cond_t`/`rwlock_t` mapped the internal `initialized`
  field through `std::atomic<int>`, whose deleted copy ctor made the structs non-copyable. gcc-16's
  libstdc++ copy-initializes `__gthread_mutex_t` from `PTHREAD_MUTEX_INITIALIZER`, so **libstdc++ would
  not compile at all**. The C++ branch now maps `_ATOMIC(t)` to plain `t` (layout-identical; every
  atomic access is in libphoenix's C sources) and drops `#include <atomic>`, which hard-errors under the
  `-std=gnu++98` TUs that reach it. Matches glibc/musl. Affects any gcc-16-era C++ compiler.
* ★ `e9bb8c4` — `termios.h` defined the `c_oflag` constants only as enum constants, invisible to the
  preprocessor. xterm's `#ifndef OPOST / #define OPOST 0` therefore fired, output post-processing was
  compiled off, and newlines lost their CR (the classic "staircase"). Added self-referential
  `#define OPOST OPOST` macros, mirroring the rationale already used for the baud rates in that header.
* `26317c2` `<assert.h>` re-includable per the C standard; `3a74c04` `<sys/wait.h>` builds under `-ansi`;
  `eee04d8` libmcs-compat helpers (`__signbitd` etc.) made **weak** so a port carrying its own libm
  (MicroPython) no longer hits a multiple-definition link error.

**libm poles and edge cases** (found by the new test suite, verified against glibc)

* `7b22fa3` — `asinh(-0.0)`/`atanh(-0.0)` returned `+0.0` (`x < 0.0` treats −0.0 as positive);
  `log1p(-1)` returned NaN instead of `-INFINITY`; `log1p(+inf)` returned NaN, which also broke the
  `atanh(±1)` poles.
* `7ca437b` — `scalbln`/`scalblnf` clamped to `INT_MAX`, which then overflowed inside `ldexp`'s
  `exponent += conv.exponent + exp` and returned ~0 instead of ±inf. Clamped to ±100000 instead.
* `b740469` — `wcstombs()` stored `(char)pwcs[i]` with no range check, silently truncating wide chars
  above 0xff instead of returning `(size_t)-1` + `EILSEQ` (its own siblings already did).

### New libc / POSIX functionality implemented

Additive; grouped rather than enumerated. Most entries replaced a stub that returned 0/NULL or a
declaration with no definition — i.e. they were previously *link errors or silent no-ops*.

| area | what landed | what it unblocked |
| --- | --- | --- |
| ★ dynamic linking | `dl/dl.c` + `<dlfcn.h>` (`3f98897`, `9f1a545`, `d30d36e`): first `dlopen`/`dlsym`/`dlclose`/`dlerror` on Phoenix, entirely in userspace with **no kernel change**. Maps text/RO file-backed at final protection (never triggers the W^X escalation reject), data anon+filled, applies `R_AARCH64_{RELATIVE,GLOB_DAT,JUMP_SLOT,ABS64}`, runs `DT_INIT_ARRAY`, `dlsym` walks `.dynsym`. `dlopen(NULL)` returns a main-program handle. | CPython extension modules and `ctypes` |
| ★ `*at()` family | `unistd/at.c` (`eae5151`): `openat`/`unlinkat`/`fstatat`/`faccessat`/`fchmodat`/`fchownat`/`mkdirat`/`mknodat`/`renameat`/`readlinkat`/`symlinkat`/`linkat` + Linux-compatible `AT_*`, layered on the kernel's fd→path record | gnulib sets `HAVE_*AT=1`; coreutils/findutils stop emulating via `save_cwd`+`fchdir`, which mutates the global cwd and is thread-unsafe |
| math (`libm/phoenix/`) | `cbrt`, `hypot`, `rint`/`nearbyint`, `lrint`/`lround`/`llroundl`, `fdim`/`fmax`/`fmin`/`copysign`, `exp2`/`log2f`, `erf`/`erfc`, `scalbn`/`scalbln` family, `log1p`/`expm1`/`asinh`/`acosh`/`atanh`, `nextafter`/`nexttoward`, `tgamma`/`lgamma`/`exp10`/`remainder`/`logb`/`ilogb`/`scalb`/`significand`, `floorl`/`ceill` (128-bit long double); `INFINITY`/`NAN`/`HUGE_VAL*` redefined as compiler builtins so they are constant expressions | every numeric port; the games and CPython |
| wide char / wctype | `wchar/wchar.c` (+555) and a new `wctype/` (`0cb9f72`, `e29c840`, `9128c5d`, `1f10581`, `a3e976c`, `b15587a`, `54df17b`): the C99 wide string/memory set, restartable `mbsinit`/`mbrtowc`/`wcrtomb`/`mbsrtowcs`/`wcsrtombs`, `wcwidth`/`wcswidth`/`wcscoll`/`wctob`/`wcsdup`, `wcspbrk`/`wcsspn`/`wcscspn`/`wcsstr`/`wcstok`, `wcsto{l,ul,ll,ull,d,f,ld}`, and the whole `<wctype.h>` `isw*`/`tow*`/`wctype`/`wctrans` family (C/POSIX locale) | bash multibyte, ncurses, CPython (needs the full `wcsto*` set) |
| locale | `<langinfo.h>` + `nl_langinfo()` (`7bf090f`) | how ncurses/mc/vim decide multibyte mode |
| time | `strptime()` implemented (`855dfc6`, was a stub returning NULL, so every date parse silently failed); `strftime` rewritten for POSIX flag/width syntax plus `%C %h %D %F %I %p %R %r %X %u %U %W %x %z %n %t` and ISO-week `%V %g %G` (`cc5cfbb`, `408832c` — closes upstream #351); `times()` returns real elapsed ticks (`b6f5986`) | shells, log/HTTP-date parsers, coreutils `date` |
| fs / process info | `statfs()`/`fstatfs()` + `<sys/statfs.h>`/`<sys/vfs.h>` (`676234a`); `getmntent` family + `<mntent.h>` (`29f5373`); `RLIMIT_*` ids and a `getrlimit` that fills the out-param (`3b45a15`); `mlock`/`munlock`/`mlockall`/`munlockall` (`ec9afd0`); `getrusage` out-param defined | coreutils `df`/`stat -f`/`sort` configure and run; OpenSSL's secure heap |
| randomness | `getrandom()`/`getentropy()` + `<sys/random.h>` (`40053fd`), drawn from `/dev/urandom` | libsodium, recent OpenSSL, language runtimes |
| string / signal | `memccpy`, `stpncpy`, `strtok_r`, `psignal` (`7a1cd73`); `memmem`, `getsubopt` (`cbe4946`); `strerror()` returns POSIX text instead of errno *macro names* (`e71331d`, new `string/errno.desc`); `siginterrupt` (`ad494b5`) | ~20 previously `TEST_IGNORE`d cases in upstream's own string/signal suites |
| stdio / scanf | POSIX `%m` allocation modifier for `%ms`/`%m[`/`%mc` (`a2731ac`); BSD `setlinebuf` | |
| misc / net | `gethostbyname`/`gethostbyaddr` over the working `getaddrinfo` (`9128c5d`); `getservbyname`/`getservbyport` with a 26-entry IANA table, `reallocf`, `umask` (`55034eb`); `getpwuid_r`/`getpwnam_r`, group iteration stubs; `sysconf(_SC_NPROCESSORS_ONLN/CONF)`, `_SC_CLK_TCK`, `_SC_LINE_MAX`, `_POSIX_VERSION`; `timerclear`/`timeradd`/`timersub` macros and a real `timerisset`; `wctomb`, `makedev`/`major`/`minor`; `getprogname`/`setprogname` | curl, dropbear, BSD-flavoured software |

### Stability / robustness

* ★ `bd6ae05` **the allocator's coalesce loops walked into unvalidated neighbours**
  (`stdlib/malloc_dl.c`). `free()` validates the chunk being freed (`malloc_chunkValid`), but
  `_malloc_chunkJoin()` then walks the chunks on *either side* of it, and those headers were never
  checked. An overflow out of the **preceding** block smashes that block's footer,
  `malloc_chunkPrev()` derives a bogus sibling from it, the backward loop promotes it (`it = sibling`),
  and the forward loop's `malloc_chunkIsLast()` is the first code to dereference its garbage `->heap`
  — observed on hardware as a Data Abort inside the allocator with a page-aligned fault address and
  no indication of which allocation was actually at fault. Each neighbour is now validated against the
  heap the caller already vouched for, so the fault becomes a message naming the block and coalescing
  stops instead of deriving a write address from a corrupt size. Two related gaps closed with it:
  `realloc()` had **no** header guard at all, unlike `free()`; and `malloc_chunkPrev()` declared
  `unsigned prevSize` for a `size_t` footer — 32-bit on LP64, so it mis-truncated a *corrupted* footer
  into a plausible small offset, defeating the very validation being added. Behaviour on a healthy heap
  is unchanged. **How the search was bounded:** a host harness compiling the real `malloc_dl.c` against
  host `mmap`/`munmap` ran 4.8 M operations over 24 seeds checking nine invariants (coalescing
  completeness, `freesz` accounting, RB-tree structure, bin/binmap consistency, heaps
  mmap'd == munmap'd) and found **no allocator defect** — which is what redirected the investigation to
  the caller. Test `6925aff` adds `malloc_fragment_and_drain`: nothing in the suite previously emptied a
  heap, so the coalesce loops and `free()`'s `munmap` of a fully-free heap were untested.
* ★ `4c97a79` → `c8ee89e` **detached-thread stack teardown** (`pthread/pthread.c`). The old `to_cleanup`
  scheme had each exiting detached thread defer its stack to a global slot for the *next* exiting thread
  to `munmap` — a cross-thread free racing the owner still executing on that stack, which on SMP was a
  Data Abort in the `munmap` epilogue with `far == sp`. `4c97a79` stopped the crash by leaking; `c8ee89e`
  replaced the leak with race-free reclaim: the exiting thread parks `{stack, size, tid}` on a retired
  list, and a **live** thread drains it from its own stack in `_pthread_reapRetired()` (called at the top
  of `pthread_create`/`pthread_join`), popping the list atomically and `threadJoin`ing each tid — which
  blocks until the owner reaches the ghost list, i.e. is provably off its stack — before unmapping. On
  malloc OOM it falls back to leaking rather than an unsafe free. HW: 1000-thread churn with 512 KiB
  stacks, 0 failures.
* ★ `4c97a79` also fixes a confirmed **lock leak**: `pthread_join` locked `pthread_list_lock` and called
  `_pthread_release` (which does not unlock — the detached self-exit path relies on the kernel
  force-unlocking on death) but never unlocked on the live-joiner path.
* ★ `f6489b8` — `pthread_detach` cast the `pthread_t` straight to `pthread_ctx *` and dereferenced it, so
  re-detaching an already-detached-and-terminated thread was a use-after-free (a detached thread frees
  its own ctx on exit). Now walks the live list under the same lock and returns `ESRCH`.
* `ac3baed` — `fcntl`'s variadic third argument was read as `unsigned`, truncating a `struct flock *` on
  any LP64 target, so `F_GETLK`/`F_SETLK`/`F_SETLKW` handed the kernel a broken pointer. Read as
  `unsigned long` (musl's pattern); `struct flock` and `F_{RD,WR,UN}LCK` now come from the shared
  `<phoenix/posix-fcntl.h>` instead of duplicate local definitions.

### aarch64 / Pi-specific

Deliberately short — most of the above is arch-neutral.

| change | where | what |
| --- | --- | --- |
| `75c60e7` | `arch/aarch64/signal.S` | Phoenix delivers no `ucontext` to signal handlers, so a process cannot backtrace its own fault. The kernel already copies the interrupted `cpu_context_t` onto the signal stack; the trampoline now stashes that pointer and the interrupted pc into `_dbg_signal_ctx` / `_dbg_signal_pc` (the latter because `signalCtx->pc` is clobbered with the handler address). No behaviour change for code that ignores the globals. |
| `bad2009` | `include/arch/aarch64/limits.h` | `PTHREAD_STACK_DEFAULT = 256 KiB` (the arch-specific *value* of the generic mechanism above) |
| `0b20a2a` | `arch/aarch64/reboot.c` | Generalise `reboot()`/`reboot_reason()` beyond ZynqMP: add a `__CPU_GENERIC` branch and use the `pctl.task.reboot` union member for non-ZynqMP |

`phoenix-rtos-corelibs`: **`d026ff0` adds `libdbg`** — `dbg_init()` / `dbg_backtrace(tag)` /
`dbg_arm_watchdog(secs)`, printing the interrupted PC and an x29 frame-pointer walk over UART on
SIGSEGV/ILL/BUS/FPE/ABRT or a SIGALRM watchdog tick, so a fault *or a hang* names real code with the
board still booted (symbolise host-side with `addr2line`; link with `-fno-omit-frame-pointer`). It is
**not standalone**: it depends on libphoenix `75c60e7`, and the frame walk is `#ifdef __aarch64__` so the
library still builds elsewhere. `2311290` adds `STORAGE_DEEPFS_STACKSZ` (16 pages) with a header comment
recording why: `storage_run()` carves all worker stacks as adjacent slices of one `malloc` with no guard
page, so the copy-pasted 8 KB convention overflowed on an ext2-over-SD handler chain and surfaced as a
bogus ext2 list-corruption crash.

### Testing improvements (phoenix-rtos-tests)

**Tests that were themselves wrong** — the most immediately useful findings for maintainers:

* ★ `99d6690` — `libcache` disabled itself after its first run. `test_genCharFile`/`test_genIntFile`
  returned the initial `ret = -1` when the source file already existed, and the runner gates every group
  on `ret > -1` with no else — so on any persistent filesystem the suite printed `0 Tests 0 Failures /
  OK` forever after the first run. It had been vacuously green on the Pi's NFS root for as long as those
  files existed.
* ★ `92299b0`, `9b02f1f` — `test_mmap`/`test_malloc` spawned soak workers with **1024-byte** stacks; each
  worker calls `test_printf` on top of its locals, so the crashes (Instruction Abort at pc=0, EL0/EL1
  aborts) were the tests' own stack overflows, not kernel bugs. Bumped to 16 KiB.
* ★ `991d70c` — `test_condwait` never locked the mutex before `condWait`, which must atomically release
  it, so the kernel correctly returned `-EPERM` and the test reported FAILED against correct code.

**Un-gated cases, closing known upstream issues.** Six commits remove `TEST_IGNORE` guards now that the
functions exist: scanf `%m` (`28bec6a`), `memccpy`/`stpncpy`/`strtok_r`/`psignal` (`8d2e0d5`), `strftime`
padding and extra specifiers (`bb3c12c`, `881a439` — **#351**), `fstatvfs` `EBADF` (**#1632**) and
`statvfs` trailing slash (**#1723**) (`5037bda`), and a stale printf guard (`8a544cd`).

**Regressions that lock in a specific fix** — each written to fault on regression rather than corrupt
quietly: `5982203` → `5fa3847` (double `fclose` is not a wild write); `4b06a2b` → `c8ee89e` (detached
stack teardown/reclaim); `adcceba` + `291708a` → `02ab4e0`/`bad2009` (guard page present, explicit 0 still
honoured, default stacksize ≥ 64 KiB *and* a default thread actually runs an 8 KiB frame — asserted on the
attribute, since a real overflow kills the process the test would have to report from); `eadbf4c` →
`f6489b8`; `ca616da` → `033ee1f`; `90117b1` fork/COW isolation (guards the kernel's page-fault protection
derivation); `68095fe` — a closed-but-not-unlinked AF_UNIX name must not reach a *live* socket, and each
case asserts both the expected failure **and** that a bystander socket received nothing, because
cross-delivery otherwise hides behind a reported error.

**New suites**: `libc/semaphore` (counting semaphore, multi-waiter burst — the `e75c4fe` case);
`libc/pthread` +620 lines covering spinlocks, mutex attributes (recursive relock, errorcheck →
`EDEADLK`), robust-mutex owner-death → `EOWNERDEAD` → `pthread_mutex_consistent` (all exercising the
2026-08 upstream `mutexCreateWithAttr` work, previously untested), and fd-sweep-vs-`open()`/`socket()`
races; `libc/math` (`c99extra`, `erf`, `gammaextra`, `round`, `exp` — which caught `7b22fa3` and
`7ca437b`); `libc/misc` (`dlopen_self`, `statfs_basic`, `resource_limits`, `mlock_noswap`,
`rusage_times`, `unistd_sysconf`, `stubs_fixed`, the `*at` family, `fchdir` success *and*
never-false-succeed); `libc/string` wide-char/wctype and `strerror` text; `printf/snprintf_sizing`
(the exact `vsnprintf(NULL, 0)` contract `839b24b` depends on); `libc/time` `strptime` and `timeval`.

**Harness work**: `b63c495`/`99d28b7`/`7c913a3` encode `posix_open`'s new race contract (every open
either succeeds or returns `EBADF`, and the sum is non-zero so the test cannot pass vacuously) instead of
asserting one side of a timing race that NFS-root latency always loses; `83eda31`/`097ae7a` add a
64 KiB parent-filled canary and per-operation snapshots of stdio's globals, reporting via raw `write(2)`
and `_exit()` because a child crashing *while formatting its own failure* is indistinguishable from one
that died silently; `f9e3102`/`fd4dcbe`/`17dd8be` make socket failures say what failed and flush per test
so a stalled run still shows progress.

### Known limitations and workarounds

Flagged so nothing above reads as more finished than it is.

* `8dc40bb` + `e643fa5` are **hardening of an escalation path, not a root fix**. `__cxa_finalize` now
  refuses to walk a visibly corrupt handler list, but the corruption that produces `idx == 180` against
  `ATEXIT_MAX == 32` on the Pi 4 is an open kernel-heap investigation in the coordination repo.
* `ae9801e` enforces `unlink`-rejects-directory **client-side** with an extra `lstat` round-trip; the
  correct fix is a dir check in the five fs servers' `mtUnlink`, which was declined as too wide.
* `c01f7a7`'s `rename()` replace is **non-atomic** — Phoenix has no rename operation.
* `491618c` deliberately reports `ANSI_X3.4-1968`, not UTF-8: `mbrtowc`/`wcrtomb` map bytes 1:1 with no
  UTF-8 decoder, so advertising UTF-8 would push ncurses/mc/vim onto a path the libc cannot back.
  Reversible when real multibyte lands.
* `676234a` reports `f_type = 0` (no per-fs magic); `3b45a15` reports `RLIM_INFINITY` (nothing is
  enforced); `ec9afd0`'s `mlock` family is a no-op (no swap).
* dl is **Phase A**: relocation is eager (`RTLD_LAZY` accepted as `RTLD_NOW`), the host must be linked
  **unstripped** (its `.symtab` is read off disk via `argv[0]`, valid verbatim only because Phoenix has
  no ASLR), and there is no `PT_INTERP`/auxv or dynamic TLS.
* `open_enough_dirs` (#1610) still cannot pass (`4c94672`): Phoenix `opendir` is oid/message-based and
  consumes no kernel fd, so it never reaches `EMFILE` — a design difference, not addressed.
* `libm/phoenix` remains incomplete against C99 (upstream's own `libm/README.md` says so); the fork
  filled only what its ports needed.
* One transitional marker survives: `TODO(TD-14-console-open-fastpath)` in the `/dev/console` open path.
  The rest of the TD-12/13/14 UART trace probes were added and stripped again within this range and
  leave no net diff, as does `d5461a9` (reverted by `4b5cc61`).
## Device drivers, USB, networking, filesystems

This is the largest and most self-contained body of work in the fork: roughly 44 000 added lines across
`phoenix-rtos-devices` (400 commits, +36 863), `phoenix-rtos-lwip` (+3 992), `phoenix-rtos-filesystems`
(+2 939) and `phoenix-rtos-usb` (+478/-67). Almost all of `phoenix-rtos-devices` is *new files*: upstream
has no `audio/`, `bt/`, `gpu/`, `misc/`, `video/` or `wifi/` top-level directory at all, and `usb/` holds
only `ehci`, `cdc-demo` and `libusbclient` — so the Pi 4's xHCI host-controller driver is a new HCD for
Phoenix rather than a port of an existing one. `phoenix-rtos-usb` is the mirror image: no new source files,
37 commits of pure framework work on the shared DMA allocator, hub and enumeration paths. Alongside the
board work there is a new userspace NFSv4 client filesystem (`filesystems/nfs/`, 2 700 lines) that lets a
Phoenix box boot with `/` on an NFS export, and a BCM2711 gigabit Ethernet driver for lwIP.
Note on scope: the BCM2711 HEVC (`rpivid`) hardware video decoder lives outside these four repos
(`tools/hevc-decode/` in the coordination repo); it is covered in
[Hardware experiments outside the Phoenix repos](#hardware-experiments-outside-the-phoenix-repos).

### 1. New Pi 4 on-board device drivers

All are userspace servers in the standard Phoenix idiom (`mmap(MAP_PHYSMEM)` + `portCreate` +
`create_dev` + message loop). ★ marks code a maintainer on a non-Pi target would plausibly want.

| device | driver path | exposes | state |
|---|---|---|---|
| ★ VL805 xHCI USB 3.0 host controller + BCM2711 PCIe bridge | `devices/usb/xhci/` (`xhci.c`, `bcm2711-pcie.c`) | `libusbxhci` HCD behind the `usb` daemon | Working: full ring/slot/endpoint model, control and interrupt-IN transfers (no bulk/isochronous path in the driver), root-hub and behind-hub addressing, error recovery. Reliable enumeration after the two-step-BSR fix (§3). |
| V3D 4.2 GPU (Mesa gallium + V3DV) | `devices/gpu/rpi4-v3d/` | in-process winsys (`mesa/v3d_phoenix_winsys.c`) or `/dev/v3d-srv` + `libv3d-client` | OpenGL, OpenGL ES 3.1 and Vulkan render on HDMI. No DRM, no kernel GPU driver, no Vulkan WSI. See note. |
| VideoCore property mailbox | `devices/misc/rpi4-vcmbox/` | `/dev/vcmbox` + `libvcmbox` | Complete and the mandatory path — see note. |
| HDMI framebuffer | `devices/video/rpi4-fb/` | `/dev/fb0` (read/write + `RPI4FB_GETMODE`) | Byte read/write and geometry only. Deliberately **no** `FBIOGET_*` veneer and **no** `mmap(fd,0)` of the surface (needs new kernel VM work); no arbitration against the boot console. |
| ★ HDMI framebuffer console + PL011 UART tty | `devices/tty/pl011-tty/` (+ vendored `teken/`) | `/dev/tty0`, `/dev/console`, `FBCONSETMODE` | Full VT100/xterm console driven by FreeBSD `teken` (BSD-2). GNU nano and mc render correctly. |
| BCM2711 EMMC2 SD card | `devices/storage/bcm2711-emmc/` | `/dev/mmcblk0`, ext2 root | Boots from SD. UHS-I DDR50, 128 KiB multi-block transfers, DMA **reads** only — writes stay on PIO (§4). |
| BCM43455 SDIO WiFi | `devices/wifi/rpi4-wifi/` (4 805 lines) + `lwip/drivers/wifi43455.c` | `/dev/wifi` (text scan/ctl), `/dev/wifidata` (raw frames), `wifi` CLI, lwIP netif `wl2` | Firmware download, WPA2 join via the firmware supplicant, full-MTU data path, DHCP lease over the air. Throughput is poll-bound (§4). |
| BCM43455 Bluetooth | `devices/bt/rpi4-hci/` | `/dev/hci0` (raw H4 HCI), `btctl` | Controller reset, patch-RAM upload, `BD_ADDR`, HCI inquiry. Raw HCI byte stream only — no host stack (L2CAP/GAP) above it. |
| PWM audio (3.5 mm jack) | `devices/audio/rpi4-audio/` | `/dev/audio0` (s16 PCM write) | Self-chained DMA ring, DREQ-paced, with playback-rate backpressure and PIO fallback. No `snd` backend; audible sign-off is attended. |
| SoC thermal / throttle | `devices/sensors/rpi4-thermal/` | `/dev/thermal`, `/dev/throttled` | Complete for what the SoC allows: telemetry only, the VideoCore firmware owns the trip point. Then used to answer the obvious question about a passively cooled board rendering 3D: **6.3 min under QuakeSpasm, 35.0 °C → a 53–55 °C plateau (flat from t = 240 s), `throttle=0x0` on all 19 samples**, 0 faults — no under-voltage, no ARM capping, no sticky bits, ~5 °C of headroom to the first soft cap. Limits: 6.3 min not 30, one board, open-air bench. |
| Hardware RNG (iproc RNG200) | `devices/misc/rpi4-hwrng/` | `/dev/hwrng` | Complete; backs `/dev/urandom` and `getentropy`. |
| GPIO | `devices/gpio/rpi4-gpio/` | `/dev/gpio` (snapshot), `RPI4GPIO_GETPIN` | **Read-only by design.** Driving outputs needs a bench rig and is deferred. |
| ★ USB HID keyboard / mouse | `devices/tty/usbkbd/`, `devices/tty/usbmouse/` | `/dev/kbd0`, `/dev/mouse0` | Cooked ASCII stream *and* a raw 8-byte HID report mode (so a game can see key-up). Hosted inside the `usb` daemon; `N_URBS=1`, so the interrupt path is lossy under fast input. |
| BCM2711 GENET v5 gigabit Ethernet | `lwip/drivers/bcm-genet.c` (2 038 lines) + `drivers/ephy.c` | lwIP netif `en1` | Working and fast (§4). Multi-slot TX, 256 unique RX buffers, optional cacheable RX/TX, TX pipelining, RX input batching. |
| BCM54213PE PHY (*extension to the existing `ephy` driver*) | `lwip/drivers/ephy.c` (+185) | — | Reads speed/duplex from the Auxiliary Status Summary (0x19) and programs RGMII RXC-RXD skew via the MISC shadow register. `INT_B` is not routed to a GIC SPI on this board, so link state is MDIO-polled (as Linux and U-Boot do). |

Support daemons, not devices: `misc/rpi4-klogd` (klog ring → `/var/log/messages`, the file half of a
DEBUG/USER logging split), `misc/rpi4-sysinfo` (boot banner + device-node inventory),
`misc/rpi4-ipcprobe` (one-shot AF_UNIX / `getrandom` readiness probe, not a default component).

**The mailbox server is load-bearing.** The BCM2711 has one VideoCore property-mailbox FIFO at
`0xfe00b880` with *no hardware arbitration*, and it is the only route to SoC temperature, the board MAC,
throttle state and power/clock for V3D, HVS and USB. At boot, thermal, genet, the USB daemon's VL805
bring-up, the SDIO driver and V3D power-on all want it; two concurrent readers pop and discard each
other's response word. `rpi4-vcmbox` owns the FIFO and serialises every caller — a Phoenix server handles
one message at a time, so serialisation is free — reusing a single early-allocated low-PA uncached bounce
buffer so it stays VideoCore-addressable on 4/8 GB boards. This "one server owns the unarbitrated
peripheral" pattern is reused verbatim by the V3D server, and is the generalisable idea here.

**V3D, honestly.** Mesa's `v3d` gallium driver and `v3dv` (Vulkan) are cross-compiled against Phoenix and
driven through a hand-written winsys that implements `DRM_IOCTL_V3D_*` directly on the hardware — BO =
`mmap` + `va2pa`, GPU VA through the V3D MMU's flat page table, `SUBMIT_CL` = CT0/CT1 QBA/QEA plus
`FLDONE`/`FRDONE` and an L2T flush. There is no DRM layer and no kernel GPU driver; submits are
synchronous, so `drmSyncobj*` is stubbed. The Mesa archives are built by standalone Python scripts
(`mesa/build-{v3d,gl,v3dv}-phoenix.py`) that re-emit the host Mesa build's `compile_commands.json` with
the Phoenix toolchain — they are *not* wired into the framework Makefiles, which cannot run meson/ninja.
`/dev/v3d-srv` (`rpi4-v3d.c` + `v3d_gpu.c`) is the multi-client answer: it takes sole ownership of the
GPU's single MMU page-table base, submit registers and power domain, and clients route MMIO-touching
ioctls to it. It is HW-proven and built as a first-class component, but it is **not auto-launched** —
taking exclusive ownership conflicts with the in-process-winsys GPU apps that currently ship, so the
shipping path today is one app at a time with the winsys linked in-process.

Three submit engines are implemented, not one. `DRM_V3D_SUBMIT_TFU` and `DRM_V3D_SUBMIT_CSD` each
started as an unhandled ioctl falling through to `default: return 0`, so *every* Texture Formatting
Unit job and *every* compute dispatch was a silent no-op with the destination left unwritten. TFU is
the buffer→image path V3DV uses for every tiled texture upload, blit and mipmap generation, so
Vulkan textures were simply black until `ioc_submit_tfu` existed — Mesa carried a matching
`DISABLE_TFU` force in the meantime and dropped it once the ioctl landed. (CSD's own story is §7.)
The hardware findings inside these paths are the transferable part:

- **GPU VA-window sizing is a bug class, not a tuning knob**, because the MMU just walks a flat
  `PT[va>>12]` array from `MMU_PT_PA_BASE` with no page-table-length register to bound it. The
  table began as a single 4 KiB page = 1024 PTEs = 4 MiB of GPU VA, so a 1024×768 colour + depth
  target (3 MiB each) overflowed it: `create_bo` wrote PTEs *past the table* **and** placed the BO
  at an unmapped VA, so the GPU store landed nowhere and the render target came back all-zero —
  misread for a while as tile-state sizing. It is now 256 pages = 1 GiB with a loud bounds guard
  instead of silent corruption (`v3d_gpu.c:156`, `4bfc06a`), the in-source rationale recording
  that a full SuperTuxKart race exhausts 256 MiB of it.
- **Unacknowledged QPU interrupts stall fragment dispatch.** Linux clears them on every IRQ; this
  driver polls, so it now W1Cs only the QPU bits mid-render (never bit 0, so `FRDONE` cannot be
  lost). In-source and HW-verified: "this collapsed the STK in-game render wedge **330 -> 0**"
  (`v3d_gpu.c:1080`, `5be4655`).
- **Binner wedges came in two sub-modes and neither register appears in Linux's V3D 4.2 path.**
  One is overflow exhaustion: the binner spills per-tile primitive lists when Mesa's initial
  `tile_alloc` runs out, and a fixed 4 MiB pool was enough at 1024×768 (~192 tiles) but not at
  1080p (~510), where a heavy scene exhausted it and the binner wedged *every* frame
  (`OUTOMEM|SPILLUSE`, ~3 fps) — now a persistent 32 MiB pool handed over whole on the first
  `OUTOMEM`, with the residual case logged rather than hidden (Linux instead allocates a fresh
  256 KiB BO per event, unbounded). The other is the QPU bin-vs-render reserve: `CTL_MISCCFG`'s
  `QRMAXCNT` field is now written once at init, low favouring fragment shaders and high favouring
  the coordinate shader, tuned to a value with 0 bin *and* 0 render wedges. In-source note worth
  keeping: **Linux on V3D 4.2 never writes `MISCCFG` at all**, so this is a deliberate divergence.
- **Submits were unserialised** (`e8a72e9`) — vkQuake submits from threads — and BOs of clients
  that died without `GEM_CLOSE` were never reclaimed (`1eb8608`; see the platform gap about client
  death). Measured across two X lifecycles in one boot: second-session cost **15.7 MB -> 2.3 MB**
  and **+85 -> +8** map entries, with `reaped 80 BO(s) from exited client(s)` logged. Honest
  scope, from the same write-up: whether the leak grew linearly was never established.
- **Mesa's on-disk shader cache was five no-op stubs**, so every GL app recompiled its shaders on
  the V3D each boot; `v3d_phoenix_stubs.c` now implements it (BLAKE3 keys, one file per key,
  atomic temp+`rename`, a version-segmented `/vN` directory). HW: a cold boot wrote 52 blobs, a
  warm boot hit all 52 with no recompile. Its failure mode is worth carrying — see the caveats.

Mesa itself is patched, and not only for Phoenix. Two of the 16 carried commits are ordinary
upstream `u_vbuf` defects that would bite any gallium driver on a non-x86 host: a missing NULL
check on the translate object (`translate_generic_create` can fail, and aarch64 has no
`translate_sse` to fall back on — quake3 crashed mid-combat), and a draw silently dropped when
`u_vbuf_translate_begin` fails on the index-unrolling path (`goto out` skips `draw_vbo` and
`debug_warn_once` is inert in release builds — Quake III's world rendered black while player
models stayed correct). A third is an upstream V3D limitation rather than a Phoenix bug: the TFU
multi-level mipmap generator supplies OPAD only for level 0 and *infers* the tiling of levels ≥ 1,
and that inference matches `v3d_setup_slices`/the TMU only for power-of-two bases — so Quake II's
NPOT model skins (banner 260×195, soldier 288×195) scrambled at distance while every POT texture
was correct. Fix: decline the TFU fast path for NPOT bases and fall back to render-based mipmap
generation. Also fork-local and load-bearing: Early-Z is force-disabled on Phoenix, because a
depth-tested *tilted* quad hung the GPU while a parallel one was clean — the commit records
"avoids constant depth-drain render wedge (5fps->38fps)", so it is a live divergence from upstream
Mesa with a measured cost and benefit.

### 2. Changes to existing Phoenix drivers and subsystems

- `devices/pcie/server/pcie.c` (+763): BCM2711 host-bridge support for the generic PCIe server — an
  indexed config-space backend, root/downstream bridge window shaping, link-state gating, and the
  `NOTIFY_XHCI_RESET` mailbox call the firmware needs before VL805 is enabled. **Caveat:** the Pi 4 does
  not use this daemon. Bridge bring-up was folded into the `usb` daemon via
  `libusbxhci`'s `bcm2711_pcie_initVL805()`, and `pcie` is deliberately absent from the a72 target. The
  code stands as the separate-daemon pattern for a future a72 board.
- ★ `devices/tty/libtty/libtty.c`: `FIONREAD` implemented (was `-EINVAL`) — see §3.
- `devices/libklog/libklog.c`: reworked so the log drain can be owned by a tty driver that
  attaches directly to the kernel log port `{0,0}` instead of going through a `/dev/kmsg` devfs node
  (nothing registers one on this board).
- `filesystems/dummyfs/srv.c`: srv-init stabilisation and cleanups; `dummyfs` is the pre-takeover RAM `/`.
- ★ `filesystems/ext2/`: fs-global operation serialisation — see §3.
- ★ `lwip/port/`, `lwip/include/arch/`: `sys_mbox_trypost_coalesce`, `dmammap_cached`, the netif-driver
  list exposed for out-of-tree netifs, `/dev/ipstats`, socket-layer fixes (§3), and lwIP checksum
  algorithm 3 as the default (word-at-a-time; a measurable win on the A72, no-op where overridden).
- ★ `usb/` (all 37 commits): the shared USB framework — DMA allocator, hub, enumeration. See §3 and §5.
- Known divergence worth stating: the xHCI HCD registers **no interrupt handler at all** — it is
  poll-only, where the EHCI HCD it sits beside is interrupt-driven. That is an SMP performance item
  and a difference an upstream reviewer should see stated rather than discover.

### 3. ★ General bug fixes

These are defects that would bite any target: mostly in shared Phoenix code, plus a few in the new
Pi 4 drivers whose *mechanism* generalises even where the register does not.

**USB / xHCI**

- ★ `usb/mem.c` `12c4fe8` — stale cache lines in recycled DMA pages. `usb_allocUncached` maps
  `MAP_UNCACHED` physical pages a prior *cached* owner had dirtied; those stale dirty lines wrote back
  over the uncached pool *after* it was initialised, smashing the free-list with other processes' boot
  banners and failing USB bring-up ~1 boot in 8. Fix: one `dc civac` over the freshly mapped region
  (EL0-legal via `SCTLR_EL1.UCI`; the A72's PIPT D-cache acts on the physical line regardless of this
  mapping's attributes). 0 events in ~15 cold boots.
- ★ `usb/dev.c` `7259b26` — a control transfer's buffer overflowed into the adjacent DMA-pool
  free-chunk header.
- ★ `xhci.c` `255ce87` — interrupt-IN delivered exactly one report, ever. The submit path `memset` the
  ring and rewrote slot 0 with a *fixed* producer cycle bit on every submit, never advancing an enqueue
  pointer; after the controller followed the Link TRB and toggled its dequeue cycle, every resubmit
  looked un-owned. Replaced with a real circular producer. This is why a USB keyboard registered only
  the first keypress.
- ★ `xhci.c` `53383d1` — single-step (`BSR=0`) Address Device intermittently wedges the VL805's command
  processor: EnableSlot completed, then Address Device produced *no* completion event at all (a
  non-responsive device yields an *error* completion, never a missing one). Linux issues `BSR=1`
  (setup-context-only) then `BSR=0`; doing the same took Address-Device timeouts from ~3 of 4 cold boots
  to 0 in ~15.
- ★ `xhci.c` `e371967` — no error recovery existed at all: Disable Slot, Reset Endpoint and Set TR
  Dequeue Pointer appeared nowhere, and pipe destroy freed only software state. So the framework's
  enumeration retry re-drove a slot the hardware still held halted (one transient Split Transaction
  Error → Context State Error on both retries), costing roughly one boot in three its keyboard and
  mouse. Now Disable Slot is issued when the default control endpoint (DCI 1) is torn down.
- ★ `xhci.c` `4576e72` — PORTSC RW1C over-clear: the four `C_*` change-bit clears wrote back the *other*
  change bits still set, so a sibling port event that raced the write was silently cleared and lost.
  Same commit: the shared per-controller `inputCtx` scratch buffer was reallocated on every
  `allocSlotSpace`, leaking one buffer per device behind a hub.
- ★ `usb/hub.c` `47eede9` — no reset-recovery delay. Enumeration ran EnableSlot/Address Device the
  instant port reset completed; USB 2.0 §7.1.7.5 mandates TRSTRCY ≥ 10 ms before a device accepts
  SET_ADDRESS (Linux waits ~50 ms). 50 ms added.
- ★ `usb/hub.c` `03bd903`, `3c7fdb2` — a device that failed enumeration was retried forever, because
  each failed attempt reset the port and the port-status change re-triggered enumeration; the resulting
  transfer-timeout flood rebooted the board (~85 resets observed). Per-port failure counting with a
  give-up limit fixes that, and `3c7fdb2` fixes the follow-on: the counter was only cleared when a
  *tracked* device disconnected, and a device that never enumerated was never tracked — so a port that
  hit the limit ignored every later replug, permanently.
- ★ `xhci.c` `148bf48` — **an event-ring consumer must key on the TRB, not on (slot, endpoint).** A
  short control-IN read emits a `SHORT_PACKET` event on the Data TRB *in addition to* the Status IOC
  event, but `ep0ControlRead` consumed one event per call and matched only on (slot, ep0) — so
  transfer *N* matched *N−1*'s Status TRB. The drift began at the first short string-descriptor read,
  and every later read then returned a stale residual-0 completion *before its own data landed*: the
  hub-class GET_DESCRIPTOR reported 15 bytes into an all-zero buffer, `bNbrPorts` came back 0, and
  `calloc(0, 8)` made the log say **`usbhub: Out of memory!` with ~3.8 GB free**. Both ep0 paths now
  drain the whole TD's events until their own Status-TRB physical address, which also clears events a
  prior transfer left behind (in-source at `xhci.c:2622`).
- ★ **A missing barrier before the doorbell** — a generic aarch64 driver hazard, not a Pi 4 one. The
  command ring had `dsb sy` before its doorbell; the ep0 and interrupt doorbells did not, so
  Normal-NC TRB writes could reorder past the Device-memory doorbell write and the controller could
  DMA-read a ring whose newest TRB or cycle bit had not landed. Centralised into `xhci_dbWrite32()`
  (`xhci.c:538`), mirroring Linux's wmb-before-doorbell.
- ★ **No command-ring recovery existed**, so a reported "3/3 retries failed" was one hung command
  plus two unreachable retries: a command the controller dequeues but never completes parks its
  dequeue on that TRB while the producer runs ahead, so the framework's retry loop never
  re-executed anything. `xhci_cmdRingRecover()` (`xhci.c:1631`) does CRCR.CA abort → wait CRR=0 →
  drain the event ring → re-init, which makes retries real *and* exposes the true per-attempt
  failure rate.
- **A behind-hub low-speed device needs a Configure Endpoint on the *hub's own* slot context.**
  Beyond the route string and TT fields, the controller will not route split transactions below a
  hub until that hub's slot context carries `Hub=1` + `NumberOfPorts` — and it must be a Configure
  Endpoint, not an Evaluate Context (xHCI §4.6.6). Also required: per-slot interrupt pipes (a single
  shared `interruptPriv` was hub-owned) and `N_STATUSTHRS` 1 → 2 so the host actually ran the
  URB-completion consumer.
- **PCI `BUS_MASTER` clear silently drops MMIO writes to operational xHCI registers.** In-source at
  `bcm2711-pcie.c:1033`: on the BCM2711 root + VL805, leaving `BUS_MASTER` clear lets writes to
  DCBAAP, CRCR and CONFIG be dropped after HCRST *while capability reads still work*. A write that
  vanishes while reads keep working is a nasty debugging shape.
- **HCRST does not clear the VL805's internal CRCR** — which is *why* one daemon owns both the
  bridge and the controller (the doc reports that design in §2 without the cause). A boot-time
  daemon that programmed a command ring and exited left the controller answering a later process's
  fresh ring with Command Completion Events carrying the **old** ring's physical address, even
  though post-HCRST `USBSTS` read clean. So no process may touch the controller before the one that
  intends to drive it; hence the `--bridge-only` mode. Traceability caveat: the evidence is a
  2026-05-28 UART capture, not a surviving in-tree comment.
- `usb/dev.c` `e0911ce` — `sprintf` of `/dev/usb-%04x-%04x-if%02d` into `char[32]`; `%02d` is a *minimum*
  width, so a corrupt interface number overflowed the stack buffer.

**GENET (gigabit Ethernet)**

- ★ `bcm-genet.c` (lwip `55090e6`) — **gigabit netboot was 100 % broken until the RX status prefix
  matched the firmware, not Linux.** This is a hard functional failure, unrelated to the throughput
  work in §4. The 64-byte status-block path in the RBUF **RX front-end** (`RBUF_CTRL.RBUF_64B_EN`)
  dropped a frame that closely trailed the Pi's *own* TX at gigabit — no FCS error, MIB-uncounted,
  gigabit-only — so the peer's reply at ~100 µs was lost and only its ~1 s RTO retransmit arrived,
  which is what timed out every NFS mount. Fix: drop `RBUF_64B_EN` and keep `RBUF_ALIGN_2B` for
  lwIP's `ETH_PAD`, taking the RX status prefix 66 → 2 (in-source at `bcm-genet.c:927`). After it,
  NFS mounts at gigabit with 0 failed attempts, the **SYN→SYN-ACK gap collapses 1 s → 7 ms**, drops
  4 → 0, and a full 128 MB read completes (134 217 728 B in 15.334 s), reliable over n=3 mounts.
  Two things about *how* it was found are the reusable part: it came from a register-by-register
  diff of the VideoCore firmware's known-good GENET state against ours (`rbufctl` fw `0xc040` vs
  phx `0xc043`), because **our configuration matched Linux and the firmware's did not**; and the
  first candidate from that diff was a recorded negative — disabling the RX checksum checker to
  match the firmware changed nothing ("gap still ~1.0 s"), so the checker should not be
  re-suspected. The 8.5 MB/s that followed is the separate pre-existing NFS-read ceiling, which is
  the work §4's 29.9 MB/s figure comes from.

**tty and filesystems**

- ★ `libtty` `b247643` — `FIONREAD` fell through to `-EINVAL`, so any program asking how many input bytes
  are pending got a failed ioctl and a garbage count (readline/bash rely on it). Returns
  `fifo_count(rx_fifo)`.
- ★ `ext2` `463aec1` — the block and inode allocators mutate fs-global state (on-disk bitmaps, the
  in-memory group-descriptor table, superblock free counts) with a non-atomic
  read-bitmap/toggle/write-bitmap/update-counts sequence, and *nothing* protected it: `obj->lock` and
  `objs->lock` cover objects, not the filesystem. Under a multi-worker server two threads interleave and
  corrupt the bitmaps — a deterministic Data Abort in `ext2_block_destroyone` under concurrent-write
  stress. Fixed with one per-fs mutex taken at the `libext2_*` entry points, which are never re-entrant,
  so it is trivially the outermost lock (`fs->lock > {obj->lock, objs->lock} > storage`).
- ★ `nfs` `fc2f62b` — an upstream **libnfs 6.0.2** bug worth knowing about: `readlink_cb` records
  `-ENAMETOOLONG` for an over-long target, then `cb_data_is_finished()` overwrites `status` with the RPC
  success code, so `nfs_readlink` returns phantom success with the buffer *unmodified*. Any
  `readlink`/`realpath` of a symlink longer than the caller's buffer resolved to stale garbage. Fixed by
  staging through a `PATH_MAX` buffer and applying POSIX truncation locally.
- ★ `lwip/port/sockets.c` `7428162` — three independent socket-layer defects: `FIONBIO` read the flag
  before `lwip_ioctl` could set it (so the socket stayed blocking), `getnameinfo` could write past the
  caller's buffer when only the host *or* only the service was requested, and `getifaddrs` did not
  report all interfaces.
- The ~20-commit V3D "corrupt control list" arc (`6502f67`, `1e0d1c2`, `6c1e321` and the diagnostics
  around them) converged on two lifetime bugs in the winsys' own BO table rather than anything in the
  GPU: BO handles and closed BOs' CPU addresses were being recycled, so a stale handle resolved to a
  live BO and a new mapping landed on a live one. Fix: never recycle handles or mappings, and invalidate
  a BO's PTEs on close.
- ★ **A general V3D tiling rule, learned twice.** The fork's own `should_tile` optimisation forced
  large `PIPE_BIND_RENDER_TARGET` surfaces to RASTER so `glReadPixels` stayed a fast linear copy —
  but Mesa marks **every** renderable RGBA8 texture `PIPE_BIND_RENDER_TARGET`, so Quake III's 1024²
  *sampled* merged-lightmap atlas was laid out linear and the TMU sampled linear-as-UIF: the
  q3dm7 "lightmap-black" bug, plus Quake II floor speckle and vkQuake striping. (A 512² atlas
  escaped only because it was under the width gate, which is why the smaller map looked fine.)
  Confirmed at descriptor level by a runtime `V3DTEX` dump — pre-fix the 1024 atlas read
  `tiling=RASTER`, post-fix `tiling=UIF_XOR`, identical to the working 512. Adding
  `!(bind & PIPE_BIND_SAMPLER_VIEW)` to the gate then caused a **worse** second bug, which is the
  instructive half: Mesa stamps `PIPE_BIND_SAMPLER_VIEW` on *every* renderbuffer
  (`main/renderbuffer.c:276`), so the fullscreen scanout target was excluded too and left tiled,
  and the HVS display engine — which scans LINEAR only — read it as linear, shredding every GL
  game's HDMI output into horizontal stripes. Fixed by having the winsys flag the next scanout so
  the gate forces RASTER regardless of the spurious bit. **The transferable rule: anything the
  display engine scans out must be RASTER; anything the TMU samples must stay tiled.** Note what
  masked the regression — the frame-dump SSIM path reads back through a tiling-*aware* GPU blit and
  scored 0.993 while the actual HDMI output was garbage.
- ★ **`glGenerateMipmap` is pathologically expensive on this port**, because every mip level is a
  separate TFU copy plus an L2T flush. yQuake2's gl3/GLES3 renderer mipmaps every texture and so
  took 7+ minutes to load a map, never reaching the 3D view; defaulting `gl3_nomip=1` (override
  `YQ2_GL3_MIPMAP=1`) drops that to seconds and produced the first full textured first-person
  GLES3 frame on HDMI, at a stated cost of bilinear rather than trilinear filtering. This is a
  reusable per-level cost model for any GLES client on V3D, and it is the GPU-side half of the
  "asset load is slow" note on the yquake2 row.

### 4. Performance

| what | before → after | how |
|---|---|---|
| GENET / NFS throughput | NFS read **29.9 MB/s**, write **19.7 MB/s** on a Pi 4B | 256 unique RX buffers (aliasing was corrupting RX), multi-slot TX, TX pipelining, RX input batching, cacheable TX mapping via `dmammap_cached`, `recvmbox` coalescing |
| SD card read | single-block → **~16×**; then DDR50 20.1 → **33.4 MB/s**; then 128 KiB transfers → **38.3 MB/s** | CMD18/CMD25 multi-block with Auto-CMD23, SDMA read path, UHS-I DDR50 (ACMD41 S18R → CMD11 voltage switch → CMD6 → HC2), `SDCARD_MAX_TRANSFER` and the libcache sector raised to 128 KiB |
| SD card write | ~11× multi-block, ~17.4 MB/s at the DDR50 clock | stays on **PIO**: SDMA writes silently corrupted the first block 1–2 runs in 10 on this Arasan/BCM2711 controller (reads are exact), so DMA writes are gated off deliberately |
| NFS RPC latency | **20×** overall slowdown removed | `libnfs poll_timeout` was 100 ms, and the socket `poll()` never woke on readiness — every RPC ate a 100 ms stall. Set to 1 ms. |
| NFS path resolution | d(d+1)/2+2d+1 round trips → linear in depth | `_resolve_abspath` walks a path prefix by prefix and `nfs_ops_lookup` re-`lstat`'d every component of each prefix. A 5-component `stat()` spent ~36 ms re-asking about `/usr`, `/usr/share`, … Fixed with a deliberately short (100 ms) per-node positive attribute cache, invalidated on every mutation and on NFSv4 reclaim. |
| NFS directory scan | ~38 ms *per* `readdir()` → one listing per scan | `nfs_ops_readdir` opened the directory, walked to the cookie, emitted one entry and closed — a full READDIR round trip per entry. On a 649-entry export directory one scan cost ~25 s. The snapshot is now held on the node across a sequential scan. |
| NFS per-RPC size | 32 KB → 1 MB | raises large sequential read/write ceiling |
| WiFi | RX and TX both to low single-digit MB/s | SDPCM glom de-aggregation (below) plus RX poll cadence. **Do not quote a single figure:** the fork's own `fcb4311` retracts its earlier numbers after identical code re-measured 2.6× apart. |

WiFi throughput is worth a note because the mechanism is clear even where the numbers are not. The
firmware bundles received frames into superframes on SDPCM channel 3 and the driver had no
de-aggregation, so it dropped ~18% of all inbound frames and left TCP to retransmit them; asking the
firmware to stop (`bus:rxglom = 0`) is accepted and ignored. The wire format was decoded from a hardware
dump after three guesses failed. What remains is not the radio: the idle RX poll interval sets the
ceiling (an N µs sleep caps you near one frame per N µs), and each frame costs a message round trip plus
a backplane window setup plus four PIO SDIO transfers. An event-driven read and F2 DMA are the named
next levers. Two other measured findings were kept but explicitly *disproved* as the cause: SDPCM credit
windows never closed (`blocked=0`), and byte-mode RX is 5× faster than block mode here.

One bound on all of the storage numbers, because it says where *not* to optimise: **NFS is
latency-bound, not bandwidth-bound, and the floor is not a Phoenix defect.** `tools/nfs-bench`
measured a random 4 KiB NFS read at **1.46 ms (687/s)** against tmpfs's **0.07 ms (14 025/s)** —
about **20×** — and a host-side `tcpdump`/`tshark` breakdown over ~2000 READ RPCs put nfsd's own
processing at a median 0.030 ms against a client round-trip of 0.687 ms, so the read is dominated
by wire RTT and the Phoenix client's own overhead is modest. Measured over the 100 Mbit link of
the time; the recorded conclusion is that this floor is network-inherent rather than a fixable
client defect, which was the question being asked. That is what makes RAM-staging assets worth doing per application (see the yquake2 row) and jumbo
frames not worth trying (ruled out both by the lab switch and as moot, since Linux reaches
112 MB/s at standard MTU). Not everything slow is explained by it: SuperTuxKart's ~27 s first-run
cost was measured *not* to be shader compilation, asset reads or steady-state rendering, and the
leading remaining hypothesis is the kernel's contiguous physical allocator.

### 5. Stability and robustness

- `bcm2711-emmc` runs its fs server single-threaded and with enlarged pool-thread stacks (`0982cdb`,
  `07bb181`, `1741541`) — SD-boot crashed in `ext2_obj_get` before `psh`.
- `usb/mem.c` hardening (`016f9bb`, `c0af52e`, `53b3db2`): the DMA allocator validates chunk *and*
  buffer headers and, on corruption, logs a self-localising report (the allocation abutting the smashed
  header, plus a recent-free ring) and leaks the rest of the chain instead of faulting the daemon
  mid-enumeration. `1242d39` makes duplicate transfer completions idempotent; `a4b4628` drains the
  URB finished list from `usb_init`.
- Bounded waits and truncation guards on every mailbox client (`70b5b34`, `b511550`, `432db4c`,
  `ec7f2ae`): unbounded mailbox spins and 32-bit truncation of a 64-bit physical address were the two
  recurring defect classes across these drivers.
- Isolating USB in its own daemon means a USB-side failure no longer takes the network down with it
  (`03bd903`).
- NFS resilience: bounded retry on transient RPC errors (`82b5530`, `434d1b3` — fixes intermittent
  `exec` failures), recovery from NFSv4 lease expiry `NFS4ERR_EXPIRED` (`8231627`), a stable NFSv4
  client id so a rapid reboot does not collide with its own prior state (`4b5acb4`), and takeover of `/`
  degrading to the RAM root rather than bricking the boot (`eed921c`).
- A round of leak and bounds fixes from a dedicated review pass: `hub->portEnumFails` on teardown and on
  the `hub_conf` error path, `usbkbd`/`usbmouse` insertion error paths, `rpi4-audio` partial-mmap leak
  and DMA 1 GB-straddle guard, `rpi4-fb` rejecting (not truncating) a write past the surface end.

### 6. Networking (lwIP) and filesystems (NFS)

**lwIP.** Beyond the two new netifs, the port gained `sys_mbox_trypost_coalesce` (a producer merges onto
the queued tail entry instead of taking a new slot, which is what keeps a gigabit RX burst from
overrunning the recvmbox; coalescing only runs on a non-empty queue, so no wakeup is lost),
`dmammap_cached` for buffers the CPU copies through, the netif-driver list exposed so an out-of-tree
netif can register, `/dev/ipstats` for driver triage, an RTT ping-pong and streaming mode in `net-test`
(this is how the throughput numbers above were measured), and optional iperf. The `wifi43455` netif
notes two things a *new* netif name has to do that an in-list driver gets free: `netif_dev_init` applies
mtu/hwaddr/flags defaults only to a hardcoded list of driver names, so the driver must set them itself
including `NETIF_FLAG_UP` (without it `dhcp_start()` returns `ERR_ARG`, which showed up on hardware as
`dhcp_start: -16` right after a successful join); and since the WiFi daemon starts *after* lwIP, init
must register link-DOWN and wait for its device files rather than requiring them. It also stopped calling
`netif_set_default()` unconditionally, which had been silently pushing all off-subnet traffic over WiFi.

**NFS.** `filesystems/nfs/` is a new userspace NFSv4 client filesystem server on libnfs (`srv.c`,
`nfs_ops.c`, `nfs_node.c`, ~2 700 lines) with a node cache keyed by both path and id, a filehandle
cache with lazy close, and a "takeover" mode: the export is mounted as `/` *after* a RAM `/` has brought
up lwIP, which is what makes NFS-as-rootfs possible on a diskless board at all. Getting POSIX semantics
right on top of it took most of the work, and each fix is a real gap rather than Pi-specific:
`readdir` synthesises `.` and `..` (NFS READDIR omits them, and POSIX requires them) with
server-resolved inode numbers; `st_blocks` was 0 for every file because libnfs does not map the server's
`SPACE_USED`, so `du` and `ls -s` reported nothing; a node unlinked while open stayed bound in the
by-path table, so a later create of the same name reused it and writes went through an orphaned
filehandle while `stat` resolved the new inode — a freshly written file could `stat` as 0 bytes; and
FIFOs and device nodes now work on an NFS root, by recording the name locally and splicing the owning
server's oid onto it (`mkfifo` had returned `EIO`, because Phoenix asks the owning filesystem for an
`otDev` node carrying posixsrv's pipe oid and the code discarded it, then tried `nfs_mknod`, which
NFSv4 rejects). That limit is stated rather than hidden: such a name lives in one mount, is not listed
by a `readdir` of the export, and does not survive a remount — which matches what the object is.
`be68a10` closes out a leak audit of the directory-snapshot cache (an NFSv4 reclaim stranded a whole
listing, tens of KB per reclaim, because with the dircache disabled nothing in libnfs would ever free
it). Verification is stated in test terms throughout: over the netboot NFS root, `libc/stdio` 80 tests
0 failures, `libc/misc` 207 tests down to 2 known-clock failures, `libc/dirent` 38/0.

### 7. V3D compute (CSD) — three changes an unrelated experiment surfaced

The winsys had an implemented-but-never-called `DRM_V3D_SUBMIT_CSD` handler (coord `1067af16a`,
"implement CSD — was a no-op stub"). The first code to actually dispatch compute on it was the
machine-learning work in the coordination repo (see the last section), and it immediately found
two defects. Both are compute-only; the render `SUBMIT_CL` path is untouched by all three.

| change | where | what/why |
|---|---|---|
| ★★ A debug print that dominated every compute measurement | `gpu/rpi4-v3d/v3d_gpu.c` (`95542eb`) | `ioc_submit_csd` logged `rpi4-v3d: CSD done cfg0=… num_completed=N` to the UART on **every** dispatch. At serial baud that is ~6 ms per line, which swamped the work being timed: an *empty* CSNOP kernel "measured" 21 ms/dispatch while a real 256×256 matmul "measured" 11.5 ms — the ordering is the tell. Gating the print to timeout/error only moved the same benchmark to 0.377 ms/dispatch against a 0.020 ms empty-dispatch floor, and **inverted the recorded verdict** on GPU compute from "6.6× slower than the CPU" to parity. The generalisable point: a per-operation console print in a driver hot path is not a diagnostic, it is a measurement defect. |
| ★ CSD post-dispatch cache flush did not match the hardware's requirement | `gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c` (~2358), `v3d_gpu.c` (~914); originally coord `616f114db`, migrated by `b0c9cdb` | Compute TMU-general stores **never reached DRAM**. The winsys issued one combined `L2TFLS\|TMUWCF` with `FLM=FLUSH`; Linux's `v3d_clean_caches` drains `TMUWCF` *alone* (and waits), and only then flushes `L2TFLS` with `FLM=CLEAN`. Replicating that order made the compute write path work — the store-and-read-back milestone passed immediately. |
| `SUBMIT_CSD` through the GPU daemon | `gpu/rpi4-v3d/` (`c40d0fe`, `1010570`, `92070af`) | Compute dispatch and client BO mapping routed through `/dev/v3d-srv`, so a GPU-compute client runs without linking the winsys in-process. Later hardened by sizing the CSD completion wait for real dispatches and refusing to kick CSD into a busy unit (draining it after a timeout). |

## Application and game ports, target integration, build system

This section covers three of the fork's repositories: `phoenix-rtos-ports` (174 commits, +55 210/−397), `phoenix-rtos-project` (222 commits, +3 412/−18) and `phoenix-rtos-build` (35 commits, +957/−23). The ports repo grew **41 new `port.def.sh` recipes** (36 userland, 5 3D game engines) and modified 7 existing ones; nothing was removed. The project repo carries the whole Raspberry Pi 4 board bring-up — a new `_targets/aarch64a72/generic` target plus an `aarch64a72-generic-rpi4b` project with its own ARM stub, relocating `kernel8.img` trampoline, firmware `config.txt` and three boot variants (NFS-root / netboot / SD). The build repo adds aarch64 generic target admission, the gcc-16.2 / binutils-2.47 toolchain rebase, and — most reusable — a set of `port_manager` staleness fixes that close real silent-stale-build holes. Note for reviewers: `phoenix-rtos-project/.gitmodules` is repointed at the `rpi-phoenix-rtos` org forks (commit `3a38eb5`), so submodule gitlinks in this repo do not reference canonical upstream.

The single most valuable material for upstream is not the port count but §3: nearly every patch in this repo is a written-up diagnosis of a libphoenix/libstdc++ gap, several with byte-exact hardware evidence.

### 1. Application ports

36 new userland recipes, all in `phoenix-rtos-ports/<name>/port.def.sh`. Licence is called out only where it constrains a maintainer's reuse. They are not independent: the interesting ones sit at the top of real dependency chains that the framework now resolves (`supertuxkart` alone declares twelve `depends=`), which is itself part of what is being demonstrated — `port_manager` can build a non-trivial DAG, not just leaf packages.

| port | version | notes |
|---|---|---|
| **★ python** | 3.14.4 | Static `python3` + extension modules, `.so` `dlopen`, zlib/ssl/hashlib, HTTPS, curses. The heaviest single proof that libphoenix is a usable POSIX libc. 302-line recipe, PSF-2.0 |
| **★ coreutils** | 9.5 | All 104 GNU tools, output verified bit-exact against the host. Also runs the gnulib `tests/*.sh` suite under the ported bash. GPL-3.0-or-later |
| **★ bash** | 5.2.21 | Fully interactive GNU shell (job control, readline). GPL-3.0-or-later |
| **★ sqlite3** | 3.53.4 | In-memory + file VFS, `integrity_check=ok`; multi-process rollback-journal proven over real `fcntl` locks. WAL is single-process only (no `xShmMap`) |
| **★ redis** | 7.2.4 | Serves 241 commands over lwIP TCP; RDB persistence works. `MALLOC=libc`, `ae_select` event loop |
| **★ sdl2** | 2.30.12 | Real SDL 2.30.12 with two *new upstream-shaped backends* written for Phoenix: `src/video/phoenix` (one fullscreen `/dev/fb0` window, input drained from `/dev/kbd0` + `/dev/mouse0`) and `src/audio/phoenix` (pull model over `/dev/audio0`). Zlib licence; the GL-context glue is kept outside `libSDL2.a` to preserve that |
| **★ libnfs** | 6.0.2 | Backs NFS-as-rootfs. Carries three real NFSv4 bug fixes (see §3). LGPL-2.1 |
| xorg_libs | 2023.2 | 24 tarballs in one recipe (libX11 1.8.7, libxcb 1.16, libXt/Xaw/Xmu/Xpm/Xext/Xrandr/Xrender, xcb-util family, pixman 0.42.2, xtrans, xkbfile). Version anchored on xorgproto |
| xorg_server | 21.1.24 | Xorg with a **new Phoenix DDX** in-tree at `xorg_server/files/ddx/` (`fbdev.c` 1020 lines, `ddxLoad.c` 631, built-in keymap, HID→evdev map). Both a software-fb and a glamor/GPU server are built. The GPU server additionally carries patches to upstream glamor. **The set shrank on 2026-09-09 and the reason is the interesting part:** it was an R↔B swap on `XPutImage`'d content (RGBA transfer format), a screen-pixmap upload Y-mirror plus its symmetric download flip, an extension of that flip to the `glamor_spans.c` transfer sites, and the `DestroyPixmap` hook-chain fix in §3. All three Y-flip compensators are now **retired**: they existed because Mesa forced `Y_0_TOP` for the 1920x1080 glamor screen pixmap under a heuristic that tests SIZE, not scanout-ness (`st_atom_framebuffer.c`, `fb->Width >= 1024 && fb->Height >= 768`), even though that pixmap is a plain GL texture presented by `glReadPixels` into a shadow — nothing about it is scanout-backed. Forcing `Y_0_TOP` put every glamor GL path into a flipped coordinate world held upright by hand-rolled flips, and any path that missed one emitted its box at `y' = H-1-y`. Opting this one context out (`phx_scanout_flip_gate`, mesa `d5852136ba0`; shim clears it before `st_create_context`) leaves the pixmap `Y_0_BOTTOM` — plain upstream behaviour — and deletes all three compensators, which is a net *removal* of code. So what remains is the R↔B swap, the `DestroyPixmap` chain fix, and one new non-glamor patch to `os/connection.c` that asks for a 256 kB receive ring (see the AF_UNIX row in the kernel performance table). Build-lineage caveat, stated because this section otherwise reads as though all X lives in the ports repo: `xorg_server/` has no `patches/` directory, so the glamor-accelerated server is built from the coordination repo's `tools/x11-port/build-xserver-core.sh` path instead |
| xorg_fonts | 2.13.2 | freetype 2.13.2 + fontconfig 2.14.2 + cairo 1.16 + expat + libXft/libXfont2/libfontenc + PCF fonts (`font-misc-misc`, `font-cursor-misc`, `font-adobe-75dpi`, `encodings`, `font-alias`). Two of those are mandatory rather than decorative: `font-cursor-misc` is a separate upstream package and the only source of the `cursor` font every `XCreateFontCursor` caller opens, and `font-alias` is what fixes Xt's `Cannot convert string "8x13" to type FontStruct`. **A generalisable trap for fontconfig on a network root:** point it at `/usr/share/fonts/truetype` only, never the parent — the X core bitmaps are served by the X server's own `-fp` and never resolve through Xft, so indexing them buys nothing, and it made WindowMaker's first Xft font load `FT_New_Face`-open all **412 core PCFs** over NFS. That took desktop startup to 5 min 40 s and was misread as "the window manager does not draw" for a night; root-caused with this fork's own `libdbg` (`dbg_arm_watchdog` + `addr2line`), which named the stack `WMCreateFont → XftInit → FcConfigBuildFonts → FcFileScanFontConfig → FT_New_Face → sys_open`. Coord `71ab64d9a`, ports `de63acf`: startup 5 min 40 s → ~1 min |
| xorg_apps | 1.1.2 | xcalc, xclock, xlogo, xedit (Xaw/Xt clients) in one recipe, anchored on xcalc |
| windowmaker | 0.95.9 | Window manager; the desktop actually used on HDMI. GPL-2.0-or-later |
| xterm | 396 | Interactive terminal emulator over `/dev/ptmx` — see the pty gap in §3 |
| dillo | 3.2.0 | Renders live HTTPS pages under Xphoenix (TLS via mbedTLS in-process). GPL-3.0-only |
| mc | 4.8.31 | Midnight Commander, full-screen curses app. GPL-3.0-or-later |
| nano | 9.2 | Editor; gnulib-based, hence two gnulib patches. GPL-3.0-or-later |
| xbill | 2.1 | Small Xaw game — an X11 client-stack smoke test. GPL-2.0-or-later |
| jq | 1.7.1 | Incl. regex via oniguruma; exposed a `malloc(0)` bug (§3) |
| oniguruma | 6.9.9 | Regex engine, jq dependency |
| ncurses | 6.4 | Terminal library; `-fPIC` build feeds Python's `curses` module |
| glib2 | 2.56.4 | With `libintl`/`nameser`/`resolv` stub headers supplied by the recipe. LGPL-2.1-or-later |
| fltk | 1.3.10 | Dillo's widget toolkit. LGPL-2.0-only |
| harfbuzz | 14.4.0 | Text shaping (STK) |
| **★ ffmpeg** | 6.1 | Registered `if: false`: no *image* component consumes the recipe, but the decode core is hardware-proven — MJPEG (plane-0 avg 127 vs host ffmpeg 127.03) and H.264 (avg 123, bit-exact) decode on the Pi, display on `/dev/fb0`, and play in an X window (`tools/ffmpeg-port/e4_x11_play.c`, 2 898 frames, 0 faults, concurrent with a GPU app). LGPL-2.1-or-later, built without `--enable-gpl`. Its own porting gap: heavy decoders overflow the default main-thread stack, so the decode body runs on an ≥8 MB pthread — the same `SIZE_USTACK` ceiling as coreutils, reached from a different direction |
| libjpeg-turbo | 3.0.4 | IJG AND BSD-3-Clause AND Zlib |
| libpng | 1.6.40 | |
| libogg | 1.3.5 | Ogg container; STK/game music |
| libvorbis | 1.3.7 | Vorbis decode — where the pthread-stack bug in §3 was found |
| libsamplerate | 0.2.2 | Audio resampling |
| libiconv | 1.18 | LGPL-2.1-or-later |
| libffi | 3.4.6 | Python `ctypes` |
| enet | 1.3.18 | UDP networking; needed `SOMAXCONN`/`MSG_TRUNC` fallbacks (§3) |
| bzip2 | 1.0.8 | |
| xz | 5.4.7 | 0BSD; feeds busybox seamless tar |
| **★ ca_certificates** | 2026.7.22 | Mozilla trust store installed at `/etc/ssl/certs/ca-certificates.crt` — the missing piece that made cross-built curl/dillo TLS actually verify. MPL-2.0 (recipe installs `LICENSE` alongside, per MPL §3.2) |
| **★ llama2** | 20240529 (`350e04f`) | llama2.c CPU inference: 260K model at 370 tok/s, 15M at 5.8 tok/s, bit-identical to host. MIT |

**Existing recipes modified** (a different review ask — these are fixes/bumps to upstream's own ports):
**★★ `openssl111`'s Phoenix aarch64 target left `bn_ops` unset**, which is more important than the
version bump below it and will recur on riscv64 and any future 64-bit Phoenix target. Without it
`bn_ops` defaults to `THIRTY_TWO_BIT` while the aarch64 bignum asm uses 64-bit limbs — a size
mismatch that overflows the heap inside every TLS handshake, corrupting allocator metadata; it is
what blocked HTTPS for every consumer. One line, `bn_ops => "SIXTY_FOUR_BIT_LONG RC4_CHAR"`,
applied to both 64-bit targets, with the reasoning written into `openssl111/30-phoenix.conf:37`.
Also: `openssl111`
1.1.1a → **1.1.1w** (`e8fc54f`, plus the EOL `old/1.1.1/` source URL and a timestamp-preserving install patch); `zlib` 1.2.11 → **1.3.1**; `mbedtls` 2.28.0 → **2.28.10**; `wpa_supplicant` 2.9 → **2.11**; `lua` **5.3.6 → 5.4.7** (whole patch set migrated and rebased, incl. the healthcheck/priority patches); `curl` gains `--with-zlib` and `--with-ca-bundle=` (a cross build silently left `CURL_CA_BUNDLE` undefined, so *every* HTTPS transfer had no trust store); `lighttpd` gains a webdav mmap guard plus a fix to its static-plugin-table generation (the old `grep mod_` also matched **commented-out** modules, compiling 13 plugins where the config enables 9); `busybox` config enables awk, xz decompress and seamless tar.

### 2. Game ports

Five 3D engines, all folded into a **single static ELF each** (Phoenix has no dynamic-executable loading, so upstream's `.so` game/renderer split cannot be used), all installing into the rootfs at `/usr/bin`. All are GPL-2.0-or-later except SuperTuxKart (GPL-3.0-or-later) — relevant to what a maintainer can look at.

| engine | version (pin) | renderer path | state |
|---|---|---|---|
| **★ quakespasm** (GLQuake) | 0.97.0 (`f5fe178`) | desktop GL → Mesa/V3D → `/dev/fb0` | Flagship. Textured real levels at ~1080p/~40 fps on HDMI, audio wired. Cleanest HW evidence of the five. Also the only *networked* demonstration: the multiplayer client joins a real dedicated server, loads the map and runs in-game at 26 fps over Phoenix's own TCP/IP stack (the two enabling lwIP defects — the `getnameinfo` out-of-bounds write and `FIONBIO` — are in the drivers section, §3) |
| **★ supertuxkart** | 1.4 | GLES3 (STK "SP" renderer) | Boot → fully-lit in-game 3D race, 0 crashes, host-comparison SSIM 0.991, **8/9/9 fps** on the shipped image (`scale_rtts_factor=0.75`; it was 5.84 fps / ~171 ms per frame at full resolution). It ran at *exactly* 1 fps until a libstdc++ toolchain defect was root-caused — see §3. 11 patches, 12 port dependencies |
| yquake2 (Quake II) | 8.71 (`a9e88f6`) | `ref_gl3` / GLES3 default, `ref_gl1` selectable | Renders full 3D. Client + integrated server + baseq2 game + one renderer in one ELF. Asset load is slow over NFS, mitigated by RAM-staging to `/tmp` — **per application, not in general**: measured 5.49× for quake3e (`CL_InitCGame` 63.77 s → 11.61 s) and 3.6× for quakespasm, but a net *loss* for SuperTuxKart, where 73 s of copying saved 2.7 s. RAM-staging is also the real justification for the `DUMMYFS_SIZE_MAX` 32 → 256 MiB bump reported in §4 |
| quake3e (Quake III) | 1.32 (in-tree "Q3 1.32e", `f694bbb`) | desktop GL → Mesa/V3D | Runs; QVM bytecode modules need no `dlopen`, but its aarch64 JIT does need the code buffer `mmap`'d RWX up front, because `mprotect` cannot add `PROT_EXEC` later (see *Platform gaps*). Known open defect: a V3D CT0 binner wedge on `q3dm7` (a lightmap-black bug on the same map was root-caused and fixed — see the V3D tiling rule in the drivers section) |
| vkquake | 1.34 (`1aa13a5`) | **Vulkan** via the ported V3DV ICD (SPIR-V→NIR→QPU) | Runs — the only user-shader Vulkan consumer. No SDL dependency at all: SDL is *entirely* shimmed (`glue/sdl-shim/SDL.h` + `pl_phoenix_sdlcompat.c`). Known open defect: torch sprites intermittently missing (~10–20 % of runs) |

Per engine the recipe carries a `glue/pl_phoenix_*.c` Phoenix backend plus one generated single-ELF patch (`quakespasm` 857 lines, `vkquake` 557, `yquake2` 476, `quake3` 331). vkQuake additionally vendors pre-compiled shaders (`vkquake_shaders.c`, 30 506 lines) and Vulkan entry trampolines (`vk_trampolines.c`, 648 lines).

Three structural notes a maintainer may care about more than the games themselves:

- **The `.so` seam is the recurring problem, not the graphics.** yQuake2 has two dynamic-load seams (game DLL, renderer DLL) and quake3e has three module slots; each port had to be re-plumbed into one link unit. Any Phoenix port of a plugin-architecture application will hit this until `PT_INTERP`/auxv loading exists.
- **Four of the five sit on the ported SDL2** (`depends="sdl2"`), so the SDL video/audio backends in `phoenix-rtos-ports/sdl2/overlay/src/{video,audio}/phoenix/` are the real reusable asset here: ~1 300 lines of driver that any future SDL application on Phoenix inherits for free. vkQuake is the exception and shims SDL away entirely. Anyone inheriting those backends should know what was missing: they called only `SDL_SendKeyboardKey` (scancodes) and never `SDL_SendKeyboardText`, so **no `SDL_TEXTINPUT` event was ever generated** — a whole input category, which is why Quake III's `~` console accepted only Enter. Fixed with a SHIFT-aware HID→char mapping (`c019e12`). The same investigation cleared the mouse path as source-correct; those failures were the `N_URBS=1` and xHCI issues covered elsewhere.
- **★ Redirecting GL's default framebuffer 0 to a real FBO turns upstream GLES *hints* into destructive operations** — a reusable design warning for anyone implementing GL/GLES on a framebuffer-only target. What makes surfaceless V3D work at all is that `glBindFramebuffer(target, 0)` is redirected to a user FBO backed by the scanout buffer, and that silently changes the meaning of every GL call whose behaviour differs between the winsys framebuffer and a user FBO. It cost a 100 %-black screen with no GL error and the game running: yQuake2's GLES3 renderer calls `glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, {COLOR_ATTACHMENT0, DEPTH_ATTACHMENT, STENCIL_ATTACHMENT})` pre-swap, which on a windowed driver is a no-op hint because those enums are invalid on FB 0 — but against our FBO they are *valid*, so Mesa reached `v3d_invalidate_resource`, the unflushed job lost its colour tile store, and the page flip presented a buffer the GPU never wrote. Fixed in the port glue rather than the game (`96c0f4b`), precisely because the trap waits for any GLES client; the same class is documented and still unwrapped for `glDrawBuffers`, `glClearBufferfv` and `glDiscardFramebufferEXT`.

Two of the game-side defects are really findings about the platform, and both were misdiagnosed
repeatedly first:

- **A single-pose alias-model VBO crossing exactly one 4 KiB page corrupted the model.** Found by
  rendering all 61 Quake alias models through the unmodified path with an on-screen block code for
  attribution: among `numposes == 1` models the broken set was `{g_light 227, g_nail 217, g_nail2
  228, suit 237}` verts and the clean set `{g_rock 165, g_rock2 164, g_shot 90, … ≤ 165}`. At 24
  bytes/vert — an earlier "fix" had allocated a *duplicate* pose block — the boundary is exactly
  4096: 165×24 = 3960 clean, 217×24 = 5208 broken. The duplicate block was dead code (Pose2 is
  disabled at blend == 0, and `numposes == 1` is always blend == 0), so deleting it takes
  single-pose VBOs to `nverts*16`, whose maximum in Quake data is 237×16 = 3792. **61/61 render
  clean.** It sat behind five earlier false "fixed" claims.
- **Every Quake `+command` was silently ignored on shareware data, which invalidated the visual
  test evidence.** `Cmd_StuffCmds_f()` iterates the `cmdline` cvar, and upstream sets that cvar
  only in the *registered* branch of `COM_CheckRegistered()` — after the `gfx/pop.lmp` checksum,
  which the shareware branch returns before. All testing here uses the free shareware pak, so no
  `+command` had ever taken effect and every HDMI comparison was captured at wherever the attract
  demo happened to be. An upstream Quake bug, fixed in both forks (`b2a47ae`, vkquake `65f599e`),
  and the reason a visual defect survived five re-diagnoses.

### 3. ★ Phoenix gaps found by porting

Each item below is a platform gap, evidenced by a patch that states its own root cause; paths are relative to `phoenix-rtos-ports/`. They are ordered by what they would cost upstream to fix against what they buy — the first five are all small, self-contained libc changes that remove whole classes of future port patches.

A patch in this repo is therefore best read as a **bug report with a workaround attached**, not as a local hack: the workaround stays in the port, but the fix belongs in libphoenix.

1. **★★★ Default pthread stack is 4 KiB with `guardsize` 0 — no guard page.** `supertuxkart/patches/0011-stk-ogg-heap-pcm-buffer.patch`. A `std::thread` (NULL `pthread_attr_t`) gets `ALIGN(PTHREAD_STACK_MIN, PAGE_SIZE)` = 4096 B. STK's ogg decoder wrote a 44 100-byte buffer on that stack and silently overwrote neighbouring mappings. Proven byte-exact, not inferred: a kernel dump of the faulting stack was found verbatim inside decoded `menutheme.ogg` PCM, and two crashes' corrupted return addresses sat exactly 44 100 bytes apart. Symptoms varied per run (jump to a garbage address, or a fault inside `malloc`). **A default stack that small with no guard page turns every stack overrun into silent corruption.** This is the one gap in this list the fork has already closed — see `02ab4e0` and `bad2009` under *libphoenix, corelibs, test suite* — but the STK patch stays, because a 44 KiB buffer does not belong on any thread stack regardless of its size.
2. **★★ libphoenix exports `gettime`/`settime` as global symbols** (`libphoenix/include/sys/time.h:34`). gnulib declares its own `gettime`/`settime`, so *every* gnulib-based GNU package collides. Patched identically in two ports — `coreutils/patches/0001-` and `nano/patches/0001-rename-gnulib-gettime-settime.patch` — and will recur for every future GNU port. These two names belong under a reserved prefix (`phoenix_`/`_`) or in a Phoenix-specific header.
3. **★★ No wide-character output at all: no `swprintf`/`vswprintf`/`fwprintf` in libphoenix, and no wide iostreams in libstdc++.** `supertuxkart/patches/0006-irrlicht-phoenix-swprintf-shim.patch` had to hand-write a 106-line wide-printf shim (integers, floats, pointers, wide strings) because Irrlicht — and therefore all of STK — calls `swprintf`; `0009-stk-spinner-no-wide-iostream.patch` replaces `std::wstringstream`. Also `xterm/files/include/wctype.h` is a locally supplied header.
4. **★★ `<fenv.h>` in the sysroot is a poison pill that `#error`s on include** (verified: `.toolchain/aarch64-phoenix/.../usr/include/fenv.h` says "shall not be used as is"), and `aligned_alloc`/`posix_memalign` are absent from `<stdlib.h>` entirely. See `supertuxkart/patches/0004-simde-skip-poison-phoenix-fenv.patch` and `0005-vma-phoenix-aligned-alloc.patch` (which hand-rolls an over-allocating aligned malloc). Both are C11-mandatory surface; any C++/SIMD/graphics library trips over them, and a header that cannot be included is worse than one that returns errors.
5. **★★ gnulib's stdio internals need a Phoenix branch, and there is no public API for them.** `coreutils/patches/0002-port-gnulib-stdio-internals-phoenix.patch` and `nano/patches/0002-port-gnulib-fseterr-phoenix.patch` reach *directly into* `FILE` — `fp->bufeof - fp->bufpos`, `fp->flags & (1<<1)` for `F_WRITING`, `fp->flags |= (1<<3)` for `F_ERROR` — because `<stdio.h>` exposes no `__freadahead`/`__freading`/`__fseterr`. Without the `fseterr` branch gnulib's `#error` fires and nano does not build at all. Exporting these three (as musl/glibc do) removes a whole class of port patches.

Bugs the fork fixed in *its own dependencies* while chasing these, worth mentioning because they were long-lived and silent (`libnfs/patches/`):

- `01-nfs4-renew-lease.patch` adds a synchronous NFSv4 `RENEW`; without it an idle NFSv4.0 lease expires and the client sees `NF4ERR_EXPIRED` surfacing as `ERANGE` / `exec -12`.
- `02-nfs4-st-blocks-512-units.patch`: the v4 path divided used-space by `NFS_BLKSIZE` (4096) instead of 512, so `stat().st_blocks` came back ~8× low and `du`/`ls -s` under-reported. The v3 path was already correct.
- `03-nfs4-open-create-excl-typo.patch`: `flags|O_EXCL` where `flags&O_EXCL` was meant — a bitwise OR that is non-zero for every value — so **every** NFSv4 create was sent as `EXCLUSIVE4`. That carries a random verifier instead of an attribute list, which is why new files came back mode 000 with garbage atimes (measured: 2147411476 — the verifier, not a clock bug).

Further gaps, same evidence standard:

- **★★ `std::chrono::steady_clock` has 1-*second* resolution here** — a libstdc++ configury defect,
  and the most transferable toolchain finding in the fork. libstdc++ for `aarch64-phoenix` is
  configured with **no time backends at all** (verified in the shipped sysroot's `c++config.h`:
  `_GLIBCXX_USE_CLOCK_MONOTONIC`, `_GLIBCXX_USE_CLOCK_REALTIME`, `_GLIBCXX_USE_GETTIMEOFDAY` and
  `_GLIBCXX_USE_NANOSLEEP` are all `#undef`), so `steady_clock::now()` falls all the way back to
  `std::time()`. Cross-configure cannot run target test programs, so these probes default off —
  meaning *any* Phoenix C++ target built this way inherits the defect, and it fails silently
  everywhere except where a zero-delta guard makes it catastrophic. It did: SuperTuxKart ran at
  **exactly 1 fps**, because `getLimitedDt()` sits in `while (dt == 0) { sleep(1); … }` and the
  frame loop therefore slept ~1000 × 1 ms per frame. The per-port workaround
  (`supertuxkart/patches/0012-stk-phoenix-monotonic-clock.patch`, ports `6f08c26`) reads
  `CLOCK_MONOTONIC` directly under `__phoenix__` and took the game to **~171 ms/frame, 5.84 fps**,
  matching an independently measured frame budget. The real fix is rebuilding libstdc++ with
  `--enable-libstdcxx-time=rt`; note that `steady_clock::now()` is compiled into `libstdc++.a`
  (`src/c++11/chrono.cc`), so defining the macros in `c++config.h` alone changes nothing. C is
  unaffected — `clock_gettime(CLOCK_MONOTONIC)` has real nanosecond resolution here.
- **No `dlfcn.h` / dynamic-executable loading at port time.** `sdl2/patches/0003-dynapi-disable-on-phoenix.patch`; all five games fold their `.so` seams into one ELF. libphoenix has since gained `dlopen`/`dlsym`/`dlclose` for `ET_DYN` objects, but `PT_INTERP`/auxv is still unimplemented — and the `zlib` recipe documents the sharp edge that follows: a stray `libz.so` in the shared prefix makes the linker prefer it, stamping a `PT_INTERP` requesting `/lib/ld.so.1` into the binary, which then **dies before `main()` with no message** (on 2026-09-04 that shipped Xphoenix, python3, dillo, wmaker, curl and lighttpd all unrunnable).
- **`pthread_getschedparam`/`pthread_setschedparam` are declared but not implemented** — link error, not compile error. `sdl2/patches/0004-systhread-priority-noop-on-phoenix.patch`.
- **`-pthread` is rejected by the driver and there is no `libpthread`** (it is a symlink to `libphoenix.a`), so stock autoconf/CMake pthread probes fail. `sdl2/patches/0001-cmake-phoenix-pthread-detection.patch`; also the cause of a link-line collision fixed in the build repo (§5).
- **No `<semaphore.h>`, no `LC_MESSAGES`, no `CLOCK_PROCESS_CPUTIME_ID`, and `struct rusage` lacks `ru_maxrss`/`ru_minflt`/`ru_majflt`.** `supertuxkart/patches/0008`, `0010`, `0003`.
- **`MSG_TRUNC`/`MSG_CTRUNC`/`SOMAXCONN` missing from the lwIP-backed socket headers**, and `htonl` is a macro not reached transitively. `enet/patches/0001-phoenix-socket-const-fallbacks.patch`, `xorg_libs/patches/libxcb-1.16-phoenix.patch`.
- **Session/pty model gaps:** Phoenix has SysV `setpgrp(void)`+`setsid()` but no BSD `setpgrp(pid,pgrp)` and no `TIOCSPGRP`; the SVR4 `/dev/ptmx` master exists but xterm needed the child slave-open path wired by hand (`xterm/patches/xterm-396-phoenix.patch`, 152 lines, well documented).
- **No file-backed `MAP_PRIVATE` to rely on**, so `llama2/patches/01-phoenix-no-mmap-checkpoint.patch` reads the whole model into RAM and `lighttpd/patches/04-mod-webdav-mmap-guard.patch` guards a no-mmap build.
- **No `/proc` and no `kvm`/`sysctl`,** so WindowMaker's `GetCommandForPid()` had no implementation. `windowmaker/patches/0001-phoenix-getcommandforpid.patch` implements it from the kernel's `threadsinfo()` table — the author marks it upstream-inclinable.
- **`malloc(0)` returned NULL** (found via jq; fixed in `libphoenix/malloc_dl.c` to allocate size 1). Helps every port that assumes `malloc(0) != NULL`.
- **Reported as a strength, not a gap:** `xorg_libs/patches/libX11-1.8.7-phoenix-fontset-basename-ownership-58.patch` is a genuine upstream libX11 bug — `destroy_oc()` `Xfree()`s a `.rodata` string literal. glibc silently tolerated it; **libphoenix's allocator correctly aborted** (status 0x46), which is how it was found. The patch is upstreamable to xorg/libX11 as-is.
- **★ Also a genuine upstream bug, in glamor this time**, and the most upstreamable finding in the X11 work: `glamor_init()` saves the previous `screen->DestroyPixmap` into `glamor_priv->saved_procs.destroy_pixmap` and installs `glamor_destroy_pixmap()`, which ends in a hard `return fbDestroyPixmap(pixmap)` and **never calls the saved pointer** — it is only read again to restore the hook at CloseScreen. Anything wrapped *below* glamor is silently cut out of the chain. Upstream xf86 never notices, because `glamor_init()` runs at ScreenInit and `DamageSetup()` later at extension init, so damage always wraps *above* glamor; this DDX brings damage up via `shadowSetup()` (`tools/x11-port/ddx/fbdev.c:591`) **before** `glamor_init()` (`:603`), i.e. into the one slot glamor does not honour. Consequence: `damageDestroyPixmap()` never ran for *any* pixmap, so `damage.c`'s teardown never destroyed `DamagePtr`s on dying pixmaps, glamor's stipple damage outlived its drawable, and `FreeGC` (`dix/gc.c:781`) released `gc->stipple` before the DDX `DestroyGC` at `:783` → `glamor_destroy_gc` → `glamor_invalidate_stipple` → `DamageUnregister()` on freed memory. Fixed by a screen-hook-only wrapper that does the same FBO teardown then calls `saved_procs.destroy_pixmap`, reinstating its own slot afterwards because `damageDestroyPixmap` unwraps and re-wraps with itself; the exported `glamor_destroy_pixmap()` is left untouched, since ~15 internal glamor callers use it to drop private pixmaps and must *not* traverse the screen chain. `tools/x11-port/patches/xorg-server-21.1.24-glamor-destroypixmap-chain.patch`, coord `bde9a44a3` — **6/6 X sessions crashed before it, 0/18 after**. The kernel half of the same crash is the vfork kstack / borrowed-map lifetime work above (`6d8f40a5`).

  **★ Update (2026-09-09): the ordering described above was itself the bug, and it had a second,
  silent consequence — this is the largest single X11 finding in the fork.** `shadowSetup()` calls
  `DamageSetup()`, which wraps `screen->CreateGC` with `damageCreateGC`; `glamor_init()` then wraps
  `CreateGC` with `glamor_create_gc`, and that function (`glamor/glamor_core.c:283`) ends in a hard
  `fbCreateGC(gc); gc->funcs = &glamor_gc_funcs;` — it **never calls the saved `CreateGC` either**.
  So with shadow first, `damageCreateGC` was bypassed for *every GC ever created*: damage never
  wrapped `gc->funcs`, `damageValidateGC` never ran, `damageGCOps` was never installed, and
  `DamageRegion` stayed **empty forever**. Measured: `damage calls = 0` across 320 presents. The only
  thing putting pixels on HDMI was the DDX's 300 ms *idle safety-net* timer, always all 1080 rows — so
  **the GPU desktop reached the screen ~2.3 times a second no matter how fast its clients ran**, which
  is why it read as "sluggish" rather than as a bug. Unlike the `DestroyPixmap` case this produced no
  crash, only silence, which is why it went unnoticed for so long. Fixed by initialising glamor
  **before** `shadowSetup()`, i.e. putting damage *above* glamor — what upstream xf86 gets for free —
  after which damage fires and presents become both partial and on-demand: **2.3 → 25.6 presents/s**.
  Two DDX changes were needed to get there, and both were measured rather than assumed:
  presenting per damaged **Y band** instead of per `RegionExtents` (a bounding box is full-height
  whenever two clients are dirty, so it shipped all 1080 rows every time: 1080 → 21.8 rows/present),
  and **accumulating** damage to present on a timer instead of from the damage callback — presenting
  directly measured 336 presents/s and shipped *more* total rows than the whole-screen timer had.
  Fitting two hardware points (1080 rows = 77 ms; ten 21-row presents = 32 ms) gives ≈1.74 ms fixed +
  0.07 ms/row per present, from which bands closer than ~25 clean rows are worth merging. Reading back
  only the damaged *columns* is a further 1.3× — but only on the readback: a per-row `lseek()+write()`
  to `/dev/fb0` costs **80 µs**, so partial-X on the write side was a net loss (23.6 ms against
  16.6 ms) and full-width rows are still written. Coord `7093916bd`, `ff8434d79`.

### 4. New board / target integration

`phoenix-rtos-build` (`c80264a`, `f05f148`, `makes/include-target.mk`) admits two new generic aarch64 targets, `aarch64a72-generic` and `aarch64a53-generic`, and adds `build-core-aarch64a72-generic.sh` — a core lane that additionally builds `phoenix-rtos-lwip` (`3e4028a`) and links the xHCI HCD against `libvcmbox` plus hosts `usbkbd`/`usbmouse` inside the USB daemon (`aad9a50`, `30f6867`).

`phoenix-rtos-project` adds a conventional shared target (`_targets/aarch64a72/generic/`: `build.project`, `nvm.yaml` — a 32 MB `loader` disk with `plo` at 0 and `kernel` at 0x200000 — `preinit.plo.yaml`, `user.plo.yaml`), a mirror `aarch64a53/generic`, and the board project `_projects/aarch64a72-generic-rpi4b/`. What the board actually required beyond a normal target:

- **`board_config.h`** (113 lines): GIC-400 distributor/CPU bases, PL011 base + 48 MHz clock + an early virtual address, BCM2711 mailbox at `0xfe00b880`, the PCIe outbound window (`0x6_00000000` CPU ↔ `0xf8000000` PCIe) and the VL805 xHCI BDF/class, framebuffer geometry, `PLO_SMP_ENABLE`, and two capacity knobs whose rationale is written into the header: `KERNEL_LOG_SIZE` 2 KiB → 64 KiB (the 2 KiB klog ring overflowed before userspace attached to drain it, making the replayed boot log non-deterministic — a genuinely confusing symptom) and `DUMMYFS_SIZE_MAX` 32 → 256 MiB for RAM-staging game assets.
- **`phoenix-armstub8-rpi4.S`** (464 lines, BSD-3, derived from the Raspberry Pi / Circle armstub8 lineage): EL3 → EL2/EL1 drop, `SCR_EL3`, GIC and local-timer/prescaler init, `CPUECTLR_EL1.SMPEN`, and the spin-table release for cores 1–3. **★ It also applies the Cortex-A72 r0p3 errata that ARM Trusted Firmware applies in `cortex_a72_reset_func` — and it has to**, because `CPUACTLR_EL1` (`S3_1_C15_C2_0`) and `CPUECTLR_EL1` are implementation-defined and *trap from EL1* on this part, so the kernel cannot apply them; an earlier kernel-side attempt hung on the very first `mrs`. Applied: **859971** → `CPUACTLR_EL1[32]` `DIS_INSTR_PREFETCH` (required before the I-cache is safe to enable on A72 r0–r0p3), **1319367** → `CPUACTLR_EL1[46]` `DIS_HW_PAGE_AGGREGATION`, plus `DIS_LOAD_PASS_STORE`, `DIS_L1D_HW_PREFETCH` and an L2/table-walk prefetch policy write. Two things a maintainer bringing Phoenix up on another A72/A5x board needs and would otherwise learn the hard way: the in-file note that the *previous* code wrote `CPUACTLR2_EL1[0]` against an **undefined A72 sysreg encoding** ("a no-op at best, silent state corruption at worst"), and the documented ordering rule that **`CPUACTLR_EL1` must be written before `SMPEN`**, since writes after the core joins the coherency domain may be silently dropped on r0–r0p3. Derivation: `docs/done/a72-errata-sweep.md`.
- **`phoenix-kernel8-reloc.S` + `.lds`** (129 + 26 lines): a position-independent `kernel8.img` trampoline that carries `plo` as a `.payload` section, copies it to `PLO_RPI_PLO_DEST` with cache maintenance and re-inits the UART to 115200 so early failures are visible. It exists because the firmware's load address and plo's link address differ.
- **`config.txt`** (65 lines, mostly comments recording *why*): `armstub=`, `initramfs loader.disk 0x08000000`, `dtoverlay=vc4-fkms-v3d` (fake-KMS so the firmware ungates V3D — its MMIO reads `0xdeadbeef` otherwise — while the firmware framebuffer stays for fbcon), `gpu_mem=128` + `max_framebuffer_height=4096` sized for a triple-height framebuffer, and `uart_2ndstage=0` because VideoCore firmware shares the console UART and once dumped 191 000 lines of xHCI trace into a test run.
- **Three boot variants driven by `RPI4B_VARIANT`**, in `build.project` (285 lines) and `user.plo.yaml` (290 lines):

  - `nfsroot` (default) — a real `/` on NFS, with a second `dummyfs` instance mounted at `/tmp`, because an NFS root cannot host the AF_UNIX node X11 needs at `/tmp/.X11-unix/X0`.
  - `netboot` — dummyfs root, NFS subtree at `/mnt`, plus the `nfs-smoke` triage probe.
  - `sd` — ext2 root on the eMMC2 card, RAM-backed `/tmp`.

  The variant also decides what gets *built*: `nfs` and `nfs-smoke` link the libnfs **port**, which the ports stage builds *after* core, so neither can be a core default component (that would demand a not-yet-built port during a cold-sysroot build). They are built in `b_build_project` per variant instead (`aa177cd`) — a dependency-ordering constraint any project mixing ports into the boot set will meet.
- **`lwip/lwipopts.h`** (119 lines) with a documented gigabit tuning series: `LWIP_TCPIP_CORE_LOCKING_INPUT=1` (~1.8× RX, `d2c4a6f`), `LWIP_CHKSUM_ALGORITHM=3` moved into the lwIP `arch/cc.h` to avoid clashing with a stock build (`b49fb77`), `TCP_WND` 32→44×MSS (`0993e81`), `LWIP_INGRESS_CREDIT` (`07ba705`) — NFS read 26.3 → 29.9 MB/s. A second netif token `wifi43455` is registered (`0281848`).
- **Two further projects ride the same target definitions:** `_projects/aarch64a53-generic-rpi4b/` (an A53-flavoured Pi 4 project — same SoC, generic-A53 core settings, its own `config.txt`; commit `22376b7` corrects its GIC-400 base addresses) and `_projects/aarch64a53-generic-qemu/` with `scripts/aarch64a53-generic-qemu.sh`. **Be precise about what that QEMU lane is: it is plo-only — the kernel never starts.** plo needs a firmware DTB in `x0` and QEMU supplies none, so it faults in early hal init, and `-dtb` does not help; the ceiling is recorded in `docs/misc/2026-09-08-qemu-boot-ceiling.md` so nobody re-attempts it. The lane is still useful, but as a *structural* boot check on cut SD images (`scripts/qemu-boot-sdimage.sh`), not as a place to develop kernel changes. Useful precedent: the generic `_targets/aarch64aXX/generic` split means a third aarch64 board should need only a `_projects/` directory.
- Also: a 1120-line `busybox_config`, a rootfs overlay (`etc/rc.psh`, `etc/ntp.conf` for boot-time clock sync, a curses smoke test), firmware staging + DTB staging helpers (`rpi4b_stageDtb`, `rpi4b_stageFirmware`, with an optional `fdtput` memory patch for the QEMU lane), and `ports.yaml` (266 lines) — the per-project port selection list, whose comments record the deliberate `if: false` → `if: true` promotion order (sdl2 → X11 stack → dillo/nano/mc → python → games).

### 5. Build system improvements

Everything here is board-independent and reusable. The common theme is *silent staleness*: in each case the build produced a plausible, working artefact that did not contain the change that had just been made, and in each case the fix is a dependency or invalidation key that upstream's build was missing rather than a workaround.

- **★ The whole image builds reproducibly from a blank OS, and that is gated.** Worth stating
  first, because it is the first thing an outside reader wants to know. A wiped VM on a fresh
  Ubuntu 24.04 cloud image → `git clone` the coordination repo → bootstrap →
  `rebuild-rpi4b-fast.sh --variant sd --with-showcase` produced an 835 MiB SD image with
  `BUILD_RC=0`, containing base + GLQuake + the X11 desktop, with no rsync and no VM-side edits.
  The release gate is `docker build --no-cache --pull` against the repo's own `Dockerfile`.
  **The masking trap is worth as much as the result:** `rm -rf .buildroot` is *not* a clean-build
  proxy — out-of-tree caches survive it and hide fresh-toolchain breakage (the toolchain-bundled
  sysroot headers under `.toolchain/…/usr/include`, the legacy X server core archives,
  `/tmp/x11-phoenix`, `/tmp/phoenix-iconv`), and even plain `docker build` reuses a stale clone
  layer. This is also the real reason for the `.gitmodules` repoint reported above: several pinned
  upstream sources had *vanished* — Mesa `e8791b4`, quakespasm `4abb324`, vkquake `f4d923e` all
  answer `not our ref` and their archive URLs 404 — so mirroring every pin in the fork's own org
  is a **correctness requirement, not tidiness**.
- **★ A fourth silent-build class: a port staging a system header into the shared sysroot breaks a
  *different* port, build-order-dependently.** Same family as the three below — no isolation
  between a port's staged headers and the shared sysroot — and it hid for months. The Midnight
  Commander recipe staged its own stub `<langinfo.h>` and `<mntent.h>` over the real libphoenix
  ones in the shared prefix; the `mntent.h` stub lacked `hasmntopt`, so `configure` (reading the
  real header) set `HAVE_HASMNTOPT` while the compile saw no declaration. Invisible because the
  victim built *before* the offender and merely inherited the poisoned header on the next run.
  Now documented in-tree at `mc/port.def.sh:64`.
- **★ Relink every program when libphoenix changes.** `makes/binary.mk` + `Makefile.common` (`382d7dd`, `a80c1fe`). Most components never name libphoenix in `LIBS` — they get it from the sysroot — so their link rule had no dependency on it. On 2026-09-04 a libphoenix stdio fix left **119 of 336 rootfs ELFs still linked against the previous libc**; the ABI was unchanged, so they worked, which is precisely why nobody would notice. The fix adds `$(wildcard $(PREFIX_SYSROOT)/lib/libphoenix.a)` as a prerequisite. Its own footgun is fixed in the same series: prerequisites are expanded *inside* `--whole-archive`, and since `libc`/`libm`/`libpthread` are all symlinks to `libphoenix.a`, the sentinel collided with `-lpthread` ("multiple definition of `setsid`" ×40) — hence `LINK_INPUTS = $(filter-out $(LIBC_RELINK_DEP),$^)`.
- **★ `port_manager` staleness model** (`5868ef9`, `4361efc`, `f44fd69`, `49ca648`, `f05ded2`, `24bf9c1`, `fc3de5c`, `15d5fc6`, `925550b`, `18c3e04`). Three independent silent-stale-build classes closed:

  - *Frozen libc feature detection.* autoconf and CMake cache their probe answers, so a port configured before libphoenix gained `strtok_r` kept compiling its own `static` fallback forever — and once the libc **header** declares the function that fallback stops being redundant and becomes a hard error (`evdns.c: static declaration of 'strtok_r' follows non-static declaration`, which broke the entire ports stage). The fix fingerprints libphoenix's exported symbol list and drops configure markers only when the **libc API** actually changes, so an ordinary rebuild does not force everything to re-configure. 16 autoconf + 11 CMake ports shared the bug; ffmpeg was additionally missed because it writes `config.log` under `ffbuild/` rather than at the root.
  - *Filename-keyed patch markers.* A regenerated patch with an unchanged name was silently skipped and the port kept building the old patch's source with no diagnostic — not theoretical here, since the game patches are generated from forks. Markers are now keyed on **content hash**, and the refusal is self-healing: it removes the stale work directory and says to re-run, one failure instead of a permanent manual step (`PHOENIX_PORT_KEEP_STALE_WORKDIR=1` opts out).
  - *Dependency and recipe edits.* A dependency's **version** change, and editing a recipe at all, now rebuild the dependents.
- **★ `ports.mk` second source of truth documented.** `PORTS_SUPPORTED_VERSIONS`/`PORTS_DEFAULT_VERSIONS` expand into `-I`/`-L` flags for every make-built component, so a version there that no longer matches the recipe points the whole system at a `versioned-ports/openssl-<old>/` directory the ports stage never creates. Bumped to 1.1.1w with a comment naming the hazard.
- **★ Pin the language standard.** `Makefile.common`: `CFLAGS += -std=gnu17`, `CXXFLAGS += -std=gnu++17`, placed before the `EXPORT_*FLAGS` capture so ports inherit it. gcc ≥ 15 defaults to C23, where an implicit function declaration is a hard error — which breaks every gnulib-based port. No-op on gcc-14.
- **★ `PhxVersion` learns upstream letter patch-releases** (`575632a`, `port_manager/version.py`). openssl ships `1.1.1`, `1.1.1a` … `1.1.1w`. PEP 440 knows only `a`/`b`/`c`/`rc` and reads them as **pre**-releases, so `PhxVersion("1.1.1w")` raised `InvalidVersion` and aborted `discover_ports()` outright, while the letters that *did* parse sorted backwards (`1.1.1a < 1.1.1`, the opposite of upstream's meaning). A trailing letter is now translated to a PEP 440 post-release (`a → .post1` … `w → .post23`), giving `1.1.1 < 1.1.1a < 1.1.1w < 1.1.2` for all 26 letters, while `__str__` still returns the original string so namevers and install directories read `1.1.1w`. Covered by doctests and `port_manager_test.py`.
- **`port.subr` grows two public helpers** — `b_port_apply_patches` (content-hashed markers, self-healing refusal) and `b_port_invalidate_stale_configure` — plus a `b_port_download` form that saves a remote file under a different local name, which is what makes GitHub's `/<tag>.tar.gz` and `/archive/<sha>.tar.gz` endpoints usable as reproducible, hash-pinned sources. Seven ports here pin a commit archive that way.
- **Toolchain rebase to gcc-16.2.0 + binutils-2.47** (`f90bc71`, `96f5697`, `20bc28f`): `binutils-2.47-04-aarch64-phoenix.patch` (BFD/config.bfd target vector), `gcc-16.2.0-11-aarch64-phoenix.patch` (`aarch64*-*-phoenix*` in `config.gcc`), `-05-libstdcpp.patch` (265 lines, libstdc++ PIC configury), `-09-fix-libc-spec.patch` (`STD_LIB_SPEC` → `LIB_SPEC`), `-04-arm-pic_crtstuff.patch`. Plus multi-mirror GNU fetch (`643293e`), `-j nproc` for toolchain and image builds (`7463000`), and autotools host-triplet normalisation for aarch64 (`2a6aebb`). One known hole in the libstdc++ configury survives this rebase and is worth fixing upstream of the fork: **no time backends are enabled**, so `std::chrono::steady_clock` has 1-second resolution — the defect described in §3. `--enable-libstdcxx-time=rt` is the fix; it was deliberately not attempted here.

---

## Hardware experiments outside the Phoenix repos

Three bodies of work in this port are not changes to Phoenix at all: they are programs *built
on* Phoenix, living in the coordination repo under `tools/`, written to answer a hardware
question. They are here because two of them exercise the SoC harder than any driver in the
fork, and because one — the H.265 decoder — is a complete non-coherent DMA-master device driver
written entirely in Phoenix **userspace**, with no kernel change, no driver-framework change
and no new syscall. That is a claim about Phoenix's userspace driver model, which is why it
belongs in this document rather than outside it.

All three are fork-authored and BSD-3. None is a shipped image component; each is
cross-compiled and staged into the netboot root by hand. Provenance caveat at the end.

### 1. BCM2711 HEVC / H.265 hardware video decode (`tools/hevc-decode/`)

The Pi 4's `rpivid` / `hevc_dec` block is a fixed-function, register-driven **stateless** H.265
decoder at ARM-physical `0xfeb00000`, with its own ARGON interrupt controller at `0xfeb10000`
(GIC SPI 98 → Phoenix absolute IRQ 130). Unlike the SoC's H.264 decoder — which runs on the
VideoCore firmware and is reachable only through VCHIQ + MMAL, a firmware-comms wall this fork
deliberately did **not** climb — the HEVC block needs no firmware blob at decode time: the
mailbox enables its clock, and everything after that is MMIO. That made it the only tractable
hardware-offload target on this board, and it was ported register-by-register from Linux's
`drivers/media/platform/raspberrypi/hevc_dec/hevc_d_h265.c` with the V4L2 layer dropped.

Decode is two hardware phases: phase 1 executes a **DMA command buffer** of
`u64 addr|(data<<32)` entries after a `CFBASE` doorbell (cross-checked by `CFSTATUS == CFNUM`);
phase 2 is direct APB, kicked by `NUMROWS`, with completion signalled on the ARGON
`ACTIVE1`/`ACTIVE2` bits. The result is roughly **2 900 hand-written lines** — `hevc-m2.c`
(1 388), a from-scratch Annex-B/SPS/PPS/slice parser `hevc_parse.c` (599), an ISOBMFF demuxer
`hevc_mp4.c` (276), the cited register map `hevc_regs.h` (119), plus the M0/M1 bring-up probes
— that decode real H.265 **bit-exact against ffmpeg's software decoder**, unpack Broadcom's
SAND/COL128 tiled output to linear, and display on `/dev/fb0`.

Coverage is a subset, not "H.265 support": every coding tool **x265 enables by default** is
covered bit-exact — I/P/B slices, arbitrary non-reference-B counts, hierarchical b-pyramid over
a general POC-indexed DPB, multi-reference, temporal MVP, SAO, WPP wavefront and weighted
prediction — from 64×64 to 1080p, in 8-bit and 10-bit (Main10). Tools *outside* that subset
(tiles, non-zero deblock offsets, AMP, emulation-prevention bytes inside headers, the
Range-Extensions intra profile) are rejected or mismatch, and are listed as out-of-subset
rather than as bugs.

| change | where | what/why |
|---|---|---|
| ★ H.265 hardware decode with no VCHIQ, no V4L2, no firmware blob | `tools/hevc-decode/hevc-m2.c`, `hevc_regs.h` (coord `34bbecb75` → `bf02fd8c7`) | The rpivid block driven directly: mailbox clock (`RPI_FIRMWARE_HEVC_CLK_ID` 11) set to the firmware-reported maximum, version-register `0x202` gate, phase-1 DMA command buffer + phase-2 APB, CABAC `prob_init` per `init_type = 2 − slice_type`, SPS/PPS/slice/QP/CONFIG2 register packing, plain-physical `>>6` addressing (the scb bus has no `dma-ranges`). First hardware video decode on Phoenix. |
| Bring-up probes: reachability, DMA, IRQ | `tools/hevc-probe/hevc-probe.c` (M0), `tools/hevc-decode/hevc-m1.c` (M1) | M0 proves the block is reachable without VCHIQ (clock, `VERSION == 0x202`, ARGON INTC). M1 proves the two facilities a DMA master needs **already exist in Phoenix**: `mmap(MAP_UNCACHED\|MAP_CONTIGUOUS\|MAP_ANONYMOUS)` + `va2pa()` yields contiguous sub-4 GB uncached buffers, and `interrupt(130, …)` registers the GIC SPI-98 handler with zero spurious IRQs. |
| ★ Bit-exact conformance harness against ffmpeg | `hevc-play <file> <golden.nv12>`; `testdata/*.265` + `gen-*.sh` (coord `98352206c`) | Every capability claim is a byte-compare of the hardware's output against an ffmpeg software decode of the same bitstream: 37 committed test vectors (`.265` plus four `.mp4`) from scripted x265 encodes. Negative controls are committed too — `-DHEVC_NO_WEIGHT` builds a deliberately non-weighted decoder that mismatches by 491 284 pixels, proving the weighted-prediction path is actually exercised. |
| Runtime `.265` player + in-tool MP4/MOV demux | `hevc_parse.c`, `hevc_mp4.c` (coord `7a2fb69ea`, `c90e95dcc`, `ca466493b`) | `hevc-play` parses geometry from the SPS and per-frame params from each slice header, so arbitrary in-subset files decode with no rebuild. An `.mp4`/`.mov` is demuxed to Annex-B in-tool (no libavformat): the video track's samples are located through `stsc` → `stco`/`co64` with sizes from `stsz`, so a normal interleaved audio+video file plays and the audio chunks are simply never visited. Deliberately narrow — exactly one HEVC video track, non-fragmented; `moof`, no-video and multi-video files are **rejected loudly** rather than mis-handled. |
| 10-bit (Main10) decode and display | coord `b302065cf`, `bf02fd8c7` | Four register deltas keyed off the SPS bit depth (CONFIG2 low 10 bits `0x088`→`0x3AA`, `RPI_SPS0` +`0x220000`, `RPI_QP` += `QpBdOffsetY`), and an `NV12_10_COL128` output layout packing 3 samples LSB-first per 32-bit little-endian word (96 samples per 128-byte SAND column). Bit-exact on luma **and** chroma, and renders on HDMI after a 10→8 downshift. |
| ★ Non-coherent DMA ordering: `dsb sy`, not `dmb ish` | `hevc_dma_fence()` (coord `5f9945956`, `f3767112d`) | The doorbell needs a **full system** barrier. `__sync_synchronize()` emits inner-shareable `dmb ish`, which does not order Normal-NC writes against a DMA master outside the CPU inner domain — the block then reads a stale command buffer. The mirror-image case is the *completion read*: `f3767112d` adds a `dsb sy` after acking ARGON `ACTIVE` and before the CPU reads the output, because a plain relaxed volatile read can be hoisted ahead of the DMA whereas Linux's `readl` carries an implicit `__iormb`. **A general lesson for any Phoenix userspace DMA driver on a non-coherent SoC, not an rpivid quirk.** |
| IRQ-driven completion | coord `1fd69c736` | Decode blocks in `condWait` on the SPI-98 handler instead of hot-polling `ARG_IC_ICTRL` every 10 µs, matching Linux. Dual-checked (ISR flag plus a 2 ms direct-poll fallback) so it is correct whether or not the IRQ fires, and falls back to polling if unregistered. The IRQ genuinely fires — ~2 ISR calls per frame, one per phase. |

**Limits.**

- **One open, non-software defect.** A small number of output pixels are wrong
  non-deterministically in a fraction of decodes — the same clip is bit-exact on one run and
  corrupt on the next, occasionally even an IDR I-frame. It is worse under heavier memory
  traffic (a concurrent `fb_blit` during on-HDMI playback, and independently on
  high-complexity clips that do more PU/coeff/reference DMA); simple low-traffic clips are
  effectively always clean (an `ultrafast` clip verified 15/15 back-to-back). Every
  decoder-side software cause has been ruled out and each refutation is recorded: the barriers
  are architecturally complete (the completion-read barrier measurably helped, 3/10 → 15/15
  clean on one clip, but did not close it); it is **not** PU/coeff buffer exhaustion
  (`RPI_STATUS` never sets the exhaustion bits on a corrupt frame and `CFSTATUS == CFNUM`
  always); it is **not** CPU polling contention (true IRQ-driven completion left the rate
  unchanged); and a survey of the Linux driver plus the BCM2711 DT found no missing
  initialisation — Linux programs nothing beyond clock-enable, INTC-enable and the version
  check, and this port already sets the clock to the firmware maximum with low-PA DMA buffers.
  The recorded conclusion is a genuine SoC memory-fabric interaction under decode DMA load,
  and the decisive next step named is a Linux-on-the-same-Pi-4 side-by-side. An earlier belief
  that it was `fb_blit`-only and bit-exact headless was **explicitly corrected** — it
  manifests headless too.
- **Subset, not a codec** (see the out-of-subset list above).
- **Not wired into anything.** There is no libavcodec integration and no `/dev/` node for the
  rpivid block: this is a standalone tool, not a Phoenix video subsystem. Do not read that as a
  statement about the `ffmpeg` port — that port's *software* MJPEG/H.264 decode is separately
  hardware-proven on `/dev/fb0` and in an X window (see its row in the application-ports table).
  The two results are independent and should not be conflated.
- **H.264 is walled, deliberately.** There is no directly-addressable H.264 register block on
  BCM2711; H.264 decode lives on the VideoCore firmware behind VCHIQ + MMAL. Scoped and
  banked, not attempted.
- **Decode rate, measured 2026-09-09.** A real 1080p H.265 phone clip (1058 frames) plays on
  `/dev/fb0` at **21.7 fps, 0 faults**. The decoder is not the limit: ~41.5 ms of each 46 ms frame
  is the **framebuffer blit** and ~4.5 ms is the decode, so this is a framebuffer-bandwidth number
  and full-screen 1080p is its worst case. (Earlier revisions of this document stated that no rate
  figure existed for any clip.)
- Full register spec: `docs/misc/2026-08-28-hevc-m2-register-spec.md`. Capability matrix and the
  eight hard-won gotchas: `tools/hevc-decode/README.md`.

### 2. Machine-learning inference, CPU and V3D GPU compute (`tools/cnn-mnist/`, `tools/v3d-driver-port/csd_*.c`)

The question was whether the Pi 4's V3D GPU can accelerate small neural-network inference under
Phoenix. Answering it required bringing up V3D **compute** (CSD) for the first time — the winsys
had a `DRM_V3D_SUBMIT_CSD` handler that nothing had ever called (the two driver defects that
surfaced are in the drivers section, §7). Three staged milestones proved dispatch, then a store
reaching DRAM, then work-group IDs; from there a matmul compute kernel was generated through
`tools/v3d-shader-tool` (NIR → `v3d_compile`, not hand-assembled QPU) and found **numerically
bit-identical to the CPU** — `max_rel_err = 0`.

The performance story is the honest part, and it went through two retractions worth recording
because both were measurement defects rather than results. The first verdict was "GPU 6.63×
slower" (11.96 vs 1.80 ms/matmul). That was ~97 % **UART print latency** — see §7 of the
drivers section; the tell was that an *empty* kernel "measured" 21 ms, more than the real
matmul. Gating that print moved the same benchmark to 0.377 ms/dispatch against a 1.644 ms CPU
baseline, and a size sweep then reported 10.9×/11.0×/3.6×/parity at N = 256/512/1024/2048. But
the **end-to-end** measurement retracted that framing in turn: a real 3-layer MLP
(784→256→256→10) with every matmul on the GPU runs at **1.75 ms/image versus 1.57 ms on the CPU
— 0.90×, parity, the GPU marginally slower** — because the microbenchmark's CPU baseline was
cache-cold and inflated, where the MLP's identical layer takes ~0.38 ms cache-hot.

So the defensible claim is narrow, and it is a correctness claim rather than a speed claim:
**GPU neural-network inference on Phoenix is numerically correct and at CPU parity.** The GPU
predictions match the numpy reference 12/12 including a digit the model itself misclassifies.

| change | where | what/why |
|---|---|---|
| ★ First V3D GPU **compute** dispatch on Phoenix | `tools/v3d-driver-port/csd_probe.c` (coord `10b3522e1`, `a58c2e83d`, `2ed54d3f7`) | Three staged hardware milestones: an empty CSNOP kernel dispatches through `SUBMIT_CSD` (`rc = 0`, `CSDDONE`, 0 faults) → a constant store reaches DRAM and reads back → work-group IDs correct. This validated the winsys' previously-untested `ioc_submit_csd`, and it is what surfaced the cache-flush defect. |
| Numerically bit-exact GPU matmul | `tools/v3d-driver-port/csd_matmul.c` (coord `3c631da59`, `3b6ebd4b3`) | 256×256 matrix-vector on V3D CSD versus CPU, persistent buffer objects, `max_rel_err = 0` — including the first working TMU *general loads*. Later made runtime-`N`: `N` is read from a fourth SSBO so it drives both the loop bound and the row stride — a compile-time constant had let the compiler bake the stride as an immediate while only the bound tracked the uniform, silently corrupting every `N != 256`. |
| ★ End-to-end GPU NN inference — correct, at CPU parity | `tools/cnn-mnist/mlp_train_export.py`, `tools/v3d-driver-port/mlp_gpu.c` (coord `7239b0313`) | A 784→256→256→10 ReLU MLP with every matmul on the GPU, run as a `/dev/v3d-srv` client with persistent per-layer weight BOs and the 10-class layer zero-padded to the fixed 256-row grid. **Predictions match the numpy reference 12/12**, including a model-misclassified digit. Steady state over 50 × 12 images: **GPU 1.75 vs CPU 1.57 ms/image = 0.90×**. The per-dispatch facts that survive: 0.377 ms/matmul, 0.020 ms empty-dispatch floor. |
| CNN inference on the CPU | `tools/cnn-mnist/cnn.c`, `cnn_train_export.py` (coord `7be8d0fe4`, `e20fbaa95`) | A self-contained trained convnet — 1×28×28 → 3×3 conv (8 ch) → ReLU → 2×2 maxpool → flatten(1352) → linear head → argmax, weights exported from a numpy im2col-backprop trainer. On hardware all 10 test predictions match the numpy reference exactly, including a digit the model gets wrong, with 0 faults — so conv/ReLU/pool/dense compute is correct on Phoenix. |
| LLM inference on the CPU | `phoenix-rtos-ports/llama2/` — see the `llama2` row in the application-ports table | llama2.c, CPU only, bit-identical to the host reference. **Never integrated with the GPU path**: an LLM token costs dozens of matmuls, and at parity-or-worse per matmul that is strictly a loss. |

**Limits.**

- **No GPU speedup is claimed or proven.** End-to-end it is parity (0.90×). The 4.4× and ~11×
  microbenchmark ratios rest on a CPU baseline the project's own later measurement called
  inflated; they are recorded as superseded framing, not as results. What is proven is
  numerical correctness and a negligible dispatch floor.
- **The one attempt at a decisive win is a recorded failure.**
  `tools/v3d-driver-port/csd_gemm8.c` (coord `2fc2638d4`) is a batched 8-column GEMM — the
  shape that gives weight reuse a per-vector GEMV lacks. It compiles clean host-side but
  **hangs the hardware**: the dispatch times out with `CSD status = 0x7`, `num_completed = 0`
  and garbage output, apparently because the 8-accumulator kernel drops to 2 threads under
  register pressure and never signals completion. Kept in-tree as an honest record of the
  attempt; a tiled/shared-memory GEMM was explicitly ruled out as an optimisation grind.
- **GPU time is super-linear in the reduction length** with the naive per-row GEMV (each of the
  256 invocations re-reads the whole input vector), so it hits a bandwidth wall past roughly
  N = 1024. Small/medium reductions are the favourable regime; LLM-scale reductions are not.
- **The models are small** (~62 000 MACs per image for the CNN) and were evaluated on 10 (CNN)
  and 12 (MLP) embedded digits. That establishes correctness, not throughput. The 95.5 % /
  97.75 % accuracies come from the host trainer, not from a run on the Pi.
- **There is no GPU-accelerated CNN** — only a GPU-accelerated MLP. The conv-as-im2col GEMM
  that would have put the CNN on the GPU was named as the next step and never built.
- Design and feasibility notes: `docs/done/2026-08-13-ml-phase2-v3d-gpu-matmul-design.md`.

### 3. Per-master memory-bus attribution: the BCM2711 AXI performance monitor (`tools/axi-pmu/`)

Every bandwidth number elsewhere in this port — NFS throughput, genet RX, V3D fill rate — was
*inferred* from wall-clock timing. `tools/axi-pmu/` (313 lines) reads the BCM2711's **System AXI
bandwidth monitors** at `0xfe009800` instead, so bus traffic is measured at the hardware. It is
the Phoenix analogue of the Linux `drivers/perf/` Raspberry Pi AXI PMU driver: mmap the perf
block (page-aligned, so `0xfe009000` + `0x800`), program one of three bandwidth watchers
(`BW0/1/2` at `0x40/0x80/0xc0`) with a 6-bit `BUS_WATCH` master selector, and read 31-bit
address/write/read transaction counters. The enable sequence matters and is easy to get wrong:
reset the monitor, reset the watcher, configure it, then enable **with the WATCH bit** — omit
that last bit and every counter reads zero.

Bus indices are not guessed. They come from the vendor driver's `system_bus_string_2711[]`
table (`0 DMA_L2, 1 TRANS, 2 JPEG, 3 VPU_UC, 4 DMA_UC, 5 SYSTEM_L2, 6 HVS, 7 ARGON, 8 H264,
9 PERIPHERAL, 10 ARM_UC, 11 ARM_L2`), which the tool prints inline; index ≥ 12 is unnamed and
not chased.

The transferable result is a **method**, recorded specifically so it is not re-tried the wrong
way. The first attempt at attributing the network path read 4/8/16 MB over NFS and looked for
the bus whose transaction count doubled per step. Every candidate did — because at fixed link
bandwidth a larger read simply takes proportionally *longer*, so read size is a proxy for
wall-clock time and any time-proportional master scales with it (HVS held a constant ~355 MB/s
and an unnamed free-running counter ~2.85 GB/s across all three sizes). The discriminator that
works is to **hold time fixed, toggle the workload, and normalise each window to transactions
per second before differencing** — the windows are never equal (a 1 000 ms idle `nanosleep`
versus a ~680 ms 16 MB read), so raw deltas are invalid and rates are valid. Time-proportional
masters then cancel and the target master stands out.

| change | where | what/why |
|---|---|---|
| ★ BCM2711 System AXI bandwidth-monitor reader | `tools/axi-pmu/axi-pmu.c` (coord `31fa83325`, `abb608319`) | First measurement of real bus traffic in this port rather than inference from throughput. Base `0xfe009800`, three bandwidth watchers, per-watcher A/W/R transaction + wait + max-outstanding counters masked to 31 bits, with the vendor enable sequence. Written as a plain userspace tool against `MAP_PHYSMEM`, in the same idiom as `rpi4-thermal`/`rpi4-hwrng`. |
| ★ Rate-normalised idle-vs-workload differencing | `tools/axi-pmu/axi-pmu.c` NET-ISO mode; `README.md` (coord `7e599a798`) | The method for isolating one small master against large, time-proportional background traffic — documented together with the confound it replaces, expressly so the size dose-response is not re-attempted. |
| genet RX-DMA identified as bus 9 (PERIPHERAL) | coord `7e599a798` | PERIPHERAL's **write** rate rises 94 K/s → 187 K/s (+92.7 K/s) during an NFS read. Independent magnitude cross-check: 92.7 K writes/s × ~256 B per burst ≈ 23.7 MB/s, against a measured 23.5 MB/s NFS read — so this is the network *data* path, not background. Bus 6 (HVS) cancels to −0.02 % across the same windows and is write-zero throughout, ruling it out for an RX-DMA **write** master; bus 10 (ARM) gains +1.12 M writes/s, the CPU copying received data. |
| Vendor bus-name table adopted | coord `7e599a798` | `system_bus_string_2711[]` from `external/linux/drivers/perf/raspberrypi_axi_monitor.c`, printed inline, replacing guessed bus identities. This settles bus 6 as display scanout (an earlier note had guessed it was genet-adjacent) and identifies **bus 7 = ARGON = the rpivid HEVC decoder** — the watch value for measuring the decoder's own memory traffic, and the intended instrument for the open HEVC memory-fabric defect in §1. |
| Mechanism validated against a known workload | coord `abb608319`; `README.md` | A CPU `memcpy` dose-response on bus 10 is exactly linear — 4/8/16 MB × 4 gives 1.05/2.13/4.26 M read transactions, a clean 2× per size step — with read ≈ write symmetry (as a memcpy must have) and a **stable 16 bytes per transaction across all three sizes**, a hardware-plausible 128-bit AXI burst. |

**Limits** — the tool's own recorded caveats, not smoothed over:

- **The absolute GB/s figures are partly definitional.** "1.43 GB/s measured versus 1.40 GB/s
  wall-clock" uses a *hardcoded* 16 B/transfer that was itself back-derived from known bytes ÷
  transactions, so the agreement is near-tautological. The load-bearing evidence is the
  linearity, the read ≈ write symmetry and the constant 16-byte burst — not an independent
  absolute oracle.
- **"Bus 10 = the CPU memcpy path" is measured, not an architectural identity.** The vendor enum
  labels 10 = `ARM_UC` and 11 = `ARM_L2`, but bus 11 read zero throughout.
- **The idle control is unreconciled.** An idle window returned ~4 M reads per 200 ms rather
  than the ~0 predicted. It is labelled "never truly idle" and the README says plainly: do not
  lean on it.
- **Scope.** System-monitor MMIO only. The second, VPU-side monitor block (`0xfee08000`, reached
  through the VideoCore mailbox) is deferred, as is any `/dev/axiperf` device — this is a tool,
  not a driver, and nothing in the shipped image consumes it.
- **Bus 7 was never measured during a live decode.** ARGON is *identified* as the rpivid decoder
  and named as the instrument for §1's open defect, but the two were never combined.

**Provenance.** Every number in this section was measured on this board by the tool named beside
it, and each is recorded in the commit that produced it plus the tool's README. Raw UART
captures are **not** retained in the repository (`artifacts/uart/` was emptied by the
pre-publication cleanup, `a4396edc8`), so the commit message and README are the primary record.
Where a measurement was later found to be an artefact of its own instrument — the GPU-compute
timings — both the retracted figure and the correction are stated above, not only the
correction.


---

## Caveats, open defects, and licensing

Stated plainly so nothing here is taken on trust.

**Closed since this document was first drafted:**
- **X desktop-exit crash — fixed, and the root cause is an upstream glamor bug.** Closing the
  desktop session faulted the glamor X server in `dixGetPrivate`. Three fixes were attempted and
  all reverted (`4133eff`, `4569f79`, `cf235b4`, reverted by `21bd0ec`) — all three were on the
  wrong object, and the fix that worked is in a different file. The record of those attempts is
  `docs/misc/2026-09-08-x-teardown-crash-open.md`, which predates the fix. Root cause and fix are
  `bde9a44a3` plus `tools/x11-port/patches/xorg-server-21.1.24-glamor-destroypixmap-chain.patch`,
  described in the ports section (§3). Quantified: **6/6 X sessions crashed before the fix, 0/18
  after.**

**Measured dead ends, recorded so nobody re-runs them.** This fork's X11 performance work ended by
*refuting* its own remaining hypotheses, and each refutation cost a cheap experiment instead of a
risky change:

- **Memory attributes are not the graphics bottleneck.** The X server's V3D buffers are mapped
  `MAP_UNCACHED`, and the present's readback ran at ~200 MB/s, so a cached alias plus a range
  invalidate looked like a large win — and would have been a coherency-sensitive change. `tools/memprobe`
  (its own anonymous memory: no GPU, no device, no coherency question) measured **uncached sequential
  reads at 1001 MB/s against cached 1316 MB/s** — only 1.3× apart, and *5× faster* than the readback
  actually achieves. The mapping was never the constraint; the readback's cost is Mesa's own
  `glReadPixels` work plus the BGRA↔RGBA CPU swizzle.
- **The AF_UNIX poller broadcast is not the cost either.** Every read and write broadcasts to *all*
  AF_UNIX pollers system-wide through one global lock, which looks obviously expensive. Going from 6 X
  clients to 2 left the mean inter-read gap **unchanged** (2.839 → 2.785 ms).
- **Deferring the screen present while a client request is in flight** buys nothing measurable
  (10.03 fps against a 9.73–10.62 baseline). Reverted.
- **Run-to-run spread on this stack is ±4.5%** (10.56 / 10.62 / 9.73 fps from identical, probe-free
  code). Any graphics claim below ~10% has to be repeated before it means anything — which is why
  several single-run figures in the X11 history should be read as directional only.

**⛔ CLOSED by the owner 2026-09-09 — do not implement. The one change that would still have moved GL-in-a-window**, and it is a project rather than a tuning
pass: the client renders to an FBO, `glReadPixels` into its own memory, then `XPutImage` **1.2 MB per
frame** back to the server, which uploads it into the screen texture — GPU → CPU → socket → CPU → GPU
every frame, and the transfer *is* the cost (disabling presents alone took it 10.62 → 14.94 fps, so
presents stalling that transfer are ~30–35% of it). The fix is not to make the transfer faster but to
not do it: share the GPU buffer and send damage instead. Client and server already talk to the same
`/dev/v3d-srv` and two concurrent daemon clients were proven bit-exact, so the plumbing exists — this
would be a mini-DRI3 for this port. It affects only GL-in-a-window; the desktop itself is already at
25.6 presents/s.

**Known-open defects in this fork** (not hidden in the sections above):
- **vkQuake intermittent missing torches** — present at a low rate; measured 9/9 clean on the
  current build, which bounds the rate rather than closing the defect.
- **Quake III `q3dm7` CT0 binner wedge** — open and banked, but the investigation reached a
  definite conclusion worth recording so the next person does not re-chase it. **It is not an MMU
  fault.** With the real fault-report registers instrumented (`MMU_VIO_ADDR` 0x1234, `MMU_VIO_ID`
  0x122c, offsets confirmed against Linux `v3d_regs.h`), a wedged trial reported `vio_addr=0`,
  `vio_id=0`, `int_sts=0`, no binner overflow, and `ct0ca` parked *inside* its own valid BCL BO —
  a binner front-end pipeline stall with zero error status. The earlier "`mmu_ill=0x8000886x` ⇒
  stale PTE" reading was a red herring: `MMU_ILLEGAL_ADDR` is the scratch-page redirect *we
  program*, so reading it back only echoes `scratch_pa`. It is intermittent (~50 % of boots) and
  workload-specific (q3dm1's 1942 faces never wedge, q3dm7's 5823 do), `r_mergeLightmaps 0` is
  **not** a fix (it wedged with the cvar set, after one lucky clean run), and a reset+drop
  mitigation ships. Named next step: diff our binner/tile-alloc setup (`TILE_BINNING_MODE_CFG`,
  tile_alloc/tile_state sizing, CT0QMA/CT0QMS) and CPU→GPU coherency before the CT0 kick against
  Mesa/Linux v3d, since Linux renders the same map on the same silicon.
- **SuperTuxKart faults intermittently in its own code** — historically ~40% of runs, at *varying*
  sites. ⚠ Earlier revisions of this document scoped this to "the `--profile-time` report path,
  after the race", which is **wrong**: `addr2line` on four distinct fault PCs gives
  `SkiddingAI::findNonCrashingPoint()`, `FontManager::loadFonts()` (**startup**, before any race),
  and **two inside libphoenix's allocator** (`lib_listRemove`, `_malloc_chunkJoin`). The allocator
  is the victim, not the cause — a 4.8 M-op harness exonerated it of any internal defect — and both
  of those sites are now guarded (a corrupt free-bin link abandons the bin instead of being
  followed). A 6-trial bench afterwards came back 0/6, which is suggestive only: no guard fired, so
  the corruption did not occur, and relinking STK perturbed its heap layout. The 11-of-17
  attribution in the reliability figure below inherits this error.
- **Quake II's underwater view is Y-mirrored — fixed with a stopgap the fix itself calls one.** It
  is "a conditional correction on a conditional bug", and it over-corrects at `viewsize <= 71`.
  The structural fix is to declare the scanout FBO `FlipY` and delete the size gate in
  `st_atom_framebuffer.c`, which would also remove glamor's three hand-rolled flips; that is not
  hypothetical, since the driver's own `GL_EXTENSIONS` dump shows `GL_MESA_framebuffer_flip_y` is
  supported. Same FB-0-redirect seam as the GLES-hint trap in the game-ports section.
- **A stale Mesa shader-cache blob renders as green speckle over an otherwise-valid frame.**
  Phoenix has no ELF build-id, so the on-disk shader cache keys on shader source only and a host
  toolchain change silently invalidates nothing: after the gcc-16.2 promotion the persistent cache
  still held gcc-14-era blobs, the GPU executed stale QPU binaries, and SuperTuxKart's loading
  screen came up speckled; `rm -rf` of the cache directory cleared it. Useful diagnostic tell —
  coloured speckle *over a valid frame* means stale-blob execution, where a real GPU wedge drops
  whole frames and logs one. Bump `V3D_PHX_CACHE_VERSION` or clear the cache on any clean rebuild.
- **GPU X11 xterm resize artefacts — investigated and not reproduced by a sound measurement.**
  Recorded as such rather than omitted: resize correctness was verified (server and client agree
  once settled), and the residual hypothesis is intermittent frames during a fast drag, since this
  stack has no compositing and presents by GPU readback.
- Individual drivers state their own limits in the drivers section (e.g. SD writes are PIO,
  GPIO is read-only, V3DV has no WSI).

**Reliability, system-level.** Computed from the UART logs already on disk rather than from a
fresh campaign (one `test-cycle-*` invocation = one power-on = one log; success = the `(psh)%`
prompt appears; fault pattern = the same markers `scripts/uart-summary.sh` uses): **132 boots, 132
reached the prompt — 100.0 %.** 17 of the 132 logs contain a fault pattern and **all 17 are
attributed, 0 unexplained** — 11 attributed to a SuperTuxKart crash (see the corrected entry above:
that attribution is not sound), 6 the X desktop-exit bug being deliberately reproduced before it was
fixed.

⚠ **The figure above was measured with a detector that could not see a board hang** — a
`uart-summary.sh` blind to both a **truncated** fault message and a **mid-print stop**. A desktop-exit
wedge that hung the entire board reported *zero faults* under it, and that wedge was live across the
same window.

**Recomputed 2026-09-09 with the fixed detector** (`scripts/reliability-tally.sh --since 20260908`,
so the number is reproducible rather than hand-derived):

| | |
|---|---|
| logs (non-empty) | **313** |
| reached the `(psh)%` prompt | **313 — 100.0%** |
| fault-bearing logs | **36** |
| **logs ending MID-PRINT (board hang)** | **5** |

The prompt rate survives. What the old figure missed is the last row, and the reason it matters is
structural: **every one of those 5 runs had already reached a prompt**, because the hang happened
*after* boot — so "reached the prompt" cannot see this class at all, and "0 unexplained" was
unsupported rather than merely optimistic.

All five are now attributed: two are the desktop-exit wedge (`xexit2`, `kmguard` — the run that
reported zero faults), one is the `process_getName` fault storm (`dfree4`), and the remaining two are
benign mid-line prints (console art from the shell demo, and a hex value from an A/B probe). **The
three real hangs are all fixed** (shortlist rows 15 and 16).

⚠ Scope the window when re-running: over *every* log on disk the same script reports 88.2% and 50%
fault-bearing, because the archive reaches back to bring-up when the board frequently did not boot and
faults were being reproduced on purpose. `--since` is therefore mandatory. **The caveat travels with the
number: all 132 are netboot.** SD boot is unverified (no card in the host reader).
`docs/misc/2026-09-08-boot-stability-tally.md`.

**Licensing.** The five game engines are GPL (GPL-2.0+, SuperTuxKart GPL-3.0+) and live only in
`phoenix-rtos-ports` as recipes plus glue — no GPL code was introduced into Phoenix core repos.
New drivers carry the upstream `%LICENSE%` marker; fork-authored tools are BSD-3.

**Measurement claims.** Where a number appears (throughput, latency, pass rates) it came from
hardware on this board with the tool named beside it. Claims that could not be corroborated from
the diff were rewritten to describe what the diff does. Several methodology findings are the
reason some of the visual claims are trustworthy and one earlier claim was not:

- **An HDMI grab of animated GPU content proves nothing either way.** The discriminator run:
  `quake3 +devmap q3dm1`, a simple map, produced a clean GPU log yet a striped HDMI grab, while
  the same cycle's *static* bootloader grab was perfectly clean — so the striping is tearing of
  continuously page-flipped content, present regardless of GPU health. The GPU parity numbers here
  therefore come from a coherent per-frame TCP capture sink (`scripts/quake-capture-sink.py`,
  `scripts/quake-visual-compare.py`) paired frame-for-frame against a host llvmpipe reference:
  **Quake II SSIM mean 0.993 / min 0.940 over 23 pairs at 1920×1080; q3dm7 SSIM 0.989 / min 0.985
  over 136 frames**, plus STK's 0.991.
- **…and a tiling-aware comparison hid a real corruption bug entirely.** That same frame-dump path
  reads back through a tiling-*aware* GPU blit and scored 0.993 "renders fine" while the actual
  HDMI output was shredded, because the HVS raw scan is exactly what the blit bypasses. Recorded in
  the Mesa commit that fixed it.
- **An intermittent visual defect must be judged by a pass rate over trials, never a screenshot.**
  Five premature closures of the vkQuake torch bug came from doing the latter, which is why
  `scripts/check-torch-rois.py` has a `--rate` mode and why the torch caveat above quotes 9/9
  rather than "fixed".
- **Historical failure rates are bounded by a host-side artefact.** A class of apparent target
  defects — "Firmware not found" netboot loops, `exec … err=-34` — was the *host's* USB NIC
  dropping frames on wake with Energy-Efficient Ethernet enabled; `netboot-server.sh` now disables
  host EEE and the offloads on every bring-up. Distinct from the BCM54213PE-side de-advertisement.
- One retraction is stated in full above rather than only its correction: the boot-time fix in the
  kernel performance table also had a USB-enumeration improvement credited to it, which a
  controlled A/B measured as not significant, and it was withdrawn.

## Repo map

All paths are relative to a checkout root holding the siblings side by side:

```
phoenix-rtos-kernel/       hal/aarch64/, proc/, vm/, posix/     ← bring-up + core fixes
phoenix-rtos-devices/      gpu/ wifi/ bt/ audio/ video/ misc/   ← new, Pi 4 drivers
                           usb/xhci/ storage/bcm2711-emmc/ tty/
phoenix-rtos-lwip/         drivers/bcm-genet.c, wifi43455.c     ← Ethernet + WiFi netifs
phoenix-rtos-filesystems/  nfs/                                 ← new NFSv4 client
libphoenix/                pthread/ stdlib/ stdio/ unistd/      ← libc gaps + fixes
phoenix-rtos-ports/        41 new port dirs (36 apps + 5 games)
phoenix-rtos-project/      _targets/aarch64a72/generic/         ← the board definition
plo/                       aarch64 boot path
```

*Generated 2026-09-08 from `origin/master..HEAD` across 14 repos.*

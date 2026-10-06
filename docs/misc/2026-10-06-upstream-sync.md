# Upstream sync 2026-10-06 (the missed Saturday 10-03 sync)

**Status: MERGED ON BRANCHES, NOT BUILT, NOT GATED.** Every repo with incoming
commits has a branch `upstream-sync-2026-10-06` in a worktree under
`/home/houp/.claude/jobs/c8f1289c/tmp/wt-sync/<repo>`. No `master` in `sources/` was
touched, nothing was pushed, no image was built, no Pi cycle was run.

**Build scope for the set: `--scope full-clean`** (syscall renumber + three ABI
struct changes + `_SC_*` renumber), plus a toolchain-bundle resync before any
standalone tool is rebuilt **and a rebuild of the toolchain's libstdc++** (it has
`_SC_NPROCESSORS_ONLN` compiled in). Details per repo below; the gate is at the end.

## Summary

| repo | incoming | conflicts | our extra commits on the branch | ABI / syscall | scope | risk |
|---|---|---|---|---|---|---|
| phoenix-rtos-kernel | 9 | 6 files | write-offset guard | **syscall appended (ours renumbered)**, `threadinfo_t`, `msg_t` semantics, `cpuTimes_t` | full-clean | **high** |
| libphoenix | 22 | 17 paths (libm tree, semaphores, sysconf) | — (all in the merge) | `sem_t`, `semaphore_t`, `_SC_*` values, new syscall stub; **toolchain libstdc++ rebuild** | full-clean + libstdc++ | **high** |
| phoenix-rtos-filesystems | 3 | 2 (ext2) | **nfs adaptation** | FS write/create contract | core (in full-clean) | **high** (NFS root) |
| phoenix-rtos-corelibs | 1 | 0 | — | libstorage fsops `write` signature | core | low |
| phoenix-rtos-posixsrv | 9 | 0 | — | new `/dev/posix/sem*` server | core | medium |
| phoenix-rtos-tests | 3 | 4 | — | — | tests | low |
| phoenix-rtos-build | 16 | 0 (1 silent duplicate) | dead-branch removal | — | — | low |
| phoenix-rtos-devices | 17 | 0 | — | — | core | low |
| phoenix-rtos-ports | 1 | 0 | — | — | — | low |
| phoenix-rtos-utils | 1 | 0 | — | — | — | none |
| phoenix-rtos-project | 8 | 7 gitlinks | — | — | — | none |
| doc, hostutils, lwip, plo, usb | 0 | — | — | — | — | — |

Branch heads (all `upstream-sync-2026-10-06`, each 0 behind `origin/master`):

| repo | master | branch head |
|---|---|---|
| phoenix-rtos-kernel | 28e93f4a | fc17cf20 (merge 38b1d888 + guard) |
| libphoenix | e834bd2 | 7839fc0 |
| phoenix-rtos-filesystems | 22089bb | ce63374 (merge e0c279e + nfs) |
| phoenix-rtos-corelibs | 183e7ae | 79f68b0 |
| phoenix-rtos-posixsrv | c1cd405 | 3989846 |
| phoenix-rtos-tests | f7f3d36 | 0eb71e5 |
| phoenix-rtos-build | 71b723d | a8e8679 (merge a2572ed + fix) |
| phoenix-rtos-devices | 961544f | 440bcec |
| phoenix-rtos-ports | 90c123e | 917cceb |
| phoenix-rtos-utils | 749e2ac | a3628e2 |
| phoenix-rtos-project | bd59b34 | 3cca596 |

The kernel, libphoenix and filesystems branches are **coupled**: kernel without
the libphoenix branch has stale syscall stubs; kernel without the filesystems
branch truncates files on the NFS root (see below). Adopt them together.

## ⚠ The change the conflict census could not see: write offset and O_CREAT

Three upstream kernel commits (`5790b996`, `ee903c32`, `69278ef2`) change the
contract between the kernel and **every** filesystem server, with no textual
conflict anywhere. Upstream adapted its own filesystems (dummyfs, ext2, fat,
jffs2, littlefs, rofs, meterfs) and libstorage. Nothing of ours.

1. **`write()` takes the new offset from the server** (`f->offset = msg->o.io.offs`).
   `proc_write()` zeroes the message, so a server that does not fill the new
   field answers 0, and every sequential `write()` would rewind to offset 0.
   The kernel types everything opened by path as `ftRegular` (devices
   included), so this hits **every driver**, and is destructive for our block
   devices: `dd` to `/dev/mmcblk0` (self-flash) or a USB stick would write every
   block over the first one. Upstream's own block drivers have the same
   problem; it shipped it.
   **Fix (kernel `fc17cf20`):** no write of N bytes can end below offset N, so
   `proc_write()` takes `o.io.offs` only when it is `>= N` and otherwise
   advances by N, as before. Arch-neutral, one hunk, covers every server we
   did not inventory.
2. **O_APPEND moved from `open()` into the filesystem.** `open()` no longer
   seeks to the end; the FS must append on each write. Our **nfs** ignored the
   mode → every `>>` / `fopen("a")` on the NFS root would have written at 0.
3. **`open(O_CREAT)` now creates first** and looks the name up only on
   `-EEXIST`. Our **nfs** created with `nfs_creat()` = `O_CREAT|O_TRUNC`
   UNCHECKED, which succeeds on an existing file **and empties it** — every
   `open(O_CREAT)` of an existing file on the NFS root would have truncated it
   (shell `>>`, logs, sqlite, redis). Worse, the kernel then believes it created
   the file and **unlinks it** if the open fails later.

**nfs fix (filesystems `ce63374`):** report `o.io.offs`; O_APPEND writes at the
server's current size (forced stat); create exclusively (`nfs_open2(O_CREAT|O_EXCL)`
= GUARDED v3 / EXCLUSIVE4 v4) and then set the times (EXCLUSIVE4 parks its
verifier in atime/mtime); answer `-EEXIST` for a FIFO spliced by `mkfifo()`
(it exists only in our node table, the server would otherwise create a regular
file shadowing it). dummyfs and ext2 already answer `-EEXIST` (checked).

Residual: a read-only FAT stick (libfat) answers `-EROFS` to the create, so
`open(existing, O_RDWR|O_CREAT)` there now fails where it used to succeed.
Opening for write on a read-only FS fails anyway; noted, not fixed.

## Per repo

### phoenix-rtos-kernel — 9 incoming, branch `upstream-sync-2026-10-06` @ fc17cf20

Notable: `!proc: track thread/process sys/user time, add sys_cpuTime syscall`
(timer read on every syscall/IRQ/exception boundary, per-cpu spinlock),
`proc: handle clockid in proc_threadNanoSleep`, `PH_CLK_TCK`, the three open/write
commits above, `sys_statvfs` errno, riscv64/mps3/ia32 fixes.

Conflicts (6 files) and resolution:
- `include/syscalls.h` — **upstream appended `sys_cpuTime` after `schedSet`.** Kept
  upstream's order exactly; our eight (`sys_fdpath, memExport, memUnexport,
  pollNotify, sigreturnContext, sys_sigaltstack, futexWait, futexWake`) follow it,
  **each renumbered +1**.
- `hal/aarch64/exceptions.c` — kept our watchpoint handler; upstream's
  `proc_cpuTimeIntrEnter/Leave` around `exceptions_dispatch` came in clean.
- `hal/aarch64/interrupts_gicv2.c` — kept our unclaimed-IRQ masking and
  `hal_timerIrq()`; added `proc_cpuTimeIntrEnter/Leave`.
- `proc/threads.h` — both sides added `cpuId`; kept one (upstream's comment) plus
  upstream's `sysTime/userTime/inKernel` beside our `killable/lentKstack/magic/
  sigaltstack/ustacksz`.
- `proc/threads.c` — upstream's order (`selected->cpuId` before `current[]`), which
  its per-cpu spinlock relies on; upstream's `sysTime/userTime/inKernel` init.
- `posix/posix.c` — kept our `posix_sweepFds()` exit path, added upstream's
  user/sys time bookkeeping; took upstream's create-first `O_CREAT` path.

ABI: syscall numbers (8 renumbered); `threadinfo_t` gains `sysTime` mid-struct
(shifts `priority/state/vmem/wait/cpuId`; consumers psh `ps`/`top`/`pm`,
WindowMaker); `cpuTimes_t` new; `msg_t` `o.io.offs` added inside the union (size
unchanged, semantics changed — above). nsleep arity unchanged but `clockid` is
now honoured (old binaries passed the libc value).

Invariants checked by reading the merged code (a compile cannot see them):
- `proc_threadNanoSleep` now returns `-EINVAL` for any clock other than
  `PH_CLOCK_REALTIME/MONOTONIC`. Every libphoenix caller of `nsleep()`
  (`time.c`, `unistd/sys.c` usleep/sleep, `sys/select.c`) passes `PH_CLOCK_*`; no
  other repo calls it. **Old binaries passed libc `CLOCK_MONOTONIC` = 0, which
  this kernel rejects: every `sleep`/`usleep`/`nanosleep` in a stale binary fails.**
  One more reason the stale census must read 0.
- `_threads_cpuSpinlockSet()` (every syscall entry) spins until
  `current[t->cpuId] == t`. The only non-NULL store to `threads_common.current[]`
  is the scheduler's, paired with `selected->cpuId = cpuId` (same as upstream);
  our SMP code adds only NULL stores. No unpaired path found.

Accounting caveat: our extra paths that return to EL0 without going through
`syscalls_dispatch` / `threads_setupUserReturn` (e.g. `sigreturnContext`) leave
the thread marked in-kernel until its next interrupt; that skews sys/user split
slightly, not totals.

Syntax: all 51 built aarch64 kernel files compile clean from the worktree
(`wt-syntax-check.sh`; the two `*-nommu.c` failures are pre-existing and not built
for aarch64).

Risk **high**: scheduler/IRQ paths touched on every boundary (cost: one CNTVCT
read per syscall/IRQ; spinlock per boundary).

### libphoenix — 22 incoming, @ 7839fc0

Notable: libm restructured (one file per function, libmcs as a submodule option,
aarch64 HW ceil/floor/fabs/round/sqrt/trunc in `libm/arch/aarch64`, headers in
`libm/include`, generated `libm_feature_config.h`); POSIX semaphores (named ones
served by posixsrv), `semaphoreCount/TryDown/DownAtClock`, `SEM_VALUE_MAX`,
`e5eb65b semaphores: fix lost wakeup` (the same bug we fixed in `e75c4fe`);
CPU-time clocks (`clock_getcpuclockid`, `pthread_getcpuclockid`,
`CLOCK_PROCESS_CPUTIME_ID`), `clock()`/`times()` from `sys_cpuTime`; full `_SC_*`
table; `_POSIX_*` options at 202405L; `NGROUPS_MAX/RE_DUP_MAX/TZNAME_MAX`;
C/C++ `static_assert`; `pthread_key_cleanup` off-by-one.

Conflicts and resolution:
- **libm** (12 paths incl. rename/rename, modify/delete, file/directory):
  took upstream's layout and the `libm/libmcs` gitlink; dropped our vendored
  libmcs tree. Our `exp.c/power.c/trig.c/hyper.c` were upstream's old monolithic
  files plus additions only (verified function by function: no body of an
  upstream function was changed). Upstream's split set covers the base functions
  and the float wrappers (with `sinhf` calling `sin` fixed); our additions moved
  unchanged to `libm/phoenix/expextra.c` (scalbn/scalbln, exp2, log2f, rint,
  nearbyint, lrint/llrint, lround/llround, fdim, fmax, fmin, copysign + float)
  and `powerextra.c` (hypot, cbrt + float). `c99extra.c`, `erf.c`,
  `gammaextra.c`, `longdouble.c` kept. Re-applied to `libm/include/math.h`:
  INFINITY/HUGE_VAL*/NAN as compiler builtins (upstream reverted to `nanf("")`,
  which is not a constant expression — MicroPython's static initializers) and the
  gammaextra declarations. Compat helpers stay weak. Our libm delta vs upstream is
  now purely additive (8 files). `install-headers` skips libmcs' `#error`
  `fenv.h` (ours is `include/fenv.h`) and the `.in` template.
  Behaviour change: `math_errhandling` is now `MATH_ERRNO` (was `MATH_ERREXCEPT`).
- **Semaphores** (`include/semaphore.h` add/add, `posix/Makefile`): both sides
  implemented `sem_*`. Upstream's unnamed `sem_post()` goes through
  `semaphoreUp()`, which takes a mutex — not async-signal-safe, and WebKit posts
  from a signal handler (thread suspension). Unified: one `sem_t` with upstream's
  `type` tag; named semaphores are upstream's (`posix/sem.c`); unnamed ones stay
  ours (user-space count + pipe, lock-free post, EINTR-able wait), moved to
  `pthread/sem_unnamed.c` behind `posix/sem.c` (renamed so the archive does not
  get two `sem.o` members). `sem_clockwait()` kept for both kinds (named
  MONOTONIC deadlines converted to REALTIME for posixsrv).
- `sys/semaphore.c` — took upstream's (waiter count + signal when waiters > 0
  subsumes our signal-every-up fix; adds the new calls).
- `include/unistd.h` / `unistd/conf.c` — upstream now numbers `_SC_*` 0..111,
  colliding with our private 100..104. Took upstream's numbering
  (**`_SC_LINE_MAX`, `_SC_NPROCESSORS_CONF/ONLN` change value**); our non-POSIX
  `_SC_PHYS_PAGES/_SC_AVPHYS_PAGES` moved to 200/201. Kept our extra `_POSIX_*`
  claims (MONOTONIC_CLOCK, THREADS, THREAD_ATTR_*, *PRIORITY_SCHEDULING, TIMERS),
  now all at 202405L like upstream's; `sysconf()` returns them instead of
  upstream's "unsupported" -1. Kept `_SC_NPROCESSORS_*` via
  `platformctl(pctl_cpucount)` and `_SC_OPEN_MAX` = 1024.
- `sys/times.c` — upstream's real user/system split; still accepts `NULL`.
- `time/time.c` — git's merge of this file was unusable (our TZ/strptime rewrite);
  rebuilt from ours plus upstream's `clock_getres`, `clock_getcpuclockid`,
  `clock_gettime` (CPU clocks), `clock()`, `nanosleep`/`clock_nanosleep`
  (PH_CLOCK ids, REALTIME allowed).
- `pthread/pthread.c`, `sys/Makefile` — union of both.

ABI: `sem_t` (12 → 16 bytes, new layout), `semaphore_t` (+`waiters`), `_SC_*`
values, `sys_cpuTime` stub.

⚠ **`_SC_NPROCESSORS_ONLN` is baked into the toolchain's `libstdc++.a`.**
`std::thread::hardware_concurrency()` there calls `sysconf(102)` (checked with
objdump: `mov w0, #0x66`). After this merge 102 is upstream's `_SC_2_UPE`, which
returns -1, so **`hardware_concurrency()` returns 0 in every C++ port** until
libstdc++ is rebuilt against the merged headers. `--scope full-clean` does not
rebuild it and `sync-toolchain-from-sysroot.sh` refreshes only headers and
`libphoenix.a`. Remedy: rebuild the toolchain's libstdc++ (the Docker
`--no-cache` release build does this). The scan of every other toolchain archive
found no other baked key that changed (`libstdc++exp.a` uses 4 and 1, unchanged).
Not worked around in code: keeping 101/102 would collide with upstream's
`_SC_2_SW_DEV`/`_SC_2_UPE`, a permanent divergence. Note `.gitmodules` now exists (libmcs submodule,
URL phoenix-rtos/libmcs); our build does not need it (`LIBM_USE_LIBMCS=n`).

Syntax: every libphoenix file in the build compiles clean from the worktree
against the merged kernel headers (199/204; the 4 failures are pre-existing
non-standalone files — identical result on master).

Risk **high**: new libm sources (the aarch64 HW ceil/floor/round/trunc/sqrt/fabs
files build: `TARGET_SUFF` is `aarch64`), semaphore plumbing, sysconf renumber.

### phoenix-rtos-filesystems — 3 incoming, @ ce63374

Notable: `!change write API to accept extra mode argument` and `to report the
new file offset` (all upstream FSes), stm32u3 target.
Conflicts: `ext2/ext2.c` (our `ext2_readdir` with next-position vs upstream's new
`ext2_write` signature — both kept), `ext2/libext2.c` (our `fs->lock` around the
write kept, new signature). Plus the **nfs** commit above.
Verified on the host: `tools/libext2-hosttest/run-all.sh` against the worktree
ext2 (copy with a 5→6-argument `ext2_write` shim; the harness itself is unchanged):
**ALL GREEN** — both block sizes, 10 seeds, concurrency 2/4 threads, busy
predicate 14/14, no-unmount 6/6, e2fsck clean everywhere. The harness calls
`ext2_write` with the old signature, so it needs that one-line adaptation when
this merge lands (coordination repo, `tools/libext2-hosttest/*.c`).

### phoenix-rtos-corelibs — 1 incoming, @ 79f68b0
`!libstorage: pass mode and o.io.offs to FS write`. Clean; our libcache/libdbg/
readdirNext intact. Compile-checked with the new headers.

### phoenix-rtos-posixsrv — 9 incoming, @ 3989846
Shared semaphore server (`semaphore.c`, `/dev/posix/sem/`, `/dev/posix/semctl`),
deferred-request timeout ownership protocol, `rq_timeoutAt()`, pty noncanonical
timed read fix, poll reply on unpack failure, pipe unlink fix. Clean merge; our
hwrng-backed `/dev/urandom` and lazy tmpfile dir intact. All changed files
compile clean. Risk medium: the timeout protocol touches every deferred request
(pty, pipes, poll).

### phoenix-rtos-tests — 3 incoming, @ 0eb71e5
Upstream: POSIX semaphore suite (`sem_unnamed`, `sem_named`), `proc/test_cputime`,
lsb_vsx #1429. Both sides created `libc/semaphore/main.c`: one binary,
`test-libc-semaphore`, now runs all four groups (`test_semaphore`,
`posix_semaphore`, `sem_unnamed`, `sem_named`); yaml/Makefiles unioned
(`proc` builds both `test_cputime` and our `test-msg-abandon`). All compile.

### phoenix-rtos-build — 16 incoming, @ a8e8679
image_builder/nvm_config/strip fixes + tests (not used by our image path),
stm32u3 script, `LIBM_USE_HW=n` for soft-float targets (aarch64 keeps HW),
`port_manager: add aarch64a53 host subst`. **Silent duplicate**: the merge was
clean but left a second `aarch64*` branch in `reset_env()` after our own
(`2a6aebb`) — unreachable; removed (`a8e8679`). Ours keeps
`HOST_TARGET=$TARGET_FAMILY` (no port reads it) and `HOST=aarch64-phoenix`.

### phoenix-rtos-devices — 17 incoming, @ 440bcec
STM32U3 support, multi drivers adapted to the dummyfs write API, lps22xx, sht2x,
usbacm POLLIN fix. Nothing in the Pi build changes (usbacm is not built for
rpi4b). Clean.

### phoenix-rtos-ports — 1 incoming, @ 917cceb
`libm: Add compatibility for new libm`: lsb_vsx header copies, MicroPython drops
`__signbit` from its absent list (it now uses libphoenix's weak `__signbitd`).
Clean. MicroPython should be rebuilt and smoke-run.

### phoenix-rtos-utils — 1 incoming, @ a3628e2
stm32u3 target only. Clean. (psh `ps`/`top` must be rebuilt for `threadinfo_t`.)

### phoenix-rtos-project — 8 incoming, @ 3cca596
Submodule bumps + `lint-submodule-refs` CI. Per convention every gitlink kept at
our value (we build from `sources/`). Note: the new CI job runs on pushes to our
fork's master and checks moved gitlinks are on the submodule's base branch.

### No incoming
doc, hostutils, lwip, plo, usb — 0 behind. (`_build` is not a repo.)

## How the syntax checks were run

`syntax-check.sh` with `SYNTAX_CHECK_SRC` stages only same-directory headers, so a
merge whose headers change in other directories (`proc/threads.h`,
`include/syscalls.h`, libphoenix ↔ kernel) compiles against stale ones. I used
`scripts/wt-syntax-check.sh` (same real compile command, worktree directories
first, writes nothing) and, for cross-repo headers, a scratch copy that prepends
the kernel worktree's `include/` (as `phoenix/`), libphoenix's `include/` and
`libm/include`, and corelibs' `libstorage/include`. Confirmed with `-H` which
`msg.h` / `storage/fs.h` was picked. New files with no make rule yet were
compiled with a sibling's command line. Nothing was staged into `.buildroot`.

## Adoption order and gate

1. Fast-forward nothing yet. Build from the branches: point `sources/` at them
   (or merge them into master locally) for **all eleven repos at once**.
2. **`./scripts/rebuild-rpi4b-fast.sh --scope full-clean`**, then
   `strings loader.disk | grep` for a new symbol (e.g. `sys_cpuTime`) to prove the
   image is not stale. `rm` of `libphoenix/arch/aarch64/syscalls.o` is implied by
   full-clean (incremental builds keep stale syscall stubs).
3. Resync the toolchain bundle (`scripts/sync-toolchain-from-sysroot.sh`) **before**
   rebuilding the standalone tools (`rpi4-wifi`, `rpi4-hci`, probes) — they link the
   bundle's `libphoenix.a` and headers and would keep old syscall numbers and old
   `_SC_*` values.
4. **Rebuild the toolchain's libstdc++** against the merged libphoenix headers
   (`hardware_concurrency()` otherwise returns 0 — see libphoenix above), then
   relink the C++ ports (full-clean order: toolchain first, then the build).
5. Adapt `tools/libext2-hosttest/*.c` to the 6-argument `ext2_write` and re-run
   `run-all.sh` on the landed tree.

Pi gate, in this order:

1. **Stale census first:** `scripts/check-stale-binaries.sh` → 0 stale (a green
   app gate over stale binaries is luck, not health).
2. Boot to `(psh)%`, console interactive (pl011-tty unchanged, but posixsrv's
   deferred-request rework is on the pty path: `bash`, `echo`, `exit`).
3. libc suites: `test-libc-pthread`, **`test-libc-semaphore`** (all four groups;
   `sem_named` exercises the new posixsrv server), **`test-libc-math`** (new libm
   sources and HW rounding), **`test-libc-time`** (CPU clocks, nanosleep clock ids),
   **`test-libc-posixsrv`** (deferred-request rework), `test-libc-statvfs` (errno
   change), `test-libc-signal`, upstream's **`test_cputime`**, the rest of the
   libc set.
4. **Write-offset / create probe on each filesystem** — NFS root, ext2 (SD root
   or `/dev/umass*`), dummyfs `/tmp`:
   `echo a > f; echo b >> f; cat f` (expect `a`,`b`); open an existing file with
   `O_RDWR|O_CREAT` and check its size and mtime survive; `cp` a multi-MB file and
   `cmp`; `mkfifo p` then `echo x > p` with a reader (must hit the pipe);
   `ls -l` dates of new NFS files sane (EXCLUSIVE4 verifier).
5. **Block-device writes**, via the `rpi4-storage-test` skill on a scratch USB
   stick: sequential `dd` + `cmp` (the guard in `proc_write`). Only then anything
   that writes `/dev/mmcblk0`.
6. `ps`, `top` (threadinfo layout, new sysTime), WindowMaker window titles in the
   X desktop. CPU count from **both** languages: C `sysconf(_SC_NPROCESSORS_ONLN)`
   = 4 **and** a three-line C++ binary printing
   `std::thread::hardware_concurrency()` = 4 — the C check alone passes while every
   C++ port sees 0.
7. WebKit browser smoke (signal-handler `sem_post` path), MicroPython smoke.
8. **Showcase 7/7**, 0 faults, HDMI looked at.
9. SSH login + lighttpd (both write logs with O_APPEND).

Then fast-forward each master to its branch, push, snapshot a manifest.

# Upstream sync 2026-10-07 (redo of 10-06 on top of builds 39–48)

**Status: MERGED ON BRANCHES, NOT BUILT, NOT GATED.** Every repo with incoming
commits has a branch `upstream-sync-2026-10-07` in a worktree
`/home/houp/.claude/jobs/c8f1289c/tmp/wt-us-<repo>`. No `master` in `sources/` was
touched, nothing was pushed, no image was built, no Pi cycle was run.

It replaces the 2026-10-06 branches (`docs/misc/2026-10-06-upstream-sync.md`, read
that first: its write-offset/O_CREAT analysis, ABI list and gate still apply and
are not repeated in full here). Upstream has **not moved** since 10-06 (same
`origin/master` in all 16 siblings, `git fetch` 2026-10-07); our masters moved by
builds 39–48. So the incoming set is identical and the 10-06 resolutions were
reused wherever tonight's work did not touch the same code; where it did, the
resolution was redone (libm, pthread, posixsrv's timed requests, nfs writes).

**Build scope for the set: `--scope full-clean`**, a toolchain-bundle resync,
**and a rebuild of the toolchain's libstdc++** — same reasons as 10-06 (syscall
renumber, `threadinfo_t`/`sem_t`/`semaphore_t`, `_SC_*` renumber baked into
`libstdc++.a`).

## Summary

| repo | incoming | conflicts | resolution | extra commits on the branch | ABI / syscall | risk |
|---|---|---|---|---|---|---|
| phoenix-rtos-kernel | 9 | 6 files | 10-06 resolution reused; build 39–48 code (perf/trace, pollwake, objcache, msg hooks) untouched by upstream | write-offset guard (cherry-pick of 10-06's), **vm: failed create names no file** (new) | `sys_cpuTime` appended, **our 8 renumbered +1**; `threadinfo_t`; `msg_t o.io.offs` semantics; `cpuTimes_t` | **high** |
| libphoenix | 22 | 23 paths | 10-06 reused outside `libm/`+`pthread/`; **libm: ours kept, upstream's split NOT taken**; pthread hand-merged | **libm: keep long double decls on aarch64** (new) | `sem_t`, `semaphore_t`, `_SC_*` values, `sys_cpuTime` stub, `math_errhandling` | **high** |
| phoenix-rtos-filesystems | 3 | 2 (ext2) | 10-06 reused | nfs contract fix, **re-done on the chunked writes** | FS write/create contract | **high** (NFS root) |
| phoenix-rtos-posixsrv | 9 | 5 files | **both sides rewrote timed-request ownership**: ours kept as implementation, upstream's API + semaphore server on top | — (all in the merge) | new `/dev/posix/sem*` server; `rq_timeout()` unit µs | **medium-high** |
| phoenix-rtos-tests | 3 | 4 | 10-06 reused | — | — | low |
| phoenix-rtos-corelibs | 1 | 0 | = 10-06 branch (master unchanged) | — | libstorage fsops `write` signature | low |
| phoenix-rtos-build | 16 | 0 (1 silent dup) | = 10-06 branch | dead-branch removal | — | low |
| phoenix-rtos-devices | 17 | 0 | = 10-06 branch | — | — | low |
| phoenix-rtos-ports | 1 | 0 | fresh merge | — | — | low |
| phoenix-rtos-utils | 1 | 0 | fresh merge | — | — | none |
| phoenix-rtos-project | 8 | 7 gitlinks | = 10-06 branch (gitlinks kept at ours) | — | — | none |
| doc, hostutils, lwip, plo, usb | 0 | — | — | — | — | — |

### Branch heads

All `upstream-sync-2026-10-07`, each **0 behind `origin/master` and 0 behind its
`master`** at the time of writing (kernel, tests and ports masters moved during
the session — build 48 — and the branches were rebuilt on the new masters).

| repo | master (base) | origin/master | branch head | commits on the branch (first-parent) |
|---|---|---|---|---|
| phoenix-rtos-kernel | 9e21ba33 | b094f63a | **f95f545a** | a25eebec merge · 5ac90b0e write-offset guard · f95f545a vm create |
| libphoenix | 6bf27ab | a5c5606 | **1a4a799** | 444fcb7 merge · 1a4a799 long double decls |
| phoenix-rtos-filesystems | 94ea9be | 5053fbf | **1a54151** | 2126b70 merge · 1a54151 nfs contracts |
| phoenix-rtos-corelibs | 183e7ae | 5fcc3a9 | **79f68b0** | (10-06 merge) |
| phoenix-rtos-posixsrv | 85ce3dd | c923754 | **35f3cec** | 35f3cec merge |
| phoenix-rtos-tests | bc2ee7b | 124e546 | **de899f4** | de899f4 merge |
| phoenix-rtos-build | 71b723d | c4985cc | **a8e8679** | (10-06 merge + fix) |
| phoenix-rtos-devices | 961544f | ed99dc9 | **440bcec** | (10-06 merge) |
| phoenix-rtos-ports | b8e11fa | d31880a | **943368e** | 943368e merge |
| phoenix-rtos-utils | 64ff3b6 | f226e3e | **4bfadfb** | 4bfadfb merge |
| phoenix-rtos-project | bd59b34 | 3a0d6c7 | **3cca596** | (10-06 merge) |

The kernel, libphoenix and filesystems branches are **coupled** (stale syscall
stubs without libphoenix; NFS truncation/rewind without filesystems), and
posixsrv's semaphore server needs libphoenix's named semaphores. Adopt all eleven
together.

## What is different from the 10-06 branches

1. **libm: upstream's one-file-per-function split is not taken.** Tonight's
   `6bb0bdc`/`6bf27ab` route every transcendental function in `libm/phoenix`
   through FreeBSD msun. Upstream's `libm/phoenix/mathd/*.c`, `mathf/*.c` carry
   the *old* series code (`powd.c` is still `exp(y * log(x))` — KNOWN-ISSUES C15,
   wrong JetStream SHA-256). 10-06 replaced our `exp.c/power.c/trig.c/hyper.c`
   with that split; doing so now would undo the msun work. So `libm/phoenix` is
   ours verbatim (+ upstream's cosmetic `compatibility.c`/`complex.c`), `mathd/`
   and `mathf/` are not added. Taken from upstream: `libm/include/*` (with
   10-06's re-applied INFINITY/HUGE_VAL/NAN builtins and gammaextra decls),
   the `libm/libmcs` gitlink, the generated `libm_feature_config.h`, `libm/arch/`
   (built only for `LIBM_USE_LIBMCS=y`) and the new libm Makefile structure. In
   the phoenix-impl branch `HW_OBJS` is emptied with a comment: our
   `ceil/floor/round/trunc/fabs/sqrt` already use the instructions through
   `<arch.h>`, and `libm/arch/aarch64/*.c` would be a second definition (the
   upstream dedup matches by basename, `exp.o` ≠ `ceild.o`).
2. **`include/arch/*/arch.h` keep the `__ieee754_*` fast-path macros** upstream
   deleted when it moved them to `libm/arch/`. Our libm uses them; without them it
   silently falls to the software paths.
3. **New finding, also present on the 10-06 branch — every C++ port would stop
   compiling.** Upstream's `libm/include/numeric_size_config.h` now takes the
   "64-bit long double" branch only when `long double` *is* 64-bit. On aarch64
   (128-bit) neither branch is taken, so `<math.h>`/`<complex.h>` stop declaring
   all long double functions. The toolchain's libstdc++ was configured with
   `_GLIBCXX_USE_C99_MATH_FUNCS`; its `<cmath>` does `using ::acoshl;` … →
   `error: 'acoshl' has not been declared in '::'` in every C++ TU that includes
   `<cmath>` (reproduced with the real toolchain and the merged headers), and
   `libc/math/c99extra.c` (`floorl/ceill/llroundl`) fails too. Fixed on the branch
   (`1a4a799`): the previous condition (anything but x87 80-bit → 64-bit branch),
   which is what `master` ships. `<bits/stdc++.h>` + `<semaphore.h>`,
   `<unistd.h>`, `<pthread.h>`, `<complex.h>` now compile at gnu++17/20/23.
4. **posixsrv: both sides rewrote the ownership of timed requests** (ours
   c8d57f3…73d7d34, builds 41–47; upstream 4178dd3, da52356, 40a307b). They are
   the same state machine (`rq_timeoutCancel()` under the object's lock,
   armed/expired state, the timeout thread completing what it claimed). Ours is
   kept as the implementation — HW-tested, pthread fast locks, CLOCK_MONOTONIC
   condition, earliest-first expiry, an armed request holds a reference to its
   object (stronger than upstream's pin-at-fire) — and upstream's interface is
   put on top: `rq_timeout(r, time_t usecs)` (callers in event.c/pty.c convert,
   as upstream's do) and `rq_timeoutAt()` for the semaphore server. Upstream's
   `semaphore.c` unlinks a waiter *before* `rq_timeoutCancel()` and its timeout
   op tells the cases apart by `r->next`; that works unchanged on ours and is
   documented beside the protocol. Taken as is: semaphore server, `pipe_unlink()`
   destroying only when both ends are closed, the pending-writer copy fix, pty
   answering `prevlen` (b089005 = same bug as our 7ed8949; both kept, they agree),
   `(void)` params, extern-less decls. Not taken: 774f200 (our event.c already
   answers a request whose message fails to unpack, with 0 events).
5. **nfs contract fix re-done on the chunked `nfs_ops_write`** (5f1f5e1, build
   41): each chunk goes to `*offs + done`, `*offs` advances by what the server
   acknowledged, also on a short write after a reclaim.
6. **vm file-object cache (build 43/48) × create-first O_CREAT.** Upstream's
   `open(O_CREAT)` now sends `mtCreate` first and looks the name up on
   `-EEXIST`, so failed creates become routine. `vm_objectNotify()` treated any
   *answered* create's `o.create.oid` as a new file id; a failed one carries the
   request's zeroed oid. `f95f545a` acts on a create only when `o.err == EOK`.
7. pthread: tonight's TSD/P17/lifetime rewrite kept; upstream's field renames,
   `pthread_getcpuclockid()`, `__getSystickInterval()`, `pidExists()` and the
   `PTHREAD_DESTRUCTOR_ITERATIONS` off-by-one (`<=` → `<`, at most 4 rounds) on
   top. `1a7753d` (a waiter that loses the token read keeps its deadline) follows
   `pthread/sem.c` → `pthread/sem_unnamed.c` (field names `unnamed.fd/value`).

## ⚠ Behaviour changes to watch at the gate

- **Write offset / O_CREAT / O_APPEND contract** — unchanged from 10-06 (see its
  section; kernel guard + nfs fix are on the branches).
- **`<complex.h>` becomes usable by applications.** Master installs libmcs'
  `complex.h` without the feature config, so it `#error`s for every app (the
  toolchain's `c++config.h` has `_GLIBCXX_HAVE_COMPLEX_H` and
  `_GLIBCXX*_USE_C99_COMPLEX` undefined). The merged build installs a generated
  `libm_feature_config.h` with `LIBMCS_WANT_COMPLEX`, so `complex.h` now
  declares the full C99 complex API while `libm/phoenix/complex.c` implements only
  `cabs/carg/cexp` (+f). A port whose configure probes `complex.h` can flip
  (the "new libc header flips a port's configure" trap), and **the libstdc++
  rebuild may switch on `_GLIBCXX_USE_C99_COMPLEX`**, after which `std::sqrt`/
  `std::pow` on `std::complex` call `csqrt`/`cpow`… that do not exist → link
  errors. Gate step below diffs `c++config.h`.
- `math_errhandling` is `MATH_ERRNO` (was `MATH_ERREXCEPT`) — what the msun
  wrappers actually do.
- `sleep/usleep/nanosleep` in a **stale** binary fail (`-EINVAL`, libc clock id
  0); syscalls 110+ in a stale binary hit the wrong handler. The stale census must
  read 0.
- `open(existing, O_RDWR|O_CREAT)` on a read-only FAT stick now fails (10-06
  residual, not fixed).

## Invariants re-checked on the merged code (a compile cannot see them)

- Every `nsleep()` caller passes a `PH_CLOCK_*` id (the merged kernel rejects
  others): `time/time.c`, `unistd/sys.c`, `sys/select.c`. Tonight's pthread.c
  rewrite does not call `nsleep()`; its cond/mutex/rwlock timeouts pass
  `PH_CLOCK_RELATIVE/REALTIME/MONOTONIC` to the kernel as before.
- The restored `<arch.h>` fast paths are live in the real compile: the
  preprocessed `libm/phoenix/power.c` contains `fsqrt`, `exp.c` contains `frintp`
  (the libm-hosttest uses its own `inc/arch.h`, so it does not prove this).
- `<complex.h>`'s long double blocks are declarations and `CMPLXL` only; no
  long double function is mapped onto a double one.
- `libm/libmcs` gitlink: nothing in the build path fetches it.
  `prepare-buildroot.sh` copies siblings, `bootstrap-linux-host.sh` initialises
  only `lib-lwip`, `build.sh` only runs `git submodule status`. A clean clone
  leaves it empty, which is fine (`LIBM_USE_LIBMCS=n`). Only a `--recursive`
  clone of our libphoenix would reach `github.com/phoenix-rtos/libmcs` (branch
  `phoenix`) — mirror it to the fork before anything does that.

## Per repo

### phoenix-rtos-kernel — 9 incoming, @ f95f545a

Merge `a25eebec` has exactly 10-06's resolution (syscalls.h order, cpuTime hooks
in `exceptions.c`/`interrupts_gicv2.c`, `threads.h/.c` union with upstream's
scheduler order, posix.c create-first + time bookkeeping) applied to the new
master. Only real overlap with builds 39–48: `threads_timeintr()` — upstream
voided the now-unused `context`, ours passes it to `trace_eventThreadSample()`;
ours kept. Upstream does not touch `vm/`, `perf/`, `proc/msg.c`, `posix/pollwake.c`,
`posix/uchannel.c`/`usocket.c`. `prof` (utils) and `scripts/prof-report.py` read
`syscalls.h` at build/run time, so the renumber reaches them; the
`tools/gpu-lane/pollwake` shim object bakes syscall numbers and must be rebuilt.

`5ac90b0e` = 10-06's `fc17cf20` (take `o.io.offs` only if ≥ bytes written, else
advance). `f95f545a` = the vm create fix above.

Syntax: **62/62** aarch64 kernel C files compile clean from the worktree.

### libphoenix — 22 incoming, @ 1a4a799

Merge `444fcb7`; resolution in the commit message and above. ABI as 10-06
(`sem_t` 12 → 16 bytes, `semaphore_t` + `waiters`, `_SC_LINE_MAX` 100→19,
`_SC_NPROCESSORS_CONF/ONLN` 101/102→26/27, `_SC_PHYS_PAGES/_SC_AVPHYS_PAGES`
→200/201, `sys_cpuTime` stub). `.gitmodules` + `libm/libmcs` gitlink exist now;
the build does not need the submodule (`LIBM_USE_LIBMCS=n`).

Checks:
- **178/182** aarch64 sources compile clean against the merged kernel +
  libphoenix headers (overlay include dir); the 4 failures
  (`regex/engine.c`, `regex/wordexp.c`, `regex/wordfree.c`,
  `stdlib/malloc_trivial3.c`) fail identically on master (non-standalone).
- **`tools/libm-hosttest`: output identical to master's** (every ULP table,
  0 Annex F mismatches, 72/72 SHA-256 constants, `RESULT: 0 phx failure(s)`) and
  the same 162 exported symbols.
- `run-libc-hosttests.sh` set against the worktree: string/wchar/num/fmt/scanf
  clean; libtime clean (12.9 M cases, 0 diffs) once its shim knows
  `<sys/proc.h>`/`<sys/sched.h>`/`PH_CLOCK_*`/4-arg `nsleep` (on the coord branch).
- C++: `<bits/stdc++.h>` etc. compile with the merged headers (after `1a4a799`).

### phoenix-rtos-filesystems — 3 incoming, @ 1a54151
Merge `2126b70` = 10-06's (ext2 readdir/next-position + 6-arg `ext2_write`,
libext2 `fs->lock`). `1a54151` = 10-06's `ce63374` on the chunked write. 53/53
built files compile clean. `tools/libext2-hosttest` still needs the 5→6-arg
`ext2_write` adaptation when this lands (as 10-06).

### phoenix-rtos-posixsrv — 9 incoming, @ 35f3cec
See above. All 8 files compile (incl. the new `semaphore.c`, against the merged
`msg.h`/`semaphore.h`). `tools/posixsrv-hosttest` (real posixsrv.c/event.c/pty.c +
libtty): **7/7 cases under ASan+UBSan and under TSan**, also against master (its
shim gets a `semaphore_init()` stub, on the coord branch). semaphore.c itself
has no host coverage: `test-libc-semaphore` (`sem_named`) is its first test.

### phoenix-rtos-tests — 3 incoming, @ de899f4
= 10-06 (one `test-libc-semaphore` with four groups; `proc` builds
`test_cputime` and `test-msg-abandon`). Every built test file compiles against the
merged headers (222 + the new semaphore/cputime files); `libc/math/c99extra.c`
needed `1a4a799`.

### corelibs, build, devices, project — = 10-06 branches
Masters did not move since 10-06; the branches are fast-forwarded to the 10-06
heads. corelibs 35/35, devices 37/37, lwip 110/110, usb 12/12, utils 56/56 files
compile against the merged headers (lwip/usb from `sources/`, no incoming).

### phoenix-rtos-ports — 1 incoming, @ 943368e; phoenix-rtos-utils — 1, @ 4bfadfb
Clean merges (lsb_vsx/MicroPython `__signbit`; stm32u3 target).

## Coordination-repo changes on this branch

- `tools/libm-hosttest/Makefile`: works on both layouts (detects
  `libm/include/math.h`, generates `libm_feature_config.h`); libmcs sources from
  `MCSROOT` (default the submodule — `git -C sources/libphoenix submodule update
  --init libm/libmcs` after adoption, or `MCSROOT=<old tree>`). Verified on master
  and on the branch (identical output).
- `tools/libtime-hosttest`: `shim/sys/proc.h`, `shim/sys/sched.h`, `PH_CLOCK_*`,
  4-arg `nsleep`, `sys_cpuTime`/`pidExists` stubs. Works on master and branch.
- `tools/posixsrv-hosttest/shim.c`: `semaphore_init()` stub. Works on both.

## How the compile checks were run

`syntax-check.sh`/`wt-syntax-check.sh` compile against the *installed* sysroot
headers, i.e. master's libphoenix and kernel headers, so they cannot see a header
change in another repo. I used the real per-file compile command from
`make -n` in `.buildroot/<repo>` with the worktree dirs and an overlay include dir
first: libphoenix-worktree `include/` + `libm/include/` + a generated
`libm_feature_config.h`, kernel-worktree `include/` merged into `phoenix/`,
corelibs `libstorage/include`. Kernel files were compiled without the overlay.
New files without a rule borrowed a sibling's command line. Nothing was written to
`.buildroot`. For C++ the libstdc++ dirs were passed with `-nostdinc++ -isystem`
so `<cmath>`'s `#include_next <math.h>` reaches the overlay.

## If a master moves again before adoption

Branches are merges on top of master; rebuild them rather than stacking merges:
`tree=$(git merge-tree --write-tree <new master> <old branch merge>)`, then
`git reset --hard master; git merge --no-commit origin/master; git read-tree -u
--reset $tree; git commit -C <old merge>` and cherry-pick the extra commits. Check
`git merge-tree` printed no conflicts first.

## Adoption order and gate

0. `scripts/snapshot-integration-state.sh` (the rollback manifest).
1. For **all eleven repos at once**, in `sources/<repo>` on `master`:
   `git merge --ff-only upstream-sync-2026-10-07` (each branch contains its
   master; if one refuses, its master moved — rebuild the branch, above).
2. `scripts/heavy-build.sh -- ./scripts/rebuild-rpi4b-fast.sh --scope full-clean`;
   prove the image is new (calibrated 2026-10-07 on build 47: syscall names are
   NOT strings in `loader.disk`, `strings | grep schedSet` reads 0 on a good image):
   - kernel: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm .buildroot/_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf | grep -E ' proc_cpuTime$'`
     → 1 line (today `proc_schedSet` is found the same way);
   - posixsrv in the image: `strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep -c 'semaphore init'`
     → ≥ 1 (today `tmpfile init` reads 1);
   - libphoenix: `aarch64-phoenix-nm` of the sysroot `libphoenix.a` shows
     `sys_cpuTime` and `pthread_getcpuclockid`.
3. `scripts/sync-toolchain-from-sysroot.sh` **before** rebuilding anything that
   links the toolchain bundle (rpi4-wifi, rpi4-hci, probes, the gpu-lane
   pollnotify shim objects).
4. **Rebuild the toolchain's libstdc++** against the merged headers (Docker
   `--no-cache` release build, or the toolchain build script), then
   **diff its `bits/c++config.h` against the current one**
   (`.toolchain/aarch64-phoenix/aarch64-phoenix/include/c++/aarch64-phoenix/bits/c++config.h`):
   `_GLIBCXX_HAVE_COMPLEX_H`, `_GLIBCXX11_USE_C99_COMPLEX`,
   `_GLIBCXX98_USE_C99_COMPLEX`, `_GLIBCXX_USE_C99_MATH_FUNCS`,
   `_GLIBCXX_HAVE_*L` math macros. A flipped complex macro means std::complex
   math will need `csqrt/cpow/...` that libphoenix lacks — pin it off or implement
   them before relinking C++ ports. Then relink the C++ ports (full-clean order).
5. Adapt `tools/libext2-hosttest/*.c` to the 6-arg `ext2_write`, run
   `run-all.sh`; run `scripts/run-libc-hosttests.sh` and `tools/libm-hosttest`
   (`make run`) and `tools/posixsrv-hosttest` (`make run`, `make run SAN=thread`)
   on the landed tree.

Pi gate, in this order:

1. **Stale census first:** `scripts/check-stale-binaries.sh` (and
   `scripts/check-no-stale-binaries.sh`) → **0 stale**.
2. Boot to `(psh)%`; console interactive (pty path changed: `bash`, `echo`, `exit`).
3. libc + new tests: **`test-libc-pthread`** (incl. cond clocks),
   **`test-libc-pthread-tsd`** (+ `-tsd-alloc`), **`test-libc-pthread-lifetime`**,
   `test-libc-pthread-fork`, **`test-libc-semaphore`** (all four groups;
   `sem_named` is the first run of upstream's posixsrv semaphore server),
   **`test-libc-math`** (incl. accuracy; `c99extra` long double),
   **`test-libc-time`**, **`test-libc-posixsrv`** (incl. `pty_timed`),
   **`test-libc-inet-loopback-tcp`**, **`test-libc-unix-poll-wake`**,
   `test-libc-statvfs`, `test-libc-signal`, the rest of the libc set;
   **`test-objcache`** (mem), **`test_bigwrite`** (fs), upstream's
   **`test_cputime`** (proc), `test-msg-abandon`, `test-prof-sampling`.
4. Write-offset / create probes on NFS root, ext2 and dummyfs `/tmp` (10-06 gate
   step 4: `echo a > f; echo b >> f; cat f`; `O_RDWR|O_CREAT` on an existing file
   keeps size/mtime; multi-MB `cp`+`cmp`; `mkfifo` + writer/reader; sane dates).
   Also: exec a binary, overwrite it with `cp`, exec again (object cache drops the
   old pages), and `>>` to a file that is mmapped.
5. Block-device writes via the `rpi4-storage-test` skill (sequential `dd` + `cmp`
   on a scratch stick) before anything writes `/dev/mmcblk0`.
6. `ps`, `top` (threadinfo layout), WindowMaker titles; CPU count from **C**
   (`sysconf(_SC_NPROCESSORS_ONLN)` = 4) **and C++**
   (`std::thread::hardware_concurrency()` = 4).
7. **Browser smoke** (WebKit: signal-handler `sem_post`, TSD, cond clocks; a page
   that loads + JetStream sha256 subtest correct), MicroPython smoke.
8. **Showcase 7/7**, 0 faults, HDMI looked at.
9. SSH login + lighttpd (both append to logs).

Then fast-forward/push each master, snapshot a manifest, delete the
`upstream-sync-2026-10-06` branches/worktrees.

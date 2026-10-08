# Upstream sync 2026-10-08 (redo of 10-07 on top of builds 49–68)

**Status: MERGED ON BRANCHES, NOT BUILT, NOT GATED.** Every repo with incoming
commits has a branch `upstream-sync-2026-10-08` in a worktree
`/home/houp/.claude/jobs/c8f1289c/tmp/wt-us8-<repo>`. No `master` in `sources/` was
touched, nothing was pushed, no image was built, no Pi cycle was run.

It replaces the 2026-10-07 branches. Read `docs/misc/2026-10-06-upstream-sync.md`
first (the write-offset / O_CREAT analysis, the ABI list) and
`docs/misc/2026-10-07-upstream-sync.md` (libm decision, posixsrv timed requests,
long double decls); neither is repeated in full here, both still apply.

**This time upstream moved too** (`git fetch` 2026-10-08): libphoenix +4,
posixsrv +3, tests +2, ports +2, project +2, doc +1 since the 10-07 heads. Kernel,
filesystems, corelibs, build, devices and utils upstream are unchanged. Our masters
moved in kernel (builds 49–51: kernel pipes, buddy/pageblock work, death reported
after the address space is freed), filesystems (build 58: dummyfs link/unlink
ctime), tests (49–58), ports (57–68: webkit/video) and project (59–65).
libphoenix, posixsrv, corelibs, build, devices, utils masters did not move.

**Build scope for the set: `--scope full-clean`**, a toolchain-bundle resync,
**and a rebuild of the toolchain's libstdc++** — same reasons as 10-06/10-07
(syscall renumber, `threadinfo_t`/`sem_t`/`semaphore_t`, `_SC_*` renumber baked
into `libstdc++.a`).

## Summary

| repo | incoming (total) | new since 10-07 | conflicts now | resolution | extra commits on the branch | risk |
|---|---|---|---|---|---|---|
| phoenix-rtos-kernel | 9 | 0 | 7 files (process.c new) | 10-07 reused; **process.c redone** (posix_died after vm_mapDestroy, 4 args) | write-offset guard, vm failed-create | **high** |
| libphoenix | 26 | 4 | 19 paths (2 beyond 10-07) | 10-07 reused; sem_clockwait: **upstream's wire format, our implementation**; fe* stubs **not taken** | long double decls | **high** |
| phoenix-rtos-filesystems | 3 | 0 | 2 (ext2) | 10-07 reused; dummyfs ctime commit merges clean | nfs contract fix | **high** (NFS root) |
| phoenix-rtos-posixsrv | 12 | 3 | 5 files (posixsrv.c beyond 10-07) | 10-07 reused; **a6c1675 not taken** (ours already total-order); rq_timeout unit test **adapted to ours** | — (all in the merge) | **medium-high** |
| phoenix-rtos-tests | 5 | 2 | 5 (unix-socket.c new) | 10-07 reused; af_unspec test added after ours | — | low |
| phoenix-rtos-ports | 3 | 2 | 0 | clean | — | low (wamr not built) |
| phoenix-rtos-project | 10 | 2 | gitlinks | gitlinks kept at ours; **TEST_DIRS += posixsrv** taken | — | low |
| phoenix-rtos-doc | 1 | 1 | 0 | clean | — | none |
| phoenix-rtos-corelibs | 1 | 0 | 0 | = 10-07 branch (FF) | — | low |
| phoenix-rtos-build | 16 | 0 | 0 | = 10-07 branch (FF) | dead-branch removal (10-06) | low |
| phoenix-rtos-devices | 17 | 0 | 0 | = 10-07 branch (FF) | — | low |
| phoenix-rtos-utils | 1 | 0 | 0 | = 10-07 branch (FF) | — | none |
| hostutils, lwip, plo, usb | 0 | — | — | — | — | — |

### Branch heads

All `upstream-sync-2026-10-08`, each **0 behind `origin/master` and 0 behind its
`master`** (checked after the last commit; worktrees clean, no merge/cherry-pick in
progress).

| repo | master (base) | origin/master | branch head | commits on the branch (first-parent) |
|---|---|---|---|---|
| phoenix-rtos-kernel | fdb93738 | b094f63a | **a2ce03b8** | d98c5b0b merge · d486d4df write-offset guard · a2ce03b8 vm create |
| libphoenix | 6bf27ab | d51ef1b | **6a511fd** | 3ede133 merge · 6a511fd long double decls |
| phoenix-rtos-filesystems | 393b5d6 | 5053fbf | **389dfe1** | 51bfd6b merge · 389dfe1 nfs contracts |
| phoenix-rtos-posixsrv | 85ce3dd | 4673928 | **5295e26** | 5295e26 merge |
| phoenix-rtos-tests | aad9527 | 1e75ac3 | **b729d8b** | b729d8b merge |
| phoenix-rtos-ports | 51070cf | f115d7f | **44399ae** | 44399ae merge |
| phoenix-rtos-project | 6643908 | 2a6143a | **a4ff18f** | a4ff18f merge |
| phoenix-rtos-doc | ec44abd | 525d925 | **777a04b** | 777a04b merge |
| phoenix-rtos-corelibs | 183e7ae | 5fcc3a9 | **79f68b0** | (10-06 merge, fast-forward) |
| phoenix-rtos-build | 71b723d | c4985cc | **a8e8679** | (10-06 merge + fix, fast-forward) |
| phoenix-rtos-devices | 961544f | ed99dc9 | **440bcec** | (10-06 merge, fast-forward) |
| phoenix-rtos-utils | 64ff3b6 | f226e3e | **4bfadfb** | (10-07 merge, fast-forward) |

The kernel, libphoenix and filesystems branches are **coupled** (stale syscall
stubs without libphoenix; NFS truncation/rewind without filesystems), and
libphoenix ↔ posixsrv are coupled twice now (named semaphores, and the new
`SEM_DOWN_TIMEOUT` payload). Adopt all twelve together.

How the branches were made: where only our master moved, the 10-07 merge was
replayed onto it with `git merge-tree --write-tree <master> <10-07 merge>`; where
upstream also moved, that result was committed as a temporary commit with the old
upstream parent and merged with the new `origin/master` the same way. The final
commit is a plain two-parent merge `master` + `origin/master` with that tree; the
conflicts listed below are the ones that replay left, each resolved by hand. The
10-07 extra commits were cherry-picked on top and applied unchanged.

## What is different from the 10-07 branches

1. **kernel `proc/process.c`** (new conflict). Build 51 (`286f8b8d`) moved
   `posix_died()` after `vm_mapDestroy()` (the parent learns of a death only once
   the memory is free); upstream added the `userTime/sysTime` arguments at the old
   call site. Resolution: ours' position, upstream's 4-argument call. The times
   are final by then (all threads are gone) and the teardown does not touch them.
2. **kernel pipes (build 49) × write-offset contract:** `posix_write()` handles
   `ftPipe` with `pipe_write()` before `proc_write()`, so an anonymous pipe never
   sees `o.io.offs`; named FIFOs (`ftFifo`, still posixsrv) go through
   `proc_write()` but are not `F_SEEKABLE`, so the offset is not taken. The guard
   (`d486d4df`: take `o.io.offs` only when it is ≥ the bytes written, else advance)
   is the only `proc_write()` caller's path. Every other posix.c hunk replays
   10-07's resolution exactly (diff of diffs = 0 for all six 10-07 conflict files).
3. **libphoenix `sem_clockwait()`** (upstream 2a022d6 vs ours). Both sides have
   one. Upstream changed the **posixsrv wire format**: `SEM_DOWN_TIMEOUT` now
   carries `sem_timeout_t {abstime, PH_CLOCK_*}` and posixsrv (97018fd) waits on
   that clock. Taken: the payload, so a named semaphore's MONOTONIC deadline goes
   to posixsrv as is and our MONOTONIC→REALTIME carry-over (which drifted with
   any `settimeofday()` during the wait) is gone. Kept: our `sem_clockwait()` as
   the one implementation (unnamed ones through `_sem_unnamedTake()`, lock-free
   async-signal-safe post, abstime validated only when it has to block) and our
   "are not cancellation points" deviation line (still true of ours).
   `CLOCK_MONOTONIC_RAW` is now accepted as MONOTONIC for both kinds, as upstream.
4. **libphoenix `<semaphore.h>`** includes `<fcntl.h>` (2dd8479; POSIX lets
   `<semaphore.h>` define `O_CREAT`/`O_EXCL`, and upstream's tests need it).
   Upstream's `<sys/threads.h>` is not added: our `sem_t` does not embed a
   `semaphore_t`.
5. **libphoenix fenv** (65cf8a8). `libm/include/fenv.h` loses its `#error`
   (taken; it is still **not installed** — `install-headers` filters it, and
   `include/fenv.h` goes with our real `arch/aarch64/fenv.c`; Makefile comment
   updated). Upstream's 11 `fe*()` **stubs in `posix/stubs.c` are not taken**: they
   would be a second definition of our real fenv functions in the same archive,
   and `stubs.o` is pulled into nearly every link (issetugid & co.), so whichever
   member the linker met first would silently win — `fegetround()` returning 0
   and `fesetround()` doing nothing.
6. **libphoenix `mkostemps()`/`mkstemp()` create 0600** (d51ef1b, taken; was
   `DEFFILEMODE` & umask).
7. **posixsrv a6c1675 "order request tree by total earliest first": not taken**,
   ours already is that (total `rq_cmp()` with an address tie-break; `rq_arm()`
   refuses to re-arm an armed request without touching its key). `posixsrv.c` is
   the 10-07 file verbatim.
8. **posixsrv 4673928 rq_timeout unit tests: taken, adapted to our
   implementation.** Upstream's `tests/test_rqtimeout.c` `#include`s
   `posixsrv.c` and pokes upstream's internals (`posixsrv_common.timeout.tree`,
   `timeoutState`, kernel mutex/cond). Adapted: `request_t.timer`/`RQ_*`, the
   table lock + timeout tree + pthread cond in `posixsrv_common`, and a test
   object, because an armed request holds a reference to its object here. The
   cases additionally check that every cancel returns that reference and that a
   refused re-arm takes none. This matters for the build: project 7aa559a adds
   `phoenix-rtos-posixsrv` to `TEST_DIRS`, so the `test` stage (`--with-tests`)
   builds it; upstream's file as is would not compile against our posixsrv.
9. **dummyfs ctime (build 58) × create-first O_CREAT.** Merges clean (different
   functions). Combined effect: `open(existing, O_CREAT)` — now a routine
   `mtCreate` answered `-EEXIST` — no longer stamps the directory (the
   pre-build-58 `_dummyfs_link()` stamped even on failure; upstream's create
   answers `-EEXIST` before any link anyway).
10. **tests `libc/socket/unix-socket.c`** (new conflict): upstream's `af_unspec`
    test (ad7b798) goes after our peercred/sockopt/fd-passing/`message_4096`
    tests; both run lists agreed. The kernel side (412c0e49) is already in our
    kernel. Upstream's `sem_clockwait()` cases (1e75ac3) land in
    `sem_unnamed`/`sem_named` of the one `test-libc-semaphore` binary.
11. **ports**: two wamr patch updates for upstream's libm(cs) layout and CPU-time
    clocks (wamr is not built for rpi4b; its `math.patch` assumes upstream's libm
    split, which we did not take — revisit only if wamr is ever enabled).
12. **doc** now has a branch (525d925, semaphore function pages).

Other filesystem servers checked against the write/create contract (none added or
changed by us since 10-07 besides dummyfs): posixsrv `tmpfile.c` only makes
directories; posixsrv pipes/pty/special are devices (offset irrelevant, guard
covers them); our rpi4 servers that put data in the response union
(`rpi4-v3d-async`, `rpi4-kms`) answer `mtWrite` with `-EINVAL`, so no garbage
`o.io.offs` can come back from them.

## ⚠ Behaviour changes to watch at the gate

- **Write offset / O_CREAT / O_APPEND contract** — unchanged from 10-06 (kernel
  guard + nfs fix on the branches).
- **Named-semaphore timed waits on MONOTONIC** are now the first real use of a
  `PH_CLOCK_MONOTONIC` deadline through `rq_timeoutAt()` on our timeout thread
  (it sleeps on gettime()'s raw clock — the same clock libphoenix's
  `clock_gettime(CLOCK_MONOTONIC)` reads, checked in `time/time.c`).
- **`SEM_DOWN_TIMEOUT`'s ioctl number changed** (`_IOW` of a 24-byte
  `sem_timeout_t` instead of a 16-byte timespec). Master has no named semaphores,
  so only binaries built from the 10-06/10-07 branches are affected (EINVAL on a
  timed named wait) — the stale census covers it.
- `mkstemp()`/`mkostemps()` files are 0600 (were 0666 & ~umask). A port that
  `mkstemp`s and then expects group/other access must `fchmod`.
- `<semaphore.h>` now drags in `<fcntl.h>` — a port that `#define`s an `O_*` or
  `F_*` name itself could clash (none found in the compiled tests).
- `<complex.h>` usable by applications, libstdc++ `_GLIBCXX_USE_C99_COMPLEX`,
  `math_errhandling` = `MATH_ERRNO`, stale-binary `sleep()`/syscall hazards, FAT
  `O_RDWR|O_CREAT` residual — all as 10-07.
- `posix_died()` comes after the address-space teardown — that is build 51 (our
  master), not this sync; the merge only keeps upstream's user/sys-time arguments
  (already on the 10-06/10-07 branches) at the moved call. Watch `waitpid()`
  latency for big children and the child times `times()` reports.

## Invariants re-checked on the merged code (a compile cannot see them)

- Every `nsleep()` caller passes a `PH_CLOCK_*` id: `time/time.c` (2),
  `unistd/sys.c` (2), `sys/select.c` (1) — unchanged since 10-07.
- `proc_write()` has exactly one caller (`posix_write()`), after the `ftPipe` and
  `ftUnixSocket` branches.
- `posix_died()` has one caller, `process_destroy()`, after `vm_mapDestroy()`;
  declaration and definition both take 4 arguments.
- `SEM_DOWN_TIMEOUT`/`sem_timeout_t` are used only by libphoenix `posix/sem.c` and
  posixsrv `semaphore.c` (grep over every sibling + worktree).
- `posix/stubs.c` defines no `fe*` function; `libm/include/fenv.h` is still
  filtered out of `install-headers`; `#include <fenv.h>` inside libphoenix
  resolves to `include/fenv.h` (`-Iinclude` precedes `-Ilibm/include`).
- libphoenix's `CLOCK_MONOTONIC`/`MONOTONIC_RAW`/`REALTIME` values are distinct
  (0/1/2), so the new `switch` has no duplicate case.
- The `libm/libmcs` gitlink is the same commit as upstream's (611f76e8).
- `sources/` siblings all on clean `master` after the session.

## Per repo

### phoenix-rtos-kernel — 9 incoming (none new), @ a2ce03b8
Merge `d98c5b0b` = 10-07's `a25eebec` replayed on builds 49–51 (`process.c` above).
`d486d4df` = 10-07's `5ac90b0e`, `a2ce03b8` = `f95f545a` (both cherry-picked
unchanged). Syntax: **63/63** built aarch64 kernel C files compile clean from the
worktree (incl. `posix/pipe.c`, `posix/uchannel.c`, `proc/name.c`,
`proc/process.c`, `vm/object.c`, `vm/page.c`).

### libphoenix — 26 incoming (4 new), @ 6a511fd
Merge `3ede133` = 10-07's `444fcb7` + the four new commits (items 3–6);
`6a511fd` = `1a4a799` cherry-picked. Checks:
- **176/176** built aarch64 sources compile clean against the merged
  kernel + libphoenix headers (overlay), incl. `posix/sem.c`, `posix/stubs.c`,
  `stdlib/mktemp.c`, `pthread/sem_unnamed.c`, `time/time.c`
  (`string/strerror.c` needs the generated `errno.str.inc`; inputs unchanged, so
  the buildroot copy was used).
- C++: `<bits/stdc++.h>` + `<semaphore.h>` (incl. `sem_clockwait`, `O_CREAT`),
  `<unistd.h>`, `<pthread.h>`, `<complex.h>`, `<fenv.h>`, `std::sqrt(long double)`
  compile at gnu++17/20/23. Side note, pre-existing on master: the toolchain's
  `c++config.h` has `_GLIBCXX_HAVE_FENV_H` undefined, so in C++ `<fenv.h>`
  declares nothing (`fegetround` undeclared) — worth a look in the libstdc++
  rebuild diff (gate step 4).
- libm sources are byte-identical to 10-07's branch (only `libm/include/fenv.h`,
  not installed, changed), so 10-07's libm-hosttest result stands; not re-run.

### phoenix-rtos-filesystems — 3 incoming, @ 389dfe1
Merge `51bfd6b` = 10-07's `2126b70` on build 58; `389dfe1` = `1a54151`
cherry-picked (nfs unchanged on master since). **53/53** built files compile clean.
`tools/libext2-hosttest` still needs the 5→6-arg `ext2_write` adaptation.

### phoenix-rtos-posixsrv — 12 incoming (3 new), @ 5295e26
Items 7–8 above; `semaphore.c` takes 97018fd as is. **8/8** files compile clean
(+ `tests/test_rqtimeout.c` cross-compiled with posixsrv's flags and the unity
headers). Host: `tools/posixsrv-hosttest` against the worktrees **7/7 under
ASan+UBSan and under TSan**; the adapted `test_rqtimeout.c` **6/6** on the host
(unity + that harness's shim, ASan+UBSan). Upstream's semaphore server still has
no host coverage — `test-libc-semaphore` `sem_named` (now with `clockwait_*`) is
its first test.

### phoenix-rtos-tests — 5 incoming (2 new), @ b729d8b
**226/226** built test sources compile against the merged headers, plus the new
`libc/semaphore/{common,named,unnamed}.c` and `proc/test_cputime.c`.

### phoenix-rtos-project — 10 incoming (2 new), @ a4ff18f
Gitlinks kept at ours (all equal `master`'s). Taken: lint-submodule-refs CI, and
`TEST_DIRS=(phoenix-rtos-corelibs phoenix-rtos-posixsrv)` in `build.project`.

### corelibs, build, devices, utils — = 10-07 branches
Fast-forwarded to the 10-07 (= 10-06 for corelibs/build/devices) heads. Re-checked
against the new headers: corelibs **35/35**, devices **37/37**, utils **56/56**.

## How the compile checks were run

As 10-07: the real per-file command from `make -n -W <file> <obj>` in
`.buildroot/<repo>` (read-only), worktree dirs first, then an overlay include dir
(libphoenix-worktree `include/` + `libm/include/` minus `fenv.h` + a generated
`libm_feature_config.h` with `LIBMCS_WANT_COMPLEX`, kernel-worktree `include/` as
`phoenix/`, corelibs `libstorage/include`). Kernel files without the overlay. New
files without a make rule borrowed a sibling's command line. For C++ the libstdc++
dirs and the overlay are all `-isystem`, libstdc++ first, so `<cmath>`'s
`#include_next <math.h>` reaches the overlay (with the overlay as `-I` it silently
reaches the old sysroot `math.h`). Script:
`/home/houp/.claude/jobs/c8f1289c/tmp/us8-check.sh`, logs `us8-check-*.log` beside
it. Nothing was written to `.buildroot`, `sources/` or the exports.

## If a master moves again before adoption

As 10-07: rebuild, don't stack. `t=$(git merge-tree --write-tree <new master>
<branch merge>)` (check it printed no conflicts), `git reset --hard master; git
merge --no-commit origin/master; git read-tree -u --reset $t; git commit -C
<branch merge>`, then cherry-pick the extra commits. If upstream moved as well:
`c=$(git commit-tree $t -p <new master> -p <old origin/master> -m tmp)` and
`git merge-tree --write-tree $c origin/master` for the final tree.

## Adoption order and gate

0. `scripts/snapshot-integration-state.sh` (the rollback manifest).
1. For **all twelve repos at once**, in `sources/<repo>` on `master`:
   `git merge --ff-only upstream-sync-2026-10-08` (each branch contains its
   master; if one refuses, its master moved — rebuild the branch, above).
2. `scripts/heavy-build.sh -- ./scripts/rebuild-rpi4b-fast.sh --scope full-clean`
   (**with tests**: `--with-tests` / `RPI4B_WITH_TESTS=1`, so the new
   `test-rqtimeout` builds and ships); prove the image is new (syscall names are
   NOT strings in `loader.disk`):
   - kernel: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm .buildroot/_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf | grep -E ' proc_cpuTime$'` → 1 line;
   - posixsrv in the image: `strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep -c 'semaphore init'` → ≥ 1;
   - libphoenix: `aarch64-phoenix-nm` of the sysroot `libphoenix.a` shows
     `sys_cpuTime`, `pthread_getcpuclockid`, `sem_clockwait`, and `fegetround`
     defined (`T`) by `arch/aarch64/fenv.o` only — **none from `stubs.o`**
     (`misc/fenv.c` is `#ifndef __ARCH_FENV`, empty on aarch64).
3. `scripts/sync-toolchain-from-sysroot.sh` **before** rebuilding anything that
   links the toolchain bundle (rpi4-wifi, rpi4-hci, probes, the gpu-lane
   pollnotify shim objects — they bake syscall numbers).
4. **Rebuild the toolchain's libstdc++** against the merged headers (Docker
   `--no-cache` release build, or the toolchain build script), then **diff its
   `bits/c++config.h`** against the current one
   (`.toolchain/aarch64-phoenix/aarch64-phoenix/include/c++/aarch64-phoenix/bits/c++config.h`):
   `_GLIBCXX_HAVE_COMPLEX_H`, `_GLIBCXX11_USE_C99_COMPLEX`,
   `_GLIBCXX98_USE_C99_COMPLEX`, `_GLIBCXX_USE_C99_MATH_FUNCS`,
   `_GLIBCXX_HAVE_FENV_H`, `_GLIBCXX_HAVE_*L` math macros. A flipped complex macro
   means std::complex math will need `csqrt/cpow/...` that libphoenix lacks — pin
   it off or implement them before relinking C++ ports. Then relink the C++ ports
   (full-clean order).
5. Host harnesses on the landed tree: adapt `tools/libext2-hosttest/*.c` to the
   6-arg `ext2_write` and run `run-all.sh`; `scripts/run-libc-hosttests.sh`;
   `tools/libm-hosttest` (`make run`); `tools/posixsrv-hosttest` (`make run`,
   `make run SAN=thread`).

Pi gate, in this order:

1. **Stale census first:** `scripts/check-stale-binaries.sh` (and
   `scripts/check-no-stale-binaries.sh`) → **0 stale**.
2. Boot to `(psh)%`; console interactive (pty path: `bash`, `echo`, `exit`).
3. libc + new tests: **`test-libc-pthread`** (incl. cond clocks),
   **`test-libc-pthread-tsd`** (+ `-tsd-alloc`), **`test-libc-pthread-lifetime`**,
   `test-libc-pthread-fork`, **`test-libc-semaphore`** (all four groups, incl. the
   new `clockwait_*` cases in `sem_unnamed` and `sem_named` — `sem_named
   clockwait_monotonic_*`/`clock_is_honoured` exercise the MONOTONIC deadline
   through posixsrv), **`test-rqtimeout`** (posixsrv unit test, new),
   **`test-libc-math`** (incl. accuracy; `c99extra` long double),
   **`test-libc-time`**, **`test-libc-posixsrv`** (incl. `pty_timed`),
   **`test-libc-unix-socket`** (incl. upstream's `af_unspec`),
   **`test-libc-pipe`** (kernel pipes), **`test-libc-inet-loopback-tcp`**,
   **`test-libc-unix-poll-wake`**, `test-libc-statvfs`, `test-libc-signal`,
   `stat_dir_times` (libc/misc), the rest of the libc set; **`test-objcache`**,
   **`test_frag`** (mem), **`test_bigwrite`** (fs), upstream's **`test_cputime`**
   (proc), `test-msg-abandon`, `test-prof-sampling`.
4. Write-offset / create probes on **NFS root, ext2 and dummyfs `/tmp`**:
   `echo a > f; echo b >> f; cat f` (expect `a`,`b`); `O_RDWR|O_CREAT` on an
   existing file keeps size/mtime **and does not move the directory's
   mtime/ctime** (dummyfs: build 58 × create-first); multi-MB `cp` + `cmp`;
   `mkfifo p` + writer/reader (must hit the FIFO); a shell pipeline
   (`ls | wc`, kernel pipes); sane dates on new NFS files (EXCLUSIVE4 verifier);
   exec a binary, overwrite it with `cp`, exec again (object cache); `>>` to a
   mmapped file; `mktemp` file mode 0600.
5. Block-device writes via the `rpi4-storage-test` skill (sequential `dd` + `cmp`
   on a scratch USB stick) before anything writes `/dev/mmcblk0`.
6. `ps`, `top` (threadinfo layout), WindowMaker titles; CPU count from **C**
   (`sysconf(_SC_NPROCESSORS_ONLN)` = 4) **and C++**
   (`std::thread::hardware_concurrency()` = 4); a big child's memory is back
   when `waitpid()` returns (`test_frag`).
7. **Browser smoke** (WebKit WPE + GTK: signal-handler `sem_post`, TSD, cond
   clocks, GLib main loop on kernel pipes; a page that loads + JetStream sha256
   subtest correct; one MSE/HEVC video plays), MicroPython smoke.
8. **Showcase 7/7**, 0 faults, HDMI looked at.
9. SSH login + lighttpd (both append to logs).

Then fast-forward/push each master, snapshot a manifest, and delete the
`upstream-sync-2026-10-06` and `-10-07` branches/worktrees.

## Adoption log

- **2026-10-08 13:19 — toolchain rebuilt** from scratch (gcc 16.2.0 + patch 12, 12 min) against the
  fast-forwarded masters; the old one is kept as `.toolchain.pre-sync-20261008`. Installed
  `bits/c++config.h`, old → new (20 macros):
  - **complex stays OFF:** `_GLIBCXX11_USE_C99_COMPLEX` and `_GLIBCXX98_USE_C99_COMPLEX` are still
    undefined, so `std::complex` makes no `csqrt`/`cpow` calls. `_GLIBCXX_HAVE_COMPLEX_H` is now 1
    because upstream's `<complex.h>` arrived with the sync.
  - **`_GLIBCXX_USE_C99` 1 → undefined:** configure's aggregate C99 check now sees `<complex.h>`,
    and its complex test fails. Its only consumer in libstdc++ is the `%S` leap-second bound in
    `time_get` (`locale_facets_nonio.tcc:876`: 60 vs 61). No `.cc` in `libstdc++.a` uses it.
    Accepted.
  - **Now ON, because libphoenix has them:** `HAVE_FENV_H`, `USE_C99_FENV(_TR1)`, `HAVE_UCHAR_H`,
    `USE_C11_UCHAR_CXX11`, `USE_C99_INTTYPES*` (4), `USE_NL_LANGINFO_L`, `HAVE_LC_MESSAGES`,
    `USE_STRUCT_TM_TM_ZONE`, `HAVE_EXECINFO_H`.
  - `_SC_NPROCESSORS_ONLN` is 27 in the new sysroot. The Pi gate's
    `std::thread::hardware_concurrency() == 4` check proves libstdc++ took it.

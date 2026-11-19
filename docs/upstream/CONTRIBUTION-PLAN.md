# Upstream contribution plan

- **Status:** draft for the owner, 2026-11-19. Nothing has been sent or pushed.
- **Scope:** the fork's general-purpose changes, meaning code that is not Pi 4-specific: arch-independent, or aarch64-generic (also useful on zynqmp, for example). Full Pi 4 support stays in the fork and is out of scope here.
- **Source material:** [PHOENIX-RTOS-RPI4-CHANGES.md](../PHOENIX-RTOS-RPI4-CHANGES.md) (its "★ Start here" table and the per-area "General bug fixes" sections), and the fixes made from 2026-10-09 to 11-19 that the changes doc does not list yet.

## 1. Purpose and approach

The owner plans to offer the upstream maintainers ([github.com/phoenix-rtos](https://github.com/phoenix-rtos)) bug reports, bug-fix PRs and enhancement PRs, for their consideration.
This document makes that practical. It lists every candidate, states whether upstream already has an equivalent, and records whether the candidate applies to upstream `master` as it stands today.

Rules for everything we send:

- **One defect per PR, small and independent.** Our history contains long investigation series with diagnostics added and later removed. A PR carries only the final fix, with one commit for each logical step.
- **A test or a measurement with every PR.** Use a Unity test in `phoenix-rtos-tests` where one exists, otherwise a host harness or a reproducer described in the PR text.
- **Upstream code style and commit conventions.** See section 8.
- **No Pi 4 code, and no Pi 4 prose.** Fork comments often cite Pi symptoms, internal issue numbers (`#121`, `TD-xx`, `L33`, `C1`, "build 45") or a Pi-specific rationale. Rewrite them before sending.
- **Bug reports first for the big items.** Use an issue for anything that touches the kernel's IPC, process lifetime or `posix.c` file lifetime, or that is a design change (demand-zero memory, a file page cache, msun libm, TZ). Maintainers can then pick the shape of the fix before we spend time re-deriving ours.
- **Security-sensitive items go to the maintainers privately**, not as a public issue: the `sigreturn` privilege checks, and kernel data leaked to EL0. Upstream has no `SECURITY.md`, so the owner should contact the maintainers directly.

Kinds used below:

| kind | meaning |
|---|---|
| **a** | bug fix, generic (any target) |
| **b** | bug fix, aarch64-generic (helps any aarch64 target, e.g. zynqmp) |
| **c** | enhancement (new behaviour, performance, or a design change) |
| **d** | report only: a defect we worked around, or where our fix is too fork-specific to offer as a PR |

Effort is the work to prepare the PR, not to write the fix:

| effort | meaning |
|---|---|
| **S** | under 1 hour: cherry-pick, rewrite the message and comments, run the test |
| **M** | about half a day: re-derive against changed upstream code, or split a commit, then test |
| **L** | several days: a multi-commit series, an issue and discussion first, or a design choice |

## 2. How this was checked

- `git fetch origin` in every sibling repo on 2026-11-19. Upstream baselines:

  | repo | `origin/master` | fork `master` ahead / behind |
  |---|---|---|
  | phoenix-rtos-kernel | `4c7564cd` (2026-11-18) | 432 / **10** (upstream's `map_belongs`, TLS and lazy-fault commits are not merged yet) |
  | libphoenix | `fc60045` (2026-11-06) | 342 / 0 |
  | plo | `6e97833` (2026-11-06) | 88 / 0 |
  | phoenix-rtos-filesystems | `52ae003` (2026-10-30) | 110 / 0 |
  | phoenix-rtos-usb | `6397a27` (2026-11-17) | 46 / 0 |
  | phoenix-rtos-devices | `d26a279e` (2026-11-17) | 605 / 0 |
  | phoenix-rtos-corelibs | `5d676e7` (2026-11-18) | 10 / 8 |
  | phoenix-rtos-posixsrv | `4673928` (2026-10-07) | 19 / 0 |
  | phoenix-rtos-lwip | `3adc368` (2026-11-04) | 39 / 0 |
  | phoenix-rtos-tests | `7d48d34` (2026-11-18) | 264 / 1 |

- **Upstream status:** this was checked at two depths.
  - **Read in the upstream code:** every dropped candidate, every first-batch item, every draft issue, and the kernel signal, pager and `posix_open` paths.
  - **Checked mechanically:** the remaining conflicting rows. The lines our fix removes were searched for in the `origin/master` version of the same file; if they are still there, the defect is still there. Where that check was inconclusive the row says "(not re-read)".
  - Fourteen candidates turned out to be fixed or superseded upstream; they are marked and dropped.
- **Applies to upstream:** every candidate's commit, or ordered series, was cherry-picked (`-x`) onto `origin/master` in a detached worktree. Results are clean, conflicts (with the reason: an earlier fork commit, or upstream having changed the area), or needs a split. The worktrees were then removed, and no branches were created.
- **Compile check:** the touched `.c`/`.S` files were compiled with the real per-file flags of our `aarch64a72-generic-rpi4b` build (GCC 16, `-Werror` included), with the upstream worktree first on the include path. Our tree builds no other target, so this checks **aarch64 compilation only**. Limits:
  - **Header mixing.** The include paths are our build's, so any header the worktree does not provide comes from the fork, and the board HAL `hal/aarch64/generic/` exists only in the fork. For the kernel, the upstream `hal/aarch64` was put first so `<arch/…>` headers come from upstream. For libphoenix, upstream libphoenix and kernel headers were put first. With that setup, unpatched upstream `proc/process.c` and `vm/map.c` compile.
  - **NOMMU files** such as `proc/msg-nommu.c` cannot be checked here.
  - **lwIP.** Under our configuration (GCC 16, `-Werror`), unpatched upstream `port/sockets.c` fails with `unused-but-set-variable` in `do_getifaddrs`, and W01 fixes it. Upstream's own toolchain may not warn.
- **Not done:** no candidate was run on an upstream target. For each PR, build and run `ia32-generic-qemu`, and for aarch64 changes `aarch64a53-zynqmp-qemu`, before sending (section 8).

## 3. Summary

- **131 candidates examined.** 14 are already fixed or superseded upstream (one of them, `_SC_OPEN_MAX`, is a known upstream issue), and 7 are fork-only code. That leaves **110 live candidates**:

  | kind | count |
  |---|---|
  | a: generic bug fix | 78 (one of them, K34, also has an aarch64 half) |
  | b: aarch64-generic fix | 5 |
  | c: enhancement | 20 |
  | d: report only | 7 |

- **39 apply cleanly** to upstream `master` and compile on aarch64. A few more need only a trivial conflict resolved: K35 `hal_jmp`, whose hand-resolved copy compiles, and D01 libtty. Most of the ext2 series (29 of 36 commits) also applies, once the one commit upstream already has and the commits that depend on F01 are left out (section 6).
- **Already upstream, so dropped:**
  - **kernel:**
    - `vm_mapBelongs` whole range: `52b4de9d`, plus overflow checks `22d19ead` and `feb7da8a`;
    - `PROT_USER` on EL1 user-copy faults: `e18948f4` maps with the entry's protection;
    - AF_UNIX id recycling: upstream rewrote AF_UNIX (`a8300c72`) and allocates ids from a rotating `nextId`;
    - `recvmsg` control length and the `accept4` use-after-free: the code they fixed is gone in the same rewrite; the new code was not re-checked for the same defects;
    - `SO_TYPE`/`SO_ACCEPTCONN`: `d5d9b6b6`.
  - **libphoenix:**
    - `semaphoreUp` lost wakeup: `e5eb65b`;
    - `select(…, NULL)`: `03fe6fb`;
    - `std::atomic` in pthread types: `6a150a2`;
    - scanf matching-failure return: `b336093`;
    - umask: `__getumask()` in open/mkdir;
    - NULL signal handler: signal dispatch moved to the kernel, and `SIG_DFL` is now 0;
    - atexit index: atexit was rewritten, `1e3ae62`;
    - `_SC_OPEN_MAX`: known upstream as issue #1610.
  - **ext2:** the hole-terminated `block_sync` run, `aeb7e3c` (project issue #1418).
- **Possibly upstream, kept live as "re-evaluate":** posixsrv timed requests (P04). `a6c1675` and `4178dd3` look equivalent from their subjects, but the code was not re-read.
- **Needs splitting before it can be offered:**
  - P01 `/dev/urandom`: the `dst` fix is separate from `/dev/hwrng` sourcing;
  - L25 `fopen` modes: separate from the new `memmem`/`getsubopt`;
  - L22 printf `'` flag: `format.c` is separate from the fork-only `swprintf.c`;
  - L27 `fchdir`: the "fail with ENOSYS" commit `1d86e24` is separate from the implementation, which needs a fork-only kernel syscall;
  - L32 `statvfs`: keep the EBADF half and drop the trailing-slash half, which upstream now does;
  - L40: send `<assert.h>` alone;
  - K24: separate the zone hardening from its optional alloc-trace ring;
  - U07 DMA pool: separate it from the Pi's "below 3 GiB" placement;
  - the umass series (D02 to D04), which sits on a long fork-only umass history.
- **Do not offer as PRs:**
  - U03 (`dc civac` in generic `usb/mem.c`): inline aarch64 asm in generic code, and K01 is the right fix at the right layer;
  - K21 (main-thread stack size): our value is Pi-tuned, so report the double fault instead;
  - D06 (bcm2711-emmc bus address): a Pi driver;
  - F12 (libnfs `readlink`): a third-party library bug, and the NFS server is fork-only;
  - K39 and K40 (PL011 bound, heartbeat): fork-only HAL files;
  - K06 (`lib_printf` from the scheduler): the `lib_printf` calls that deadlocked were our own recovery code from the vfork work (K05). Upstream's scheduler only asserts, and `lib_assertPanic()` calls `log_disable()` before printing, so upstream does not have the defect. The changes doc's shortlist row 15 overstates this.

## 4. Recommended first batch

Ten PRs, each verified on 2026-11-19 to cherry-pick cleanly onto upstream `master` and to compile with `-Werror` on aarch64. All are small and self-contained, and each fixes a defect visible in upstream code today. Send them in this order, one or two at a time, so the first review sets the conventions for the rest.

| # | id | repo | PR title (upstream style) | our SHA(s) | readiness | test to include |
|---|---|---|---|---|---|---|
| 1 | B01 | plo | `cmds/kernel: zero the .bss part of loaded segments` | `9551073` | clean, compiles | Reproducer: fill RAM with a pattern before `kernel` (our B02 `poison` command, or any memory-fill command), then show `.bss` garbage. A search of upstream `hal/` finds no code that clears the kernel's own `.bss`, so every kernel loaded by this command depends on it. |
| 2 | L01 | libphoenix | `string: strncmp/strncasecmp must not read past n` | `f2d2789` | clean, compiles | tests `a82e0e4` (clean on upstream tests): a string ending flush against an unmapped page |
| 3 | L03 | libphoenix | `stdio/asprintf: size the buffer with vsnprintf` | `839b24b` | clean, compiles; upstream still has `malloc(1024); /* FIXME */` + `vsprintf` | add a Unity case: `asprintf` of a 2 KiB string, then `malloc`/`free` |
| 4 | K17 | kernel | `proc/threads: implement SA_RESETHAND` | `4b1975d8` | clean, compiles; upstream `include/signal.h` says `/* FIXME: implement */` | `sigaction resethand_*` from tests `a43e7fa`: split that commit (it also adds the pthread-fork test) and fix the `libc/Makefile`/`test.yaml` registration conflict |
| 5 | K28 | kernel | `vm/map: mprotect() covers every page of an unaligned length` | `fc21dab0` | clean, compiles | tests `fbfc47e` (`mem/test_mprotect`, clean) |
| 6 | K19 | kernel | `vm/object: do not rb-remove a contiguous object that was never inserted` | `d0fb0ca9` | clean, compiles | host reproduction with the kernel's own `lib/rb.c`: removing a zeroed node sets `tree->root = NULL`; describe it in the PR |
| 7 | K08 | kernel | `posix: fsync() must send the file's oid` | `482b54c2` | clean, compiles | add a case: `fsync()` on a block-device fd returns 0, not `-EINVAL` |
| 8 | F05 | filesystems | `ext2: handle mtSync` | `017eb3a` | clean, compiles | `fsync()` on an ext2 file reaches `cache_flush()`; plus the libext2 host harness (`tools/libext2-hosttest`) |
| 9 | F02, F03 | filesystems | `ext2: use 64-bit block offsets` + `ext2: fix block↔bitmap off-by-one for blocks > 1 KiB` | `7618a7d`, `505c4a3` | the pair applies cleanly on its own and compiles | host harness with a > 4 GiB, 4 KiB-block image, checked with `e2fsck` |
| 10 | U06 | usb | `libusb: send ClearFeature(ENDPOINT_HALT) to the endpoint` | `00e9ac4` | clean, compiles | USB 2.0 §9.4.1; no upstream caller today, so describe it as a latent bug |

**The owner's suggested starters, verified:**

- plo `.bss`, `strncmp`, ext2 `fsync` (F05) and libusb `ClearFeature(HALT)`: confirmed, and in the batch.
- **`semaphoreUp`:** dropped. Upstream fixed it in `e5eb65b`, which signals whenever there are waiters.
- **`vm_mapBelongs`:** dropped. Upstream fixed it in `52b4de9d` (RTOS-1507), which walks contiguous entries, and added overflow checks in `22d19ead` and `feb7da8a`. The fork should take upstream's version when it merges the 10 pending kernel commits.
- **`hal_jmp` register leak (K35, `3ab815fe`):** does not cherry-pick as-is. The conflict is one blank line of context left by our earlier `9276ba68`. Resolved by hand, the hunk inserts 31 `mov xN, xzr` before `eret`, and that copy compiles. It is safe: upstream's `crt0` and `_signal_trampoline` take their arguments from the stack. It is moved to the aarch64 batch below, because it should run on `aarch64a53-zynqmp-qemu` first.
- **ext2 allocator:** the allocator *lock* (F01, `463aec1`) conflicts with upstream's new write API (`O_APPEND`, returned offset). The fix is small but must be re-derived, so it goes to the second batch. The allocator *skip-full-groups* commit (F07, `14ce324`) applies cleanly, but it is a performance enhancement, so it is not in the first batch. F02 and F03 take the slot instead.

**First aarch64 batch.** This is kind b; send it once the first batch has set the conventions, and run each item on `aarch64a53-zynqmp-qemu`.

| id | PR title | our SHA | readiness |
|---|---|---|---|
| K01 | `hal/aarch64/pmap: clean+invalidate a page when it becomes non-cacheable` | `5d8645f6` | clean, compiles; our highest-value aarch64 fix (GPU control-list corruption 12 per 8 boots → 0) |
| K35 | `hal/aarch64: clear general-purpose registers before entering EL0` | `3ab815fe` | trivial conflict; the hand-resolved copy compiles |
| K36 | `hal/aarch64: enter EL0 with the FPU trapped; reset FPCR/FPSR on first use` | `d58166fd` | conflicts with the fork's `_exceptions.S`; it follows K35 in the same function: re-derive after K35 |
| K37 | `hal/aarch64: build the signal frame without kernel data` | `2f5e1e62` | conflicts in `cpu.c` with K34 and fork perf-sampler changes; re-derive after K34, which goes privately first (Issue 4) |

**Second batch** (all kind a; small re-derivations or a test to adapt):

- **USB:** U01 TRSTRCY, U04, U05, U08.
- **libphoenix:** L05, L07 (rewrite the comment), L14, L16, L17, L31, L36, L38.
- **ext2:** F06, the rest of the sparse-file series F04 + F13, F01 re-derived.
- **Other repos:** D01 libtty `FIONREAD`, W01 + W02 lwIP sockets, P01 split, K13, K14.
- **Re-derived:** L19 ctype, L20, L39, L11.
- **Kernel, in rough order of value:** K31 (buddy merge; needs careful review), K10, K11, K18, K26, K07, K33.

## 5. All candidates, by repo

**Column guide:**

- **Order:**
  - `1`: first batch;
  - `1b (aarch64)`: first aarch64 batch;
  - `2`: second batch;
  - `3`: later;
  - `issue first`: file the issue, then a PR once the maintainers agree on the shape;
  - `discussion`: an enhancement to propose before writing anything;
  - `drop`: already upstream;
  - `not applicable`: fork-only code.
- **Applies:** the result of the cherry-pick onto upstream `master`, plus the aarch64 compile check where it applied.
- **Our SHA(s):** fork commits, abbreviated. A series is shown as first … last, with the count in brackets.


### phoenix-rtos-kernel

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| K01 | Stale dirty cache lines survive into a non-cached mapping (`hal/aarch64/pmap.c`) | b | `5d8645f6` | open | clean; compiles | GPU control-list corruption 12 per 8 boots → 0 | 1b (aarch64) | S |
| K02 | `open()`/socket construction window: half-built `open_file_t` reachable, double free (`posix/posix.c`) | a | `e88c8b75` … `39135453` (6) | open (upstream `posix_open()` stores the new file in `p->fds[fd]` with only its lock set, then drops `p->lock`) | conflicts: `posix.c` moved upstream (O_APPEND offset, version, struct pruning); re-derive | EL1 heap-corruption hunt converged; no unit test | issue first, then 3 | L |
| K03 | AF_UNIX socket id recycling (`posix/unix.c`) | a | `69d9a448` | **fixed upstream** by the AF_UNIX rewrite `a8300c72` (ids from a rotating `nextId`) | n/a (file gone) | — | drop | — |
| K04 | Demand pager ignored short reads (`vm/object.c`) | a | `f145658f` | open (upstream `object_fetch()` does one `proc_read()` and fails only on `< 0`) | conflicts: `vm/object.c` moved (overflow-safe `round_page`); re-derive | exec of large binaries from NFS | 2 | M |
| K05 | vfork: kernel stack freed while running; borrowed map destroyed; `execkstack` uninitialised disables the stack canary | a | `20802d61`, `b98d7b9a`, `6d8f40a5` | open | conflicts; prerequisite of K20 | EL1 abort on X-session teardown gone | issue first, then 3 | L |
| K06 | `lib_printf` from the scheduler self-deadlocks a non-recursive spinlock | a | `ce445804` | fork-introduced: the `lib_printf` calls are our own K05 recovery code; upstream's scheduler asserts call `log_disable()` before printing | n/a | 1 in 6 X11 exits wedged the board → 20/20 | not applicable | — |
| K07 | `process_getName` faults the kernel on a corrupt `argv` | a | `3c8009b7` | open | conflicts: `process.c` TLS rework; small re-derive | 2982 EL1 aborts in one `top` run → 0 | 2 | S |
| K08 | `fsync()` sent `mtSync` with a zeroed oid (`posix_fsync`) | a | `482b54c2` | open | clean; compiles | `fsync()` on a block device returned `-EINVAL` always | 1 | S |
| K09 | `fcntl()` record locks were a stub returning success | c | `3844d204` | open | conflicts; also needs libphoenix side | unit tests in fork | 3 | L |
| K10 | SCM_RIGHTS descriptors not bound to their message (`posix/usocket.c`) | a | `39609195` | open | conflicts: `usocket.c` gained MSG_TRUNC/getsockname upstream; re-derive | test `fd_per_message` (tests `f4fb172`) | 2 | M |
| K11 | Framed AF_UNIX sockets: 4 KiB default ring rejects messages ≥ 4089 B | a | `c0431a84` | open (upstream default still `SIZE_PAGE`) | conflicts (same file as K10) | test `message_4096` | 2 | S |
| K12 | `getsockopt(SO_TYPE/SO_ACCEPTCONN)` on AF_UNIX | a | `d77b2827` | **fixed upstream** `d5d9b6b6` | applies textually but fails to compile (duplicate `case`) | — | drop | — |
| K13 | `meminfo()` "no amap" marker `~0U` in a `size_t` | a | `19d13cd5` | open | clean; compiles | `psh mem` printed 4 GiB | 2 | S |
| K14 | `meminfo()` credited each split entry with the whole amap | a | `e802ac0e` | open | conflicts: `vm/map.c` moved; small | test `meminfo_anonsz_of_split_entries` | 2 | S |
| K15 | Demand-zero anonymous memory on MMU targets | c | `d2284b32` | open; upstream has its own opt-in lazy mode (RTOS-1141) | conflicts with upstream's lazy-fault work | `jsc` 386 MB → 141 MB | discussion | L |
| K16 | A dying process waits forever on a server that received its request | a | `3c5f210b`, `07bd1384`, `a8880d9d` | open | conflicts | test `test-msg-abandon` 6/0 | issue first | L |
| K17 | `SA_RESETHAND` ignored (upstream header: `/* FIXME: implement */`) | a | `4b1975d8` | open | clean; compiles | test `sigaction resethand_*` (tests `a43e7fa`, registration-only conflict) | 1 | S |
| K18 | `ioctl(FIONBIO)` on an AF_UNIX socket fails (sent to placeholder `USOCKET_PORT`) | a | `99cb2ab4` | open | conflicts (posix.c) | test `socketpair_nonblock` (tests `90265ab`) | 2 | S |
| K19 | Freeing a `MAP_CONTIGUOUS` object empties the kernel object tree (`lib_rbRemove` of a never-inserted node) | a | `d0fb0ca9` | open | clean; compiles | host repro with the kernel's own `lib/rb.c` | 1 | S |
| K20 | Clients of a dead server hang forever; rid reuse; two port use-after-frees | a | `ac5ee1a6`, `9ce228c7`, `ee437693`, `3adc9950` | open (upstream allocates rids lowest-free, nothing fails pending requests) | applies, but needs K05's `borrowed` and edits `msg-nommu.c` (not compilable here) | Pi probe: baseline hang, fixed pass | issue first, then 3 | L |
| K21 | 32 KiB main-thread stack; signal push double-faults below an exhausted SP | d | `8ae20864` | open | our value is Pi-tuned; not a PR | `cksum`/`od` overflow | report | — |
| K22 | `recvmsg` did not report control length (`posix/unix.c`) | a | `381152c6` | superseded: `posix/unix.c` was replaced by the upstream rewrite `a8300c72`; the new code was not re-checked for this defect | n/a | — | drop | — |
| K23 | `accept4` use-after-free (`posix/unix.c`) | a | `9c60b783` | superseded by the same rewrite; not re-checked | n/a | — | drop | — |
| K24 | `vm/zone` free-list hardening (misaligned free, link validation, quarantine) | c | `b516c8a4` … `ec29e159` (5) | open | conflicts at `521320e9`, which also adds a trace ring (diagnostic): split | review-found replenish bug `== 1U` → `<= 1U` | 3 | M |
| K25 | `p->lock` held across blocking IPC in exit/exec fd sweeps | a | `2c489b75` | open | conflicts (posix.c) | wedged server stalls all threads | 3 | M |
| K26 | Uninitialised kernel stack `msg_t` copied to userspace; `f->path` uninitialised | a | `31045b7f`, `e5c5f833` | open (upstream `posix_ioctl` still has a bare `msg_t msg;`) | conflicts; small | `TCGETS` returned kernel stack bytes | 2 | S |
| K27 | Real backing-store error reported as `-ENOMEM` | a | `0620f2e9` | open (upstream `vm_objectPage()` returns `EOK` with a NULL page) | conflicts: our fix sits on fork-only read-ahead (`object_fetchCluster`); re-derive | misdiagnosis cost several sessions | 3 | S |
| K28 | `mprotect()` rejected a length that is not a page multiple | a | `fc21dab0` | open | clean; compiles | test `mem/test_mprotect` (tests `fbfc47e`, clean) 6/0 | 1 | S |
| K29 | `PROT_USER` derived from the fault type, EL1 user-copy fault storm | a | `043cde80` | **fixed upstream** differently: `e18948f4` maps with the entry's protection | n/a | — | drop | — |
| K30 | Page cache that survives process exit (file object cache) | c | `8e9e2738`, `b2bb5b95`, `909fcda6`, `a2ce03b8` | open | conflicts; touches a Pi config header | second map of 96 MiB file 136× faster | discussion | L |
| K31 | Page allocator never re-merged freed blocks at every order; free block across a memory hole | a | `2c003860`, `66454137` | open | clean; compiles | host sim `tools/pagealloc-hosttest`; largest free block 234 MB → full | 2 | M |
| K32 | Parent told of child's death before its memory is freed | a | `286f8b8d` | open | conflicts (process.c) | `waitpid()` then allocation failure | 3 | S |
| K33 | Fault with blocked/ignored SIGSEGV re-faults forever (upstream FIXME in `map_pageFault`) | a | `d36f9967` | open | conflicts (TLS + lazy-fault changes) | test group `faultsig` 7 cases | 2 | M |
| K34 | `sigreturn`: unchecked user context pointer, EL1 `psr` accepted | a+b | `7158b7ad`, `9276ba68` | open (upstream `syscalls_sigreturn()` writes pc/sp/psr through the user-supplied `ctx` pointer, and still calls `proc_longjmp()` after `proc_kill()` for an EL1 psr) | conflicts at `9276ba68` (fork `_exceptions.S`) | test group `faultsig` | private report first, then 2 | M |
| K35 | `hal_jmp` enters EL0 with kernel values in x0–x30 | b | `3ab815fe` | open | one trivial conflict (blank line); hand-resolved copy compiles | kernelhard on Pi; crt0/trampoline take args from the stack upstream too | 1b (aarch64) | S |
| K36 | EL0 entered with the FPU untrapped; FPCR/FPSR not reset at exec | b | `d58166fd` | open (upstream's FPU is lazy too, and `hal_jmp` leaves `CPACR_EL1` alone) | conflicts | `kernelhard` FP-across-exec case | 1b (aarch64) | S |
| K37 | Signal frame carries kernel data (aarch64 `hal_cpuPushSignal`) | b | `2f5e1e62` | open for `savesp` and `cpacr` (upstream copies the whole kernel frame); the `uc_stack` part is fork-only | conflicts; on top of K34 | `kh_sigframe` 2/2 | 1b (aarch64) | S |
| K38 | `vm_mapBelongs` checked only one byte of the range | a | `51beb23a` | **fixed upstream** `52b4de9d` (RTOS-1507) + overflow checks `22d19ead`, `feb7da8a` | n/a | — | drop; adopt upstream's version at next sync | — |
| K39 | Bounded PL011 TX FIFO wait | b | `5894e9b9` | fork-only file (`hal/aarch64/pl011.c` does not exist upstream) | n/a | — | not applicable | — |
| K40 | Per-CPU console heartbeat | c | `06981d6f` | fork-only diagnostic | n/a | — | not applicable | — |
| K41 | Only the primary CPU builds the boot translation tables | b | `eabe9942` | open | conflicts; needs a zynqmp boot-order review | SMP boot race | 3 | M |
| K42 | Signal frame push bounded at both ends of the user stack | a | `a6ddd2ca` | open | conflicts | double-fault on overflow | 3 | S |
| K43 | vfork/fork child read the parent's context before it was saved | a | `33af3e81` | open (not re-read) | conflicts | premain hang | 3 | S |

### libphoenix

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| L01 | `strncmp`/`strncasecmp` read one byte past `n` | a | `f2d2789` | open | clean; compiles | test (tests `a82e0e4`, clean) | 1 | S |
| L02 | `semaphoreUp` lost wakeup | a | `e75c4fe` | **fixed upstream** `e5eb65b` (signals whenever there are waiters) | n/a | — | drop | — |
| L03 | `vasprintf` heap overflow (`malloc(1024); /* FIXME */` + `vsprintf`) | a | `839b24b` | open | clean; compiles | glib2/mc/Dillo crashes; add a >1 KiB `asprintf` test | 1 | S |
| L04 | `free()` turns one overflow into a second unbounded write; validate chunk header | c | `aae70f0` | open | clean; compiles | fault surfaced far from the cause | 3 | S |
| L05 | Default pthread attributes had no guard page | a | `02ab4e0` | open | clean; compiles | silent overrun into a neighbour's stack | 2 | S |
| L06 | Default thread stack = `PTHREAD_STACK_MIN` (one page) | a | `bad2009` | open (per-arch value) | conflicts (fork pthread changes) | libjpeg overflow | 2 | S |
| L07 | `sync()` was an empty stub | a | `40a1efb` | open | clean; compiles; comment is Pi-specific prose, rewrite | SD writes reached the card | 2 | S |
| L08 | `select(…, NULL)` never blocked | a | `033ee1f` | **fixed upstream** `03fe6fb` | n/a | — | drop | — |
| L09 | `std::atomic` in pthread types (C++ non-copyable) | a | `94df683` | **fixed upstream** `6a150a2` (no `_ATOMIC` left) | n/a | — | drop | — |
| L10 | A signal makes `access()`/`stat()` see `ENOENT`/`EINTR` (resend aborted lookups) | a | `b33e8ec` | open | conflicts (fork `unistd/dir.c`) | test `libc/sa_restart`; host model 6/11 → 0/11 | 2 | M |
| L11 | `pthread_setspecific()` allocates under `pthread_key_lock` | a | `052b590` | open | conflicts (fork pthread) | test `pthread_tsd_alloc` (tests `23593b1`, clean) | 2 | S |
| L12 | `free()` left the heap lock held on the abandoned-release path | a | `93520e9` | fork-only code path | n/a | — | drop | — |
| L13 | Freed heaps kept for reuse + `malloc_trim()` | c | `50fd18c` | open | conflicts | 9 tests `stdlib_malloc_retain` | discussion | M |
| L14 | `setvbuf()`: `_IOLBF` with NULL buffer rejected; flag reset cleared nothing | a | `fa8521d` | open | clean; compiles | test (tests `6e7a7f6`, clean) | 2 | S |
| L15 | `malloc(0)` returns NULL | c | `6465a4a` | open (legal; compatibility) | conflicts (upstream reformatted malloc) | jq | 3 | S |
| L16 | Double `fclose()` writes through NULL | a | `5fa3847` | open | clean; compiles | quake3e fault | 2 | S |
| L17 | Partial `fflush()` re-sends the written prefix | a | `9029813` | open | clean; compiles | 234 113 duplicate UART copies | 2 | S |
| L18 | printf feed buffer 16 → 256 | c | `5674368` | open | clean; compiles | write() per 15 chars | 3 | S |
| L19 | ctype macros evaluate the argument more than once | a | `156422a` | open | conflicts (`__isspace` reworked upstream); small | test `ctype single_evaluation` (tests `0bdaa1a`, clean) | 2 | S |
| L20 | `fclose(stdout)` frees a standard stream still referenced | a | `bf35aaf` | open | conflicts (fork stdio) | test `closed_std_streams` (tests `c4b23a8`, clean) | 2 | S |
| L21 | POSIX `TZ` strings (`tzset` was a stub) | c | `46ec479`, `1461f9f`, `00e6a64` | open | conflicts | host: 12.9 M comparisons vs glibc, 0 diffs | discussion | L |
| L22 | printf rejected the POSIX `'` flag and left the argument on the `va_list` | a | `a41d8d5` | open | conflicts: fork-only `swprintf.c` in same commit: split | test `flag_grouping` (conflicts: fork swprintf test) | 2 | S |
| L23 | scanf `%i`/`%x` rejected a signed `0x` prefix | a | `5020478` | open (not re-read) | conflicts with upstream scanf rework; re-check then re-derive | host harness 20 diffs → 0 | 3 | S |
| L24 | scanf returned EOF on a matching failure | a | `01f74b0` | **fixed upstream** `b336093` | n/a | — | drop | — |
| L25 | `fopen` mode parsing (over-read on "", `t`, `x`, `e`) | a | `eb60be1`, `cbe4946` | open | conflicts: same commit adds `memmem`/`getsubopt`: split | libXfont2 workaround removed | 2 | S |
| L26 | `*at` wrappers used `PATH_MAX` stack arrays | a | `22b2c5a`, `fdfbfff` | fork-only (`unistd/at.c` not upstream) | n/a | — | not applicable | — |
| L27 | `fchdir()` stub returned 0 without changing directory | a | `1d86e24`, `2e46a0a` | open (upstream `posix/stubs.c` still `return 0`) | `1d86e24` (fail with ENOSYS) clean; `2e46a0a` needs a fork kernel syscall: split | gnulib `unlinkat` deleted in the wrong directory | 2 | S |
| L28 | `umask()` not applied | a | `c6cec41` | **fixed upstream** (`__getumask()` in open/mkdir) | n/a | — | drop | — |
| L29 | `unlink("dir")` succeeded | d | `ae9801e` | open; our fix is a client-side `lstat()` workaround; the right fix is in the fs servers | n/a | — | report | — |
| L30 | `rename()` failed `EEXIST` onto an existing destination | a | `c01f7a7` | open | clean; compiles | save-via-temp idiom | 3 | S |
| L31 | GNU `basename()` wrote into a read-only string | a | `2b8dff5` | open | clean; compiles | `xedit` start crash | 2 | S |
| L32 | `fstatvfs(-1)` garbage; `statvfs("file/")` success | a | `f09da71` | partly upstream (trailing slash via `resolve_path`, `770aceb`); EBADF part open | clean; compiles; drop the trailing-slash half | — | 3 | S |
| L33 | `sysconf(_SC_OPEN_MAX)` 512 vs kernel 1024 | a | `4c94672` | known upstream (`FIXME: issue #1610` in `conf.c`) | n/a | — | drop | — |
| L34 | `pthread_self()` NULL in a forked child | a | `f36de2b`, `d67a879` | open | applies, does not compile: needs fork pthread retire list; re-derive | test (tests `a43e7fa`) | 2 | M |
| L35 | NULL signal handler branch-called | a | `da69de7` | **superseded upstream**: signal dispatch moved to the kernel, `SIG_DFL` is 0 | n/a | — | drop | — |
| L36 | `main()` got garbage `envp` | a | `a59c800` | open (`crt0-common.c` still `main(argc, argv)`) | clean; compiles | GNU bash fault | 2 | S |
| L37 | atexit index ran off its arrays | a | `e643fa5`, `8dc40bb` | **superseded upstream** (atexit rewrite, `1e3ae62`) | n/a | — | drop | — |
| L38 | termios `c_oflag` constants not visible to the preprocessor | a | `e9bb8c4` | open | clean (header only) | xterm staircase | 2 | S |
| L39 | `gai_strerror()` answered "Unknown error" for every code | a | `40e3a55` | open | conflicts in `string/Makefile` only (fork strerror change) | test (tests `de21235`, clean) 11/0 | 2 | S |
| L40 | `<assert.h>` re-includable; `<sys/wait.h>` under `-ansi` | a | `26317c2`, `3a74c04` | assert open; `sys/wait.h` fixed upstream (`__INLINE`) | conflicts on the `sys/wait.h` half: send `26317c2` alone | — | 3 | S |
| L41 | libm poles (`asinh(-0)`, `log1p(-1)`, `scalbln` overflow) | a | `7b22fa3`, `7ca437b` | open (not re-read) | conflicts: fork-only libm files in the series | glibc comparison | 3 | S |
| L42 | `wcstombs()` truncates wide chars > 0xff | a | `b740469` | open (not re-read) | conflicts (fork wchar additions) | — | 3 | S |
| L43 | Default condvar clock was `CLOCK_MONOTONIC` | a | `2ff5fe2` | open | conflicts (fork pthread) | 56-year waits in CPython, libstdc++ | 2 | M |
| L44 | Lock-free `pthread_getspecific()` | c | `b33a8c7` | open | conflicts | JetStream 31 → 77 | discussion | M |
| L45 | `pthread_join` double unlock; retire race; detached cancel leak; sem deadline | a | `ad2af81`, `1a7753d` | open | conflicts | host tests | 3 | M |
| L46 | libm replaced with FreeBSD msun for the transcendental functions | c | `6bb0bdc`, `6bf27ab` | open | conflicts (many fork libm files) | ≤ 2.75 ULP vs MPFR | discussion | L |
| L47 | Far-future deadline of an unnamed semaphore | a | `203a347` | fork-only file (`pthread/sem_unnamed.c`) | n/a | — | not applicable | — |

### phoenix-rtos-filesystems

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| F01 | ext2 block/inode allocators unlocked (per-fs mutex) | a | `463aec1` | open | conflicts: upstream changed the write API (`O_APPEND`, new offset); small | deterministic Data Abort under concurrent writes | 2 | S |
| F02 | ext2 block offsets overflowed 32 bits (> 4 GiB filesystems) | a | `7618a7d` | open | clean; compiles | host harness + e2fsck | 1 | S |
| F03 | ext2 block↔bitmap off-by-one for blocks > 1 KiB | a | `505c4a3` | open | applies after F02 | host harness | 1 (with F02) | S |
| F04 | ext2 sparse-file series: truncate underflow, data leak, stale `ind[]`, lost run block, `setattr` on a directory, `objs_destroy` UAF, `i_blocks`, two bounds-checks-after-use (11 commits) | a | `1ef6f56` … `d46ba88` (11) | open, except `fd5b7d4` = **upstream `aeb7e3c`** (#1418) | applies as a series without `fd5b7d4`; compiles | host harness ~182 000 random ops, e2fsck clean | 2 | M |
| F05 | ext2 ignored `mtSync`: `fsync()` returned success, flushed nothing | a | `017eb3a` | open | clean; compiles | host harness | 1 | S |
| F06 | `ext2_statfs()` returned a heap pointer as `f_fsid` | a | `47634da` | open | clean; compiles | — | 2 | S |
| F07 | ext2 allocator skips full block groups without reading bitmaps | c | `14ce324` | open | clean; compiles | fewer reads per allocation | 3 | S |
| F08 | ext2 superblock free counts written on close/sync/unmount | a | `391da7e` | open | depends on F01 and the fork readdir change `0a9a12e` | e2fsck count mismatches | 3 | M |
| F09 | ext2: keep a run's first physical block | c | `e180cc6` | open | applies in the series | — | 3 | S |
| F10 | ext2 bitmap written before the block pointer (crash ordering) | a | `40807bc` | open | depends on F08 | — | 3 | M |
| F11 | dummyfs `link`/`unlink` timestamps | a | `8cf0a25` | open | clean; compiles | test `stat_dir_times` (tests `99b024e`, registration conflict) | 2 | S |
| F12 | libnfs 6.0.2 `readlink` phantom success | d | `fc2f62b` | fork-only NFS server; the defect is in libnfs | n/a | — | report to libnfs | — |
| F13 | ext2: unmount deleted cached files; unlink did not free blocks; hole read as the superblock | a | `269306a`, `8a35d91`, `62d513a` | open | clean; compiles | host harness | 2 (with F04) | S |

### phoenix-rtos-corelibs

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| C01 | libcache wrote back lines whose contents did not change | c | `a46399c` | open | clean; compiles | 12.6× on partial-line rewrites | 3 | S |
| C02 | libcache writes back only the dirty part of a line | c | `a9fdd59` | open; upstream reworked libcache since | conflicts with 6 upstream libcache commits | — | re-evaluate | M |

### phoenix-rtos-devices

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| D01 | libtty `FIONREAD` returned `-EINVAL` | a | `b247643a` | open | trivial conflict (upstream ioctl locking); re-derive from current fork code | readline/bash | 2 | S |
| D02 | umass frees the fs context while another pool thread is in it (`mtUmount` without drain) | a | `c6aa4f3a`, `4b64bc97`, `8cdd9275`, `7315705b` | open (upstream `umass_poolthr` unchanged) | conflicts (long fork umass history) | by code reading + fork drain | issue first, then 3 | M |
| D03 | umass Bulk-Only reset recovery; error after retries; failure limit | a | `cf83ee6f` | open | conflicts (fork umass); needs U06 | `test-usb-dmapressure` 1 GiB, 0 bad | 3 | M |
| D04 | umass node for a stick without a partition table | c | `8aac80e8` | open | conflicts (fork umass) | — | 3 | S |
| D05 | usbmouse report protocol (wheel) with a HID report parser | c | `b652f36b`, `a7ff1221` | open | conflicts; new files | host test 281 322 checks | 3 | M |
| D06 | bcm2711-emmc programmed a CPU address where a bus address was needed | d | `e9d465e8`, `876e9605` | Pi driver; the lesson (check `dma-ranges` per master) is general | n/a | — | mention only | — |
| D07 | `storage_read()` crossing the device end returns `-EINVAL` (zynq7000-sdcard, zynq-flash) | d | `822e9337` | open; our fix is in the Pi driver only | n/a | `dd` of a whole partition fails | report | — |

### phoenix-rtos-usb

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| U01 | usb hub: TRSTRCY reset-recovery delay (USB 2.0 §7.1.7.5) | a | `47eede9` | open | clean; compiles | Address Device timeouts | 2 | S |
| U02 | usb hub: retry-forever enumeration; give-up counter never cleared | a | `03bd903`, `3c7fdb2` | open | conflicts (fork hub initial-scan work) | ~85 resets → board reboot | 2 | M |
| U03 | usb/mem: `dc civac` over new uncached DMA pages | d | `12c4fe8` | right fix is K01 (kernel); inline aarch64 asm in generic code | not offered | — | not offered | — |
| U04 | usb/dev: control buffer overflow into the DMA pool chunk header | a | `7259b26` | open | clean; compiles | pool corruption | 2 | S |
| U05 | usb/dev: unbounded `sprintf` of the device symlink path | a | `e0911ce` | open | clean; compiles | — | 2 | S |
| U06 | libusb `usb_clearFeatureHalt()` addressed to the device, not the endpoint | a | `00e9ac4` | open | clean; compiles | device STALLs; needed by D03 | 1 | S |
| U07 | usb/mem: DMA page pool reserved up front | c | `716f924` | open | conflicts: depends on Pi "below 3 GiB" change: split | — | 3 | M |
| U08 | usb hub: acknowledge suspend and over-current changes | a | `182d146` | open | clean; compiles | port re-reported every 100 ms | 2 | S |

### phoenix-rtos-posixsrv

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| P01 | `/dev/urandom` read never advanced its destination (> 64 B reads) | a | `ef6e39b` | open | clean, but the commit also adds `/dev/hwrng` sourcing: split | — | 2 | S |
| P02 | `tmpfile()` dead after `/` is remounted | a | `8a44ce8` | open | clean; compiles | — | 3 | S |
| P03 | pipes/ptys never woke `poll()` (20 ms polling) | a | `6432961`, `d2a3b7a`, `fb46fa8`, `7e6cf77` | open | conflicts | WebKit IPC 23 ms → 2 ms | 3 | M |
| P04 | Timed requests: use-after-free, lock inversion, backwards timeout tree | a | `6c3ef06`, `90e0d88`, `7ed8949`, `73d7d34` | possibly addressed upstream (`a6c1675`, `4178dd3`); judged from the commit subjects, not re-read | conflicts | `tools/posixsrv-hosttest` | re-evaluate | M |

### phoenix-rtos-lwip

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| W01 | lwip sockets: `FIONBIO` ignored, `getnameinfo` overflow, `getifaddrs` incomplete | a | `7428162` | open | clean; compiles (upstream `port/sockets.c` does not build with GCC 16 `-Werror` without it) | — | 2 | S |
| W02 | `socket(AF_INET6)` returned an IPv4 socket when built without IPv6 | a | `d080252`, `83a56d5` | open | clean (with W01); compiles | test `test-libc-inet-socket` 8/0 | 2 | S |
| W03 | One lwIP server thread per socket serialised calls (worker pool) | c | `0c364ff`, `1179c5e` | open | conflicts | CPython loopback deadlock | discussion | L |

### plo

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| B01 | plo: ELF loader did not zero `.bss` (`p_memsz > p_filesz`) | a | `9551073` | open | clean; compiles | kernel started with garbage in `.bss` after power-off | 1 | S |
| B02 | plo `poison` debug command | c | `4509be9`, `3818c83` | open | clean (new file) | L33 runs 10/10 | later | S |
| B03 | plo aarch64 sign-compare | a | `98418d1` | fork-only file (`hal/aarch64/generic/hal.c`) | n/a | — | not applicable | — |

### phoenix-rtos-ports

| id | defect / change | kind | our SHA(s) | upstream status | applies to upstream `master` | test / evidence | order | effort |
|---|---|---|---|---|---|---|---|---|
| R01 | jq 1.8.2 recipe installs into the build host's `/usr/local/lib` | d | — | open | n/a | adoption build stopped | report | — |

### phoenix-rtos-tests (tests that go with the fixes)

These were cherry-picked onto upstream `phoenix-rtos-tests` `master`. "Registration conflict" means only the `libc/Makefile`, `libc/test.yaml` or `main.c` list of test groups conflicts, because the fork has more groups. That is trivial to resolve.

| test | for | our SHA(s) | applies |
|---|---|---|---|
| `string` strncmp/strncasecmp past-n | L01 | `a82e0e4` | clean |
| `misc` ctype single evaluation | L19 | `0bdaa1a` | clean |
| `mem/test_mprotect` unaligned length | K28 | `fbfc47e` | clean |
| `string` gai_strerror | L39 | `de21235` | clean |
| `stdlib_exit closed_std_streams` | L20 | `c4b23a8` | clean |
| `setvbuf` | L14 | `6e7a7f6` | clean |
| `pthread_tsd_alloc` (new group) | L11 | `23593b1` | clean |
| `usb/test_usb_dmapressure` (new) | D03, U07 | `1337c4c`, `7689d7f`, `1165567` | clean |
| `sigaction resethand_*` + pthread-fork | K17, L34 | `a43e7fa` | registration conflict; split per fix |
| `flag_grouping` | L22 | `fba1f8e` | conflicts: the fork-only swprintf test is in the same commit |
| `time_tz` | L21 | `cc09cff` | new file; registration conflict |
| `sa_restart` | L10 | `ae146ef` | new group; registration conflict |
| `stat_dir_times` | F11 | `99b024e` | registration conflict |
| `meminfo_anonsz_of_split_entries` | K14 | `413ac20` | conflicts (fork mprotect tests) |
| `socketpair_nonblock` | K18 | `90265ab` | registration conflict |
| `fd_per_message`, `message_4096` | K10, K11 | `f4fb172` | registration conflict |
| `faultsig` group (7 cases) | K33, K34 | `f8ad4dc`, `a781ea9`, `b6e640a` | registration conflict in `libc/signal/main.c` |
| `kernelhard` (FP across exec, buffer across a hole, signal frame) | K36, K37 (the hole case also covers upstream's `map_belongs` fix) | `decd769`, `8855cd9` | registration conflict |

The fork's host harnesses (`tools/*-hosttest/` in this coordination repo) are the strongest evidence for several fixes. They are listed here as a later contribution: maintainers may want some of them in `phoenix-rtos-tests` or in CI.

| harness | backs |
|---|---|
| `libext2-hosttest` | ext2 |
| `pagealloc-hosttest` | K31 |
| `posixsrv-hosttest` | P03, P04 |
| `libstring` | L01 |
| `libfmt` | L22 |
| `libscanf` | L23 |
| `libtime` | L21 |
| `libm` | L41, L46 |
| `libcache` | C01 |
| `usbmouse` | D05 |

## 6. Series and splits in detail

**ext2 (F01 to F13).** The fork has 36 ext2 commits on top of upstream. Each was cherry-picked in order onto `origin/master`, and the result was:

- **31 apply in sequence.** Dropping the temporary instrumentation pair `ef8d8ba`/`00663dd` leaves 29, and those compile. `9e1ff6a` + `ba9149a` (a change and its back-out) and `786aacd` (removes a stale marker) also need squashing.
- **`fd5b7d4` is already upstream** as `aeb7e3c` (julianuziemblo, 2026-10-08, "fix block sync when a hole is encountered", project issue #1418), so it is skipped.
- **`463aec1` (F01) conflicts** with upstream's write-API change (`88669e3`, `b7bcfc9`), so the lock must be re-derived.
- **`0a9a12e`** (readdir positions, fork-only design), **`391da7e`** (F08) and **`40807bc`** (F10) depend on F01 and on `0a9a12e`.

Suggested PRs:

1. F02 + F03: 64-bit offsets, bitmap off-by-one (first batch).
2. F05 `mtSync` (first batch).
3. **F13:**
   - `269306a`: unmount deleted cached files;
   - `8a35d91`: unlink did not free blocks;
   - `62d513a`: a hole read as the superblock.
4. **The truncate/indirect-block group:**
   - `fd4379a` and `1ef6f56` (`i_blocks` accounting);
   - `282a46f`, `244f1cf` and `844198a` (indirect blocks during truncate);
   - `680c9b2` (truncate underflow across holes);
   - `9a34339` (data leak from the last partial block).
5. **The write-back group:**
   - `c49d0e2` (stale `ind[]` write-back);
   - `74823c9` (run after a discontiguity);
   - `fcfb121` (out-of-range block numbers).
6. **Directories and lifetime:**
   - `9e1ff6a` + `ba9149a`, squashed (link counts on disk);
   - `d4ff499` (`fs->root` dangling);
   - `d46ba88` (`setattr` on a directory);
   - `ccd11e4` (live inode freed).
7. F06 `f_fsid`.
8. F01 lock (re-derived), then F08 and F10.
9. **Performance, offered separately:**
   - `ea33d31`, `38ddfb7`: coalesced reads;
   - F07 `14ce324`: skip full groups;
   - F09 `e180cc6`: keep a run's first physical block.

Each PR quotes the host-harness run: libext2 against a `mke2fs` image with `e2fsck` as the oracle.

**Other splits:**

- **K24 zone hardening.** Send `b516c8a4`, `8522b1db`, `1ff99ec4` and `ec29e159` without the optional trace ring of `521320e9`. Keep the `== 1U` → `<= 1U` replenish fix as its own commit.
- **P01 (`ef6e39b`).** Only the `memcpy` destination advance is a bug fix. Hardware-RNG sourcing is a separate enhancement; upstream may prefer a generic entropy-source hook instead.
- **L25.** Send `eb60be1` and the mode-parsing part of `cbe4946`. `memmem()` and `getsubopt()` are a separate enhancement PR.
- **L27.** Send `1d86e24` now (make `fchdir()` fail with `ENOSYS`; upstream's `posix/stubs.c` still returns 0). The real `fchdir()` (`2e46a0a`) needs the kernel to record each descriptor's path, which is a kernel design discussion.
- **K20 (dead server).** It applies textually, but `process.c` uses `borrowed` from the vfork series (K05). It also edits `proc/msg-nommu.c`, which needs a NOMMU build (`armv7m7-imxrt106x`, for example) to verify. Send K05 and K20 as one issue with two PRs.

## 7. Draft issues

File these in **`phoenix-rtos/phoenix-rtos-project`**. Upstream commits reference that repo's issues (`Fixes: phoenix-rtos/phoenix-rtos-project#1418`, and `FIXME: issue #1610` in libphoenix). Each draft below was checked by reading the upstream `master` code on 2026-11-19, except where it says otherwise.

### Issue 1: umass frees the filesystem context while another pool thread is inside it

- **Affected code:** `phoenix-rtos-devices/storage/umass/umass.c`: `umass_poolthr()`, `umass_fsthr()`, with `UMASS_N_POOL_THREADS = 2`.
- **Root cause:**
  - Two pool threads pop one shared `rqueue`. On `mtUmount` a pool thread calls `req->part->fs->unmount(req->part->fdata)` (for ext2, `libext2_unmount()` → `free(fs)`) and sets `fs`/`fdata` to NULL.
  - Nothing waits for requests already dequeued by the other pool thread. The FIFO orders dequeue, not completion, so the other thread can still be inside `fs->handler()` holding `fs->lock`. It then uses freed memory and unlocks a destroyed mutex.
  - A request dequeued after the unmount dereferences `part->fs == NULL`.
  - `libstorage` already has the correct pattern: `storage_umountfs()` → `requestctx_stop()` waits for `nreqs == 0`, with `nreqs` bracketing the handler call.
- **Reproducer:** mount ext2 from a USB stick, run a writer and a reader on two files, and unmount while they run. A debug allocator that poisons freed memory makes the failure deterministic.
- **Suggested fix:** give umass the same drain as libstorage. Count a request in when it is dequeued, and out after `msgRespond`. On `mtUmount`, stop accepting requests and wait for the count to reach zero before calling `unmount`. Answer requests that arrive after the unmount with `-ENODEV` instead of calling through a NULL `fs`. (Our version is fork commits `c6aa4f3a`, `4b64bc97`, `8cdd9275` and `7315705b`. They sit on top of fork-only umass changes, so the PR would be re-derived.)

### Issue 2: `open()` and socket construction publish a half-built `open_file_t`

- **Affected code:** `phoenix-rtos-kernel/posix/posix.c`: `posix_open()`, `posix_newFile()`, `posix_socket()`, `posix_socketpair()`, `posix_accept4()`.
- **Root cause:**
  - The fd slot is reserved by storing the new `open_file_t` in `p->fds[fd].file` *before* it is initialised. `p->lock` is then dropped across blocking IPCs (lookup, open, create; `accept4` blocks for a whole connection).
  - Another thread's `close(fd)`, or an exec `CLOEXEC` sweep, sees an uninitialised `refs`/`oid`/`type`. It decrements the garbage refcount and frees the file under its constructor.
  - The constructor's error path then frees it again. One block lands on the kernel zone free list twice, and the corruption surfaces later at an unrelated allocation.
- **Reproducer:** one thread loops `accept4()` on a listening socket while a second thread closes the fd numbers the first will get. A kernel built with zone free-list validation (our K24) reports the double free.
- **Suggested fix:**
  - Hold a construction reference (`refs = 2`: one for the slot, one for the constructor).
  - Mark the file as being under construction, so no path treats it as a regular file, and give it an oid that resolves to no port.
  - Re-check that the slot still holds this file before writing flags or returning the descriptor.
  - Our series: `e88c8b75`, `19dcbd5d`, `d3862861`, `c3f60f1b`, `7ac1bd10`, `39135453`. It conflicts with upstream's recent `posix.c` changes, so it would be re-derived after agreement.
- **Related, same file:** `p->lock` is held across the blocking `proc_close` in the exit/exec fd sweeps (K25, `2c489b75`). `posix_ioctl()` copies an uninitialised kernel-stack `msg_t` back to userspace for an `IOC_OUT` request the server does not answer (K26, `31045b7f`).

### Issue 3: clients of a dead server hang forever, and message ids are reused at once

- **Affected code:** `phoenix-rtos-kernel/proc/msg.c`, `proc/ports.c`, `proc/process.c`.
- **Root cause:**
  - **Pending requests are never failed.** When a server process dies, nothing walks its ports. A request the server had received leaves the sender asleep with no timeout. A queued request is rejected only when somebody calls `msgRecv` on the port again, and a dead server never does. Every client of a crashed driver wedges, and cannot even finish exiting.
  - **Message ids are reused at once.** `proc_portRidAlloc()` allocates the lowest free rid (`lib_idtreeAlloc(&p->rid, …, 0)`), so a late `msgRespond` can answer a different request.
  - **Two latent use-after-frees appear once those senders are woken.** `proc_portDestroy` drops the owner's reference but leaves the port on the owner's list. The final `port_put` locks `p->owner` after the owner is freed.
- **Reproducer:** a client sends to a server that has received the request and then calls `exit()`. The client never returns. Our probe is `tools/gpu-lane/portdeath/` in the fork: it hangs on baseline and passes with the fix.
- **Suggested fix:** on owner exit, fail every received and queued request with `-EINVAL`. Allocate rids in rotation per port. Fix the two reference-count bugs. Our series is `ac5ee1a6`, `9ce228c7`, `ee437693` and `3adc9950`. It needs the vfork-lifetime change (Issue 3b) and a NOMMU build check of `msg-nommu.c`.
- **3b, same area (separate issue).** In the vfork window the child's kernel stack *is* the parent's, and an asynchronous death frees a running kernel stack and destroys a borrowed map (K05). `thread_t.execkstack` is never initialised in `proc_threadCreate`, which silently disables the kernel-stack canary for every thread that never vforks. A dying sender also cannot abandon a request a server has received (K16).

### Issue 4: signal-return hardening (aarch64) — to be reported privately

A security-relevant defect in the signal-return path. Details are kept out of this public document and will be
shared with the maintainers privately, through the contact the owner establishes. The fork's fix is kernel
`7158b7ad` + `9276ba68` + `2f5e1e62`; the test is `libc/signal/faultsig.c` (`sigreturn_*` cases).

### Issue 5: a read that crosses the end of a storage device returns `-EINVAL` (zynq drivers)

- **Affected code:** `phoenix-rtos-devices`: `storage/zynq7000-sdcard/sdstorage_srv.c` (read path) and `storage/zynq-flash/zynq-flash.c` (lines 47 and 77). They have the same `(offs + len) > size → -EINVAL` check our Pi SD driver had.
- **Root cause:** POSIX `read()` at the end of a device returns 0. A read that crosses the end returns the bytes that are there. Rejecting both means a device cannot be read to EOF: `dd if=/dev/<part> of=img` with no `count` aborts before the last partial block and prints no summary. `e2fsck` of the result then reports a superblock/partition-size mismatch, which looks like corrupt media.
- **Reproducer:** a partition whose size is not a multiple of the `dd` block size: `dd if=/dev/mmcblk0p2 of=/tmp/img bs=1M`.
- **Suggested fix:** return 0 at or past the end, and clamp `len` to what remains. The write side is a design question: a short write, or `ENOSPC`. We changed only our own driver (`822e9337`, a Pi driver), so this is report-only.

### Issue 6 (ports): jq 1.8.2 recipe installs into the build host's `/usr/local/lib`

- **Affected code:** `phoenix-rtos-ports/jq/port.def.sh` (1.8.2).
- **Root cause:** `./configure` runs without `--prefix`, so jq's bundled oniguruma subproject keeps the default prefix `/usr/local`. Its install step writes the target's `libonig` into the **host's** `/usr/local/lib`, which fails without root and pollutes the host if run with root. Our adoption build stopped there on 2026-11-18. We went back to our 1.7.1 recipe; the follow-up was 1.8.2 with our install scheme (ports `d5e26ae`).
- **Reproducer:** build the `jq` port as a non-root user on a clean host and watch the oniguruma `make install` attempt `/usr/local/lib`.
- **Suggested fix:**
  - pass `--prefix` pointing into the build tree;
  - or `--with-oniguruma=builtin` and build without the subproject install;
  - or install only the `jq` binary, as the recipe already intends with `make install-binaries`.
- Confirm the exact make target on a clean upstream build before filing.

## 8. How to prepare a PR

1. **Branch from `origin/master` in a worktree,** never in the `sources/` checkouts:
   ```
   git -C sources/<repo> fetch origin
   git -C sources/<repo> worktree add -b up/<topic> ../wt/up-<topic> origin/master
   ```
   Push only to the owner's GitHub fork, then open the PR from there.
2. **Cherry-pick** with `git cherry-pick -x <sha>` (or the series). Where this plan says "re-derive", start from the fork's *current* code at that spot, not the original commit. The fork has since merged upstream, so the current code is closer to what upstream has.
3. **Adapt to upstream conventions.** These were read from `phoenix-rtos-doc/coding/index.md`, the PR template and the upstream log.
   - **Subject:** `subsystem/file: imperative summary`, lower case, no trailing period. Examples: `vm/map.c: fix map_belongs`, `posix/usocket: support MSG_TRUNC input flag`, `ext2/block: fix block sync when a hole is encountered`. A leading `!` marks a breaking change: `!pthread: return EINVAL from …`.
   - **Body:** a short explanation of the defect and the fix. Upstream trailers are `YT: RTOS-NNNN` / `TASK: RTOS-NNNN` (internal tracker, which we do not have; leave out), `Fixes: phoenix-rtos/phoenix-rtos-project#NNNN` when an issue exists, and `Co-authored-by:`.
   - **Rewrite our messages.** Remove internal references (`#121`, `TD-xx`, `L33`, `C1`, "build 45", "Pi-verified"), Pi symptoms used as the only motivation, and "Not hardware-tested" notes. Replace those with the target you tested on. Shorten long commentary in code comments; upstream comments are brief.
   - **`Co-Authored-By` trailer for the AI model:** whether to keep it is the owner's call. Upstream has no rule on it.
   - **Code style:** tabs, K&R braces with the function brace on its own line, MISRA-leaning rules (`U` suffixes on unsigned constants, `(void)` on ignored returns, explicit comparisons). Upstream's lint CI runs `clang-format-diff-18` on the diff against each repo's `.clang-format`. clang-format is not installed on this host, so run it in the upstream Docker image or install `clang-format-18`.
   - **License headers:** upstream has moved from `%LICENSE%` to `SPDX-License-Identifier: BSD-3-Clause` (libphoenix `442f7e0`; 530 kernel files use SPDX, 3 still use `%LICENSE%`). New files use the SPDX form and the header layout in `phoenix-rtos-doc/coding/index.md`.
4. **Test** on an upstream target, not only on the Pi:
   - `ia32-generic-qemu` for generic changes;
   - `aarch64a53-zynqmp-qemu` for aarch64 changes;
   - a NOMMU target build (for example `armv7m7-imxrt106x-evk`) when `msg-nommu.c` or other NOMMU paths are touched.
   - Upstream CI builds all of these (`phoenix-rtos-project/.github/workflows/ci-project.yml`) and runs the `phoenix-rtos-tests` suite on the QEMU targets. Add the matching test commit from section 5 as a separate PR to `phoenix-rtos-tests` and link the two.
5. **Fill in the PR template** (`.github/PULL_REQUEST_TEMPLATE.md` in each repo): description, motivation, type of change, how it was tested (targets), and the checklist.
6. **Clean up:** remove the worktree after the PR is merged or closed, and keep the fork's own history unchanged. Once a PR is merged, take upstream's version at the next sync and drop ours.

## 9. Licensing

- Every candidate modifies files that already carry the repos' licence: BSD-3-Clause (upstream's `LICENSE` files, and the `%LICENSE%` → SPDX headers). Contributions go under the same licence.
- **No CLA or DCO** was found. There is no `CONTRIBUTING.md` in the kernel, libphoenix or project repos, and neither the README nor the PR template mentions one. Only 7 of 460 kernel commits since 2025 carry `Signed-off-by`, so sign-off is not required. The owner should still ask the maintainers whether a contributor agreement is expected before the first PR.
- One enhancement brings in third-party code under another licence, and needs a licence note in the PR: **L46, msun libm**, under the FreeBSD/Sun licences (BSD-style, compatible). The TZ parser (L21) is our own code.
- Nothing in this plan contains GPL code. The fork's GPL-licensed game glue is in ports and is out of scope.

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

# Condition-variable clock census (P17), 2026-09-29

**Question.** libphoenix creates a condition variable that has no attributes on `CLOCK_MONOTONIC`
(`pthread/pthread.c:36`, `PTHREAD_COND_CLOCK_DEFAULT`). This is a local revert, `c283f2d`, of
upstream's POSIX default: the wall clock jumps 1970 → today during boot. Which code that we ship
computes an absolute `CLOCK_REALTIME` deadline and waits on such a condvar? After the clock step,
that wait lasts about 56 years.

**Method.** A read-only search, not a Pi measurement. It covered `external/mesa`, `tools/gpu-lane`,
the ports' glue and patches, libphoenix, the extracted port sources (python, xz, libevent,
ffmpeg, glib) and the toolchain's libstdc++ headers. Each site records the deadline clock, the
condvar's initialisation, and a verdict.

## libphoenix itself

- `pthread_cond_init(NULL)` and `PTHREAD_COND_INITIALIZER` both land on the MONOTONIC default
  (`pthread.c:36,158`). `pthread_cond_timedwait` uses the condvar's own clock (`pthread.c:1736`).
- `pthread_cond_clockwait` **exists** (`pthread.c:1726`, `include/pthread.h:298`).
- Safe: `pthread_mutex_timedlock` and the rwlock timed calls (REALTIME on both sides,
  `pthread.c:1208,2067,2268,2304`), and `semaphoreDown` (monotonic on both sides).
- Absent: `sem_timedwait` and `mq_*`.

## AFFECTED

| # | Code | Deadline | Condvar | Reach |
|---|---|---|---|---|
| 1 | **plain `sdl2` port** (SDL 2.30.12, `SDL_syscond.c:123`), patches 0001–0006 only | `clock_gettime(CLOCK_REALTIME)` | `pthread_cond_init(NULL)` | every `SDL_CondWaitTimeout` / generic `SDL_SemWaitTimeout` of the programs that link it (the old-stack games). Fixed only in `sdl2_kmsdrm` / `sdl2-drm` / `sdl2-wl` (patch 0011) |
| 2 | **Python 3.14 parking lot** (`Python/parking_lot.c:74,206`); `HAVE_SEM_TIMEDWAIT` undefined, so the condvar fallback is used | `PyTime_TimeRaw` (REALTIME) | `pthread_cond_init(NULL)` | timed `Lock.acquire(timeout)`, `Event.wait`, `Condition.wait`, `queue.get(timeout)`, `join(timeout)` |
| 3 | **libstdc++**: `_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT` undefined (`c++config.h:1835`), so `condition_variable::__clock_t` = `system_clock` | every `wait_for` / `wait_until`, including `steady_clock` ones (converted to `system_clock`); `std::future::wait_for` (`atomic_futex.h:296`) | default | **every C++ timed wait**. No `wait_for`/`wait_until` found in supertuxkart, dillo or fltk; labwc, weston, xfce and Mesa C++ not checked |
| 4 | Mesa v3dv, `v3dv_query.c:491–503` | `timespec_get(TIME_UTC)` + 2 s | `cnd_init` (`v3dv_device.c:2073`) | performance queries in `--vulkan` builds: a hang instead of a 2 s timeout |
| 5 | `tools/quakespasm-port/platform/pl_phoenix_vid.c:228–231` (legacy tool) | REALTIME + 100 ms | `PTHREAD_COND_INITIALIZER` | unclear whether it still ships (the ports' quakespasm glue does not contain it) |

**A related bug with a different cause:** the vkquake glue's `SDL_CondWaitTimeout`
(`ports/vkquake/glue/pl_phoenix_sdlcompat.c:187–207`, and a copy under `tools/vkquake-port/`)
passes `gettime() + ms*1000`, an **absolute** value, to the native `condWait`. `condWait` takes a
**relative** timeout (`proc/threads.c:2748`), so each wait lasts uptime + ms.

## SAFE

- Mesa `u_cnd_monotonic` (`setclock(MONOTONIC)`), wsi, `vk_sync_timeline`, `u_queue`, EGL.
- glib (`setclock(MONOTONIC)`, `HAVE_PTHREAD_CONDATTR_SETCLOCK`) and therefore GTK/XFCE timed
  waits.
- Python's own `thread_pthread.h` condvars, gtk-video, and SDL with patch 0011.
- Native `condWait` users with relative timeouts (ipcprobe, kmsprobe, v3d-async, hevc-decode).
- Not built here: lavapipe, llvmpipe, zink (`-Dllvm=disabled`).

UNCLEAR or dead code: xz's `mythread_cond_timedwait` (multithreaded liblzma only), libevent's
timed condvar (never called by the core), ffmpeg `udp.c` (`--disable-network`), jemalloc (not
built).

## What would fix it systemically

1. **Stop the clock jump before programs start** (KNOWN-ISSUES C6). Upstream's REALTIME default
   is then safe and `c283f2d` can go. This is the only fix that also covers code we cannot patch.
2. **libstdc++ with `_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT`**: libphoenix now has
   `pthread_cond_clockwait`, so a toolchain rebuild would let `steady_clock` waits use
   `CLOCK_MONOTONIC` explicitly. `system_clock` waits still need item 1 or a clockwait that
   converts between the two clocks.
3. **Per-program patches**, like SDL 0011: the plain `sdl2` port (copy 0011), and Python's
   parking lot (`pthread_condattr_setclock(CLOCK_MONOTONIC)` plus `PyTime_MonotonicRaw`).

## 2026-10-07: the fix, and the audit extended

**Fix** (libphoenix `cond-realtime-default` 2ff5fe2, not merged): the default clock is
`CLOCK_REALTIME` again, as POSIX and upstream `4ab9ad9`. A `CLOCK_REALTIME` wait, whether from the
default, `setclock(CLOCK_REALTIME)` or `pthread_cond_clockwait(CLOCK_REALTIME)`, is converted to
the monotonic clock. The offset is read with one `gettime()` at the call, and again at every
stale wake-up; the wait keeps the earlier of the two deadlines. Within one call:

- **A deadline already passed** returns `ETIMEDOUT` at once, with the mutex held. That includes
  a deadline computed before the boot-time step and used after it.
- **A backward step** never makes the wait longer than the time left to the deadline when the
  call began. The kernel already converts `REALTIME` deadlines at call time. The gap was that
  `pthread_cond_waitInternal` re-calls `futexWait` after every stale wake-up, and the kernel
  then converted again with the new offset, so the wait grew.
- **A forward step past the deadline** ends the wait at its next wake-up. Without one, the wait
  ends at the deadline as converted at the start; nothing is woken by `clock_settime()`.

This covers `c283f2d`'s stated reason, a deadline "meaningless across the jump". What it cannot
cover is a caller that passes a `CLOCK_MONOTONIC` deadline to a condvar whose clock it never set.
That now times out at once and spins, so the audit below looked for such callers. As a side fix,
deadlines too large to count in microseconds (C++'s `time_point::max()`) now saturate instead of
overflowing into the past.

The cause of the w38 symptoms `c283f2d` cited is not the default clock. SuperTuxKart's
`system_error "Invalid argument"` was the `phMutexLock` syscall-arity break
([doc](2026-09-16-upstream-mutex-abi-break.md)). The old vkQuake glue used native `condWait`,
which the pthread default never reaches. Both measurements were taken on a build with stale-ABI
binaries.

**Tests:** phoenix-rtos-tests `cond-realtime-default` e8678d5 adds
`libc/pthread/pthread_cond_clock.c`.

- Group `pthread_cond_clock` (12 cases) covers the default, static-initialised, attr-without-clock,
  `REALTIME` and `MONOTONIC` condvars, `clockwait` across clocks, `EINVAL`, past and 1970
  deadlines, a signal before the deadline, and far-future deadlines.
- Group `pthread_cond_clockstep` (3 cases) steps the clock ±10 s with `clock_settime`, so it runs
  only with `PH_TEST_CLOCKSTEP` set.
- Each timed wait runs in a watched thread. A hang is released after 3 s and reported as FAIL.

Host harness `tools/pthread-key-hosttest`, suite `cond`:

- It models `futexWait`/`futexWake` as the kernel does, and Phoenix's clock (monotonic plus a
  steppable offset).
- It runs the suite twice; the second run makes every sleeping futex wake spuriously every 20 ms.
- master fails 9 of 15 cases: every `REALTIME`-on-default case hangs.
- Upstream's plain default, without the conversion, fails 2: the far-future overflow, and the
  backward step once wake-ups are spurious.
- The fix passes 15 of 15 in both runs, also under TSan and ASan.

### Audit: who relied on the monotonic default

| Caller | Where | Deadline / condvar | Verdict |
|---|---|---|---|
| libstdc++ C++20 atomic timed waits (`counting_semaphore::try_acquire_for`, `__atomic_wait_address_until`) | gcc-16.2.0 `src/c++20/atomic.cc:605-616` | `steady_clock` epoch / `PTHREAD_COND_INITIALIZER` | **Relies on it — latent.** No caller in the image (wlroots/Mesa "binary_semaphore" hits are Vulkan). After the fix it busy-waits to the deadline: the result stays correct, CPU is wasted. Remedy: rebuild the toolchain. libstdc++'s configure check (`GLIBCXX_CHECK_PTHREAD_COND_CLOCKWAIT`, compile-only when cross-building) finds libphoenix's `pthread_cond_clockwait` and defines `_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT`. |
| libstdc++ `condition_variable(_any)`, `future::wait_for` | `condition_variable:76-172`, `atomic_futex.h:296` | `system_clock` / default | Broken before; **fixed by the change** |
| ANGLE `EGLReusableSync` (WebKit) | `EGLReusableSync.cpp:68` | via libstdc++ | Fixed by the change |
| Python 3.14 parking lot | `parking_lot.c:74,201-206` | `PyTime_TimeRaw` / NULL | Fixed by the change (C14) |
| Mesa v3dv perf queries | `v3dv_query.c:491-503` | `TIME_UTC` / `cnd_init` | Fixed by the change |
| OpenSSL thread pool | `crypto/thread/arch/thread_posix.c:161,192-199` | `gettimeofday` / NULL | Fixed by the change |
| libevent | `evthread_pthread.c:146-150` | `gettimeofday` / default | Fixed; the core never calls it |
| tests: cond timedwait, cond EINTR, pty watchdog | `libc/pthread/pthread_cond_test_functions.c:41`, `pthread_cond_eintr.c:74`, `libc/posixsrv/pty_timed.c:54-68` | `REALTIME` / default | Fixed. The pty watchdog could never fire before. |
| WebKit compat `sem_timedwait` | `webkit_wpe/files/compat/phoenix-jsc-compat.c:264-313` | `REALTIME` / NULL | Dead code (`PHX_COMPAT_SEM=0`) |
| libphoenix mutex/rwlock timed locks | `pthread.c:1721-1765`, `2902`, `3101-3145` | explicit clock | Safe |
| libphoenix `sem_timedwait`/`sem_clockwait` | `pthread/sem.c:147-220` | pipe + `poll` with a relative time | Safe (no condvar) |
| libphoenix `pthread_once`, barriers, `semaphoreDown`, `alarm` | `pthread.c`, `barrier.c`, `sys/semaphore.c`, `unistd/alarm.c` | untimed, or native cond with a monotonic attribute | Safe |
| posixsrv, lwip port | `posixsrv.c:446-483`, `port/sys_sync.h:29-43` | `setclock(MONOTONIC)` + monotonic | Safe |
| SDL patch 0011, WebKit patch 0021 | ports | `setclock` / `clockwait(MONOTONIC)` | Safe (confirmed) |
| Python locks and GIL, glib/GTK, dbus, Mesa EGL / `u_cnd_monotonic` / `u_queue` / wsi / `vk_sync_timeline` | port sources | `setclock(MONOTONIC)` | Safe |
| Native `condWait` users (devices, utils jitter benchmark) | — | relative, or created `PH_CLOCK_MONOTONIC` | Unaffected by the pthread default |

**Searched:**

- All of `sources/libphoenix`.
- The devices, filesystems, posixsrv, lwip, utils, usb, corelibs, tests, hostutils and kernel
  repositories.
- The ports' patches, glue, `files/` and `compat/`, including the tree before the old GPU stack
  was removed (`43212b6^`).
- Coord `tools/**`.
- All of `.buildroot/.../port-sources/*` for `pthread_cond_timedwait`.
- The toolchain's headers and `libstdc++.a`.

Not searched: `external/mesa`, the same tag as the shipped Mesa.

**Census corrections:**

- Rows 1 (plain `sdl2`) and 5 (quakespasm tool) are gone from the tree.
- The vkquake absolute-vs-relative `condWait` bug left with the deleted glue.
- The libpas scavenger is not built (WebKit uses mimalloc).
- libphoenix now has `sem_timedwait`.
- Python takes the condvar fallback because `_POSIX_SEMAPHORES` is undefined. It is not because
  `HAVE_SEM_TIMEDWAIT` is undefined: `pyconfig.h` has both `HAVE_SEM_TIMEDWAIT` and
  `HAVE_SEM_CLOCKWAIT`.

### `_POSIX_SEMAPHORES`: not yet

Defining it would move CPython (the only port it changes; bash's hit is an unbuilt example) onto
`sem_t`, in two places:

- The parking lot calls `sem_init`/`sem_destroy` **on every contended wait**.
- `thread_pthread.h` makes every `PyThread` lock a semaphore (`USE_SEMAPHORES`). That includes
  each buffered file's lock.

libphoenix's `sem_t` is a pipe through posixsrv. Each lock would therefore cost 2 fds and a
posixsrv round trip, a failed `sem_init` is `Py_FatalError`, and the pipe is shared across
`fork()`.

`sem_timedwait` has its own defect: two timed waiters and one post leave the loser blocked in
`read()` past its deadline.

The claim also needs `sem_open`/`sem_close`/`sem_unlink`, which master lacks (the upstream-sync
branch adds them). POSIX's `sem_init` page has no `ENOSYS` for `pshared`, so that is a documented
shortfall, not a sanctioned one.

The P17 fix makes the macro unnecessary for C14. Define it only after all of these:

1. Unnamed semaphores are rebuilt on the futex.
2. The upstream sync is merged.
3. `misc/posix_options.c` is updated.
4. `libc/semaphore` gains tests for `clockwait`, two timed waiters and `fork`.
5. CPython is force-rebuilt.

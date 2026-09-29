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

# `_POSIX_*` option macros: measured blast radius

The open decision (audit §5) was whether libphoenix should advertise the POSIX **option** macros it
can honestly claim. It was blocked on an unmeasured blast radius, and the inherited estimate — "SDL2,
CPython, glib2, gnulib, curl and Mesa all test those macros" — is **wrong**. Measured against the 58
extracted port source trees in `.buildroot/…/port-sources`:

| macro | files that test it | who |
|---|---|---|
| `_POSIX_MONOTONIC_CLOCK` | 7 | openssl (×2 versions), micropython (×3), redis (`configure.ac`), yquake2 |
| `_POSIX_THREADS` | 7 | CPython only |
| `_POSIX_TIMERS` | 3 | openssl (×2), micropython |
| `_POSIX_READER_WRITER_LOCKS` | 1 | re2, inside SuperTuxKart's shaderc |
| `_POSIX_THREAD_ATTR_STACKSIZE` | 1 | CPython |
| `_POSIX_PRIORITY_SCHEDULING` | **0** | — |
| `_POSIX_MEMORY_PROTECTION` | **0** | — |
| `_POSIX_FSYNC` | **0** | — |
| `_POSIX_CLOCK_SELECTION` | **0** | — |
| `_POSIX_THREAD_PRIORITY_SCHEDULING` | **0** | — |

**SDL2, glib2, coreutils/gnulib, curl and Mesa do not test any of them.**

## What would actually change

- **Half the macros are free.** Five have zero consumers: defining them changes nothing today and
  only helps future ports detect correctly.
- **`_POSIX_THREADS` is a no-op.** CPython already self-defines it —
  `Include/cpython/pthread_stubs.h:8` is `#ifndef _POSIX_THREADS / #define _POSIX_THREADS 1`, and its
  `config.log` confirms `#define _POSIX_THREADS 1`. The `#ifndef` guard means no redefinition
  warning either. It is CPython's only consumer, so this row is settled.
- **Two are fixes, of the exact bug fixed in yquake2 today.** micropython's `ticks_ms`/`ticks_us`
  fell through to `gettimeofday()`, and openssl's DRBG additional-data timer did the same. Defining
  the macros moves both to `CLOCK_MONOTONIC`.

  ⓘ **Correction, after reading the built object rather than the source.** The openssl row above
  named the wrong gate. The `#elif defined(_POSIX_MONOTONIC_CLOCK)` at `rand_unix.c:858` sits *inside*
  `#elif defined(OSSL_POSIX_TIMER_OKAY)`, and `OSSL_POSIX_TIMER_OKAY` is itself defined only under
  `#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0` (line 72). So before the change the **whole POSIX
  arm was dead**, not merely biased to realtime, and `get_timer_bits()` fell through to the
  `gettimeofday()` block below it. It is `_POSIX_TIMERS` that does the work here, with
  `_POSIX_MONOTONIC_CLOCK` then selecting the clock (`CLOCK_BOOTTIME` takes precedence but is not
  defined on Phoenix). Both macros were needed; either alone would have left it on the wall clock.

  Verified in the rebuilt `rand_unix.o`, not the preprocessor: `get_timer_bits` is inlined into
  `rand_pool_add_additional_data`, which now calls `clock_gettime` on the fall-through of
  `cbnz w0, …` after `OPENSSL_rdtsc` — i.e. exactly where `w0 == 0`, and `CLOCK_MONOTONIC` **is** 0 on
  Phoenix, so the compiler emitted no `mov w0, #0` at all. (A `mov w0, #0x2` does appear in this
  object, in `rand_pool_add_nonce_data`; that one is `get_time_stamp()` and wants `CLOCK_REALTIME`
  deliberately, because a nonce wants wall-clock uniqueness. Unchanged, and correctly so.)
- **yquake2 is already handled** — its `system.c` was the live instance found and fixed today
  (`4a166c4`, now unconditional), so the macro is a no-op for it.
- **redis tests it in `configure.ac`**, i.e. configure-time: it only takes effect on a reconfigure,
  which is the uneven-landing caveat in miniature.
- **re2 (inside STK's shaderc) switches to `pthread_rwlock`** —
  `util/mutex.h:24` sets `MUTEX_IS_PTHREAD_RWLOCK`. libphoenix implements the full `pthread_rwlock_*`
  set, so this is supported; it is a real code-path change in a shader-compile dependency.
- **⚠ The one behaviour change to watch: CPython thread stack size.**
  `Python/thread_pthread.h:37` gates `pthread_attr_setstacksize` on
  `_POSIX_THREAD_ATTR_STACKSIZE` — "The POSIX spec requires that use of pthread_attr_setstacksize be
  conditional on `_POSIX_THREAD_ATTR_STACKSIZE` being defined." Today CPython never sets a stack size
  and threads get libphoenix's default; defining the macro makes it set one (with its own viable-
  minimum clamp). Plausibly an improvement for deep recursion, but it is the row to test.

## Recommendation

**Do it**, and the risk is far lower than the log has been saying. Ordering that keeps it cheap:

1. Define the five zero-consumer macros first — provably inert today.
2. Then `_POSIX_MONOTONIC_CLOCK`, `_POSIX_TIMERS`, `_POSIX_THREADS`, `_POSIX_READER_WRITER_LOCKS`.
   Precondition already met: `clock_getres()` now exists (libphoenix `0604c8e`), which is what a caller
   seeing `_POSIX_TIMERS` is entitled to call.
3. `_POSIX_THREAD_ATTR_STACKSIZE` last and on its own, with CPython exercised afterwards.

Rebuild the affected ports (openssl, micropython, redis, CPython, STK) and re-run the 6/6 gate. Still
an owner call because it is a libc-contract change, but it is now a measured one.

# Toolchain / libc silent-feature audit

**Asked by the owner, 2026-09-09:** *"Can you check if any similar bugs which silently fail, could be
present in the toolchain? Any features which are OFF while should be ON on Phoenix?"* — then: *"If
there is a similar situation for the C language part (not C++) do the analogous verification as
well."*

**Short answer: yes, on both sides, and it is much bigger than `std::chrono`.** libstdc++ shipped with
~30 feature macros off that Phoenix supports, several of which produce *wrong answers* rather than
missing functionality. On the C side, three ports are affected by two systemic probe-poisoning
mechanisms. Everything below is evidence-backed; where I could not establish something I say so.

---

## 1. The defect class

A feature Phoenix supports is compiled out because a build-time probe silently answered "no". No
warning, no build failure; the code takes a degraded path at runtime. The instance we started from:
`std::chrono::steady_clock` fell back to `time()` — whole-second resolution — capping SuperTuxKart at
exactly 1 fps.

## 2. C++ / libstdc++: two causes, ~30 macros

### Cause 1 — every C++ probe fails to link (~30 checks)

From the libstdc++ build's `config.log`:

```
configure:52858: checking for struct dirent.d_type
configure:52902: aarch64-phoenix-g++ -o conftest -fno-exceptions conftest.cpp
/home/houp/.../bin/ld: cannot find -lstdc++: No such file or directory
```

Every libstdc++ feature probe is a `GCC_TRY_COMPILE_OR_LINK` run in C++ mode, and **libstdc++.a does
not exist yet when libstdc++ is configured**. So they all fail regardless of what the target
supports. Proof it is not a capability question: `glibcxx_cv_dirent_d_type=no` and
`glibcxx_cv_st_mtim=no` are pure struct-member facts needing no library symbol, and both members
exist (`phoenix/posix-stat.h:85`, `dirent.h:41-46`).

### Cause 2 — the clock probes fail to *compile*

```
configure:21932: checking for monotonic clock
conftest.cpp:72:43: error: 'tp' was not declared in this scope; did you mean 'tm'?
   72 |           clock_gettime(CLOCK_MONOTONIC, &tp);
```

The probe body is

```c
#if _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)
  timespec tp;
#endif
clock_gettime(CLOCK_MONOTONIC, &tp);
```

`_POSIX_TIMERS` is undefined, so the declaration is preprocessed away. **This is the C-side gap (§3)
causing the C++ bug.**

ⓘ For these three checks the compile error is what I *observed*; the missing `-lstdc++` of Cause 1
would also have stopped them, but a compile error comes first, so only Cause 2 is established here.
Either way both barriers are removed by the same fix.

### Why neither is fixable with a flag or a cache

The clock results live in plain shell variables (`ac_has_clock_monotonic`, `ac_has_clock_realtime`,
`ac_has_nanosleep`), not autoconf cache variables. An earlier attempt at `glibcxx_cv_*` overrides in
`build-toolchain.sh` was **inert** — a full overnight rebuild proved it: of the five chrono macros
only `_GLIBCXX_USE_SCHED_YIELD` flipped, and that by accident via `AC_SEARCH_LIBS`'s catch-all arm.

A third, smaller cause is worth recording: `GCC_CHECK_TLS`, `AC_CHECK_FUNCS(getentropy)`,
`aligned_alloc`, `timespec_get`, `__cxa_thread_atexit` and `secure_getenv` sit inside
`if $GLIBCXX_IS_NATIVE` (`configure.ac:276-300`) and so **never ran at all** in a cross build.
`crossconfig.m4`'s `*-linux*` stanza calls them explicitly to compensate; the `*-phoenix*` stanza did
not. That is why `_GLIBCXX_HAVE_TLS` is `#undef` even though `--enable-tls` is passed and the C
compiler emits real TLS (`__thread int x;` → `mrs x0, tpidr_el0`) — `--enable-tls` gates whether the
check *runs*, never its outcome (`if test "$enable_tls $gcc_cv_have_tls" = "yes yes"`).

### Two things pre-verified before the rebuild finishes

- `target_os='phoenix'` in the libstdc++ `config.log`, so the patch's `phoenix*)` arm matches (that
  `case` has no default arm, so a mismatch would have silently left all four clock macros off).
- A `printf "%s\n" "#define HAVE_X 1" >>confdefs.h` in the `*-phoenix*` stanza does survive to the
  installed header with the `_GLIBCXX_` prefix — `HAVE_HYPOT`, `HAVE_SINF`, `HAVE_STRTOF` and
  `HAVE_STRTOLD` are set exactly that way today and appear as `_GLIBCXX_HAVE_*` in `c++config.h`.

### The fix

`phoenix-rtos-build` 95d9fca adds `gcc-16.2.0-12-libstdcxx-phoenix-features.patch`, which hardcodes
the answers for `*-phoenix*` in the same two switch statements where rtems, freebsd, solaris and
openbsd already do exactly this. Every macro is confirmed twice: the symbol or struct member exists
in libphoenix, **and autoconf itself answers "yes" for it** when the same configure is re-run against
a sysroot that already has libstdc++.a. `--enable-libstdcxx-time` is dropped, because passing it
takes the branch that runs the broken link tests instead of the target case the patch extends.

✅ **VERIFIED on hardware, 2026-09-09 23:52.** Clean rebuild into `.toolchain-v2/`; the installed
`c++config.h` gained **23** macros install-to-install against `.toolchain-chrono` with **0
regressions** (`_GLIBCXX_HAVE_SLEEP`/`_USLEEP` went off, which is correct: `acinclude.m4` only probes
them in the `else` branch taken when nanosleep is absent). Then the same C++ probe program built with
both toolchains and run on the Pi:

| probe | old toolchain | v2 (fixed) |
|---|---|---|
| `steady_clock` tick | **1 000 000 000 ns** | **2 000 ns** — 500 000× finer |
| `sleep_for(20ms)`, by `steady_clock` | 1 000 000 µs | **20 023 µs** |
| …the same sleep, by C `clock_gettime` | **760 280 µs** — it really slept 0.76 s | **20 029 µs** |
| `thread::hardware_concurrency()` | 0 | **4** |
| `random_device` first draw | `3499211612` — mt19937's canonical first output for seed 5489 | varies per run |
| `filesystem::create_symlink` | errno 38 (ENOSYS) | **0** |
| `filesystem::is_symlink` | **false** | **true** |
| `filesystem::current_path()` | `''`, errno 38 | **`/`** |
| `clock_getres` | absent from libphoenix | rc=0, 1000 ns |

ⓘ One measurement trap worth recording: the probe first read `hardware_concurrency() == 0` even with
`_GLIBCXX_USE_SC_NPROCESSORS_ONLN 1`. That was the probe's own fault, not the toolchain's —
`libphoenix`'s `sysconf(_SC_NPROCESSORS_*)` is gated on `#if defined(__aarch64__) &&
defined(__CPU_GENERIC)` (`unistd/conf.c:60`), and the libphoenix *inside the toolchain sysroot* is
built without `__CPU_GENERIC`, so its `conf.o` has no `platformctl` reference at all. Relinking the
probe against `.buildroot/_build/aarch64a72-generic-rpi4b/sysroot/lib/libphoenix.a` — the one real
apps link — gives **4**. Anything measuring a libc-backed value must link the *buildroot* libphoenix,
not the toolchain's.

### What was off, ranked by consequence

**Wrong behaviour (silent):**

| macro | consequence |
|---|---|
| `_GLIBCXX_USE_LSTAT` (+ `HAVE_READLINK`) | `ops-common.h:186` aliases `lstat`→`stat`, so **`is_symlink()` is always false**; `canonical()` returns paths with symlink components unresolved *while clearing the error code*; `remove_all()` recurses into a symlinked directory's **target** |
| `HAVE_GETENTROPY` | `std::random_device` = mt19937 seeded with the constant `5489` — the same sequence in every process on every boot (`random.cc:74-86,547`) |
| `_GLIBCXX_USE_SC_NPROCESSORS_ONLN` | `std::thread::hardware_concurrency()` returns **0** on a 4-core part (`thread.cc:94,216`) |
| `_GLIBCXX_USE_ST_MTIM` | `last_write_time()` drops nanoseconds → 1-second resolution (`ops-common.h:264`) |
| `_GLIBCXX_USE_CLOCK_MONOTONIC` / `_CLOCK_REALTIME` / `_NANOSLEEP` / `_SCHED_YIELD` | the original chrono bug. Also: `yield()` a literal no-op, and `sleep_for` **busy-looped** through `usleep()` until the 1-second `steady_clock` tick advanced (`thread.cc:267,291`) |
| `HAVE_S_ISREG` | `in_avail()`/`readsome()` return 0 for a regular file — a `readsome()` loop spins forever |
| `_GLIBCXX_USE_GETCWD` | `current_path()` always fails (loud, but broken) |

**Performance only:** `HAVE_STRUCT_DIRENT_D_TYPE` (every `directory_entry` comes back
`file_type::none` → one extra `stat()` per entry, expensive over an NFS root), `HAVE_WRITEV`,
`_GLIBCXX_HAVE_TLS` (EH globals via a pthread key + a `malloc` on first throw per thread, instead of
`__thread`).

**Correctly off:** long-double math (no `*l` beyond `ceill`/`floorl`), fenv, the locale `*_l`/iconv
family, everything Linux/Windows/Darwin/Solaris/x86-specific, symbol versioning (static library),
`HAVE_DIRFD`, `USE_UTIMENSAT`, `HAVE_QUICK_EXIT`, the `pthread_*_clockwait` family.

**Not fixable, worth knowing:** `_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT` is off because
`pthread_cond_clockwait` genuinely does not exist, so `condition_variable::wait_for` measures against
`system_clock`. A forward wall-clock step (ntpclient) can end a wait early. Bounded: the generic
`wait_until` re-checks the caller's clock.

**`_GLIBCXX_USE_WCHAR_T` is a real hole and not cache-overridable** — `enable_wchar_t` is a plain
shell variable, and the probe needs `using ::<name>;` for ~50 wide functions. libphoenix's `wchar.h`
is missing 23 declarations plus `WCHAR_MIN`/`WCHAR_MAX`. Consequence: no `std::wstring` facets, no
`wostream`, no wide `fs::path` overloads. It fails *loudly* at compile time, so it is a gap, not a
silent bug. Left alone.

## 3. The C-side finding: `_POSIX_VERSION 200809L` with zero option macros

`<sysroot>/usr/include/unistd.h:65` declares POSIX.1-2008 conformance. The sysroot then defines 44
`_POSIX_*` macros — every one a *limit* (`_POSIX_PATH_MAX`, …) or `_POSIX_SPIN_LOCKS`. **Not one
option macro for the feature groups libphoenix implements.** The standard way to ask "does this
system have a monotonic clock" is `#if _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)`, and on
Phoenix that is always false.

| option macro | libphoenix support | verdict |
|---|---|---|
| `_POSIX_MONOTONIC_CLOCK` | real, monotonic `CLOCK_MONOTONIC` | **should be ON** |
| `_POSIX_CLOCK_SELECTION` | `clock_nanosleep` | **should be ON** |
| `_POSIX_THREADS` | full pthreads | **should be ON** |
| `_POSIX_THREAD_ATTR_STACKSIZE` / `_STACKADDR` | `pthread_attr_set{stacksize,stack}` | **should be ON** |
| `_POSIX_THREAD_PRIORITY_SCHEDULING` | `pthread_setschedparam` | **should be ON** |
| `_POSIX_PRIORITY_SCHEDULING` | `sched_setscheduler`, `sched_get_priority_max` | **should be ON** |
| `_POSIX_READER_WRITER_LOCKS` | `pthread_rwlock_*` complete | **should be ON** |
| `_POSIX_MEMORY_PROTECTION` | `mprotect` | **should be ON** |
| `_POSIX_FSYNC` | `fsync` | **should be ON** |
| `_POSIX_TIMERS` | clocks ✓ (incl. new `clock_getres`), no `timer_create` family | partial |
| `_POSIX_MAPPED_FILES` | `mmap`/`munmap` ✓, no `msync` | partial |
| `_POSIX_THREAD_SAFE_FUNCTIONS` | the `*_r` set except `readdir_r` | partial |
| `_POSIX_SEMAPHORES` | **none** — no `semaphore.h`, no `sem_*`; Phoenix has its own `semaphore_t` | correctly OFF |
| `_POSIX_BARRIERS`, `_POSIX_SPAWN`, `_POSIX_SHARED_MEMORY_OBJECTS`, `_POSIX_REALTIME_SIGNALS`, `_POSIX_MESSAGE_PASSING`, `_POSIX_CPUTIME` | no `pthread_barrier_*`, `posix_spawn`, `shm_open`, `sigqueue`, `mq_*`, `clock_getcpuclockid` | correctly OFF |

`clock_getres()` was **missing** and is now implemented (libphoenix `0604c8e`, test `8cbbcd6`,
resolution 1000 ns because `gettime()` reports microseconds). That was the blocker for claiming any of
the timer group: code that sees `_POSIX_TIMERS` may call it, so defining the macro without the
function would turn a silent fallback into a link error. Still absent and cheap: the `timer_*` family,
`msync`, `fdatasync`, `readdir_r`, `dirfd`.

### The live instance in our own code

`yquake2/glue/pl_phoenix_sys.c` — Quake II's frame timer gated on `_POSIX_MONOTONIC_CLOCK` and so
always took `CLOCK_REALTIME`. On Phoenix that is `CLOCK_MONOTONIC` plus a settable offset
(`libphoenix time/time.c:117`), so it steps whenever anything calls `clock_settime()` /
`settimeofday()` — ntpclient does. **Latent, not observed** (ntpclient runs at boot, before any game).
Fixed for correctness: `phoenix-rtos-ports` 4a166c4.

## 4. C-side: the ports, and two systemic probe-poisoning mechanisms

Swept all 54 generated `config.h` / `config.log` and 9 `CMakeCache.txt` under
`.buildroot/_build/aarch64a72-generic-rpi4b/port-sources/`. Ports link against
`.buildroot/.../sysroot/libphoenix.a` (byte-identical to the `.toolchain` copy).

**Mechanism (i) — a force-included compat header hard-errors every `AC_CHECK_FUNC`.**
`-include <port>-compat.h` in *configure-time* CFLAGS makes autoconf's `char foo(void);`
redeclaration collide with the real prototype:

```
conftest.c:215:6: error: conflicting types for 'openat'; have 'char(void)'
note: previous declaration of 'openat' with type 'int(int, const char *, int, ...)'
```

Hits **python, dillo, windowmaker** (the only three logs containing `from <command-line>`).

**Mechanism (ii) — `AC_RUN_IFELSE` macros take the cross-compile default of *no* with no
diagnostic at all.** `AC_FUNC_MMAP`, `AC_FUNC_STRCOLL`, `AC_FUNC_CHOWN`. The log shows
`checking for working mmap... no` with no compile, no link, no error, and no "cannot run test
program" note. The purest instance of the class.

### SHOULD BE ON, ranked

| # | port | macro(s) | why it is wrong | consequence | fix |
|---|---|---|---|---|---|
| 1 | **CPython 3.14.4** | `HAVE_OPENAT`, `UNLINKAT`, `SYMLINKAT`, `READLINKAT`, `LINKAT`, `FACCESSAT`, `FCHOWNAT` | Mechanism (i). All 12 `*at` are `T` in `libphoenix.a:at.o`; **coreutils probed the same 7 `=yes` the same day**, so they compile *and statically link*. Airtight correlation: the shim includes `<fcntl.h>`+`<unistd.h>` but not `<sys/stat.h>`, and exactly the 7 from those two headers are OFF while the 4 from `sys/stat.h` are ON | every `dir_fd=` kwarg raises `NotImplementedError`; no `os.fwalk`; **`shutil.rmtree` drops to its non-fd, symlink-race-unsafe path** | 7 × `ac_cv_func_<fn>=yes` in `ports/python/config.site`. Root fix: keep the compat header out of configure-time CFLAGS |
| 2 | **CPython 3.14.4** | `HAVE_SCHED_{SETSCHEDULER,GETSCHEDULER,SETPARAM,GETPARAM,RR_GET_INTERVAL}` | hand-forced `=no` at `ports/python/config.site:141-145`; all 5 are `T` (`af82c71`, 2026-07-05) | `os.sched_*` compiled out | delete those lines, **then confirm the static link** — the comment there cites an observed link failure and these live in `pthread.o`, which no other port link-tested |
| 3 | **SDL2 2.30.12** (CMake) | `HAVE_DLOPEN` → forces `SDL_LOADSO_DUMMY` | `check_library_exists(dl dlopen)` fails: Phoenix has no separate `libdl`. `dlopen/dlsym/dlclose/dlerror` are all `T`, and `dlfcn.h:29-32` defines the `RTLD_*` set | `SDL_LoadObject`/`LoadFunction` always fail. **Latent** — the games link GL statically — but SDL's dynamic GL/Vulkan loader paths are dead | `-DHAVE_DLOPEN=1` + select `SDL_LOADSO_DLOPEN` |
| 4 | bash, mc, libX11 | `HAVE_MMAP` | Mechanism (ii) | ~nil today (NLS off, `--without-bash-malloc`, readline `#undef`s it); will bite the next port that needs mmap | a **shared cross `config.site`** for all autoconf ports |
| 5 | bash | `HAVE_STRCOLL`, `HAVE_CHOWN`, `HAVE_FACCESSAT` | Mechanism (ii) / a cached value of unresolved origin | collates with `strcmp`; `test -r/-w/-x` uses real- not effective-uid semantics — immaterial single-user under the C locale | same shared `config.site` |

### Clean negatives worth stating

- **Mesa is not affected by this defect class.** It generates no `config.h`, and its meson configure
  runs **host-native x86_64**, so every probe returns yes; the forward class cannot occur. The one
  candidate — `__builtin_aarch64_get_fpcr` probing NO because the probe ran on x86 — is genuinely
  inert: `util/u_math.c:99` falls back to an inline `mrs %0, fpcr`, the same instruction. ⓘ The
  *mirror* risk (host-YES defines like `USE_SSE41`, `HAVE_MEMFD_CREATE` inherited into Phoenix TUs)
  was **not investigated**.
- **SDL2's timer path is clean**: `HAVE_CLOCK_GETTIME 1`, `HAVE_NANOSLEEP 1`, `SDL_TIMER_UNIX 1`. No
  repeat of the chrono bug in the showcase.
- **xz / libevent are deliberate, not silent** (`--disable-threads`, `--disable-clock-gettime`).
  ⚠ Consequence worth an owner call: libevent therefore times everything off `gettimeofday`, i.e. off
  the wall clock, though Phoenix has `clock_gettime` + `CLOCK_MONOTONIC`.
- **curl, xterm, glib2 clean.** curl's 135 "conflicting types" errors are its intentional recv/send
  signature search, which converged.
- **546 macros are correctly off**: 289 `ac_cv_func_*=no` confirmed absent from libphoenix, 169
  headers with zero present in the sysroot, 50 `have_decl`s with an empty intersection, 38 of 39
  failed CMake probes. A further 24 are off-but-supported yet **never read** by their port, so they
  have no consequence.

## 5. ⚠ OWNER DECISION: should libphoenix advertise the POSIX option macros itself?

Defining the "should be ON" rows of §3 in `libphoenix/include/unistd.h` is the general fix — every
future port would then detect these correctly with no per-port patch, and the libstdc++ patch's
Cause-2 half would become unnecessary. **I have not done it, deliberately:**

- SDL2, CPython, glib2, coreutils/gnulib, curl and Mesa all test some of these. Defining them flips
  code paths in each.
- It lands **unevenly** — an already-configured port carries a cached `config.h`, so the change only
  takes effect where a reconfigure happens. That is exactly our ports-staleness trap.
- The showcase image gates 6/6 apps today. This could silently move all of them.

**Recommendation: do it, but as its own step with a full rebuild and the 6/6 gate re-run.** The
toolchain patch gets us the C++ side today without that risk.

Two smaller follow-ups in the same family, both cheap and both safe: a **shared cross `config.site`**
for all autoconf ports (closes Mechanism (ii) permanently), and moving compat-header injection out of
configure-time CFLAGS (closes Mechanism (i)).

## 6. Also checked, and clean

- **Native TLS works in C**: `__thread int x;` → `mrs x0, tpidr_el0`. Not emutls.
- **Threads are configured**: `_GLIBCXX_HAS_GTHREADS 1`, `__GTHREADS 1`, `__GTHREADS_CXX0X 1`,
  gthr-default is the posix model.
- **libphoenix has no autoconf of its own**, so it has no silently-failing probe — its gap (§3) is an
  omission, not a broken detection.
- **The sysroot's `fenv.h` is a libmcs stub whose entire body is `#error`**, and it declares functions
  using `fenv_t`/`fexcept_t` without defining either type. Harmless today because
  `_GLIBCXX_HAVE_FENV_H` is off, but it is a live landmine: anything that turns that macro on fails to
  build. Implementing real aarch64 fenv (FPCR/FPSR) is a separate, unasked piece of work — noted, not
  done.

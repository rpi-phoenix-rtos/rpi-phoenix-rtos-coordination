# CPython 3.14.4 on Phoenix-RTOS — WORK IN PROGRESS

Big multi-cycle port (owner-sanctioned). CPython is PSF-licensed (permissive).
Resume with `build.sh` (idempotent) then `make` in the build tree.

## Done so far (2026-08-14)

- **configure taught about Phoenix** — added `*-*-phoenix*` cases to CPython's two
  cross-build `MACHDEP` blocks (else `cross build not supported for
  aarch64-unknown-phoenix`). → `ac_sys_system=Phoenix`, `MACHDEP=phoenix`. build.sh
  applies both via perl.
- **C99 libm gate passed** — needed acosh/asinh/atanh/erf/erfc/expm1/log1p/log2;
  the 5 missing were added to libphoenix (`libm/phoenix/c99extra.c`).
- **configure SUCCEEDS**; `--without-mimalloc` (CPython 3.14's bundled mimalloc
  needs madvise/MADV_DONTNEED + rusage fields Phoenix lacks → use pymalloc instead).
- **make advances to ~120 objects.** Compile gaps cleared via `phoenix-py-compat.h`
  (`-include`'d first in every TU):
  1. `struct timeval`/`struct rusage` incomplete in CPython internal headers →
     include `<sys/time.h>`/`<sys/resource.h>` early. (Real Phoenix headers live in
     `.toolchain/aarch64-phoenix/aarch64-phoenix/usr/include`, not `.../include`.)
  2. wide-char funcs libphoenix lacks — declared: wcstol/wcstoul/wcstoll/wcstoull/
     wcstod/wcstof/wcstold/wcstok/wcsstr/wcsspn/wcscspn/wcspbrk (declarations only;
     real defs needed at LINK — candidates to add to libphoenix).
  3. `clock_getres` — Phoenix has clock_gettime only; shimmed (nominal 1 ns).
  4. `O_NOFOLLOW` — absent in Phoenix fcntl.h; defined 0 (no nofollow enforcement).

## Cleared since (session 33)

- **`_SC_*` sysconf batch** — 6 direct-use names Phoenix lacks defined in
  phoenix-py-compat.h (`_SC_PAGE_SIZE`=alias `_SC_PAGESIZE`; NPROCESSORS_ONLN/
  TTY_NAME_MAX/SEM_VALUE_MAX/GETGR_R_SIZE_MAX/GETPW_R_SIZE_MAX = unknown ints →
  sysconf returns -1, CPython tolerates).
- **External-lib modules disabled** via `config.site` `py_cv_module_*=n/a`
  (zlib/binascii/_ssl/_hashlib/_ctypes/readline/_curses*/_dbm/_gdbm/_sqlite3/
  _tkinter/_bz2/_lzma/nis/_uuid/spwd) — build a core static python first; re-enable
  zlib/_sqlite3 later by cross-building libz + linking the sqlite port's lib.
- **`HAVE_CLOCK_GETTIME`** — configure's cross func-check falsely said no (Phoenix
  HAS clock_gettime) → the timespec `_PyTime_*` decls were `#if`'d out → implicit-decl
  error. Fixed with `ac_cv_func_clock_gettime=yes` in config.site.
- **libphoenix wide-char LANDED** (54df17b) — wcspbrk/wcsspn/wcscspn/wcsstr/wcstok +
  wcsto{l,ul,ll,ull,d,f,ld} implemented + host-tested vs glibc + synced to toolchain.
  So the wide-char link deps are now real (not shims). Removed their decls from the
  compat header.

## Current state / next

Re-run `build.sh` (applies the Phoenix configure patch + config.site) then `make`.
Expect the compile tail to continue a bit (more one-off constant/func gaps), then
**LINK** — remaining undefined symbols there = the final libc gaps to implement.
Then a STATIC python (`--disable-shared`, curated modules) → **runtime bring-up**
over netboot: `python3 -c 'print(sum(range(100)))'` => 4950, then a self-asserting
`selftest.py`. (compat header now carries: sys/time.h+resource.h+fcntl.h early,
clock_getres 1ns shim, O_NOFOLLOW=0, _SC_ batch.)

Realistic: several more turns. Each turn clears a batch of gaps (and each libphoenix
gap fixed benefits all ports).

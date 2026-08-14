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

## Current wall

`make` stops (~120 objects in) on **`_SC_TTY_NAME_MAX` undeclared** (a sysconf name
Phoenix's `<unistd.h>` lacks). More missing `_SC_*` / constants likely follow —
define each in phoenix-py-compat.h (runtime `sysconf` returns -1 for unknown, which
CPython tolerates). This is a one-gap-per-iteration compile tail; keep editing the
compat header + re-running `make` (it resumes from the failed object) until LINK.

## Then (next milestones)

1. **Reach LINK** — the undefined-symbol list = the real libphoenix functions to
   implement (expect the wide-char family wcstol/wcstok/… — add to libphoenix
   properly, host-tested vs glibc, one `--scope core` rebuild; promote clock_getres
   too). Build a STATIC python (`--disable-shared`) with a curated `Modules/Setup`
   (no dynamic .so extension imports on Phoenix).
2. **Runtime bring-up** — netboot the interpreter; expect further POSIX/syscall
   gaps at startup. Verify: `python3 -c 'print(sum(range(100)))'` => 4950, then a
   self-asserting `selftest.py`.

Realistic: several more turns. Each turn clears a batch of gaps (and each libphoenix
gap fixed benefits all ports).

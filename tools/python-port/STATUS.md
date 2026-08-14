# CPython 3.14.4 on Phoenix-RTOS — WORK IN PROGRESS

Big multi-cycle port. Progress so far (2026-08-14):

1. **configure taught about Phoenix** — CPython's configure has two cross-build
   `MACHDEP` case blocks that hard-error `cross build not supported for
   aarch64-unknown-phoenix`. Added a `*-*-phoenix*` case to each (ac_sys_system=
   Phoenix, _host_ident=$host_cpu → MACHDEP=phoenix). build.sh applies both via perl.
2. **C99 libm gate passed** — configure hard-errors "requires C99 compatible libm"
   unless acosh/asinh/atanh/erf/erfc/expm1/log1p/log2 all link. The phoenix libm
   was missing acosh/asinh/atanh/expm1/log1p → **added to libphoenix**
   (libm/phoenix/c99extra.c, host-tested vs glibc). erf/erfc/log2 already present.
3. **configure now SUCCEEDS** — Makefile + pyconfig.h generated (with `--disable-shared
   --without-ensurepip --disable-ipv6 --disable-test-modules --with-build-python=
   host python3.14`, and config.site with cross cache answers).

## Next wall (where `make` stops)

`make` builds ~32 objects then fails in **mimalloc** (CPython 3.14's bundled
allocator): `Objects/mimalloc/prim/unix/prim.c` needs `madvise`/`MADV_DONTNEED`
and `struct rusage` fields `ru_majflt`/`ru_maxrss` — none in Phoenix.

**Fix to try next:** re-configure with mimalloc off (a `--without-mimalloc` /
`--with-pymalloc` route, or `-DMI_...` off), or shim madvise as a no-op +
add the rusage fields. Then continue `make`, expecting further POSIX/module gaps
(build a static python with a curated Modules/Setup — no dynamic .so extensions).

## Verify (once built)

    /bin/python3 -c 'print(sum(range(100)))'   # => 4950
    /bin/python3 /selftest.py                  # a self-asserting feature script

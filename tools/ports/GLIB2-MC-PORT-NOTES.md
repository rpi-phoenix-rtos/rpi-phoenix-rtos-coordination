# glib2 + Midnight Commander (mc) port notes — aarch64-phoenix

Running log of the glib2 dependency-chain port (prerequisite for MC, task #55).
Build scripts are under `tools/ports/`; all are idempotent + self-contained and
stage into the cross sysroot at
`.buildroot/_build/aarch64a72-generic-rpi4b/sysroot`.

## Status (2026-06-29)

DONE — glib-2.0 and full dependency chain cross-compile to static aarch64-phoenix
libs and pass real link tests.

| lib | size | notes |
|-----|------|-------|
| libglib-2.0.a | 2.2 MB | mc-critical; GList/GString/GHashTable/GMainLoop all present |
| libgobject-2.0.a | 643 KB | needs libffi |
| libgmodule-2.0.a | 4.3 KB | no-dlopen null backend (Phoenix has no dlfcn.h) |
| libgthread-2.0.a | 2.4 KB | posix threads |
| libffi.a | 23 KB | aarch64 sysv backend; gobject gclosure dep |
| libiconv.a (stub) | 1.9 KB | identity UTF-8/ASCII copy, NOT a real transcoder |
| libresolv.a (stub) | tiny | res_query fail-clean; for glib configure link probe only |

Link tests (both produce static aarch64-phoenix ELFs, exit 0):
- GList + GString + GHashTable + g_string_append_printf program → links.
- GValue + g_value_init(G_TYPE_INT) program → links (exercises gobject + libffi).

Only one truly-unresolved libglib symbol: `environ`, provided by crt0.o at final
executable link (verified by a passing static link).

## Dependency tree resolved

```
glib-2.0 2.56.4 (last autotools glib; meson-only from 2.60)
├── pcre      : BUNDLED in glib (--with-pcre=internal) — no external port
├── zlib      : existing port (/tmp/x11-phoenix/lib/libz.a, in sysroot)
├── libffi    : NEW port build-libffi.sh (REQUIRED by glib-2.56 configure)
├── iconv     : stub libiconv.a (build-libiconv.sh)
├── gettext   : libintl-stub/libintl.h identity macros + --disable-nls
│               (host has no msgfmt → glib2.cache points MSGFMT/XGETTEXT=/bin/true)
├── resolv    : resolv-stub (res_query fail-clean) for configure link probe
└── nameser   : nameser-stub/arpa/nameser.h (C_IN + DNS type/class consts)
```

## Cross-compile mechanics (the load-bearing tricks)

- **glib2.cache** pre-answers every AC_TRY_RUN probe glib runs (can't execute on
  the cross target): `glib_cv_stack_grows=no`, `va_copy=yes`,
  `compliant_posix_memalign=1`, `long_long_format=ll`, `g_atomic_lock_free=yes`,
  `glib_cv_value_MSG_DONTROUTE=0` (Phoenix has no MSG_DONTROUTE), the
  gettext-detection vars, and `ac_cv_path_MSGFMT=/bin/true`. Loaded via
  `--cache-file`. Without it configure errors or hangs.
- **glib-phoenix-shim.h** (force-included via `-include`): `P_tmpdir`,
  `LC_MESSAGES=6` (Phoenix `locale.h` stops at LC_TIME=5), NLS identity-macro
  fallback.
- GLIB deps fed to configure via `*_CFLAGS`/`*_LIBS` env + `PKG_CONFIG=/bin/false`
  so configure never calls pkg-config.

## libphoenix gaps HIT (worked around in-port; NOT yet patched upstream)

These are the real Phoenix libc/header gaps. Worked around with in-port stubs for
unattended safety; the clean fixes belong in `sources/libphoenix` (attended):

1. **no iconv** — stubbed (identity). Real fix: port GNU libiconv (its gnulib
   won't cross-compile against Phoenix's include-order-fragile headers — circular
   `sys/types.h → stdint.h → stdio.h → off_t`; see build-libiconv.sh comment) or
   add an iconv engine to libphoenix.
2. **no gettext / no `<libintl.h>`** — stubbed identity macros.
3. **no `<langinfo.h>` / nl_langinfo** — glib configure detects absence and
   disables CODESET path (`glib_cv_langinfo_codeset=no`). OK for now.
4. **`<sys/socket.h>` lacks MSG_DONTROUTE** — seeded =0 in cache. Clean fix: add
   `#define MSG_DONTROUTE` to `phoenix/posix-socket.h`.
5. **`<locale.h>` lacks LC_MESSAGES** — shim defines =6. RISK: if Phoenix
   setlocale indexes a 0..5 categories array, `setlocale(6,…)` from glib's
   ggettext.c may read OOB. Clean fix: add LC_MESSAGES + handling to libphoenix
   locale.c/locale.h (ATTENDED).
6. **empty `<resolv.h>`, no `<arpa/nameser.h>`, no res_query** — stubbed.

## Runtime caveats to validate on HW (cannot test unattended)

- Stub iconv = identity only: mc charset conversion is correct for UTF-8/ASCII,
  wrong for legacy single-byte codepages.
- LC_MESSAGES=6 OOB risk (item 5 above).

## Midnight Commander (mc 4.8.31) recon — 2026-06-29

`tools/ports/build-mc.sh` drives mc's configure+build. mc needs glib-2.0 >= 2.32
(have 2.56) + a screen lib (ncurses, ported). Status: **configure PASSES, mc
itself COMPILES** through libmcskin and into lib/tty, then hits ONE blocker.

Flags that work: `--with-screen=ncurses --with-ncurses-includes/libs`,
`--without-subshell` (sidesteps grantpt/ptsname pty wall), `--without-x
--without-gpm --disable-nls --disable-vfs-undelfs --disable-vfs-sftp
--disable-doxygen-doc`. GLIB fed via `GLIB_CFLAGS/GLIB_LIBS` env +
`fake-pkg-config.sh` (mc calls `pkg-config --variable=gmodule_supported`, so a
plain `/bin/false` isn't enough — the fake script answers mc's specific queries).

mc-specific gaps worked around:
- **mntent API**: Phoenix `<mntent.h>` is empty, no getmntent. Supplied
  `mc-support/mntent.h` (glibc-compatible struct) + stub libmcsupport.a
  (no-mounts). Seeded `fu_cv_sys_mounted_getmntent1=yes` in mc.cache.
- **glib-unix.h staging bug** (fixed in build-glib2.sh): glib-unix.h is a
  top-level public header (`<glib-unix.h>`), installs to include/glib-2.0/ not
  glib/. Staging now copies the 3 glibinclude_HEADERS (glib.h, glib-unix.h,
  glib-object.h) to the top level.

### THE mc BLOCKER (next session): ncurses needs WIDE-CHAR build

mc's `lib/tty/tty-ncurses.h:35` unconditionally `#define ENABLE_SHADOWS 1`, and
`tty-ncurses.c` (tty_colorize_area, under ENABLE_SHADOWS) calls the WIDE ncurses
API: `getcchar`, `setcchar`, `mvin_wchnstr`, `mvadd_wchnstr`, `cchar_t`. Our
ported ncurses (build-ncurses.sh) is NARROW (no `--enable-widec`), so these are
undeclared.

FIX (clean, reusable): rebuild ncurses with `--enable-widec` to produce
`libncursesw.a` (which exports getcchar/setcchar/*_wchnstr), and point mc at it
(`--with-ncurses-libs` + link `-lncursesw`). This is the single remaining step to
a compiling mc. (A hackier alternative — `#undef ENABLE_SHADOWS` via the mc shim —
loses dialog shadows and risks other widec call sites; prefer widec ncurses.)

NOTE: widec ncurses pulls in wchar/multibyte libc (wcwidth, mbrtowc, wcrtomb).
libphoenix has the wide-char set (added during the X11 lib port per MEMORY), so
this should link; verify with `nm`.

## Build order

```
tools/ports/build-libiconv.sh   # stub iconv
tools/ports/build-libffi.sh     # aarch64 libffi
tools/ports/build-glib2.sh      # pulls the above in if missing; builds all 4 glib libs
```

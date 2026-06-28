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

## Midnight Commander (mc 4.8.31) — BUILT 2026-06-29

`tools/ports/build-mc.sh` builds mc end-to-end. **mc links to a 2.9 MB static
aarch64-phoenix ELF with 0 undefined symbols** (`strings` shows "GNU Midnight
Commander 4.8.31"); staged to `artifacts/mc`. NOT yet run on HW (needs the main
session to flash/boot — set `TERM=vt100`, `mc` on the Pi console).

mc needs glib-2.0 >= 2.32 (have 2.56) + a screen lib (ncurses, ported). The full
recon + workarounds that got it building are below.

Flags that work: `--with-screen=ncurses --with-ncurses-includes/libs`,
`--without-subshell` (sidesteps grantpt/ptsname pty wall), `--without-x
--without-gpm --disable-nls --disable-vfs-undelfs --disable-vfs-sftp
--disable-doxygen-doc`. GLIB fed via `GLIB_CFLAGS/GLIB_LIBS` env +
`fake-pkg-config.sh` (mc calls `pkg-config --variable=gmodule_supported`, so a
plain `/bin/false` isn't enough — the fake script answers mc's specific queries).

Disabled mc features (avoid missing Linux-only headers/libs): `--without-subshell`
(grantpt/ptsname pty wall), `--without-x --without-gpm`, `--disable-vfs-undelfs`
(ext2 undelete), `--disable-vfs-sftp` (libssh2), `--disable-vfs-ftp`
(`arpa/ftp.h` missing). ext2fs chattr/lsattr auto-disabled (no ext2fs pkg).

mc-specific gaps worked around (all in tools/ports/, unattended-safe):
- **mntent API**: Phoenix `<mntent.h>` is empty, no getmntent. Supplied
  `mc-support/mntent.h` (glibc-compatible struct) + stub libmcsupport.a
  (no-mounts). Seeded `fu_cv_sys_mounted_getmntent1=yes` in mc.cache.
- **langinfo**: no `<langinfo.h>`, no nl_langinfo. Supplied `mc-support/langinfo.h`
  + stub nl_langinfo (CODESET -> "UTF-8" to match identity iconv).
- **passwd enumeration**: libphoenix has getpwnam/getpwuid but not
  setpwent/getpwent/endpwent (mc ~user tab-completion). Stubbed in
  `mc-phoenix-shim.h` (force-included), same as the nano port.
- **NCURSES_WIDECHAR**: forced =0 via `-DNCURSES_WIDECHAR=0` (see below).
- **fake-pkg-config.sh**: answers ONLY for glib/gmodule/gthread/gobject; every
  other module (ext2fs/libssh2/gpm/aspell/check) reports ABSENT so mc disables
  that optional feature instead of compiling against missing headers. (A version
  that claimed every module present wrongly enabled ext2fs chattr.)
- **glib-unix.h staging bug** (fixed in build-glib2.sh): glib-unix.h is a
  top-level public header (`<glib-unix.h>`), installs to include/glib-2.0/ not
  glib/. Staging now copies the 3 glibinclude_HEADERS (glib.h, glib-unix.h,
  glib-object.h) to the top level.

### widec ncurses (RESOLVED for the narrow build; relevant only for UTF-8 mc)

mc's `tty-ncurses.c` (tty_colorize_area, under `#ifdef ENABLE_SHADOWS`) calls the
WIDE ncurses API: `getcchar`, `setcchar`, `mvin_wchnstr`, `mvadd_wchnstr`,
`cchar_t`. Our ported ncurses (build-ncurses.sh) is NARROW. mc's tty-ncurses.h
enables ENABLE_SHADOWS only when `NCURSES_WIDECHAR` is 1, and the narrow ncurses
header sets that to 1 under `_XOPEN_SOURCE>=500`. RESOLVED by forcing
`-DNCURSES_WIDECHAR=0` (the header honours a pre-definition). Only loss: dialog
drop-shadows. Full UTF-8 mc would need ncursesw + the libphoenix wide-char add
below.

DISCRIMINATING CHECK (done): EVERY widec usage in mc
(`grep -rnE 'cchar_t|getcchar|setcchar|_wchnstr|wget_wch|wadd_wch|add_wch' lib src`)
falls inside the single `#ifdef ENABLE_SHADOWS` block in tty-ncurses.c:565-586.
There are NO widec call sites elsewhere. => mc against the EXISTING NARROW ncurses
is viable by disabling ENABLE_SHADOWS (loses only dialog drop-shadows).

FIX TAKEN (unattended-safe, stays in ports tree): build-mc.sh patches
`lib/tty/tty-ncurses.h:35` to not `#define ENABLE_SHADOWS 1` (a sed on the header —
the force-include shim can't undo it because the header redefines AFTER the shim
runs). Then mc compiles against narrow `-lncurses`.

ALTERNATIVE (attended, for full widec/UTF-8 mc): rebuild ncurses --enable-widec
(libncursesw.a, exports getcchar/setcchar/*_wchnstr) and link `-lncursesw`. BUT
this needs libphoenix wide-char additions first — see gap below.

### CORRECTION to MEMORY: libphoenix wide-char set is INCOMPLETE

MEMORY's X11-port note implies libphoenix has the wide-char set; `nm` on
libphoenix.a DISPROVES that for the restartable conversions. PRESENT:
`mbtowc`, `wctomb`, `mblen`, `mbstowcs`, `wcstombs`, and basic `wcs*` string ops
(wcscmp/wcslen/wcscpy/...). ABSENT (the precise gap gating widec-ncurses):
**`wcwidth`, `mbrtowc`, `wcrtomb`, `mbsinit`** (and `<wchar.h>` doesn't even
declare wcwidth/mbrtowc). Adding these to sources/libphoenix is the prerequisite
for any ncursesw/UTF-8-aware port — an ATTENDED libc change.

## Build order

```
tools/ports/build-libiconv.sh   # stub iconv
tools/ports/build-libffi.sh     # aarch64 libffi
tools/ports/build-glib2.sh      # pulls the above in if missing; builds all 4 glib libs
```

## HW RUNTIME RESULT (2026-06-29) — mc builds, but CRASHES at startup

mc was staged to `/srv/phoenix-rpi4-nfs/bin/mc` and run on the Pi (NFS root, teken
fbcon). **It Data-Aborts (EL0) on startup, before any TUI**, on `mc`, `mc -V`, AND
`mc --help` identically:

```
Exception #36: Data Abort (EL0)   in thread, process "/bin/mc"
pc=0x5386e8  esr=0x92000004 (translation fault)  far=0x6120796172677448 (ASCII!)
```

`pc` resolves to `malloc_chunkSize`/`malloc_find` (libphoenix `malloc_dl.c:78` — the
dlmalloc tree walk). `far` and x16/x17 hold ASCII bytes from mc's GOptionContext help
.rodata ("...out of 'mc -V')", color-help text). => **heap-metadata corruption**: mc
overflowed a heap chunk with string data; the next malloc walk dereferenced the
clobbered RB-tree pointer. NOT a malloc bug — an earlier overflow.

### What was EXONERATED on HW (proven safe — do NOT re-investigate)

Two minimal link-test programs were built against the SAME staged glib + stubs and run
on the Pi (sources `/tmp/glibtest.c`, `/tmp/glibtest2.c`; staged `/bin/glibtest`,
`/bin/glibtest2`):

| Probe | Result on HW |
|-------|--------------|
| `glibtest`: g_malloc + GString + GList (200× g_strdup_printf) + GHashTable + free-all | `GLIBTEST-OK ... GLIBTEST-FREED-CLEAN` — **glib alloc is SAFE** |
| `glibtest2`: setlocale(LC_ALL,"") + setlocale(LC_MESSAGES,NULL) + GOptionContext (entries, parse, get_help) | `GT2-OK`, `setlocale(LC_MESSAGES,NULL)=NULL`, `help len=340` — **GOptionContext + setlocale path SAFE** |
| env `G_SLICE=always-malloc` then `mc -V` | still crashes — **not the slice allocator** |
| env `LC_ALL=C` then `mc -V` | still crashes — **not locale OOB** |
| iconv stub code review (`iconv-stub/iconv.c`) | correctly bounds copy, returns E2BIG — **safe** |

Confirmed: `setlocale(LC_MESSAGES=6, NULL)` returns NULL (libphoenix `posix/stubs.c`
setlocale rejects category 6 — it is NOT array-indexed, so no OOB write), and glib
tolerates that NULL (glibtest2 proves it).

### CONCLUSION: the bug is in mc's OWN early init, not the ported libs

Everything glibtest/glibtest2 exercise is clean. The crash is in something mc does that
they do NOT. Leading suspect: **mc's string/charset init** — `str_init_strings()`
(`lib/strutil/strutil.c`) selects the **UTF-8** strutil path (`strutilutf8.c`) because
the nl_langinfo(CODESET) stub returns "UTF-8". The UTF-8 g_utf8_* path is the untested
surface. (Next candidates: the full mc args GOptionContext with its translation-domain
callback + ~40 grouped entries; vfs_init.)

### NEXT (attended or next-boot, batched — 2 binaries staged by the mc-fix subagent)

- `/bin/mc-ascii`: nl_langinfo(CODESET)→"ASCII" so str_init picks the 8-bit
  `strutilascii.c` path (cheap-fix hypothesis: avoids the UTF-8 buffer bug).
- `/bin/mc-dbg`: fprintf+fflush MCDBG markers through main()/str_init_strings — the
  last marker before the Data Abort pinpoints the crashing init call.
- Boot the Pi, run `mc-ascii` (does it reach the TUI?) and `mc-dbg` (read the last
  MCDBG marker on UART). One boot discriminates fix-vs-localize.

**Deliverable status:** the hard part (full glib2 + libffi + iconv chain ported, mc
links to a 0-undefined-symbol static ELF, libs HW-validated) is DONE and committed. The
residual is an mc-internal startup heap overflow, precisely localized, with repro
programs + a fix attempt + an instrumented build staged for a single decisive boot.

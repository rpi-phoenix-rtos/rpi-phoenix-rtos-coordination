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

### TWO DIAGNOSTIC BINARIES BUILT + STAGED (2026-06-29)

Both produced reproducibly by `build-mc.sh` via a new `MC_VARIANT` env
(`stock|ascii|dbg`); both verified as valid static aarch64-phoenix ELFs with 0
undefined symbols. `/bin/mc` (the original stock UTF-8 build) is left untouched.

**1. `/bin/mc-ascii` — the cheap-fix attempt.** `MC_VARIANT=ascii` compiles the
langinfo stub with `-DMC_CODESET_ASCII`, so `nl_langinfo(CODESET)` returns
`"ASCII"` instead of `"UTF-8"`. (Verified by disasm: mc-ascii's CODESET jump-table
slot points at the bytes `41 53 43 49 49` = "ASCII"; mc-dbg's points at
`55 54 46 2d 38` = "UTF-8".)

  Where mc decides UTF-8-vs-8bit — `lib/strutil/strutil.c`:
  - `main()` (`src/main.c:267`) calls `str_init_strings(NULL)`.
  - With `termenc==NULL` it takes `codeset = g_strdup(str_detect_termencoding())`,
    and `str_detect_termencoding()` = `g_ascii_strup(nl_langinfo(CODESET), -1)`.
  - `str_choose_str_functions()` then tests `codeset` against two tables IN ORDER:
    `str_utf8_encodings[]` = {"utf-8","utf8"} → `str_utf8_init()` (strutilutf8.c);
    else `str_8bit_encodings[]` = {cp-125x, iso-8859, koi8, cp-866/850/852, …} →
    `str_8bit_init()` (strutil8bit.c); **else** → `str_ascii_init()`
    (strutilascii.c).
  - "ASCII" matches NEITHER table (it is not in str_8bit_encodings — checked the
    full array), so it falls to the **else** branch → `str_ascii_init()`, the plain
    8-bit path that the crashing UTF-8 path's g_utf8_* ops never run. "ASCII" is
    also mc's own `DEFAULT_CHARSET` (lib/global.h:144), confirming it's the
    intended safe fall-through.

**2. `/bin/mc-dbg` — the instrumented fallback.** `MC_VARIANT=dbg` keeps the
**UTF-8** codeset (so it still reproduces the crash) and applies
`tools/ports/mc-dbg-instrument.patch` (23 `fprintf(stderr,…)+fflush` markers).
The LAST `MCDBG:` line on the UART before the Data Abort pinpoints the crashing
init call. Marker order on a clean boot:

```
MCDBG: enter main
MCDBG: setlocale begin
MCDBG: setlocale done
MCDBG: str_init_strings begin
MCDBG:   str_init_strings: enter (termenc=(null))
MCDBG:   str_init_strings: codeset=<UTF-8 for mc-dbg>
MCDBG:   str_init_strings: iconv_open done
MCDBG:   str_init_strings: str_choose_str_functions done   <- past UTF-8 strutil init
MCDBG: str_init_strings done
MCDBG: mc_setup_run_mode begin
MCDBG: mc_setup_run_mode done
MCDBG: mc_args_parse begin                                  <- GOptionContext build+parse
MCDBG: mc_args_parse done
MCDBG: OS_Setup begin
MCDBG: OS_Setup done
MCDBG: events_init begin
MCDBG: events_init done
MCDBG: mc_config_init_config_paths begin
MCDBG: mc_config_init_config_paths done
MCDBG: vfs_init begin
MCDBG: vfs_init done
MCDBG: load_setup begin
MCDBG: load_setup done
```

Reading it — IMPORTANT precedence (the crash is heap-metadata corruption that
surfaces at a *later* malloc tree-walk, NOT a direct deref, so the last MCDBG
marker localizes where the corruption is *tripped*, not where it was *planted*):

- **`/bin/mc-ascii` reaching the TUI is the ground-truth signal** that the
  codeset/strutil path was the cause. Trust this over the marker.
- The dbg last-marker is a coarse localizer only. If the last line is
  `str_init_strings: codeset=UTF-8` (no `str_choose_str_functions done`), the
  corruption is tripped inside the UTF-8 strutil init. But because surfacing is
  delayed, a UTF-8 overflow planted in str_init can instead trip later (e.g. at a
  malloc inside `mc_args_parse`) — in which case the last marker reads
  `mc_args_parse begin` AND mc-ascii still fixes it. So do NOT conclude "the args
  parser is the culprit, mc-ascii won't help" from the marker alone; let the
  empirical mc-ascii result outrank the marker when they disagree.
- Also note: `fprintf` itself may call the allocator, so mc-dbg's exact crash
  point can shift slightly vs stock `mc` — treat the marker as approximate.

### Reproducing the variants

```
MC_VARIANT=ascii tools/ports/build-mc.sh   # -> /bin/mc-ascii + artifacts/mc-ascii
MC_VARIANT=dbg   tools/ports/build-mc.sh   # -> /bin/mc-dbg   + artifacts/mc-dbg
MC_VARIANT=stock tools/ports/build-mc.sh   # -> /bin/mc (default; rebuilds canonical)
```

The script is idempotent across variant switches: it rebuilds `libmcsupport.a`
with the variant's codeset `-D`, reverse-applies any stale dbg patch then
forward-applies it only for dbg, removes the stale `.o`s of the two patched TUs,
and `rm`s `src/mc` to force a relink (libmcsupport.a is not a make dependency of
`src/mc`, so a plain `make` would otherwise ship a stale binary). Each build
prints a PRE-FLIGHT that `strings`-greps the baked-in CODESET and (for dbg) the
MCDBG marker count, so a stale binary is caught at build time.

**Deliverable status:** the hard part (full glib2 + libffi + iconv chain ported, mc
links to a 0-undefined-symbol static ELF, libs HW-validated) is DONE and committed. The
residual is an mc-internal startup heap overflow, precisely localized, with repro
programs + a fix attempt + an instrumented build staged for a single decisive boot.

## FINAL HW VERDICT (2026-06-29) — mc-dbg + mc-ascii run on the Pi

Both ran on the NFS-root teken-fbcon boot (label `mc-dbg2`):

- **mc-dbg** (`-V`): last marker before the Data Abort = **`MCDBG: mc_args_parse begin`**
  (str_init_strings + mc_setup_run_mode all completed cleanly first, incl. the UTF-8
  `str_choose_str_functions done`). So the corruption is TRIPPED at `mc_args_parse`'s
  GOptionContext build+parse allocation burst.
- **mc-ascii** (`-V`): **also Data-Aborts** (identical EL0 fault). Per this doc's own
  precedence rule, **the empirical mc-ascii result outranks the marker**: since the
  8-bit/ASCII strutil path crashes too, **the codeset / UTF-8 strutil path is
  EXONERATED** — it is NOT the planter.

**Net localization:** heap corruption is planted in mc's **codeset-INDEPENDENT** early
init (everything common to mc-dbg and mc-ascii up to and including `mc_args_parse begin`
— i.e. str_init_strings' common part / iconv_open / mc_setup_run_mode / the start of
mc_args_parse) and surfaces at the first large malloc burst inside `mc_args_parse`.
glib alloc, iconv stub, setlocale, basic GOptionContext, AND the codeset/strutil choice
are ALL exonerated on HW.

**Why this is the stop point (not a quick fix):** the remaining suspects (the iconv
identity stub being driven by mc's str_convert during init, or a fixed buffer in mc's
own init) require **heap-canary / malloc-guard instrumentation to find the PLANT site**
— the trip site is already known and isn't where the bug is. That is multi-cycle
attended debugging, outside the unattended envelope. **#55 handoff: mc + the entire
glib2 stack are ported and committed (the deliverable); the runtime crash is localized
to a codeset-independent heap overflow in mc's init, tripping in mc_args_parse, with
`/bin/mc-dbg` (markers) + `/bin/mc-ascii` (codeset-exoneration proof) staged.** Next
attended step: build an `MC_VARIANT=guard` with `MALLOC_CHECK_`-style heap canaries or
bisect mc_args_parse with finer markers to catch the overflowing write.

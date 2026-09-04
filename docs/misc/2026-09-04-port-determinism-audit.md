# Port determinism audit: where a clean build and an incremental build disagree

**Why this exists.** The full-clean rebuild aborted at `lighttpd`, and the two defects
there were both *determinism* defects rather than compile errors: the build's OUTPUT
depended on which of several config files was found, and an extraction pattern matched
commented-out lines. The owner's standing request is to "prove determinism with a clean
chain", so the whole ports tree was swept for the same classes.

Scope: all 67 `sources/phoenix-rtos-ports/*/port.def.sh` plus the helper scripts they call.
Read-only; nothing built.

**Confidence marking is deliberate.** Findings I re-verified myself are marked
**[verified]**. The rest are **[reported]** — plausible, specific, with file:line, but not
independently re-checked. Do not act on a `[reported]` row without confirming it first;
that is the rule that caught five false closures on #67.

## The fact everything hangs off

`phoenix-rtos-build/port_manager/candidates.py:175-183`: `install_path` returns
`$PREFIX_BUILD` for every port whose `conflicts` is empty, and `openssl111:22` is the
**only** port with a non-empty `conflicts`. So for 66 of 67 ports `PREFIX_H`/`PREFIX_A`
are **one shared directory** with no per-port namespacing. Every finding below is a
consequence of that: ports can, and do, overwrite each other's headers and archives.

## Ranked

**1. Two different JPEG libraries write the same archive and header. [verified]**
`xorg_fonts/port.def.sh:104-107` guards on `$PREFIX/lib/libjpeg.a` +
`$PREFIX/include/jpeglib.h` and then builds **IJG jpeg-9e** (`--prefix="$PREFIX"`). The
`libjpeg` port builds **libjpeg-turbo 3.0.4** (`CMAKE_INSTALL_PREFIX=${PREFIX_PORT_INSTALL}`,
`LIBDIR=lib`, `INCLUDEDIR=include`) and `make install`s it unconditionally (`:67`) to the
same two paths. Neither port declares the other as a dependency, so the order is
unconstrained.

These are **not ABI-compatible**: turbo targets the libjpeg 6.2 ABI by default
(`JPEG_LIB_VERSION` 62) while IJG 9e is 90, and `jpeg_decompress_struct` grew fields
across 7/8/9. A consumer can therefore compile against one header and link the other
archive — a struct-layout mismatch, i.e. silent memory corruption rather than a link
error. Live consumers: `supertuxkart:154` (`-DJPEG_LIBRARY=${pfx}/lib/libjpeg.a`),
`dillo:72` (`-ljpeg`), `fltk:64` (`--disable-localjpeg`).

*Clean vs incremental:* on a clean build `xorg_fonts`' guard fires and jpeg-9e lands
first; whether turbo then overwrites it is port-manager state. On an incremental build
turbo's files already exist, so `xorg_fonts` skips jpeg entirely. The header and the
archive can even end up from **different** implementations.

**Fix direction:** the owner's own rule — one build path per port. Give `xorg_fonts` a
dependency on the `libjpeg` port and delete its private jpeg-9e build. Turbo is
API-compatible with IJG for consumers, so this is a removal, not a port.

**2. Same collision for libpng. [reported]** `xorg_fonts:92` vs `libpng:38-51`, both
targeting `$PREFIX/lib/libpng16.a`. Same version (1.6.40) but different flag sets, so
which flags the shipped archive was built with is not determined by the recipes.

**3. My own lighttpd fix is INERT on incremental builds. [verified]**
`lighttpd/port.def.sh:32` wraps the whole `p_prepare` body — including the
`plugin-static.h` generation at `:46` — in `if [ ! -f "$PREFIX_PORT_WORKDIR/config.h" ]`.
The generated header's *input* lives outside the workdir, but the guard keys on a file
inside it, and `b_port_invalidate_stale_configure` only fires on a libphoenix API change,
never on a `lighttpd.conf` change. So the anchored-grep fix committed earlier today takes
effect on a clean build and **not** on an incremental one: editing `lighttpd.conf` does
not change the linked plugin table. **Fix:** move the generation out of the guard; it is
cheap and its input is external.

**4. Ports that stage headers into the shared prefix change other ports' configure
answers. [reported]** `windowmaker:84-93` stages `libftw.a` + `ftw.h`, and libphoenix has
no `<ftw.h>` — so `AC_CHECK_HEADERS(ftw.h)` answers differently before and after
windowmaker runs. `glib2:61-70` stages **stub** `libintl.h`, `resolv.h`, `arpa/nameser.h`
and a stub `libresolv.a` whose own comment says it fails at runtime — and `mc:96` already
links `-lresolv`. `ncurses:55-57` flattens ncurses headers into the shared include root;
the scar tissue is already in-tree, `xterm:89` must seed `cf_cv_lib_tgetent=no` because
configure otherwise finds a tgetent provider in the sysroot. Same class as the `mc`
`<mntent.h>`/`<langinfo.h>` incident this repo already paid for.

**5. The four game ports include `-I/tmp/mesa-v3d-build/src`. [reported]**
`quake3:150`, `quakespasm:128`, `yquake2:124`, `supertuxkart:226`, with directory
assertions that `b_die` without it. Generated Mesa headers come from a host-local `/tmp`
tree, recorded in no manifest and wiped on reboot. Within a single build the gpu phase
creates it, so this is a coupling rather than a clean-room blocker — but the binaries'
contents depend on when `build-gl-phoenix.py` last ran. `ports.yaml` documents the
`tools/.gpu-libs` absolute-path wart as accepted; it does **not** mention the `/tmp`
include path.

**6. `b_port_invalidate_stale_configure` is autoconf-only. [reported]**
`port.subr:209` gates on `[ -f config.status ] || [ -f config.log ]`, so the 11 CMake
ports (whose `CMakeCache.txt` caches compiler and libc probe results), `ffmpeg` and
`openssl111` are never invalidated — their probes stay frozen at whenever the port was
first configured. `ffmpeg:88-94`'s hand-flip of `HAVE_ERF/EXP2/EXP2F/LOG2F` from 0→1,
commented "fresh-libc reconcile", is a manual patch over exactly this.

**7. ~35 X11 tarballs fetched with no sha256. [reported]** `xorg_libs:73-91`,
`xorg_fonts:69-89`, `xorg_apps:94-106` — `curl` with no checksum, unlike every
framework-fetched anchor. `xorg_libs`/`xorg_fonts` additionally cache into
`${PHOENIX_DISTFILES:-$HOME/.phoenix-distfiles}/xorg`, **outside the buildroot**, keyed by
basename and reused unverified forever. This host therefore builds from a `$HOME` cache
nothing verifies while a clean room re-downloads whatever the mirror serves that day.

**8. Ports that write into the rootfs outside `b_install`. [reported]** `xorg_apps`,
`windowmaker`, `xterm`, `xbill`, `mc`, `dropbear`, `busybox` write directly into
`${PREFIX_FS}/root/...`, several best-effort (`mc:137-139` `|| true`, whose own comment
says mc degrades to monochrome without the skins). A `build.sh fs project image` run after
a clean therefore yields an image missing `/bin/{xterm,wmaker,xcalc,…}` **with no error**,
while an incremental run has them surviving from the previous ports stage.

**9. `coreutils` installs whatever AArch64 ELF is in `src/`. [reported]** `:62-79` —
`make -k … || true`, then install by `readelf | grep -q AArch64`, backstopped only by a
`>= 100` count. A binary from a previous successful link is indistinguishable from one
built this run.

**10. `micropython` generates its golden test results with the BUILD HOST's python3.
[reported]** `:118-129`, guarded by `[ -f "$test.exp" ] && continue`, so the `.exp` files
are kept forever from whenever the port was first tested. Test verdicts are
host-dependent and not reproducible in a clean room.

Lower-tier, tabled in the agent's full report and not repeated here: `glib2`/`mc` donor
`config.sub` picked from whatever other port happens to be extracted (`glib2:45`,
`mc:56`); `xorg_server` compiling `libmd` inside the checked-out ports repo (`:56-61`,
already papered over with a `.gitignore`); `azure_sdk` baking `$AZURE_CONNECTION_STRING`
into a source file; `lsb_vsx` baking the host `$PATH`; env-var-selected build inputs in
`heatshrink`/`lua`/`busybox`/`micropython`.

## What the sweep did NOT find

- **No second instance of the unanchored-grep class.** `lighttpd:46` was the only one;
  every python guard is `^`-anchored, `jq`'s generation is deterministic, and
  `mc/fake-pkg-config.sh` is pure.
- **No other port combines a disabled feature with unconditionally-compiled optional
  modules** — the combination that broke lighttpd. `ffmpeg:70-81` is the exemplary
  inverse (`--disable-autodetect --disable-everything`, then an explicit enable list).
- **The `config.h`/`Makefile` stamp guards are no longer the marker mismatch** the
  `libpng:29-37` comment describes: `b_port_invalidate_stale_configure` now deletes
  `config.status`, `config.cache`, `config.h`, `config.log` and `Makefile`. The residual
  gap is finding 6.

## Order of work

One port at a time, per the owner's rule. First two: **finding 3** (my own fix is
currently inert — smallest, and it makes work already committed actually take effect)
then **finding 1** (two libraries at one path, with a live consumer in supertuxkart).
Findings 4 and 6 are the ones that will keep producing "impossible" bugs, but each needs
its own controlled change.

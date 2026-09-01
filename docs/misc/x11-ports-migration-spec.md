# X11 Port → phoenix-rtos-ports Migration Spec

**Owner directive (2026-08-14 + 2026-08-21):** move the X11 stack out of the
coordination repo's `tools/x11-port/` into the `phoenix-rtos-ports` project.
**Chosen model (owner, 2026-08-21): HYBRID — aggregate ports by layer**
(`xorg-libs`, `xorg-fonts`, `xorg-server`) + individual thin **app** ports.

This spec was extracted (read-only) from `tools/x11-port/build-x11-phoenix.sh`,
the per-component `build-*.sh` scripts, and `tools/x11-port/patches/`. It is the
source of truth for the port recipes. Everything below builds today (X11 runs on
HW) into `/tmp/x11-phoenix` + `/tmp/wmaker-deps`; the migration relocates the
build into the framework, staging into `$PREFIX_SYSROOT` instead of `/tmp`.

## Global build idiom (from `build-x11-phoenix.sh` `xbuild()`)

Each Layer-1 autotools lib:
```
./configure --host=aarch64-phoenix --prefix=<sysroot> --disable-shared --enable-static \
  CC=<tc>gcc AR=<tc>ar RANLIB=<tc>ranlib \
  CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $XCFLAGS_EXTRA" \
  LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib" <extra>
```
- `PKG_CONFIG_PATH` MUST include BOTH `$PREFIX/lib/pkgconfig` and
  `$PREFIX/share/pkgconfig` (xorgproto/xcb-proto `.pc` land in `share`).
- `<extra>` carries cross run-test cache overrides (`xorg_cv_malloc0_returns_null`,
  `ac_cv_lib_m_hypot`) since cross builds can't run the probe binaries.

## Topological build order (the DAG)

```
# external prereq (NOT in x11-port): glib-2.56.4  (tools/ports/src/glib-2.56.4)

# ---- LAYER 1  xorg-libs ----
xorgproto-2023.2 · libXau-1.0.11 · xtrans-1.5.0 · libXdmcp-1.1.5 ·
xcb-proto-1.16.0 (HOST build, python codegen) · libpthread-stubs-0.5 ·
libxcb-1.16 [patch] · libX11-1.8.7 [patch] · libXext-1.3.5 · libXrender-0.9.11 ·
libXrandr-1.5.4 · libxkbfile-1.1.3 · xcb-util-0.4.1 · xcb-util-image-0.4.1 ·
xcb-util-renderutil-0.3.10 · xcb-util-keysyms-0.4.1 · xcb-util-wm-0.4.2 ·
pixman-0.42.2 (lib-only) · libICE-1.1.1 [patch] · libSM-1.2.4 ·
libXt-1.3.0 (malloc0=yes) · libXmu-1.2.1 (malloc0=yes) · libXpm-3.5.17 (lib-only) ·
libXaw-1.0.16 (lib-only)

# ---- LAYER 2  xorg-fonts ----
zlib-1.3.1 · libpng-1.6.40 · jpeg-9e · freetype-2.13.2 (--without-harfbuzz: breaks cycle) ·
libfontenc-1.1.8 · libXfont2-2.0.6 [patch] (lib-only) · expat-2.5.0 ·
fontconfig-2.14.2 [2 inline perl patches; in-tree; needs gperf] · libXft-2.3.8 ·
cairo-1.16.0 · harfbuzz-2.6.7 (needs glib) · fribidi-1.0.13 · pango-1.42.4
# gdk-pixbuf: NOT ported (future-work comment only)

# ---- LAYER 3  xorg-server ----
libmd (local sha1) · xorg-server-1.20.14 core [record-malloc0 patch] ·
Xphoenix = fbdev DDX (local ddx/ + patched ddxLoad.c + generated XKB keymap)

# ---- APPS (thin ports, after their libs) ----
libftw (local gap-fill) → WindowMaker-0.95.9 [2 patches + inline]
twm-1.0.12 · xeyes-1.1.2 · xterm-396 [patch] · xcalc-1.1.2 · xclock-1.1.1 ·
xedit-1.2.2 [patch] · xlogo-1.0.7 · oclock-1.0.5 · ico-1.0.6 · jwm-2.4.6 [inline] ·
xbill (git HEAD) · xlaunch/startx (local) · xphxdemo (local)
```

## Per-component details

### Layer 1 (xorg-libs) — URLs + flags
- **xorgproto 2023.2** — `x.org/releases/individual/proto/xorgproto-2023.2.tar.gz`; headers + `.pc` only; no deps.
- **libXau 1.0.11** / **xtrans 1.5.0** / **libXdmcp 1.1.5** — `.../individual/lib/`; dep xorgproto.
- **xcb-proto 1.16.0** — `xorg.freedesktop.org/archive/individual/proto/…tar.xz`; **HOST build** (native python codegen), not cross.
- **libpthread-stubs 0.5** — `.../archive/individual/lib/…tar.xz`; installs only `pthread-stubs.pc`.
- **libxcb 1.16** — `.../archive/individual/lib/…tar.xz`; `--disable-mitshm`; **patch** `libxcb-1.16-phoenix.patch`; deps xcb-proto,libXau,libXdmcp,libpthread-stubs.
- **libX11 1.8.7** — `.../individual/lib/`; `--without-xmlto --disable-specs --disable-devel-docs xorg_cv_malloc0_returns_null=no`; `XCFLAGS_EXTRA="-DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"`; **patch** `libX11-1.8.7-phoenix-fontset-basename-ownership-58.patch`.
- **libXext/libXrender/libXrandr/libxkbfile** — `.../lib/`; `xorg_cv_malloc0_returns_null=no`.
- **xcb-util{,-image,-renderutil,-keysyms,-wm}** — `xcb.freedesktop.org/dist/`; dep libxcb.
- **pixman 0.42.2** — `.../individual/lib/`; `--disable-gtk`; **lib-only** (`make -C pixman install` + hand-copy `pixman-1.pc`).
- **libICE 1.1.1** — `xorg_cv_malloc0_returns_null=no`; `-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0`; **patch** `libICE-1.1.1-phoenix.patch`.
- **libSM 1.2.4** — `xorg_cv_malloc0_returns_null=no --without-libuuid`.
- **libXt 1.3.0** / **libXmu 1.2.1** — **`xorg_cv_malloc0_returns_null=yes`** `ac_cv_lib_m_hypot=yes` (load-bearing: adds `-DMALLOC_0_RETURNS_NULL -DXTMALLOC_BC`; wrong value aborts Xt apps).
- **libXpm 3.5.17** / **libXaw 1.0.16** — lib-only (tools need `getpwuid_r`/deferred syms).

### Layer 2 (xorg-fonts)
- **zlib 1.3.1** — `zlib.net/fossils/`; own configure `--static`.
- **libpng 1.6.40** — sourceforge; `--with-zlib-prefix`.
- **jpeg 9e** — `ijg.org/files/jpegsrc.v9e.tar.gz`.
- **freetype 2.13.2** — savannah; `--without-zlib --without-png --without-harfbuzz --without-bzip2 --without-brotli` (breaks freetype↔harfbuzz cycle).
- **libfontenc 1.1.8** / **libXfont2 2.0.6** (`ac_cv_lib_m_hypot=yes malloc0=no`, `-DO_NOFOLLOW=0 -DNOFILES_MAX=256`, lib-only, **patch** `libXfont2-2.0.6-phoenix-fdopen-rt.patch` now-redundant).
- **expat 2.5.0** — github R_2_5_0; `--without-docbook --without-examples --without-tests`.
- **fontconfig 2.14.2** — freedesktop; in-tree build; needs **gperf**; cache vars `ac_cv_func_random=no initstate=no setstate=no random_r=no`; **2 inline perl patches** (fccache.c timercmp redefine, fccompat.c FcRandom lazy-seed).
- **libXft 2.3.8** — `PKG_CONFIG="pkg-config --static"` + explicit FREETYPE/FONTCONFIG/XRENDER libs.
- **cairo 1.16.0** — `--enable-ft --enable-fc --enable-png --disable-xlib --disable-xcb --disable-gl --disable-{script,ps,pdf,svg,interpreter}`; `ax_cv_c_float_words_bigendian=no`; core-lib-only.
- **harfbuzz 2.6.7** — github; `--with-freetype=yes --with-glib=yes --with-gobject=no --with-icu=no --with-cairo=no`; needs g++ + glib `.pc`.
- **fribidi 1.0.13** — github; `make CFLAGS="-O2 -std=gnu11"` (override forced `-ansi`).
- **pango 1.42.4** — gnome; `--with-cairo --without-xft --disable-introspection --disable-gtk-doc`; inline `.pc` gen + `cairo.la` sed fix; host-isolated `PKG_CONFIG_LIBDIR`.

### Layer 3 (xorg-server)
- **libmd** — local `tools/x11-port/libmd-phoenix/{sha1.c,sha1.h}`; `--with-sha1=libmd`.
- **xorg-server 1.20.14** — `x.org/releases/individual/xserver/`; `--enable-kdrive --disable-{xephyr,xorg,xwayland,xnest,xvfb,dmx,glamor,dri,dri2,dri3,glx,int10-module,vgahw,vbe,xdmcp,xinerama} --with-sha1=libmd --without-dtrace --disable-{systemd-logind,secure-rpc,config-udev,config-hal,unit-tests} --without-systemd-daemon`; **patch** `xorg-server-1.20.14-record-malloc0.patch`.
- **Xphoenix (fbdev DDX)** — local `ddx/{fbdev.c,fbdev_stub.c,ddxLoad.c,hid_evdev_map.h}`; hand-`ld` link of 25 core archives with `--start-group`; `-L$SYSROOT/lib` FIRST; requires `xkb/gen-builtin-keymap.sh` first.

## Phoenix patches to preserve (patches/)
| File | Purpose |
|------|---------|
| libxcb-1.16-phoenix.patch | `<arpa/inet.h>` for htonl; `MSG_TRUNC`/`MSG_CTRUNC`=0 |
| libX11-1.8.7-phoenix-fontset-basename-ownership-58.patch | heap-copy base_name_list (don't Xfree .rodata) |
| libICE-1.1.1-phoenix.patch | drop K&R `long time();` clashing with `<time.h>` |
| libXfont2-2.0.6-phoenix-fdopen-rt.patch | `fdopen(fd,"rt")`→`"r"` (now redundant) |
| xorg-server-1.20.14-record-malloc0.patch | RECORD early-return on numContexts==0 (malloc(0)=NULL) |
| xterm-396-phoenix.patch | SVR4 pty/pgrp (`USE_SYSV_PGRP`+`USE_USG_PTYS`, `/dev/ptmx`) |
| xedit-1.2.2-phoenix-lispbegin-savepackage-null.patch | guard PACKAGE=savepackage NULL |
| WindowMaker-0.95.9-phx-diag.patch | `phxfile:` direct-TTF font path + PHX_DIAG markers |
| WindowMaker-0.95.9-phoenix-getcommandforpid.patch | GetCommandForPid via threadsinfo() (no procfs) |

Inline script edits to reproduce: fontconfig timercmp/FcRandom + rand_r cache vars;
WindowMaker/jwm `SHELL`/`WMAKER_SHELL` `#ifndef` guards + `-Drint=round`
`-D_SC_LINE_MAX=5` `-include wmaker-phoenix-compat.h` `-Wl,-z,stack-size=0x100000`;
fribidi `-std=gnu11`; cairo `float_words_bigendian=no` + core-only; pango `.pc`
gen + `cairo.la` sed; server CFLAGS `-DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI
-D_POSIX_THREAD_SAFE_FUNCTIONS=200809L -DO_NOFOLLOW=0 -DSI_USER=0 -DHAVE_CBRT=1`;
DDX `-L$SYSROOT/lib` first + pre-group ddxLoad.c.

## Host build-tool prerequisites
Cross `aarch64-phoenix-{gcc,g++,ar,ranlib}` (g++ for harfbuzz); host `pkg-config`
(`--static`); host `python` (xcb-proto codegen, host build); **gperf** (fontconfig);
host DejaVu TTF + host X11 font/locale/encodings trees (for `stage-x11-runtime.sh`).

## Tricky bits for the framework port
1. **Prefix fragmentation** — today: `/tmp/x11-phoenix` (shared) + `/tmp/wmaker-deps`
   (snapshot copy with `.pc` perl-rewritten) + in-tree `src/*/.libs` + external glib.
   Ports needs ONE coherent sysroot; drop the snapshot+rewrite hack.
2. **Cross-script ordering bug (latent)** — cairo/pango consume fontconfig's *in-tree*
   artifact built only by `build-wmaker.sh`; no script sequences it first. In ports,
   **fontconfig = one shared port** that wmaker/cairo/pango all depend on.
3. **Configure-time header installs** — xorgproto, xcb-proto `.pc`, pixman, freetype,
   fontconfig must be *installed* before dependents `./configure`; the `share/pkgconfig`
   vs `lib/pkgconfig` split forces BOTH on PKG_CONFIG_PATH.
4. **HOST vs TARGET** — xcb-proto/gperf/pkg-config/python run on host; separate them.
5. **freetype↔harfbuzz cycle** broken via freetype `--without-harfbuzz`.
6. **glib** external prereq (`tools/ports/src/glib-2.56.4`) — declare as dep.
7. **malloc0 inconsistency** — libXt/libXmu need `=yes`, all others `=no`; bake per-recipe.
8. **Lib-only installs** (pixman, libXpm, libXaw, libXfont2) — tools fail to link;
   normal `make install` will fail → install `.a`+headers+`.pc` only.
9. **Server not a normal install** — hand-`ld` `--start-group`, out-of-tree DDX,
   generated XKB keymap, `-L$SYSROOT/lib` first (beat stale toolchain libphoenix).
10. **gdk-pixbuf** not ported (future work).
11. **Runtime assets host-sourced** (`stage-x11-runtime.sh` copies host X11 locale/font/
    encodings). Ports build binaries only; either build upstream font/xkb/locale
    packages or document the host-copy dependency.

## Migration order (recommended)
1. `xorg-libs` (Layer 1) staging to `$PREFIX_SYSROOT` — get the whole layer building
   in-framework; validate `.a`+headers+`.pc` land, one Pi smoke (e.g. twm or xeyes
   still runs) before moving up.
2. `xorg-fonts` (Layer 2) depending on xorg-libs.
3. `xorg-server` (Layer 3) → Xphoenix.
4. Rewire the existing `windowmaker`/`xterm` ports off `/tmp` onto the sysroot; add
   the remaining app ports (jwm/twm/xcalc/xclock/xeyes/xlogo/oclock/ico/xbill).
5. Runtime-asset staging as a data step.
Keep `tools/x11-port/` until each layer is validated in-framework; retire piecewise.

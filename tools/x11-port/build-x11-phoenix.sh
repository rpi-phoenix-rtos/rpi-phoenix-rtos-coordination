#!/usr/bin/env bash
#
# Phoenix-RTOS — X11 (tinyx/kdrive) library port for aarch64-rpi4b
#
# Cross-compiles the X11 library stack for Phoenix-RTOS into an ISOLATED prefix
# (/tmp/x11-phoenix), bottom-up in dependency order. This is HOST-SIDE build work
# only — it never touches the flagship netboot image / default components (the X
# libs live beside the OS until a server is runnable). Each lib self-verifies as
# "cross-compiles + installs into the prefix".
#
# Status (2026-06-17): base-lib tier builds — xorgproto, libXau, xtrans, libXdmcp.
# Next bricks: libxcb (needs xcb-proto python codegen), then libX11, then the
# kdrive Xfbdev server. See PROGRESS.md for the ladder + blocker inventory.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
#
set -u

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$HERE/src"
PATCHDIR="$HERE/patches"
XBASE=https://www.x.org/releases/individual
XARCHIVE=https://xorg.freedesktop.org/archive/individual

mkdir -p "$PREFIX" "$SRC"
# share/pkgconfig holds xcb-proto.pc; lib/pkgconfig holds the rest.
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"

# apply_patches <name-version> — apply patches/<nv>*.patch (idempotent, -N).
apply_patches() {
	local nv=$1 p
	for p in "$PATCHDIR/$nv"*.patch; do
		[ -f "$p" ] && (cd "$SRC/$nv" && patch -p1 -N <"$p" >/dev/null 2>&1)
	done
	return 0
}

# host_build <name-version> <url> — build a HOST tool/package (e.g. xcb-proto's
# python codegen), no cross-compile.
host_build() {
	local nv=$1 url=$2
	fetch_extract "$nv" "$url" || return 1
	cd "$SRC/$nv" || return 1
	[ -f config.status ] || ./configure --prefix="$PREFIX" >"/tmp/$nv-conf.log" 2>&1 \
		|| { echo "$nv: CONFIGURE FAIL"; tail -3 "/tmp/$nv-conf.log"; return 1; }
	make install >"/tmp/$nv-build.log" 2>&1 || { echo "$nv: BUILD FAIL"; tail -4 "/tmp/$nv-build.log"; return 1; }
	echo "$nv: OK (host)"
}

# fetch_extract <name-version> <url>
fetch_extract() {
	local nv=$1 url=$2
	cd "$SRC" || return 1
	[ -d "$nv" ] && return 0
	timeout 90 curl -sSL -o "$nv.tar.gz" "$url" || { echo "$nv: FETCH FAIL"; return 1; }
	# Auto-detect compression: several upstream URLs are .tar.xz (xcb-proto,
	# libxcb, libpthread-stubs, ...) but are saved here as "$nv.tar.gz". `tar xf`
	# detects xz/gz/bz2 from the magic, so it handles all of them (a hardcoded
	# `xzf` fails on the .xz downloads with "EXTRACT FAIL").
	tar xf "$nv.tar.gz" || { echo "$nv: EXTRACT FAIL"; return 1; }
}

# autotools cross-build into $PREFIX (static libs). $3 = extra configure args.
xbuild() {
	local nv=$1 url=$2 extra=${3:-}
	fetch_extract "$nv" "$url" || return 1
	apply_patches "$nv"
	cd "$SRC/$nv" || return 1
	if [ ! -f config.status ]; then
		# shellcheck disable=2086
		./configure --host=aarch64-phoenix --prefix="$PREFIX" --disable-shared --enable-static \
			CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
			CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include ${XCFLAGS_EXTRA:-}" \
			LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib" $extra >"/tmp/$nv-conf.log" 2>&1 \
			|| { echo "$nv: CONFIGURE FAIL (see /tmp/$nv-conf.log)"; tail -4 "/tmp/$nv-conf.log"; return 1; }
	fi
	make install >"/tmp/$nv-build.log" 2>&1 || { echo "$nv: BUILD FAIL (see /tmp/$nv-build.log)"; tail -6 "/tmp/$nv-build.log"; return 1; }
	echo "$nv: OK"
}

# --- base-lib tier (builds cleanly for Phoenix as of 2026-06-17) ---
xbuild xorgproto-2023.2  "$XBASE/proto/xorgproto-2023.2.tar.gz"
xbuild libXau-1.0.11     "$XBASE/lib/libXau-1.0.11.tar.gz"
xbuild xtrans-1.5.0      "$XBASE/lib/xtrans-1.5.0.tar.gz"
xbuild libXdmcp-1.1.5    "$XBASE/lib/libXdmcp-1.1.5.tar.gz"

# --- XCB tier (builds for Phoenix; libxcb needs 2 Phoenix-gap patches + --disable-mitshm) ---
host_build xcb-proto-1.16.0       "$XARCHIVE/proto/xcb-proto-1.16.0.tar.xz"   # python codegen
xbuild     libpthread-stubs-0.5   "$XARCHIVE/lib/libpthread-stubs-0.5.tar.xz" # pthread-stubs.pc (pthread is in libc)
# libxcb: patches/libxcb-1.16-phoenix.patch adds <arpa/inet.h> (htonl macro) + MSG_TRUNC/
# MSG_CTRUNC no-op guards (Phoenix sys/socket.h lacks them; fd-passing unused).
xbuild     libxcb-1.16            "$XARCHIVE/lib/libxcb-1.16.tar.xz" "--disable-mitshm"

# --- libX11 (the core Xlib) — builds for Phoenix with these knobs (2026-06-18): ---
#   xorg_cv_malloc0_returns_null=no : cross-compile run-test cache (Phoenix malloc(0)!=NULL)
#   -DMAXHOSTNAMELEN=256            : Phoenix headers lack it
#   -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L
#                                   : route Xos_r.h to the POSIX getpwnam_r/getpwuid_r path
#                                     (needs libphoenix getpwuid_r/getpwnam_r + sys/poll.h, added 89d1543)
XCFLAGS_EXTRA="-DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L" \
xbuild libX11-1.8.7 "$XBASE/lib/libX11-1.8.7.tar.gz" \
	"--without-xmlto --disable-specs --disable-devel-docs xorg_cv_malloc0_returns_null=no"

# --- extension + rendering libs (build for Phoenix as of 2026-06-18) ---
xbuild libXext-1.3.5     "$XBASE/lib/libXext-1.3.5.tar.gz"     "xorg_cv_malloc0_returns_null=no"
xbuild libXrender-0.9.11 "$XBASE/lib/libXrender-0.9.11.tar.gz" "xorg_cv_malloc0_returns_null=no"
xbuild libXrandr-1.5.4   "$XBASE/lib/libXrandr-1.5.4.tar.gz"   "xorg_cv_malloc0_returns_null=no"

# --- server-side X libs (for the kdrive xorg-server, 2026-06-18) ---
# libxkbfile (xkb keymap parsing) + the xcb-util family (Xephyr's xcb helpers). All static.
xbuild libxkbfile-1.1.3  "$XBASE/lib/libxkbfile-1.1.3.tar.gz"  "xorg_cv_malloc0_returns_null=no"
XCBB=https://xcb.freedesktop.org/dist
xbuild xcb-util-0.4.1             "$XCBB/xcb-util-0.4.1.tar.gz"             ""
xbuild xcb-util-image-0.4.1       "$XCBB/xcb-util-image-0.4.1.tar.gz"       ""
xbuild xcb-util-renderutil-0.3.10 "$XCBB/xcb-util-renderutil-0.3.10.tar.gz" ""
xbuild xcb-util-keysyms-0.4.1     "$XCBB/xcb-util-keysyms-0.4.1.tar.gz"     ""
xbuild xcb-util-wm-0.4.2          "$XCBB/xcb-util-wm-0.4.2.tar.gz"          ""

# pixman (software rasteriser, independent). The LIBRARY builds; only pixman's test/ utils.c
# fails (its static gettime() clashes with Phoenix's non-standard sys/time.h gettime), so build
# + install just the pixman/ subdir and copy the .pc manually.
if [ ! -f "$PREFIX/lib/libpixman-1.a" ]; then
	fetch_extract pixman-0.42.2 "$XBASE/lib/pixman-0.42.2.tar.gz"
	( cd "$SRC/pixman-0.42.2" \
	  && ./configure --host=aarch64-phoenix --prefix="$PREFIX" --disable-shared --enable-static \
	       --disable-gtk CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	       CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include" LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib" \
	       >/tmp/pixman-conf.log 2>&1 \
	  && make -C pixman install >/tmp/pixman-build.log 2>&1 \
	  && cp pixman-1.pc "$PREFIX/lib/pkgconfig/" && echo "pixman-0.42.2: OK (lib only)" ) \
	  || echo "pixman-0.42.2: FAIL (see /tmp/pixman-*.log)"
fi

# --- font tier (build for Phoenix as of 2026-06-18) ---
# zlib (libfontenc needs zlib.h). zlib uses its own configure (not autotools --host).
if [ ! -f "$PREFIX/lib/libz.a" ]; then
	fetch_extract zlib-1.3.1 "https://zlib.net/fossils/zlib-1.3.1.tar.gz"
	( cd "$SRC/zlib-1.3.1" && CC="${TC}gcc --sysroot=$SYSROOT" AR="${TC}ar" RANLIB="${TC}ranlib" \
	  ./configure --prefix="$PREFIX" --static >/tmp/zlib-conf.log 2>&1 && make install >/tmp/zlib-build.log 2>&1 \
	  && echo "zlib-1.3.1: OK" ) || echo "zlib-1.3.1: FAIL"
fi
# libpng (WRaster's PNG image backend — Window Maker theme pixmaps, switch-panel
# images, and PNG backgrounds. Needs zlib, above). Without it WRaster loads only
# XPM and silently fails every .png (the swback.png/theme cluster). Static.
if [ ! -f "$PREFIX/lib/libpng16.a" ]; then
	fetch_extract libpng-1.6.40 "https://download.sourceforge.net/libpng/libpng-1.6.40.tar.gz"
	( cd "$SRC/libpng-1.6.40" \
	  && ./configure --host=aarch64-phoenix --prefix="$PREFIX" --disable-shared --enable-static \
	       CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	       CPPFLAGS="--sysroot=$SYSROOT -I$PREFIX/include" CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include" \
	       LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib" --with-zlib-prefix="$PREFIX" \
	       >/tmp/libpng-conf.log 2>&1 \
	  && make -j4 >/tmp/libpng-build.log 2>&1 && make install >/tmp/libpng-install.log 2>&1 \
	  && echo "libpng-1.6.40: OK" ) || { echo "libpng-1.6.40: FAIL"; tail -6 /tmp/libpng-*.log; }
fi
# libjpeg (IJG; WRaster's JPEG image backend — JPEG backgrounds/themes). Plain
# autotools, no SIMD/NASM dep. Static.
if [ ! -f "$PREFIX/lib/libjpeg.a" ]; then
	fetch_extract jpeg-9e "https://www.ijg.org/files/jpegsrc.v9e.tar.gz"
	( cd "$SRC/jpeg-9e" \
	  && ./configure --host=aarch64-phoenix --prefix="$PREFIX" --disable-shared --enable-static \
	       CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib CFLAGS="--sysroot=$SYSROOT" \
	       >/tmp/jpeg-conf.log 2>&1 \
	  && make -j4 >/tmp/jpeg-build.log 2>&1 && make install >/tmp/jpeg-install.log 2>&1 \
	  && echo "jpeg-9e: OK" ) || { echo "jpeg-9e: FAIL"; tail -6 /tmp/jpeg-*.log; }
fi
# freetype (minimal — no external codec deps). libXfont2's scalable-font backend.
xbuild freetype-2.13.2 "https://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.gz" \
	"--without-zlib --without-png --without-harfbuzz --without-bzip2 --without-brotli"
xbuild libfontenc-1.1.8 "$XBASE/lib/libfontenc-1.1.8.tar.gz"
# libXfont2: server-side font lib. Needs the cross run-test cache + several Phoenix gaps:
#   -DO_NOFOLLOW=0 (no symlinks), -DNOFILES_MAX=256 (missing limit), ac_cv_lib_m_hypot=yes.
# The libXfont2.a archive BUILDS; only an in-tree font *tool* link needs the hypot symbol, which
# lands in libm.a on the next libphoenix rebuild (hypot added in 6e2b929) — so the X server link
# will resolve it. We install the .a + headers directly.
XCFLAGS_EXTRA="-DO_NOFOLLOW=0 -DNOFILES_MAX=256" \
xbuild libXfont2-2.0.6 "$XBASE/lib/libXfont2-2.0.6.tar.gz" \
	"ac_cv_lib_m_hypot=yes xorg_cv_malloc0_returns_null=no" || true
[ -f "$SRC/libXfont2-2.0.6/.libs/libXfont2.a" ] && cp "$SRC/libXfont2-2.0.6/.libs/libXfont2.a" "$PREFIX/lib/" \
	&& ( cd "$SRC/libXfont2-2.0.6" && make install-data >/dev/null 2>&1 ) && echo "libXfont2-2.0.6: OK (lib + headers)"

# --- toolkit base (build for Phoenix as of 2026-06-18) — needed by traditional X clients ---
# libICE needs patches/libICE-1.1.1-phoenix.patch (drop old-K&R "long time();" vs <time.h>).
# libXt/libXmu need the same MT-safe-pwd + MAXHOSTNAMELEN defines as libX11.
PWD_DEFS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"
XCFLAGS_EXTRA="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0" \
	xbuild libICE-1.1.1 "$XBASE/lib/libICE-1.1.1.tar.gz" "xorg_cv_malloc0_returns_null=no"
XCFLAGS_EXTRA="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0" \
	xbuild libSM-1.2.4  "$XBASE/lib/libSM-1.2.4.tar.gz"  "xorg_cv_malloc0_returns_null=no --without-libuuid"
# NOTE (2026-06-24, twm "Cannot perform malloc" triage): Phoenix malloc(0) RETURNS
# NULL (libphoenix stdlib/malloc_dl.c). libXt's XtMalloc(0) is only guarded when
# MALLOC_0_RETURNS_NULL+XTMALLOC_BC are defined, which requires
# xorg_cv_malloc0_returns_null=yes (the run-test cache value; was wrongly forced =no).
# With =no, any XtMalloc(0)/XtCalloc(0,..)/XtRealloc(NULL,0) aborts with
# "Error: Cannot perform malloc". =yes adds -DMALLOC_0_RETURNS_NULL -DXTMALLOC_BC so
# those size-0 requests are bumped to size 1 instead of failing.
# The malloc0=yes flag below IS the fix; a diagnostic-only Alloc.c patch
# (libXt-1.3.0-alloc-diag.patch, printed the failing size) was removed once
# this root cause was confirmed.
XCFLAGS_EXTRA="$PWD_DEFS" \
	xbuild libXt-1.3.0  "$XBASE/lib/libXt-1.3.0.tar.gz"  "xorg_cv_malloc0_returns_null=yes ac_cv_lib_m_hypot=yes"
XCFLAGS_EXTRA="$PWD_DEFS" \
	xbuild libXmu-1.2.1 "$XBASE/lib/libXmu-1.2.1.tar.gz" "xorg_cv_malloc0_returns_null=yes ac_cv_lib_m_hypot=yes"
# libXpm: lib builds; its sxpm/cxpm tools link getpwuid_r (deferred, see below), so lib-only.
if [ ! -f "$PREFIX/lib/libXpm.a" ]; then
	fetch_extract libXpm-3.5.17 "$XBASE/lib/libXpm-3.5.17.tar.gz"
	( cd "$SRC/libXpm-3.5.17" && ./configure --host=aarch64-phoenix --prefix="$PREFIX" --disable-shared \
	    --enable-static xorg_cv_malloc0_returns_null=no CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $PWD_DEFS" LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib" \
	    >/tmp/libXpm-conf.log 2>&1; make -C src install >/tmp/libXpm-build.log 2>&1; make install-data >/dev/null 2>&1
	  PC=$(find . -name xpm.pc|head -1); [ -n "$PC" ] && cp "$PC" "$PREFIX/lib/pkgconfig/"
	  [ -f src/.libs/libXpm.a ] && cp src/.libs/libXpm.a "$PREFIX/lib/" && echo "libXpm-3.5.17: OK (lib only)" )
fi
# libXaw (Athena widgets) — builds once libphoenix carries the standard wide-char functions
# (wcsncpy/wcscpy/wcscat/wcschr/wcsrchr/wcsncmp/wmem* + mbtowc; committed libphoenix 0cb9f72,
# their HEADERS synced to the sysroot). Its tools also pull deferred libc symbols, so lib-only.
if [ ! -f "$PREFIX/lib/libXaw7.a" ]; then
	fetch_extract libXaw-1.0.16 "$XBASE/lib/libXaw-1.0.16.tar.gz"
	( cd "$SRC/libXaw-1.0.16" && ./configure --host=aarch64-phoenix --prefix="$PREFIX" --disable-shared \
	    --enable-static xorg_cv_malloc0_returns_null=no ac_cv_lib_m_hypot=yes CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $PWD_DEFS" LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib" \
	    >/tmp/libXaw-conf.log 2>&1
	  make install >/tmp/libXaw-build.log 2>&1
	  [ -f "$PREFIX/lib/libXaw7.a" ] && echo "libXaw-1.0.16: OK" || echo "libXaw-1.0.16: FAILED (see /tmp/libXaw-build.log)" )
fi

# --- THE EXE BOUNDARY (CROSSED 2026-06-18) ---
# All X executables LINK against libc, so they need the libphoenix additions present as SYMBOLS in
# libc.a/libm.a. All the libc fixes are now COMMITTED in libphoenix (getpw*_r/sys-poll 89d1543;
# hypot 6e2b929; wide-char+multibyte 0cb9f72/e29c840). After a libphoenix rebuild
# (./scripts/rebuild-rpi4b-fast.sh --scope core --build-only) the on-device libc carries them.
# IMPORTANT: the cross-toolchain bundles its OWN copy of libphoenix.a/libc.a/libm.a under
# .toolchain/aarch64-phoenix/aarch64-phoenix/lib/ and the auto-linked libc comes from THERE, not the
# build sysroot. After a libphoenix change, sync that bundle so X exes link the fresh libc:
#   cp <sysroot>/lib/lib{phoenix,c,m}.a .toolchain/aarch64-phoenix/aarch64-phoenix/lib/
# (sync_toolchain_libc below does this.) Then X executables link cleanly.

sync_toolchain_libc() {
	local tclib="$REPO_ROOT/.toolchain/aarch64-phoenix/aarch64-phoenix/lib"
	[ -d "$tclib" ] || return 0
	for l in libphoenix.a libc.a libm.a; do
		[ -f "$SYSROOT/lib/$l" ] && cp "$SYSROOT/lib/$l" "$tclib/$l"
	done
	echo "synced fresh libphoenix/libc/libm into the toolchain bundle"
}

# --- apps: twm (the first real X11 application proven to build for aarch64-phoenix, 2026-06-18) ---
# twm exercises the full toolkit (libXmu/libXt/libXext/libX11/libxcb/libSM/libICE) + libXrandr.
# Two non-obvious knobs: PKG_CONFIG="pkg-config --static" (so the static private deps xcb/Xau/Xdmcp
# are on the link line) and LDFLAGS -L$SYSROOT/lib. It LINKS into a static aarch64-phoenix ELF; it
# cannot RUN until the X server exists, but it proves the entire client+toolkit+libc link closure.
build_twm() {
	sync_toolchain_libc
	if [ -f "$PREFIX/bin/twm" ]; then echo "twm: already built"; return 0; fi
	fetch_extract twm-1.0.12 "$XBASE/app/twm-1.0.12.tar.gz" || return 1
	( cd "$SRC/twm-1.0.12" && PKG_CONFIG="pkg-config --static" ./configure --host=aarch64-phoenix \
	    --prefix="$PREFIX" CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $PWD_DEFS" \
	    LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib -L$SYSROOT/lib" >/tmp/twm-conf.log 2>&1
	  make install >/tmp/twm-build.log 2>&1
	  [ -f "$PREFIX/bin/twm" ] && echo "twm-1.0.12: OK (aarch64-phoenix ELF)" || echo "twm: FAILED (see /tmp/twm-build.log)" )
}
# --- apps: xeyes (a real, iconic upstream X11 app — 2026-06-18) ---
# xeyes-1.1.2 chosen deliberately: it predates the XInput2/libXi dependency of 1.2.x, so it needs
# only libXt/libXmu/libXext(SHAPE)/libXrender — all already built — and links a static
# aarch64-phoenix ELF. (1.2.x would require building libXi first.) Proves a genuine, recognizable
# X client builds for Phoenix, not just a hand-written demo. Runs once the fbdev DDX server lands.
build_xeyes() {
	sync_toolchain_libc
	if [ -f "$PREFIX/bin/xeyes" ]; then echo "xeyes: already built"; return 0; fi
	fetch_extract xeyes-1.1.2 "$XBASE/app/xeyes-1.1.2.tar.gz" || return 1
	( cd "$SRC/xeyes-1.1.2" && PKG_CONFIG="pkg-config --static" ./configure --host=aarch64-phoenix \
	    --prefix="$PREFIX" CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $PWD_DEFS" \
	    LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib -L$SYSROOT/lib" >/tmp/xeyes-conf.log 2>&1
	  make install >/tmp/xeyes-build.log 2>&1
	  [ -f "$PREFIX/bin/xeyes" ] && echo "xeyes-1.1.2: OK (aarch64-phoenix ELF)" || echo "xeyes: FAILED (see /tmp/xeyes-build.log)" )
}

# --- apps: the self-contained native Xlib drawing client (no fetch; source in apps/) ---
build_xphxdemo() { ( cd "$HERE/apps" && ./build.sh ); }

# Build apps only when explicitly requested (needs the libphoenix rebuild done first).
if [ "${1:-}" = "--with-apps" ]; then
	build_twm
	build_xeyes
	build_xphxdemo
fi

# --- THE SERVER (2026-06-23): kdrive fbdev DDX -> Xphoenix ---
# xorg-server 1.20.14 ships no Xfbdev (removed in 1.17), so the fbdev backend is fresh new code:
# hw/kdrive/fbdev/fbdev.c (KdCardFuncs + DDX hooks + shadow-FB write()-blit to /dev/fb0). It links
# against the already-built kdrive core archives + the X11 lib stack here. build-xfbdev.sh does the
# compile+link (it expects the core archives under src/xorg-server-1.20.14/*/.libs/, produced by the
# server ./configure + make documented in PROGRESS.md). Produces a static aarch64-phoenix Xphoenix ELF.
if [ "${1:-}" = "--with-server" ]; then
	"$HERE/build-xfbdev.sh"
fi

echo "=== installed X11 libs in $PREFIX/lib ==="
ls "$PREFIX/lib/"*.a 2>/dev/null || echo "(none yet)"

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

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
HERE="$(cd "$(dirname "$0")" && pwd)"
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
	tar xzf "$nv.tar.gz" || { echo "$nv: EXTRACT FAIL"; return 1; }
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
XCFLAGS_EXTRA="$PWD_DEFS" \
	xbuild libXt-1.3.0  "$XBASE/lib/libXt-1.3.0.tar.gz"  "xorg_cv_malloc0_returns_null=no ac_cv_lib_m_hypot=yes"
XCFLAGS_EXTRA="$PWD_DEFS" \
	xbuild libXmu-1.2.1 "$XBASE/lib/libXmu-1.2.1.tar.gz" "xorg_cv_malloc0_returns_null=no ac_cv_lib_m_hypot=yes"
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

# --- THE EXE BOUNDARY ---
# All X executables (apps like twm/xclock AND the server) LINK against libc, so they need the
# libphoenix additions IN libc.a/libm.a — currently only the HEADERS are synced to the sysroot; the
# SYMBOLS (getpwnam_r/getpwuid_r/hypot/mbtowc/wcsncpy/wcscpy/...) land on a libphoenix rebuild. All
# the libc fixes are now COMMITTED in libphoenix (89d1543/6e2b929/0cb9f72); so the only remaining
# step before linking any X exe is to rebuild libphoenix. The static LIBRARIES above all build today.

# --- next brick (the big one) ---
# The kdrive Xfbdev server (xorg-server). Needs the on-device libc to carry the libphoenix
# additions (getpwnam_r/getpwuid_r/hypot/sys/poll.h) — rebuild libphoenix first. Expect heavy
# OS-integration work (kdrive backend, input via /dev/kbd0+/dev/mouse0, shadow-FB + write()-blit
# to /dev/fb0) + a stream of further Phoenix libc gaps. This is the multi-session frontier.

echo "=== installed X11 libs in $PREFIX/lib ==="
ls "$PREFIX/lib/"*.a 2>/dev/null || echo "(none yet)"

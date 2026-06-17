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
SRC="$(cd "$(dirname "$0")" && pwd)/src"
XBASE=https://www.x.org/releases/individual

mkdir -p "$PREFIX" "$SRC"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"

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
	cd "$SRC/$nv" || return 1
	if [ ! -f config.status ]; then
		# shellcheck disable=2086
		./configure --host=aarch64-phoenix --prefix="$PREFIX" --disable-shared --enable-static \
			CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
			CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include" \
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

# --- next bricks (TODO) ---
# libxcb needs xcb-proto (python codegen) + the xcb util headers; --disable-mitshm
# (no shm_open in libphoenix). Then libX11 (--host + xcb + xtrans + xorgproto,
# --disable-xcb-sloppy-lock, --without-xmlto). Then the kdrive Xfbdev server.

echo "=== installed X11 libs in $PREFIX/lib ==="
ls "$PREFIX/lib/"*.a 2>/dev/null || echo "(none yet)"

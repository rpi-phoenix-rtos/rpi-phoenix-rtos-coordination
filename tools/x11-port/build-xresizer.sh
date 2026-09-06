#!/bin/sh
#
# Phoenix-RTOS — build xresizer, the mouse-free window-RESIZE probe for the X
# servers (owner bug #1).  Single .c file, linked static against the ported X
# libs that the xorg_libs framework port installs into the buildroot.
#
# Usage: ./build-xresizer.sh   (stages into the LIVE fsid=0 NFS export)
set -eu

TOOLS=$(cd "$(dirname "$0")" && pwd)
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
BUILD=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b
SYSROOT=${BUILD}/sysroot
ART=${ROOT}/artifacts/x11
SRC="$TOOLS/xresizer/xresizer.c"
OUT="$TOOLS/xresizer/xresizer"

# The live root export is the one declared fsid=0; a stale hardcoded path stages
# into a DEAD directory and the Pi then runs an old binary (see the memory note).
NFS="${SHOWCASE_STAGE_DIR:-$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' \
	/etc/exports /etc/exports.d/*.exports 2>/dev/null || true)}"

fail() { echo "ERROR: $*" >&2; exit 1; }
[ -f "$BUILD/lib/libX11.a" ] || fail "$BUILD/lib/libX11.a missing — build the xorg_libs port first"

XCLOSURE="-Wl,--start-group -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"

"${TC}gcc" --sysroot="$SYSROOT" -I"$BUILD/include" -Wall -O2 -static \
	-o "$OUT" "$SRC" -L"$BUILD/lib" -L"$SYSROOT/lib" $XCLOSURE

file "$OUT" | grep -q "ARM aarch64" || fail "binary is not an aarch64 ELF"
mkdir -p "$ART"; cp "$OUT" "$ART/xresizer"

if [ -n "$NFS" ] && [ -d "$NFS/bin" ]; then
	sudo cp "$OUT" "$NFS/bin/xresizer" && sudo chmod 755 "$NFS/bin/xresizer"
	echo "[OK] staged -> $NFS/bin/xresizer  (run: startx_gpu xresizer)"
else
	echo "[OK] built -> $ART/xresizer  (no live export found; not staged)"
fi

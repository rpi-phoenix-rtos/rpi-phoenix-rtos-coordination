#!/bin/sh
#
# Phoenix-RTOS — build xcolortest, a colour-correctness probe for the fbdev X
# server. It draws labelled rectangles in known colours via XAllocColor (the
# same path wmaker/xbill use) and logs each allocated pixel value, so a HDMI
# snapshot + the UART log together prove requested RGB -> packed pixel ->
# displayed colour. Single .c file, linked static against the ported X libs.
#
# Usage: ./build-xcolortest.sh   (stages to the NFS export /srv/phoenix-rpi4-nfs)
set -eu

TOOLS=$(cd "$(dirname "$0")" && pwd)
TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
NFS=/srv/phoenix-rpi4-nfs
ART=/home/houp/phoenix-rpi/artifacts/x11
SRC="$TOOLS/xcolortest/xcolortest.c"
OUT="$TOOLS/xcolortest/xcolortest"

fail() { echo "ERROR: $*" >&2; exit 1; }
[ -f "$PREFIX/lib/libX11.a" ] || fail "$PREFIX/lib/libX11.a missing — run build-x11-phoenix.sh first"

# libXext/libX11/libxcb closure (xcolortest only needs core Xlib).
XCLOSURE="-Wl,--start-group -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"

"${TC}gcc" --sysroot="$SYSROOT" -I"$PREFIX/include" -static \
    -o "$OUT" "$SRC" -L"$PREFIX/lib" -L"$SYSROOT/lib" $XCLOSURE

file "$OUT" | grep -q "ARM aarch64" || fail "binary is not an aarch64 ELF"
mkdir -p "$ART"; cp "$OUT" "$ART/xcolortest"
if [ -d "$NFS/bin" ]; then
    cp "$OUT" "$NFS/bin/xcolortest"; chmod 755 "$NFS/bin/xcolortest"
    echo "[OK] staged -> $NFS/bin/xcolortest  (run: startx xcolortest)"
else
    echo "[OK] built -> $ART/xcolortest  (NFS export not mounted; not staged)"
fi

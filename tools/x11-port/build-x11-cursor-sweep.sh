#!/bin/bash
#
# build-x11-cursor-sweep.sh - build the cursor-motion test driver (x11-cursor-sweep).
# A minimal Xlib client (no GL) that warps the pointer to exercise the shadow-RAM
# cursor over static content. Needs the ported X client libs (build-x11-phoenix.sh
# → /tmp/x11-phoenix). Output: /tmp/x11-cursor-sweep (stage into an NFS export /bin).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TC="${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
XPREFIX="/tmp/x11-phoenix"
SYSROOT="${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot"
SRC="${ROOT}/tools/x11-port/x11-cursor-sweep.c"
OUT="/tmp/x11-cursor-sweep"

[ -x "$TC" ] || { echo "FAIL: toolchain gcc not found: $TC" >&2; exit 1; }
[ -f "$XPREFIX/lib/libX11.a" ] || { echo "FAIL: missing $XPREFIX/lib/libX11.a (run build-x11-phoenix.sh)" >&2; exit 1; }
# libiconv sometimes lives only in the sysroot; mirror it into the X prefix.
[ -f "$XPREFIX/lib/libiconv.a" ] || cp "$SYSROOT/lib/libiconv.a" "$XPREFIX/lib/" 2>/dev/null || true

echo "=== compiling + linking x11-cursor-sweep ==="
"$TC" "$SRC" -o "$OUT" \
	-I"$XPREFIX/include" -L"$XPREFIX/lib" -L"$SYSROOT/lib" \
	-Wl,--start-group -lX11 -lxcb -lXau -lXdmcp -liconv -Wl,--end-group -lm \
	-Wl,-z,stack-size=8388608
echo "=== OK: $OUT ==="
file "$OUT"

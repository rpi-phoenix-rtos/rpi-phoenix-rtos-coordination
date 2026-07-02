#!/usr/bin/env bash
#
# Phoenix-RTOS — build /bin/dillo-dbg: the Dillo browser RELINKED with the
# --wrap allocator tracer (tools/ports/dillo-dbg-wrap.c) so the main session can
# capture the FREE-TRACE/FREE-BT smoking-gun line for the #58-class startup
# bad-free abort (status 0x46), then addr2line it against the binary.
#
# Strategy: Dillo's normal build (build-dillo.sh) must have already produced the
# 53 src/*.o objects + the dlib/dpip/IO/dw/lout static libs. We do NOT rebuild
# them; we recompile only the tiny wrap shim (-O0 -g -fno-omit-frame-pointer so
# __builtin_return_address(0) — the direct free() caller — is exact) and RELINK
# the existing objects with -Wl,--wrap=malloc,calloc,realloc,free, putting the
# wrap object first so its __wrap_* symbols win. The same link closure as the
# release dillo (full --start-group X11/FLTK/image group).
#
# Output: $NFS/bin/dillo-dbg + artifacts/x11/dillo-dbg (for addr2line).
# Host-side build only; does NOT boot/flash the Pi.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
XPREFIX=/tmp/x11-phoenix
FPREFIX=/tmp/fltk-phoenix
SRC=${ROOT}/tools/ports/src
XDIR=$SRC/dillo-3.2.0
ART=${ROOT}/artifacts/x11
NFS=/srv/phoenix-rpi4-nfs
WRAPSRC=${ROOT}/tools/ports/dillo-dbg-wrap.c
SHIM=${ROOT}/tools/ports/dillo-phoenix-shim.h

fail() { echo "FAIL: $*"; exit 1; }

[ -d "$XDIR/src" ]                  || fail "$XDIR/src missing — run build-dillo.sh first"
[ -f "$XDIR/src/dillo.o" ]          || fail "src/dillo.o missing — run build-dillo.sh first (need the .o objects)"
[ -f "$XDIR/dlib/libDlib.a" ]       || fail "dlib/libDlib.a missing — run build-dillo.sh first"
[ -f "$WRAPSRC" ]                   || fail "$WRAPSRC missing"
[ -f "$FPREFIX/lib/libfltk.a" ]     || fail "$FPREFIX/lib/libfltk.a missing"

# --- compile the wrap shim (-O0 -g -fno-omit-frame-pointer) ------------------
WRAPOBJ=$XDIR/src/dillo-dbg-wrap.o
echo "=== compiling allocator wrap shim ==="
${TC}gcc --sysroot="$SYSROOT" -O0 -g -fno-omit-frame-pointer \
	-c "$WRAPSRC" -o "$WRAPOBJ" || fail "wrap shim compile failed"

# --- gather the exact object list the release dillo links --------------------
# (mirrors am_dillo_OBJECTS; glob is stable because build-dillo.sh produced them)
OBJS=$(ls "$XDIR"/src/*.o | grep -v 'dillo-dbg-wrap.o')

# --- the static lib closure (mirrors dillo_LDADD, wrapped start/end-group) ---
DEPLIBS="\
$XDIR/dlib/libDlib.a \
$XDIR/dpip/libDpip.a \
$XDIR/src/IO/libDiof.a \
$XDIR/dw/libDw-widgets.a \
$XDIR/dw/libDw-fltk.a \
$XDIR/dw/libDw-core.a \
$XDIR/lout/liblout.a"

OUT=$XDIR/src/dillo-dbg
echo "=== relinking dillo-dbg (--wrap malloc/calloc/realloc/free) ==="
# -g keeps the dbg symbols for addr2line; wrap object FIRST so __wrap_* win;
# --wrap flags route malloc/free through the shim.
${TC}g++ --sysroot="$SYSROOT" -g \
	-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free \
	"$WRAPOBJ" $OBJS \
	$DEPLIBS \
	-ljpeg -lpng16 \
	-L"$FPREFIX/lib" -L"$XPREFIX/lib" -L"$SYSROOT/lib" \
	-Wl,--start-group \
	  -lfltk_images -lfltk -lpng16 -ljpeg -lz \
	  -lX11 -lxcb -lXau -lXdmcp \
	  -lstdc++ -lm -lpthread -lphoenix -lc \
	-Wl,--end-group \
	-lz -liconv \
	-o "$OUT" >/tmp/dillo-dbg-link.log 2>&1 || { tail -50 /tmp/dillo-dbg-link.log; fail "dillo-dbg link failed"; }

[ -x "$OUT" ] || fail "dillo-dbg not produced"

# --- PRE-FLIGHT VALIDATION ---------------------------------------------------
echo "=== PRE-FLIGHT ==="
file "$OUT"
case "$(file "$OUT")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*"ARM aarch64"*) echo "[WARN] aarch64 ELF but not reported static" ;;
	*) fail "dillo-dbg is not an aarch64 ELF" ;;
esac

echo "=== __wrap_* present? ==="
if ${TC}nm "$OUT" 2>/dev/null | grep -qE ' [Tt] __wrap_free'; then
	echo "[OK] __wrap_free linked in"
else
	fail "__wrap_free NOT in binary — --wrap did not take"
fi
${TC}nm "$OUT" 2>/dev/null | grep -E ' [Tt] __wrap_(malloc|calloc|realloc|free)' | sed 's/^/  /'

echo "=== undefined symbols (nm -u) ==="
und=$(${TC}nm -u "$OUT" 2>/dev/null)
if [ -z "$und" ]; then
	echo "[OK] 0 undefined symbols"
else
	echo "$und"
	fail "undefined symbols present"
fi

# --- stage -------------------------------------------------------------------
mkdir -p "$NFS/bin" "$ART"
cp "$OUT" "$NFS/bin/dillo-dbg"
cp "$OUT" "$ART/dillo-dbg"

echo "=== dillo-dbg staged ==="
ls -la "$NFS/bin/dillo-dbg"
echo "binary:   $NFS/bin/dillo-dbg"
echo "artifact: $ART/dillo-dbg   (addr2line target)"
echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="

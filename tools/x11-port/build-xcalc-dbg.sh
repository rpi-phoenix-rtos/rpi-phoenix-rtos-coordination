#!/usr/bin/env bash
#
# Phoenix-RTOS — build xcalc-dbg: an allocator-traced xcalc for the #58
# "Double free detected" triage.
#
# Identical to the normal xcalc build, except it links in apps/xcalc-dbg-wrap.c
# and wraps malloc/calloc/realloc/free so that the OFFENDING free is reported
# (with the pointer + whether it was ever malloc'd) on the UART *before*
# libphoenix's free() aborts the process. See xcalc-dbg-wrap.c for the
# hypothesis-A / hypothesis-B discriminator.
#
# Host-side build only. Stages to $NFS/bin/xcalc-dbg. Run on the Pi exactly like
# xcalc:
#     export XFILESEARCHPATH=/usr/share/X11/app-defaults/%N
#     startx xcalc-dbg     (or launch xcalc-dbg under twm)
# and read the FREE-TRACE: line(s) on the UART.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

APP=xcalc
VER=1.1.2
NV=$APP-$VER

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
TOOLS=/home/houp/phoenix-rpi/tools/x11-port
SRC=$TOOLS/src
XDIR=$SRC/$NV
WRAP=$TOOLS/apps/xcalc-dbg-wrap.c
ART=/home/houp/phoenix-rpi/artifacts/x11
NFS=/srv/phoenix-rpi4-nfs

APP_CFLAGS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"
XCLOSURE="-Wl,--start-group -lXaw7 -lXmu -lXt -lSM -lICE -lXpm -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"
WRAPFLAGS="-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"

fail() { echo "FAIL: $*"; exit 1; }

[ -f "$PREFIX/lib/libXaw7.a" ] || { "$TOOLS/build-xcalc.sh" || fail "base xcalc build failed"; }
[ -f "$WRAP" ] || fail "$WRAP missing"
[ -d "$XDIR" ] || fail "$XDIR missing (run build-xcalc.sh first)"

echo "=== compiling wrap tracer (aarch64-phoenix) ==="
${TC}gcc --sysroot="$SYSROOT" -g -O0 -I"$PREFIX/include" -c "$WRAP" \
    -o "$XDIR/xcalc-dbg-wrap.o" || fail "wrap compile failed"

echo "=== compiling xcalc objects (aarch64-phoenix) ==="
for f in xcalc math actions; do
	${TC}gcc --sysroot="$SYSROOT" -g -O0 -DHAVE_CONFIG_H -I"$XDIR" -I"$PREFIX/include" \
	    $APP_CFLAGS \
	    -c "$XDIR/$f.c" -o "$XDIR/$f.dbg.o" \
	    >/tmp/xcalc-dbg-$f.log 2>&1 || { tail -20 /tmp/xcalc-dbg-$f.log; fail "$f compile failed"; }
done

echo "=== linking xcalc-dbg ==="
${TC}gcc --sysroot="$SYSROOT" -static -g -O0 -L"$PREFIX/lib" -L"$SYSROOT/lib" \
    -o "$XDIR/xcalc-dbg" \
    "$XDIR/xcalc.dbg.o" "$XDIR/math.dbg.o" "$XDIR/actions.dbg.o" "$XDIR/xcalc-dbg-wrap.o" \
    $WRAPFLAGS $XCLOSURE \
    >/tmp/xcalc-dbg-link.log 2>&1 || { tail -30 /tmp/xcalc-dbg-link.log; fail "link failed"; }

[ -x "$XDIR/xcalc-dbg" ] || fail "xcalc-dbg not produced"

mkdir -p "$ART"; cp "$XDIR/xcalc-dbg" "$ART/xcalc-dbg"
echo "=== published -> $ART/xcalc-dbg ==="
if [ -d "$NFS/bin" ]; then
	cp "$XDIR/xcalc-dbg" "$NFS/bin/xcalc-dbg"; chmod 755 "$NFS/bin/xcalc-dbg"
	echo "=== staged -> $NFS/bin/xcalc-dbg ==="
fi

echo "=== PRE-FLIGHT ==="
file "$ART/xcalc-dbg"
case "$(file "$ART/xcalc-dbg")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$ART/xcalc-dbg" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols" || { echo "$und"; fail "undefined symbols present"; }
echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW: export XFILESEARCHPATH=/usr/share/X11/app-defaults/%N ; startx xcalc-dbg"
echo "    then read the FREE-TRACE: line on the UART."

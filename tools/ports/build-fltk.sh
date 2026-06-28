#!/usr/bin/env bash
#
# Phoenix-RTOS — cross-build FLTK 1.3.x (the Fast Light Toolkit GUI library) for
# aarch64-phoenix as a REUSABLE static library (libfltk.a + libfltk_images.a +
# headers). FLTK is the prerequisite GUI toolkit for the Dillo web browser (#53).
#
# FLTK is a C++ X11 client toolkit. It links against the already-built static X11
# client stack in /tmp/x11-phoenix (libX11/libxcb/libXau/libXdmcp + the image
# libs libpng16/libjpeg/libz). The deliverable here is the LIBRARY; the real
# pass/fail is that FLTK's own minimal test/hello.cxx (an Fl_Window + Fl_Box)
# links statically against the X closure with 0 undefined symbols.
#
# DELIBERATELY DISABLED (those libs are absent from /tmp/x11-phoenix and not
# needed for a baseline browser):
#   --disable-xft       no Xft/fontconfig -> FLTK falls back to core XLFD fonts
#   --disable-xinerama  no libXinerama (single-head fbdev anyway)
#   --disable-xcursor   no libXcursor   (FLTK uses core X cursors)
#   --disable-xfixes    no libXfixes
#   --disable-gl        no GLX yet on the Phoenix X stack
#   --disable-xdbe      no double-buffer extension lib
# IMAGE SUPPORT: png/jpeg/zlib come from the /tmp/x11-phoenix prefix (NOT the
# bundled copies) via --disable-localpng/localjpeg/localzlib + -I/-L into it.
#
# Host-side build only. Idempotent. Does NOT install into /tmp/x11-phoenix.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=fltk-1.3.10
URL=https://www.fltk.org/pub/fltk/1.3.10/$NV-source.tar.gz

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
XPREFIX=/tmp/x11-phoenix          # READ-ONLY: the shared X11 client lib stack
PREFIX=/tmp/fltk-phoenix          # our own install prefix
SRC=/home/houp/phoenix-rpi/tools/ports/src
XDIR=$SRC/$NV
ART=/home/houp/phoenix-rpi/artifacts/x11

fail() { echo "FAIL: $*"; exit 1; }

[ -f "$XPREFIX/lib/libX11.a" ] || fail "$XPREFIX/lib/libX11.a missing — build the X11 client stack first"

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV-source.tar.gz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 180 -o "$SRC/$NV-source.tar.gz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV-source.tar.gz" || fail "extract failed"
fi
[ -f "$XDIR/configure" ] || fail "$XDIR/configure missing"

# Refresh config.sub/config.guess if they don't know the "phoenix" OS triplet
# (1.3.10 already ships a phoenix-aware config.sub, but stay defensive).
for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		src=$(grep -lr phoenix "$SRC"/*/$cfg 2>/dev/null | head -1)
		[ -n "$src" ] && cp "$src" "$XDIR/$cfg" && echo "=== refreshed $cfg (phoenix-aware) ==="
	fi
done

# Common cross flags. Point the compiler + linker at BOTH the X11 prefix (X11 +
# image libs + their headers) and the cross sysroot. The shim aliases the C99
# rint()/rintf() (absent from Phoenix libm) onto round()/roundf().
SHIM=/home/houp/phoenix-rpi/tools/ports/fltk-phoenix-shim.h
XCFLAGS="--sysroot=$SYSROOT -I$XPREFIX/include -include $SHIM"
XLDFLAGS="--sysroot=$SYSROOT -L$XPREFIX/lib -L$SYSROOT/lib"

if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	# FLTK's png probe links `-lpng` WITHOUT `-lz`, which is a static-link false
	# negative against our prefix libpng16 (it needs zlib after it). Left to
	# autodetect, FLTK falls back to its bundled png/zlib. Seed the cache vars so
	# it takes the SYSTEM png/zlib path (prefix libpng16 + libz), avoiding a
	# bundled build and keeping image support consistent with the rest of the stack.
	( cd "$XDIR" && ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="$PREFIX" \
	    --enable-static --disable-shared \
	    --disable-gl --disable-xft --disable-xinerama --disable-xcursor \
	    --disable-xfixes --disable-xdbe \
	    --disable-localpng --disable-localjpeg --disable-localzlib \
	    --x-includes="$XPREFIX/include" --x-libraries="$XPREFIX/lib" \
	    CC=${TC}gcc CXX=${TC}g++ AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="$XCFLAGS" CXXFLAGS="$XCFLAGS" \
	    CPPFLAGS="$XCFLAGS" LDFLAGS="$XLDFLAGS" \
	    ac_cv_lib_png_png_read_info=yes \
	    ac_cv_lib_png_png_get_valid=yes \
	    ac_cv_lib_png_png_set_tRNS_to_alpha=yes \
	    >/tmp/fltk-conf.log 2>&1 ) || { tail -30 /tmp/fltk-conf.log; fail "configure failed"; }
fi

# Build the libraries only (src/). The top-level "all" also builds fluid (a cross
# aarch64 binary) and the test/ programs, and some test targets try to RUN fluid
# on the host -> breaks the build. We only need the .a files; we link the smoke
# test by hand below.
echo "=== building $NV libraries (src/) ==="
( cd "$XDIR/src" && make >/tmp/fltk-build.log 2>&1 ) || { tail -40 /tmp/fltk-build.log; fail "make src/ failed"; }

[ -f "$XDIR/lib/libfltk.a" ] || fail "libfltk.a not produced"

# Install headers + the static libs into our own prefix (idempotent).
mkdir -p "$PREFIX/lib" "$PREFIX/include"
cp -a "$XDIR"/lib/libfltk*.a "$PREFIX/lib/"
rm -rf "$PREFIX/include/FL"
cp -a "$XDIR/FL" "$PREFIX/include/FL"
# fltk-config (the helper that emits cflags/libs) — handy for downstream Dillo.
[ -f "$XDIR/fltk-config" ] && cp -a "$XDIR/fltk-config" "$PREFIX/"

echo "=== installed -> $PREFIX/lib ==="
ls -la "$PREFIX"/lib/libfltk*.a

# --- SMOKE TEST: link FLTK's own minimal hello (Fl_Window + Fl_Box) ----------
# This is the REAL deliverable test: a trivial FLTK program must link statically
# against the X closure with 0 undefined symbols.
echo "=== link smoke test: test/hello.cxx ==="
HELLO=/tmp/fltk-hello
# libfltk_images pulls in png/jpeg; fltk needs the X11 client closure + libm/c.
# --start-group/--end-group resolves the C++/X cross-references in any order.
${TC}g++ --sysroot="$SYSROOT" -static \
    -I"$PREFIX/include" -I"$XPREFIX/include" \
    "$XDIR/test/hello.cxx" -o "$HELLO" \
    -L"$PREFIX/lib" -L"$XPREFIX/lib" -L"$SYSROOT/lib" \
    -Wl,--start-group \
      -lfltk_images -lfltk -lpng16 -ljpeg -lz \
      -lX11 -lxcb -lXau -lXdmcp \
      -lstdc++ -lm -lphoenix -lc \
    -Wl,--end-group \
    >/tmp/fltk-hello.log 2>&1 || { tail -40 /tmp/fltk-hello.log; fail "hello.cxx link failed"; }

[ -x "$HELLO" ] || fail "hello binary not produced"
mkdir -p "$ART"; cp "$HELLO" "$ART/fltk-hello"

echo "=== PRE-FLIGHT ==="
file "$HELLO"
case "$(file "$HELLO")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "hello binary is not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$HELLO" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols in hello" || { echo "$und"; fail "undefined symbols present in hello link"; }

# Confirm key FLTK symbols are in the library.
echo "=== libfltk.a symbol spot-check ==="
n=$(${TC}nm "$PREFIX/lib/libfltk.a" 2>/dev/null | grep -cE " T .*(fl_open_display|Fl_Window)")
echo "Fl_Window / fl_open_display defined symbols: $n"
[ "$n" -gt 0 ] || fail "expected FLTK core symbols not found in libfltk.a"

echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "libfltk.a:        $PREFIX/lib/libfltk.a"
echo "libfltk_images.a: $PREFIX/lib/libfltk_images.a"
echo "headers:          $PREFIX/include/FL"
echo "smoke binary:     $ART/fltk-hello"

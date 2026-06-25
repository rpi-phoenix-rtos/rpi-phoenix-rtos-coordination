#!/usr/bin/env bash
#
# Phoenix-RTOS — build Window Maker (wmaker) for aarch64-phoenix.
#
# Window Maker is the HEAVIEST window manager ported to the Pi 4 X11 stack
# (Xphoenix kdrive fbdev DDX). Unlike twm/JWM/xterm (core bitmap fonts only),
# wmaker needs ANTIALIASED fonts, so it pulls in a whole font stack that the
# shared X11 prefix (build-x11-phoenix.sh) does NOT provide:
#
#   expat  ->  fontconfig  ->  libXft   (+ wmaker's in-tree wrlib + WINGs)
#
# This script builds that font stack + wmaker, bottom-up, each static, into a
# SEPARATE prefix (/tmp/wmaker-deps) so it never writes the shared, possibly
# concurrently-rebuilt /tmp/x11-phoenix (treated strictly read-only). It first
# SNAPSHOTS the X11 lib closure it needs out of /tmp/x11-phoenix into
# /tmp/wmaker-deps, then points every configure at /tmp/wmaker-deps ONLY — so the
# wmaker build is deterministic and insulated from any re-run of build-x11.
#
# Host-side only (does NOT boot the Pi, does NOT touch the flagship image).
# Idempotent: skips fetch/extract/build/snapshot when the outputs already exist.
#
# HOST BUILD DEPS (besides the cross toolchain): gperf (fontconfig codegen).
# Install on a clean host with:  sudo apt-get install -y gperf
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix          # shared X11 prefix — READ-ONLY here
DEPS=/tmp/wmaker-deps            # our isolated build prefix (X11 closure + font stack)
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
NFS=/srv/phoenix-rpi4-nfs
ART=/home/houp/phoenix-rpi/artifacts/x11

# wmaker is installed under --prefix=/nfstest so its compiled-in data paths
# (share/WindowMaker, defaults, menus) resolve on the netboot Pi, where the NFS
# rootfs export is mounted at /nfstest (NOT /).
TGT_PREFIX=/nfstest

# Versions
EXPAT_NV=expat-2.5.0
FC_NV=fontconfig-2.14.2
XFT_NV=libXft-2.3.8
WM_NV=WindowMaker-0.95.9

EXPAT_URL="https://github.com/libexpat/libexpat/releases/download/R_2_5_0/$EXPAT_NV.tar.bz2"
FC_URL="https://www.freedesktop.org/software/fontconfig/release/$FC_NV.tar.xz"
XFT_URL="https://www.x.org/releases/individual/lib/$XFT_NV.tar.gz"
WM_URL="https://github.com/window-maker/wmaker/releases/download/wmaker-0.95.9/$WM_NV.tar.gz"

fail() { echo "FAIL: $*"; exit 1; }

# Cross idiom shared by every autotools dep here.
export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig"
# LIBDIR (not just PATH) so pkg-config CANNOT see host /usr/lib *.pc — the same
# lesson as build-jwm.sh: without it, configure auto-detects host fontconfig/Xft.
export PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig"
PKGC="pkg-config --static"

mkdir -p "$SRC" "$DEPS/lib/pkgconfig" "$DEPS/include"

# --- 0a. host build tool: gperf (fontconfig's fcobjshash.h codegen) ---
command -v gperf >/dev/null 2>&1 || fail "gperf not on PATH — run: sudo apt-get install -y gperf"

# fetch_extract <name-version> <url>
fetch_extract() {
	local nv=$1 url=$2 tb
	[ -d "$SRC/$nv" ] && return 0
	case "$url" in
		*.tar.bz2) tb="$nv.tar.bz2" ;;
		*.tar.xz)  tb="$nv.tar.xz"  ;;
		*)         tb="$nv.tar.gz"  ;;
	esac
	if [ ! -f "$SRC/$tb" ]; then
		echo "=== fetching $url ==="
		timeout 120 curl -sSL -o "$SRC/$tb" "$url" || fail "download of $tb failed (network egress?)"
	fi
	echo "=== extracting $tb ==="
	tar -C "$SRC" -xf "$SRC/$tb" || fail "extract of $tb failed"
	[ -f "$SRC/$nv/configure" ] || fail "$SRC/$nv/configure missing — not a release tarball?"
}

# --- 0b. snapshot the X11 lib closure out of the shared prefix (insulation) ---
# We physically COPY the libs/headers/pkgconfig wmaker needs from /tmp/x11-phoenix
# into /tmp/wmaker-deps and rewrite the copied .pc files to point at wmaker-deps,
# so nothing in this build ever reads the shared prefix at configure/link time.
snapshot_x11_closure() {
	if [ -f "$DEPS/lib/libX11.a" ] && [ -f "$DEPS/lib/libfreetype.a" ]; then
		echo "=== X11 closure already snapshotted into $DEPS ==="
		return 0
	fi
	[ -f "$PREFIX/lib/libX11.a" ]      || fail "$PREFIX/lib/libX11.a missing — run build-x11-phoenix.sh first"
	[ -f "$PREFIX/lib/libfreetype.a" ] || fail "$PREFIX/lib/libfreetype.a missing — run build-x11-phoenix.sh first"
	[ -f "$PREFIX/lib/libXaw7.a" ]     || fail "$PREFIX/lib/libXaw7.a missing — build-x11-phoenix.sh not finished?"
	echo "=== snapshotting X11 closure $PREFIX -> $DEPS ==="
	cp -an "$PREFIX/include/." "$DEPS/include/" 2>/dev/null
	cp -an "$PREFIX/lib/."     "$DEPS/lib/"     2>/dev/null
	mkdir -p "$DEPS/share"
	cp -an "$PREFIX/share/."   "$DEPS/share/"   2>/dev/null
	# Rewrite any pkgconfig prefix/path baked to /tmp/x11-phoenix -> /tmp/wmaker-deps.
	perl -pi -e 's{/tmp/x11-phoenix}{/tmp/wmaker-deps}g' "$DEPS"/lib/pkgconfig/*.pc 2>/dev/null
	grep -lq '/tmp/x11-phoenix' "$DEPS"/lib/pkgconfig/*.pc 2>/dev/null \
		&& fail "some .pc still reference /tmp/x11-phoenix after rewrite"
	echo "=== snapshot done: $(ls "$DEPS"/lib/*.a | wc -l) libs, $(ls "$DEPS"/lib/pkgconfig/*.pc | wc -l) .pc ==="
}

# ============================================================================
# 1. expat  (fontconfig's XML parser; no X11/$PREFIX dependency)
# ============================================================================
build_expat() {
	[ -f "$DEPS/lib/libexpat.a" ] && { echo "expat: already built"; return 0; }
	fetch_extract "$EXPAT_NV" "$EXPAT_URL"
	( cd "$SRC/$EXPAT_NV" \
	  && ./configure --host=aarch64-phoenix --prefix="$DEPS" --disable-shared --enable-static \
	       --without-docbook --without-examples --without-tests \
	       CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	       CFLAGS="--sysroot=$SYSROOT" LDFLAGS="--sysroot=$SYSROOT" >/tmp/expat-conf.log 2>&1 \
	  && make install >/tmp/expat-build.log 2>&1 ) || { tail -10 /tmp/expat-build.log; fail "expat build failed"; }
	[ -f "$DEPS/lib/libexpat.a" ] || fail "libexpat.a not produced"
	echo "expat: OK"
}

# ============================================================================
# 2. fontconfig  (needs freetype [from snapshot] + expat)
# ============================================================================
# Two Phoenix source gaps are patched in src/ (idempotent, re-applied if reset):
#   - fccache.c: libphoenix <sys/time.h> has a NON-STANDARD value-based timercmp()
#       macro; fontconfig (like glibc/BSD) passes struct-timeval POINTERS. We
#       #undef + redefine timercmp with the standard pointer-based ternary.
#   - fccompat.c: FcRandom's HAVE_RAND_R path has `static unsigned int seed =
#       time(NULL);` (non-constant static initializer — only this non-glibc path
#       hits it). Seed lazily on first call instead.
# Plus configure cache vars force the rand_r() path: libphoenix has NO
# random()/initstate()/setstate(), so we disable HAVE_RANDOM and let FcRandom
# fall through to rand_r() (present in libphoenix).
fc_patch() {
	local f="$SRC/$FC_NV/src/fccache.c"
	if ! grep -q 'Phoenix-RTOS port: libphoenix' "$f"; then
		perl -0pi -e 's{(#ifndef O_BINARY\n#define O_BINARY 0\n#endif\n)}{$1\n/* Phoenix-RTOS port: libphoenix <sys/time.h> defines a non-standard, value-\n * based timercmp() (a.tv_sec, not (a)->tv_sec); fontconfig passes POINTERS.\n * Redefine it after the include with the standard pointer-based form. */\n#ifdef timercmp\n#undef timercmp\n#endif\n#define timercmp(a, b, CMP) \\\n\t(((a)->tv_sec == (b)->tv_sec) ? \\\n\t\t((a)->tv_usec CMP (b)->tv_usec) : \\\n\t\t((a)->tv_sec CMP (b)->tv_sec))\n}' "$f"
	fi
	local g="$SRC/$FC_NV/src/fccompat.c"
	if ! grep -q 'Phoenix-RTOS port: a static initializer' "$g"; then
		perl -0pi -e 's{    static unsigned int seed = time \(NULL\);\n\n    result = rand_r \(&seed\);}{    /* Phoenix-RTOS port: a static initializer must be a constant expression;\n     * time(NULL) is not. Seed lazily on first call instead. */\n    static unsigned int seed = 0;\n    static FcBool seeded = FcFalse;\n\n    if (seeded != FcTrue)\n    \{\n\tseed = (unsigned int) time (NULL);\n\tseeded = FcTrue;\n    \}\n    result = rand_r (&seed);}' "$g"
	fi
}
build_fontconfig() {
	[ -f "$DEPS/lib/libfontconfig.a" ] && { echo "fontconfig: already built"; return 0; }
	fetch_extract "$FC_NV" "$FC_URL"
	fc_patch
	local stage=/tmp/fc-stage
	rm -rf "$stage"
	( cd "$SRC/$FC_NV" \
	  && { [ -f config.status ] || PKG_CONFIG="$PKGC" ./configure --host=aarch64-phoenix \
	       --build=x86_64-pc-linux-gnu --prefix="$TGT_PREFIX" --disable-shared --enable-static --disable-docs \
	       --with-cache-dir="$TGT_PREFIX/var/cache/fontconfig" \
	       --with-default-fonts="$TGT_PREFIX/usr/share/fonts" \
	       --sysconfdir="$TGT_PREFIX/etc" --with-expat="$DEPS" \
	       CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib CC_FOR_BUILD=gcc \
	       ac_cv_func_random=no ac_cv_func_initstate=no ac_cv_func_setstate=no ac_cv_func_random_r=no \
	       FREETYPE_CFLAGS="-I$DEPS/include/freetype2" FREETYPE_LIBS="-L$DEPS/lib -lfreetype" \
	       EXPAT_CFLAGS="-I$DEPS/include" EXPAT_LIBS="-L$DEPS/lib -lexpat" \
	       CFLAGS="--sysroot=$SYSROOT -I$DEPS/include" LDFLAGS="--sysroot=$SYSROOT -L$DEPS/lib" \
	       >/tmp/fc-conf.log 2>&1; } \
	  && make >/tmp/fc-build.log 2>&1 \
	  && make DESTDIR="$stage" install >/tmp/fc-install.log 2>&1 ) \
	  || { tail -12 /tmp/fc-build.log /tmp/fc-install.log 2>/dev/null; fail "fontconfig build failed"; }
	# Land lib + headers into the BUILD prefix; build-time .pc points at $DEPS.
	cp "$stage$TGT_PREFIX/lib/libfontconfig.a" "$DEPS/lib/"
	cp -r "$stage$TGT_PREFIX/include/fontconfig" "$DEPS/include/"
	cp "$stage$TGT_PREFIX/lib/pkgconfig/fontconfig.pc" "$DEPS/lib/pkgconfig/"
	perl -pi -e "s{^prefix=$TGT_PREFIX\$}{prefix=$DEPS}" "$DEPS/lib/pkgconfig/fontconfig.pc"
	[ -f "$DEPS/lib/libfontconfig.a" ] || fail "libfontconfig.a not produced"
	echo "fontconfig: OK"
}

# ============================================================================
# 3. libXft  (needs freetype + fontconfig + libXrender — all in $DEPS now)
# ============================================================================
build_libxft() {
	[ -f "$DEPS/lib/libXft.a" ] && { echo "libXft: already built"; return 0; }
	fetch_extract "$XFT_NV" "$XFT_URL"
	( cd "$SRC/$XFT_NV" \
	  && PKG_CONFIG="$PKGC" ./configure --host=aarch64-phoenix --prefix="$DEPS" \
	       --disable-shared --enable-static \
	       CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	       FREETYPE_CFLAGS="-I$DEPS/include/freetype2" FREETYPE_LIBS="-L$DEPS/lib -lfreetype" \
	       FONTCONFIG_CFLAGS="-I$DEPS/include" FONTCONFIG_LIBS="-L$DEPS/lib -lfontconfig -lexpat -lfreetype" \
	       XRENDER_CFLAGS="-I$DEPS/include" XRENDER_LIBS="-L$DEPS/lib -lXrender -lX11" \
	       CFLAGS="--sysroot=$SYSROOT -I$DEPS/include" LDFLAGS="--sysroot=$SYSROOT -L$DEPS/lib" \
	       >/tmp/xft-conf.log 2>&1 \
	  && make install >/tmp/xft-build.log 2>&1 ) || { tail -12 /tmp/xft-conf.log /tmp/xft-build.log 2>/dev/null; fail "libXft build failed"; }
	[ -f "$DEPS/lib/libXft.a" ] || fail "libXft.a not produced"
	echo "libXft: OK"
}

# ----------------------------------------------------------------------------
build_expat
snapshot_x11_closure
build_fontconfig
build_libxft

echo "=== font stack built into $DEPS ==="
ls "$DEPS"/lib/libexpat.a "$DEPS"/lib/libfontconfig.a "$DEPS"/lib/libXft.a 2>&1
echo "=== (wmaker build appended below in a later step) ==="

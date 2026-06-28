#!/usr/bin/env bash
#
# Phoenix-RTOS — cross-build GNU Midnight Commander (mc) for aarch64-phoenix
# (task #55). Depends on the ported glib-2.0 (build-glib2.sh) + ncurses
# (build-ncurses.sh). mc 4.8.31 is autotools.
#
# STATUS: configure-recon / WIP. glib2 + ncurses deps are done; this script
# drives mc's configure to enumerate the remaining Phoenix gaps. See
# tools/ports/GLIB2-MC-PORT-NOTES.md.
#
# Key flags (rationale in GLIB2-MC-PORT-NOTES.md):
#   --with-screen=ncurses (ncurses is ported; slang is not)
#   --with-glib-static + GLIB_CFLAGS/GLIB_LIBS env -> bypass pkg-config
#   --disable-subshell -> sidestep grantpt/ptsname pty dependency
#   --without-x --without-gpm --disable-nls --disable-vfs-undelfs
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=mc-4.8.31
URL=http://ftp.midnight-commander.org/$NV.tar.xz

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/phoenix-mc
HERE=/home/houp/phoenix-rpi/tools/ports
SRC=$HERE/src
XDIR=$SRC/$NV
NCPREFIX=/tmp/phoenix-ncurses
ZLIB=/tmp/x11-phoenix
SHIM=$HERE/mc-phoenix-shim.h
CACHE=$HERE/mc.cache

fail() { echo "FAIL: $*"; exit 1; }

# Ensure deps.
[ -f "$SYSROOT/lib/libglib-2.0.a" ] || { "$HERE/build-glib2.sh" || fail "build-glib2.sh failed"; }
[ -f "$NCPREFIX/lib/libncurses.a" ] || { "$HERE/build-ncurses.sh" || fail "build-ncurses.sh failed"; }

# mc-support: Phoenix lacks the mntent API (empty <mntent.h>, no getmntent) and
# langinfo (no <langinfo.h>, no nl_langinfo). Stage glibc-compatible headers +
# build a stub libmcsupport.a so mc's mountlist.c (no-mounts) and strutil.c
# (nl_langinfo(CODESET)->"UTF-8") build + link. See GLIB2-MC-PORT-NOTES.md.
cp -a "$HERE/mc-support/mntent.h" "$SYSROOT/usr/include/mntent.h"
cp -a "$HERE/mc-support/langinfo.h" "$SYSROOT/usr/include/langinfo.h"
if [ ! -f "$SYSROOT/lib/libmcsupport.a" ]; then
	${TC}gcc --sysroot="$SYSROOT" -O2 -c "$HERE/mc-support/mntent-stub.c" -I"$HERE/mc-support" -o /tmp/mc-mntent.o || fail "mc-support mntent compile failed"
	${TC}gcc --sysroot="$SYSROOT" -O2 -c "$HERE/mc-support/langinfo-stub.c" -I"$HERE/mc-support" -o /tmp/mc-langinfo.o || fail "mc-support langinfo compile failed"
	${TC}ar rcs "$SYSROOT/lib/libmcsupport.a" /tmp/mc-mntent.o /tmp/mc-langinfo.o || fail "mc-support ar failed"
fi

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV.tar.xz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 200 -o "$SRC/$NV.tar.xz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV.tar.xz" || fail "extract failed"
fi

for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		s=$(grep -lr phoenix /home/houp/phoenix-rpi/tools/x11-port/src/*/$cfg 2>/dev/null | head -1)
		[ -n "$s" ] && cp "$s" "$XDIR/$cfg" && echo "=== refreshed $cfg ==="
	fi
done

GINC="-I$SYSROOT/usr/include/glib-2.0 -I$SYSROOT/usr/lib/glib-2.0/include"
GLIBLIBS="-L$SYSROOT/lib -lglib-2.0 -lgmodule-2.0 -lmcsupport -lpthread -liconv -lresolv -lm"
# Our ported ncurses is NARROW. mc's tty-ncurses.h enables ENABLE_SHADOWS (which
# uses the WIDE ncurses API getcchar/setcchar/*_wchnstr/cchar_t) whenever the
# ncurses header reports NCURSES_WIDECHAR=1 — and the narrow header sets that to 1
# under _XOPEN_SOURCE>=500. Force NCURSES_WIDECHAR=0 (the header honours a
# pre-definition via #ifndef) so mc compiles against the narrow API; the only loss
# is dialog drop-shadows. (Full widec/UTF-8 mc needs ncursesw + libphoenix
# wcwidth/mbrtowc/wcrtomb/mbsinit — an attended libc add; see GLIB2-MC-PORT-NOTES.md.)
CF="--sysroot=$SYSROOT -O2 -DNCURSES_WIDECHAR=0 $GINC -I$NCPREFIX/include -I$NCPREFIX/include/ncurses"
[ -f "$SHIM" ] && CF="$CF -include $SHIM"

if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	[ -f "$CACHE" ] && cp "$CACHE" "$XDIR/mc.cache" && CACHE_OPT="--cache-file=mc.cache" || CACHE_OPT=""
	( cd "$XDIR" && ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="$PREFIX" \
	    $CACHE_OPT \
	    --with-screen=ncurses \
	    --with-ncurses-includes="$NCPREFIX/include" \
	    --with-ncurses-libs="$NCPREFIX/lib" \
	    --without-subshell --without-x --without-gpm --disable-nls \
	    --disable-vfs-undelfs --disable-vfs-sftp --disable-vfs-ftp \
	    --disable-doxygen-doc \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="$CF" CPPFLAGS="--sysroot=$SYSROOT $GINC" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$SYSROOT/lib -L$NCPREFIX/lib -L$ZLIB/lib" \
	    PKG_CONFIG="$HERE/fake-pkg-config.sh" \
	    GLIB_LIBDIR="$SYSROOT/lib" \
	    GLIB_CFLAGS="$GINC" GLIB_LIBS="$GLIBLIBS" \
	    GMODULE_CFLAGS="$GINC" GMODULE_LIBS="$GLIBLIBS" \
	    >/tmp/mc-conf.log 2>&1 ) || { echo "--- configure failed; tail ---"; tail -n 40 /tmp/mc-conf.log; fail "configure failed"; }
fi

echo "=== building $NV ==="
( cd "$XDIR" && make >/tmp/mc-build.log 2>&1 ) || { echo "--- build failed; tail ---"; tail -n 50 /tmp/mc-build.log; fail "build failed"; }

BIN="$XDIR/src/mc"
[ -x "$BIN" ] || fail "src/mc not produced"
mkdir -p /home/houp/phoenix-rpi/artifacts; cp "$BIN" /home/houp/phoenix-rpi/artifacts/mc
echo "=== mc PRE-FLIGHT ==="
file "$BIN"
${TC}nm -u "$BIN" 2>/dev/null | head
echo "=== mc OK ==="

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
# VARIANTS (env MC_VARIANT, default "stock"):
#   stock  -> output binary "mc"     ; nl_langinfo(CODESET)="UTF-8" (str_utf8 path)
#   ascii  -> output binary "mc-ascii"; nl_langinfo(CODESET)="ASCII" so mc's
#             str_choose_str_functions() falls through utf8+8bit tables to the
#             plain str_ascii_init() path (strutilascii.c). Cheap-fix attempt for
#             the startup Data Abort that surfaces in the UTF-8 strutil path.
#   dbg    -> output binary "mc-dbg" ; UTF-8 codeset (reproduces the crash) PLUS
#             the mc-dbg-instrument.patch MCDBG init-trace markers (stderr+fflush)
#             so the last marker before the abort pinpoints the crashing call.
# The variant selects (a) the langinfo stub's CODESET via -DMC_CODESET_ASCII,
# (b) whether the dbg patch is applied, (c) the output filename. To force a
# relink after a variant switch the script rebuilds libmcsupport.a and removes
# src/mc before make (libmcsupport.a is not a make dependency of src/mc).
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=mc-4.8.31
URL=http://ftp.midnight-commander.org/$NV.tar.xz

MC_VARIANT=${MC_VARIANT:-stock}
case "$MC_VARIANT" in
	stock) MC_OUT=mc;       MC_CODESET_DEF="";                  MC_DBG=0 ;;
	ascii) MC_OUT=mc-ascii; MC_CODESET_DEF="-DMC_CODESET_ASCII"; MC_DBG=0 ;;
	dbg)   MC_OUT=mc-dbg;   MC_CODESET_DEF="";                  MC_DBG=1 ;;
	*) echo "FAIL: unknown MC_VARIANT='$MC_VARIANT' (want stock|ascii|dbg)"; exit 1 ;;
esac
echo "=== MC_VARIANT=$MC_VARIANT -> output '$MC_OUT' (codeset_def='$MC_CODESET_DEF' dbg=$MC_DBG) ==="

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
# Rebuild libmcsupport.a UNCONDITIONALLY: the langinfo stub's CODESET depends on
# MC_VARIANT ($MC_CODESET_DEF), so a cached lib from a different variant would be
# stale. libmcsupport.a is also not a make dependency of src/mc, so this rebuild
# plus the src/mc removal below are what force the codeset change into the binary.
${TC}gcc --sysroot="$SYSROOT" -O2 -c "$HERE/mc-support/mntent-stub.c" -I"$HERE/mc-support" -o /tmp/mc-mntent.o || fail "mc-support mntent compile failed"
${TC}gcc --sysroot="$SYSROOT" -O2 $MC_CODESET_DEF -c "$HERE/mc-support/langinfo-stub.c" -I"$HERE/mc-support" -o /tmp/mc-langinfo.o || fail "mc-support langinfo compile failed"
${TC}ar rcs "$SYSROOT/lib/libmcsupport.a" /tmp/mc-mntent.o /tmp/mc-langinfo.o || fail "mc-support ar failed"

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

# MCDBG init-trace patch management (idempotent). The two source files it touches
# (src/main.c, lib/strutil/strutil.c) are shared across variants, so always start
# from a clean (unpatched) tree, then forward-apply ONLY for MC_VARIANT=dbg. The
# stale .o/.lo removal below forces make to recompile the affected TUs when the
# patch state flips between variants.
DBGPATCH="$HERE/mc-dbg-instrument.patch"
patch_files="src/main.c lib/strutil/strutil.c"
# Reverse-apply if currently patched (grep is the cheap state probe).
if grep -q "MCDBG: enter main" "$XDIR/src/main.c" 2>/dev/null; then
	( cd "$XDIR" && patch -p1 -R <"$DBGPATCH" >/tmp/mc-dbg-unpatch.log 2>&1 ) || fail "mc-dbg reverse-patch failed (see /tmp/mc-dbg-unpatch.log)"
	echo "=== reverted MCDBG instrumentation (clean tree) ==="
	for f in $patch_files; do rm -f "$XDIR/${f%.c}.o" "$XDIR/${f%.c}.lo"; done
fi
if [ "$MC_DBG" = "1" ]; then
	[ -f "$DBGPATCH" ] || fail "missing $DBGPATCH"
	( cd "$XDIR" && patch -p1 <"$DBGPATCH" >/tmp/mc-dbg-patch.log 2>&1 ) || { tail -n 30 /tmp/mc-dbg-patch.log; fail "mc-dbg patch apply failed"; }
	echo "=== applied MCDBG instrumentation ==="
	for f in $patch_files; do rm -f "$XDIR/${f%.c}.o" "$XDIR/${f%.c}.lo"; done
fi

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

# Force a relink: src/mc does not depend on libmcsupport.a, and (for dbg) the .o
# removals above only rebuild the two patched TUs — removing the linked binary
# guarantees make re-links it with the current libmcsupport.a + objects.
rm -f "$XDIR/src/mc"

echo "=== building $NV ($MC_VARIANT) ==="
( cd "$XDIR" && make >/tmp/mc-build.log 2>&1 ) || { echo "--- build failed; tail ---"; tail -n 50 /tmp/mc-build.log; fail "build failed"; }

BIN="$XDIR/src/mc"
[ -x "$BIN" ] || fail "src/mc not produced"

# Stage: artifacts/ (local) + NFS rootfs /bin/<MC_OUT>. The stock variant keeps
# the canonical /bin/mc; ascii/dbg stage alongside it without disturbing it.
NFSBIN=/srv/phoenix-rpi4-nfs/bin
mkdir -p /home/houp/phoenix-rpi/artifacts
cp "$BIN" "/home/houp/phoenix-rpi/artifacts/$MC_OUT"
if [ -d "$NFSBIN" ]; then
	cp "$BIN" "$NFSBIN/$MC_OUT" && echo "=== staged $NFSBIN/$MC_OUT ==="
else
	echo "WARN: $NFSBIN not present — skipped NFS staging (artifacts/$MC_OUT only)"
fi

echo "=== mc PRE-FLIGHT ($MC_VARIANT -> $MC_OUT) ==="
file "$BIN"
echo "--- undefined symbols (expect only 'environ') ---"
${TC}nm -u "$BIN" 2>/dev/null | head
echo "--- codeset string baked in (CODESET stub) ---"
${TC}strings "$BIN" 2>/dev/null | grep -iE '^(UTF-8|ASCII)$' | sort -u | head
if [ "$MC_DBG" = "1" ]; then
	echo "--- MCDBG markers baked in (count) ---"
	${TC}strings "$BIN" 2>/dev/null | grep -c '^MCDBG'
fi
echo "=== $MC_OUT OK ==="

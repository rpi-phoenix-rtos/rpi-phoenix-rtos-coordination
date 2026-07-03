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
MC_GUARD=0
case "$MC_VARIANT" in
	stock) MC_OUT=mc;       MC_CODESET_DEF="";                  MC_DBG=0 ;;
	ascii) MC_OUT=mc-ascii; MC_CODESET_DEF="-DMC_CODESET_ASCII"; MC_DBG=0 ;;
	dbg)   MC_OUT=mc-dbg;   MC_CODESET_DEF="";                  MC_DBG=1 ;;
	# guard: UTF-8 codeset (reproduces the crash) + a redzone/canary allocator
	# shim (mc-support/mc-guard-wrap.c) linked via -Wl,--wrap=malloc,... that
	# catches the startup heap OVERFLOW at the WRITER and prints the allocating
	# backtrace. Built -O0 -g -fno-omit-frame-pointer so __builtin_return_address
	# 1..3 walk and addr2line resolves mc's call sites. Needs its OWN configure
	# (different CFLAGS+LDFLAGS than the cached stock/dbg one) — see below.
	guard) MC_OUT=mc-guard; MC_CODESET_DEF="";                  MC_DBG=0; MC_GUARD=1 ;;
	*) echo "FAIL: unknown MC_VARIANT='$MC_VARIANT' (want stock|ascii|dbg|guard)"; exit 1 ;;
esac
echo "=== MC_VARIANT=$MC_VARIANT -> output '$MC_OUT' (codeset_def='$MC_CODESET_DEF' dbg=$MC_DBG guard=$MC_GUARD) ==="

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/phoenix-mc
HERE=${ROOT}/tools/ports
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
MCSUPPORT_OBJS="/tmp/mc-mntent.o /tmp/mc-langinfo.o"
# guard variant: compile the redzone/canary allocator shim -O0 -g
# -fno-omit-frame-pointer and bundle it as a MEMBER of libmcsupport.a (which is
# already on mc's final link line via GLIB_LIBS' -lmcsupport). It must be an
# archive member, NOT a named .o in LDFLAGS: libtool intercepts LDFLAGS for every
# intermediate libmc*.la build and rejects a raw .o ("cannot build libtool
# library from non-libtool objects"). The -Wl,--wrap=malloc,... flags are
# injected ONLY at the final `make` link (see below), so they never reach
# configure's conftest probe (which would fail with undefined __wrap_* refs from
# libphoenix pthread) nor the ar-built intermediate .la libs. At the final mc
# link, --wrap makes mc's/glib's malloc/free/realloc/calloc reference __wrap_*,
# which pulls this member from the archive (all four wrappers in one object).
if [ "$MC_GUARD" = "1" ]; then
	${TC}gcc --sysroot="$SYSROOT" -O0 -g -fno-omit-frame-pointer -c "$HERE/mc-support/mc-guard-wrap.c" -I"$HERE/mc-support" -o /tmp/mc-guard-wrap.o || fail "mc-support guard compile failed"
	MCSUPPORT_OBJS="$MCSUPPORT_OBJS /tmp/mc-guard-wrap.o"
fi
${TC}ar rcs "$SYSROOT/lib/libmcsupport.a" $MCSUPPORT_OBJS || fail "mc-support ar failed"

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV.tar.xz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 200 -o "$SRC/$NV.tar.xz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV.tar.xz" || fail "extract failed"
fi

for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		s=$(grep -lr phoenix ${ROOT}/tools/x11-port/src/*/$cfg 2>/dev/null | head -1)
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
# Base optimization for mc's own TUs. The guard variant overrides this to
# -O0 -g -fno-omit-frame-pointer so the allocator shim's __builtin_return_address
# 1..3 chain walks through mc's frames and aarch64-phoenix-addr2line resolves the
# allocating call sites against /bin/mc-guard.
if [ "$MC_GUARD" = "1" ]; then
	MC_OPT="-O0 -g -fno-omit-frame-pointer"
else
	MC_OPT="-O2"
fi
CF="--sysroot=$SYSROOT $MC_OPT -DNCURSES_WIDECHAR=0 $GINC -I$NCPREFIX/include -I$NCPREFIX/include/ncurses"
[ -f "$SHIM" ] && CF="$CF -include $SHIM"

# Base LDFLAGS used at configure time (plain — no --wrap, so configure's bare
# conftest.c link probe doesn't hit undefined __wrap_* refs). The guard variant's
# --wrap flags are injected only at the final `make` link, below.
LDF="--sysroot=$SYSROOT -static -L$SYSROOT/lib -L$NCPREFIX/lib -L$ZLIB/lib"
GUARD_WRAP="-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"

# The cached config.status bakes CFLAGS+LDFLAGS, which differ for guard (-O0 -g
# vs -O2; --wrap vs none). When the configured state doesn't match the requested
# guard-ness, force a reconfigure. A stamp file records what the tree is
# configured for. Also wipe object files so the new CFLAGS take effect.
STAMP="$XDIR/.mc-guard-configured"
WANT_STAMP="guard=$MC_GUARD"
if [ -f "$XDIR/config.status" ]; then
	HAVE_STAMP="$(cat "$STAMP" 2>/dev/null || echo guard=0)"
	if [ "$HAVE_STAMP" != "$WANT_STAMP" ]; then
		echo "=== variant guard-ness changed ($HAVE_STAMP -> $WANT_STAMP); forcing reconfigure + clean ==="
		( cd "$XDIR" && make distclean >/tmp/mc-distclean.log 2>&1 ) || rm -f "$XDIR/config.status"
	fi
fi

if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV (guard=$MC_GUARD) ==="
	[ -f "$CACHE" ] && cp "$CACHE" "$XDIR/mc.cache" && CACHE_OPT="--cache-file=mc.cache" || CACHE_OPT=""
	( cd "$XDIR" && ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="$PREFIX" \
	    --datadir=/usr/share --sysconfdir=/etc \
	    $CACHE_OPT \
	    --with-screen=ncurses \
	    --with-ncurses-includes="$NCPREFIX/include" \
	    --with-ncurses-libs="$NCPREFIX/lib" \
	    --without-subshell --without-x --without-gpm --disable-nls \
	    --disable-vfs-undelfs --disable-vfs-sftp --disable-vfs-ftp \
	    --disable-doxygen-doc \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="$CF" CPPFLAGS="--sysroot=$SYSROOT $GINC" \
	    LDFLAGS="$LDF" \
	    PKG_CONFIG="$HERE/fake-pkg-config.sh" \
	    GLIB_LIBDIR="$SYSROOT/lib" \
	    GLIB_CFLAGS="$GINC" GLIB_LIBS="$GLIBLIBS" \
	    GMODULE_CFLAGS="$GINC" GMODULE_LIBS="$GLIBLIBS" \
	    >/tmp/mc-conf.log 2>&1 ) || { echo "--- configure failed; tail ---"; tail -n 40 /tmp/mc-conf.log; fail "configure failed"; }
	echo "$WANT_STAMP" >"$STAMP"
fi

# Force a relink: src/mc does not depend on libmcsupport.a, and (for dbg) the .o
# removals above only rebuild the two patched TUs — removing the linked binary
# guarantees make re-links it with the current libmcsupport.a + objects.
rm -f "$XDIR/src/mc"

echo "=== building $NV ($MC_VARIANT) ==="
if [ "$MC_GUARD" = "1" ]; then
	# Override LDFLAGS only on the guard build so --wrap reaches the final mc link.
	# A command-line `make VAR=val` REPLACES the variable (no append), so carry the
	# FULL base LDF (-static + all -L) plus the wrap flags. libtool's intermediate
	# .la libs are ar-built and ignore LDFLAGS, so --wrap is harmlessly unused there.
	( cd "$XDIR" && make LDFLAGS="$LDF $GUARD_WRAP" >/tmp/mc-build.log 2>&1 ) || { echo "--- build failed; tail ---"; tail -n 50 /tmp/mc-build.log; fail "build failed"; }
else
	( cd "$XDIR" && make >/tmp/mc-build.log 2>&1 ) || { echo "--- build failed; tail ---"; tail -n 50 /tmp/mc-build.log; fail "build failed"; }
fi

BIN="$XDIR/src/mc"
[ -x "$BIN" ] || fail "src/mc not produced"

# Stage: artifacts/ (local) + NFS rootfs /bin/<MC_OUT>. The stock variant keeps
# the canonical /bin/mc; ascii/dbg stage alongside it without disturbing it.
NFSBIN="${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}/bin"
mkdir -p ${ROOT}/artifacts
cp "$BIN" "${ROOT}/artifacts/$MC_OUT"
if [ -d "$NFSBIN" ]; then
	cp "$BIN" "$NFSBIN/$MC_OUT" && echo "=== staged $NFSBIN/$MC_OUT ==="
else
	echo "WARN: $NFSBIN not present — skipped NFS staging (artifacts/$MC_OUT only)"
fi

# Stage mc's runtime share data (skins + editor syntax) to the compiled-in
# datadir (--datadir=/usr/share -> MC_DATADIR=/usr/share/mc). Without the skins
# mc falls back to a monochrome built-in skin ("Unable to load 'default' skin"):
# no colours and no visible panel selection. The on-disk default skin restores
# the blue panels + cyan selection highlight. Skin-only is enough for colour; the
# syntax/ tree adds editor highlighting.
NFSSHARE="${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}/usr/share/mc"
if [ -d "${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}/usr/share" ]; then
	mkdir -p "$NFSSHARE/skins" "$NFSSHARE/syntax"
	cp -a "$XDIR"/misc/skins/*.ini "$NFSSHARE/skins/" 2>/dev/null && echo "=== staged mc skins -> $NFSSHARE/skins ==="
	cp -a "$XDIR"/misc/syntax/*.syntax "$XDIR"/misc/syntax/Syntax "$NFSSHARE/syntax/" 2>/dev/null || true
	cp -a "$XDIR"/misc/mc.ext.ini "$NFSSHARE/" 2>/dev/null || true
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
if [ "$MC_GUARD" = "1" ]; then
	echo "--- GUARD go/no-go: __wrap_* must be DEFINED (T) in the final link ---"
	${TC}nm "$BIN" 2>/dev/null | grep -iE ' (T|t) __wrap_(malloc|calloc|realloc|free)' || fail "__wrap_* not in link — -Wl,--wrap did not reach the final link (build useless)"
	echo "--- GUARD markers baked in (expect GUARD-OVERFLOW / GUARD-ALLOC-BT strings) ---"
	${TC}strings "$BIN" 2>/dev/null | grep -E '^GUARD-' | sort -u
	echo "--- GUARD: ELF type EXEC (non-PIE, addr2line-resolvable) + has debug info ---"
	${TC}readelf -h "$BIN" 2>/dev/null | grep -E 'Type:'
	${TC}readelf -S "$BIN" 2>/dev/null | grep -q debug_info && echo "  .debug_info present (backtraces resolve)" || echo "  WARN: no .debug_info"
fi
echo "=== $MC_OUT OK ==="

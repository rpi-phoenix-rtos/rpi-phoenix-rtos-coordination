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

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix          # shared X11 prefix — READ-ONLY here
DEPS=/tmp/wmaker-deps            # our isolated build prefix (X11 closure + font stack)
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
NFS=/srv/phoenix-rpi4-nfs
ART=${ROOT}/artifacts/x11

# wmaker is installed under --prefix=/ so its compiled-in data paths
# (share/WindowMaker, defaults, menus) resolve on the booted Pi, where the NFS
# rootfs export is now mounted as "/" (the nfsroot default, #44/#45). The baked
# path-prefix (TGT_PREFIX) is empty so paths are /bin/sh, /share/WindowMaker,
# etc.; autotools --prefix is "/" (an empty --prefix is invalid). $stage$TGT_PREFIX
# therefore equals $stage, where --prefix=/ installs bin/, share/, etc.
TGT_PREFIX=
CONF_PREFIX=/

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
# share/pkgconfig holds the xorgproto .pc files (xproto, renderproto, ...) that
# xft.pc/xrender.pc Require; lib/pkgconfig holds the rest.
export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig:$DEPS/share/pkgconfig"
# LIBDIR (not just PATH) so pkg-config CANNOT see host /usr/lib *.pc — the same
# lesson as build-jwm.sh: without it, configure auto-detects host fontconfig/Xft.
export PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig:$DEPS/share/pkgconfig"
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
	       --build=x86_64-pc-linux-gnu --prefix="$CONF_PREFIX" --disable-shared --enable-static --disable-docs \
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
	perl -pi -e "s{^prefix=/?\$}{prefix=$DEPS}" "$DEPS/lib/pkgconfig/fontconfig.pc"
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

# ============================================================================
# 3b. libftw: tiny libphoenix gap-fill lib (nftw/ftw + nice + scandir/alphasort)
# ============================================================================
# libphoenix has no <ftw.h>/nftw(), no nice(), and no scandir()/alphasort().
# Window Maker (WINGs + util/) references all of them. The committed sources
# live in ftw-phoenix/; build.sh there compiles libftw.a + installs the headers
# (ftw.h, wmaker-phoenix-compat.h) into $DEPS. See WMAKER-PORT-STATUS.md.
build_libftw() {
	[ -f "$DEPS/lib/libftw.a" ] && { echo "libftw: already built"; return 0; }
	DEPS_PREFIX="$DEPS" SYSROOT="$SYSROOT" \
		TOOLCHAIN_BIN="$(dirname "$TC")" "$HERE/ftw-phoenix/build.sh" \
		|| fail "libftw build failed"
	[ -f "$DEPS/lib/libftw.a" ] || fail "libftw.a not produced"
}

# ============================================================================
# 4. Window Maker (wmaker)  — needs Xft + fontconfig + the X11 closure + libftw
# ============================================================================
# Compiled-in paths land under "/" (the NFS rootfs is now mounted as the root
# filesystem on the booted Pi, #44/#45): --prefix=/, --sysconfdir=/etc.
#
# Source patch (idempotent perl, re-applied on a fresh tree):
#   src/main.c: ExecuteShellCommand() hardcodes shell="/bin/sh" (the getenv
#   path is commented out upstream). The Pi's shell is at /bin/sh, so
#   menu/<exec> commands would fail. Guard with #ifndef WMAKER_SHELL and pass
#   -DWMAKER_SHELL="/bin/sh" — same trap as JWM's SHELL_NAME.
#
# Build defines (CFLAGS), each papering a libphoenix gap deterministically
# against the CURRENT sysroot (no libphoenix rebuild required for this build):
#   -D_SC_LINE_MAX=5  : WINGs sysconf(_SC_LINE_MAX); value 5 matches the
#                       libphoenix commit. On an un-rebuilt sysroot sysconf()
#                       returns -1 and WINGs uses its own 512 fallback; once
#                       libphoenix is rebuilt it returns _POSIX2_LINE_MAX.
#   -Drint=round      : libphoenix libm has no rint(); round() is an adequate
#                       substitute for wmaker's UI coordinate/colour rounding.
#   -include wmaker-phoenix-compat.h : declares nice/scandir/alphasort whose
#                       definitions are in libftw.a.
# Image codecs + extensions we have no cross libs for are disabled.
WM_PWD_DEFS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"
WM_GAP_DEFS="-D_SC_LINE_MAX=5 -Drint=round -include wmaker-phoenix-compat.h"
WM_SHELL_DEF='-DWMAKER_SHELL=\"/bin/sh\"'
# WMAKER_DIAG=1 compiles in the PHX_DIAG startup milestone markers (src/phx_diag.h)
# that localize the post-defaults HDMI-black startup stall on HW. Off => upstream-
# clean binary. The markers route through wmessage (the UART-proven path).
WM_DIAG_DEF=""
[ "${WMAKER_DIAG:-0}" = "1" ] && WM_DIAG_DEF="-DPHX_DIAG -I$SRC/$WM_NV/WINGs/WINGs"
# Full X11 + font + gap-fill static link closure, correctly ordered.
WM_XCLOSURE="-lXft -lfontconfig -lexpat -lfreetype -lXrender -lXpm -lXext -lXmu -lXt -lSM -lICE -lX11 -lxcb -lXau -lXdmcp -lpng16 -ljpeg -lz -lftw -lm"

# Main-thread stack size. The Phoenix kernel reads PT_GNU_STACK's p_memsz from
# the ELF as the main (initial) thread's user stack size, else falls back to
# SIZE_USTACK = 8 pages = 32 KiB on aarch64 (hal/aarch64/arch/cpu.h). 32 KiB is
# far too small for wmaker's deep call chains (manageAllWindows + WINGs + event
# machinery), which overflow it right after the desktop renders (Data Abort with
# far at the stack limit). -Wl,-z,stack-size=N sets PT_GNU_STACK p_memsz, which
# the kernel honors. 1 MiB is generous and page-aligned (round_page no-op).
WM_STACK_LDFLAG="-Wl,-z,stack-size=0x100000"

# Apply the Phoenix-RTOS wmaker patch. This carries TWO things:
#   1. the direct-TTF-file font fix ("phxfile:" handling in WINGs/wfont.c +
#      makeFontOfSize/configuration.c defaults) — the candidate FIX for the
#      WMCreateFont/XftFontOpenName startup hang. This is PLAIN C and runs
#      ALWAYS (not gated on PHX_DIAG), so it must be applied unconditionally,
#      or a clean build would ship the broken generic-name path.
#   2. the PHX_DIAG startup-milestone markers (startup.c/screen.c/monitor.c +
#      the split match-vs-open markers in wfont.c). These route through
#      PHX_MARK(), which compiles to nothing unless -DPHX_DIAG is passed, so
#      applying them unconditionally is harmless for a clean build.
# Idempotent: `patch -N` skips already-applied hunks. We hard-fail if the patch
# does NOT apply cleanly (a silently-dropped hunk would ship the broken path).
wm_phx_patch() {
	cp "$HERE/wmaker/phx_diag.h" "$SRC/$WM_NV/src/phx_diag.h"
	local p="$HERE/patches/$WM_NV-phx-diag.patch"
	[ -f "$p" ] || fail "missing wmaker patch $p"
	# --dry-run first: tolerate a fully-already-applied tree (-R detects it),
	# but reject a partially-applying / rejecting patch.
	if ( cd "$SRC/$WM_NV" && patch -p1 --dry-run -N <"$p" >/tmp/wm-patch.log 2>&1 ); then
		( cd "$SRC/$WM_NV" && patch -p1 -N <"$p" >/tmp/wm-patch.log 2>&1 ) \
			|| { tail -20 /tmp/wm-patch.log; fail "wmaker phx patch failed to apply"; }
	elif grep -q "previously applied" /tmp/wm-patch.log; then
		echo "wmaker phx patch: already applied"
	else
		tail -20 /tmp/wm-patch.log
		fail "wmaker phx patch does not apply cleanly (rejects) — regenerate it"
	fi
	return 0
}

# Apply the Phoenix GetCommandForPid patch. Phoenix has no procfs, so the build
# falls to src/osdep_stub.c, whose GetCommandForPid only warns ("not implemented
# on this platform") and returns False. This patch wraps the stub body in
# `#ifndef __phoenix__` and adds a real Phoenix implementation built on the
# threadsinfo() syscall (process name -> single-element argv) for session
# save/restore. Other stub platforms keep the warning unchanged.
# Idempotent: `patch -N` skips an already-applied tree; hard-fail on rejects.
wm_getcmd_patch() {
	local p="$HERE/patches/$WM_NV-phoenix-getcommandforpid.patch"
	[ -f "$p" ] || fail "missing wmaker patch $p"
	if ( cd "$SRC/$WM_NV" && patch -p1 --dry-run -N <"$p" >/tmp/wm-getcmd-patch.log 2>&1 ); then
		( cd "$SRC/$WM_NV" && patch -p1 -N <"$p" >/tmp/wm-getcmd-patch.log 2>&1 ) \
			|| { tail -20 /tmp/wm-getcmd-patch.log; fail "wmaker getcommandforpid patch failed to apply"; }
	elif grep -q "previously applied" /tmp/wm-getcmd-patch.log; then
		echo "wmaker getcommandforpid patch: already applied"
	else
		tail -20 /tmp/wm-getcmd-patch.log
		fail "wmaker getcommandforpid patch does not apply cleanly (rejects) — regenerate it"
	fi
	return 0
}

wm_patch() {
	local f="$SRC/$WM_NV/src/main.c"
	if ! grep -q '#ifndef WMAKER_SHELL' "$f"; then
		perl -0pi -e 's{(void ExecuteShellCommand\(WScreen \*scr, const char \*command\)\n\{)}{#ifndef WMAKER_SHELL\n#define WMAKER_SHELL "/bin/sh"\n#endif\n\n$1}' "$f"
		perl -0pi -e 's{\n\tshell = "/bin/sh";\n}{\n\tshell = WMAKER_SHELL;\n}' "$f"
	fi
}

build_wmaker() {
	fetch_extract "$WM_NV" "$WM_URL"
	wm_patch
	wm_phx_patch
	wm_getcmd_patch
	local cf="--sysroot=$SYSROOT -I$DEPS/include $WM_PWD_DEFS $WM_GAP_DEFS $WM_SHELL_DEF $WM_DIAG_DEF"
	# Force-recompile the objects the phx patch touches so the font fix (and,
	# when WMAKER_DIAG=1, the markers) are (re)baked even if wmaker was already
	# built. wfont.c/widgets.c/configuration.c live in libWINGs.a; startup.c/
	# screen.c/monitor.c are wmaker src — drop the lib so it relinks.
	rm -f "$SRC/$WM_NV/src/wmaker" "$SRC/$WM_NV/src/startup.o" \
	      "$SRC/$WM_NV/src/osdep_stub.o" \
	      "$SRC/$WM_NV/src/screen.o" "$SRC/$WM_NV/src/monitor.o" \
	      "$SRC/$WM_NV/WINGs/widgets.o"       "$SRC/$WM_NV/WINGs/widgets.lo" \
	      "$SRC/$WM_NV/WINGs/wfont.o"         "$SRC/$WM_NV/WINGs/wfont.lo" \
	      "$SRC/$WM_NV/WINGs/configuration.o" "$SRC/$WM_NV/WINGs/configuration.lo" \
	      "$SRC/$WM_NV"/WINGs/.libs/libWINGs.a "$SRC/$WM_NV"/WINGs/libWINGs.la
	# Force-regenerate the menu/data files from their .in templates so the
	# compiled-in #bindir#/#wmdatadir# (now /bin, /share) are re-substituted.
	# Without this, a generated menu cached from an earlier build (e.g. one that
	# used a different --bindir) is newer than its .in and `make` skips it,
	# silently shipping a stale WPrefs EXEC path (#45).
	for tin in "$SRC/$WM_NV"/WindowMaker/*.in; do
		[ -f "$tin" ] || continue
		# Skip Makefile.in: deleting the generated WindowMaker/Makefile would
		# break `make` on a RE-RUN (configure is gated behind `[ -f config.status ]`
		# so it never regenerates). The intent here is only the menu/data files.
		case "$tin" in */Makefile.in) continue ;; esac
		rm -f "${tin%.in}"
	done
	( cd "$SRC/$WM_NV" \
	  && { [ -f config.status ] || PKG_CONFIG="$PKGC" ./configure --host=aarch64-phoenix \
	       --build=x86_64-pc-linux-gnu --prefix="$CONF_PREFIX" --sysconfdir="$TGT_PREFIX/etc" \
	       --datadir="$TGT_PREFIX/share" --bindir="$TGT_PREFIX/bin" \
	       --disable-shared \
	       --enable-png --enable-jpeg --disable-tiff --disable-gif --disable-webp --disable-magick \
	       --disable-shm --disable-xinerama --disable-nls --disable-xlocale \
	       --x-includes="$DEPS/include" --x-libraries="$DEPS/lib" \
	       CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib xorg_cv_malloc0_returns_null=no \
	       CFLAGS="$cf" \
	       LDFLAGS="--sysroot=$SYSROOT -static -L$DEPS/lib -L$SYSROOT/lib $WM_STACK_LDFLAG" \
	       LIBS="$WM_XCLOSURE" >/tmp/wm-conf.log 2>&1; } \
	  && make CFLAGS="$cf" \
	       LDFLAGS="--sysroot=$SYSROOT -static -L$DEPS/lib -L$SYSROOT/lib $WM_STACK_LDFLAG" \
	       >/tmp/wm-build.log 2>&1 ) \
	  || { tail -20 /tmp/wm-conf.log /tmp/wm-build.log 2>/dev/null; fail "wmaker build failed"; }
	[ -x "$SRC/$WM_NV/src/wmaker" ] || fail "src/wmaker not produced"
	echo "wmaker: OK (aarch64-phoenix static ELF${WM_DIAG_DEF:+, PHX_DIAG markers})"
}

# ============================================================================
# 5. stage to the NFS export + pre-flight (no Pi cycle)
# ============================================================================
stage_wmaker() {
	local wm="$SRC/$WM_NV/src/wmaker"
	local stage=/tmp/wm-stage
	mkdir -p "$ART"
	cp "$wm" "$ART/wmaker"
	echo "=== published -> $ART/wmaker ==="

	# DESTDIR install gives us the full WindowMaker data tree + util binaries.
	rm -rf "$stage"
	( cd "$SRC/$WM_NV" && make DESTDIR="$stage" install >/tmp/wm-install.log 2>&1 ) \
		|| { tail -10 /tmp/wm-install.log; fail "wmaker install (DESTDIR) failed"; }

	if [ ! -d "$NFS/bin" ]; then
		echo "=== NFS export $NFS not present — skipped staging (artifact only) ==="
		return 0
	fi

	echo "=== staging Window Maker -> $NFS ==="
	# 5a. binaries: wmaker + all util helpers, into /bin (export root)
	cp "$stage$TGT_PREFIX/bin/"* "$NFS/bin/" 2>/dev/null
	chmod 755 "$NFS/bin/wmaker"

	# 5b. data tree: share/WindowMaker, share/WINGs, share/WPrefs + etc/WindowMaker
	mkdir -p "$NFS/share" "$NFS/etc"
	cp -a "$stage$TGT_PREFIX/share/WindowMaker" "$NFS/share/" 2>/dev/null
	cp -a "$stage$TGT_PREFIX/share/WINGs"       "$NFS/share/" 2>/dev/null
	cp -a "$stage$TGT_PREFIX/share/WPrefs"      "$NFS/share/" 2>/dev/null
	cp -a "$stage$TGT_PREFIX/etc/WindowMaker"   "$NFS/etc/"   2>/dev/null

	# 5b'. Rewrite the ACTIVE defaults databases to name the DejaVu TTF by direct
	# file path ("phxfile:" — handled in WINGs/wfont.c) instead of the generic
	# "Sans"/"sans serif" family. These DB values OVERRIDE the compiled-in
	# DEF_*_FONT / SYSTEM_FONT defaults (getFont/WMGetUDStringForKey), so they
	# are the load-bearing trigger for the font fix: a generic family name makes
	# wmaker run a fontconfig FcFontMatch scan during startup, which HANGS on the
	# Pi 4 netboot stack. We rewrite ONLY the two active startup DBs:
	#   - etc/WindowMaker/WMGLOBAL : WINGs SystemFont / BoldSystemFont
	#   - etc/WindowMaker/WindowMaker : wmaker's own Window/Menu/Icon/... fonts
	# (the on-demand .style/.theme files are NOT on the dock-render path and are
	# left untouched). The bold faces map to DejaVuSans-Bold.ttf, the rest to
	# DejaVuSans.ttf; the pixelsize from the original spec is preserved.
	# NOTE: the replacement KEEPS the surrounding double quotes. WindowMaker's
	# proplist parser needs strings with '/' ':' '.' '=' quoted, or it rejects
	# the value and the font silently reverts to the generic compiled default.
	phx_fontfix_db() {
		local db="$1"
		[ -f "$db" ] || return 0
		# "...Sans:bold..."  -> DejaVuSans-Bold.ttf   (bold before non-bold!)
		perl -pi -e 's{"Sans:bold(:pixelsize=\d+)?"}{my $sz=$1//"";"\"phxfile:'"$FONTDIR"'/DejaVuSans-Bold.ttf$sz\""}ge' "$db"
		perl -pi -e 's{"sans serif:bold(:pixelsize=\d+)?"}{my $sz=$1//"";"\"phxfile:'"$FONTDIR"'/DejaVuSans-Bold.ttf$sz\""}ge' "$db"
		# remaining plain "Sans..." / "sans serif..." -> regular face
		perl -pi -e 's{"Sans(:pixelsize=\d+)?"}{my $sz=$1//"";"\"phxfile:'"$FONTDIR"'/DejaVuSans.ttf$sz\""}ge' "$db"
		perl -pi -e 's{"sans serif(:pixelsize=\d+)?"}{my $sz=$1//"";"\"phxfile:'"$FONTDIR"'/DejaVuSans.ttf$sz\""}ge' "$db"
	}
	local FONTDIR="$TGT_PREFIX/usr/share/fonts/truetype/dejavu"
	phx_fontfix_db "$NFS/etc/WindowMaker/WMGLOBAL"
	phx_fontfix_db "$NFS/etc/WindowMaker/WindowMaker"
	echo "  rewrote active defaults DBs (WMGLOBAL + WindowMaker) -> phxfile: DejaVu"
	grep -h "Font" "$NFS/etc/WindowMaker/WMGLOBAL" "$NFS/etc/WindowMaker/WindowMaker" 2>/dev/null | head -8

	# 5c. font: stage one real TTF family (DejaVu) — fontconfig+Xft return NULL
	# (wmaker won't start) with no font file. Take it from the host if present.
	mkdir -p "$NFS/usr/share/fonts/truetype/dejavu"
	local hostttf=/usr/share/fonts/truetype/dejavu
	if [ -d "$hostttf" ]; then
		cp "$hostttf/DejaVuSans.ttf" "$hostttf/DejaVuSans-Bold.ttf" \
		   "$hostttf/DejaVuSerif.ttf" "$hostttf/DejaVuSansMono.ttf" \
		   "$NFS/usr/share/fonts/truetype/dejavu/" 2>/dev/null
		echo "  staged DejaVu TTF -> $NFS/usr/share/fonts/truetype/dejavu/"
	else
		echo "  [WARN] host DejaVu TTF not found ($hostttf) — stage a TTF before booting"
	fi

	# 5d. fonts.conf (self-contained; maps generic family names -> DejaVu) + cache dir
	mkdir -p "$NFS/etc/fonts" "$NFS/var/cache/fontconfig"
	cp "$HERE/wmaker/fonts.conf" "$NFS/etc/fonts/fonts.conf"
	echo "  staged fonts.conf -> $NFS/etc/fonts/fonts.conf + cache dir $NFS/var/cache/fontconfig"

	echo "=== staged. binaries: ==="
	ls -l "$NFS/bin/wmaker" "$NFS/bin/wmsetbg" 2>&1
}

preflight() {
	local wm="$ART/wmaker"
	echo "=== PRE-FLIGHT ==="
	file "$wm"
	case "$(file "$wm")" in
		*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
		*) fail "binary is not an aarch64 static ELF" ;;
	esac

	local und
	und=$(${TC}nm -u "$wm" 2>/dev/null)
	if [ -n "$und" ]; then echo "$und"; fail "undefined symbols present (not fully static)"; fi
	echo "[OK] 0 undefined symbols"

	# compiled-in shell path (menu/<exec> commands)
	if strings "$wm" 2>/dev/null | grep -q "^/bin/sh$"; then
		echo "[OK] WMAKER_SHELL == /bin/sh"
	else
		fail "shell path /bin/sh not compiled in — menu commands would fail"
	fi

	# compiled-in data dir (must match where we stage). The path appears inside
	# colon-separated search-path strings, so match a substring, not a full line.
	if strings "$wm" 2>/dev/null | grep -q "/share/WindowMaker"; then
		echo "[OK] data dir /share/WindowMaker compiled in"
	else
		fail "/share/WindowMaker not found in strings (data-dir path wrong)"
	fi
	if strings "$wm" 2>/dev/null | grep -q "/etc/WindowMaker"; then
		echo "[OK] global defaults dir /etc/WindowMaker compiled in"
	fi

	# fontconfig's config dir (FONTCONFIG_PATH = sysconfdir/fonts) is baked into
	# the statically-linked libfontconfig. It MUST match where fonts.conf is
	# staged, or no aliases load and XftFontOpenName("sans serif") -> NULL ->
	# wmaker won't start.
	if strings "$wm" 2>/dev/null | grep -qx "/etc/fonts"; then
		echo "[OK] fontconfig reads /etc/fonts (= staged fonts.conf dir)"
	else
		fail "fontconfig config dir /etc/fonts not baked in — fonts.conf would never load"
	fi

	echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
	echo "HW test (orchestrator): boot the (nfsroot-default) image, then:"
	echo "    ls /bin                         # expect wmaker + wmsetbg present"
	echo "    startx wmaker                   # HOME/PATH defaulted by the xlaunch launcher"
}

# ----------------------------------------------------------------------------
build_expat
snapshot_x11_closure
build_fontconfig
build_libxft
build_libftw
build_wmaker
stage_wmaker
preflight

echo "=== font stack + Window Maker built into $DEPS / staged to $NFS ==="
ls "$DEPS"/lib/libexpat.a "$DEPS"/lib/libfontconfig.a "$DEPS"/lib/libXft.a "$DEPS"/lib/libftw.a 2>&1

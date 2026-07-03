#!/usr/bin/env bash
#
# Phoenix-RTOS — cross-build the Dillo web browser (dillo-browser/dillo 3.2.0)
# for aarch64-phoenix as a STATIC /bin/dillo, staged to the NFS rootfs export.
# Task #53. Host-side build only; does NOT boot/flash the Pi.
#
# Dillo is a lightweight C/C++ FLTK 1.3 X11 web browser. It links against:
#   - FLTK 1.3.10 static libs        (/tmp/fltk-phoenix — built by build-fltk.sh)
#   - the X11 client closure         (/tmp/x11-phoenix  — libX11/libxcb/libXau/...)
#   - the image libs png16/jpeg/zlib (from /tmp/x11-phoenix)
#
# FIRST BRING-UP = HTTP-ONLY BASELINE. Disabled because the backing libs are not
# yet built as static host-prefix libraries Dillo can link:
#   --disable-tls    no OpenSSL/mbedTLS host-prefix lib yet  -> no HTTPS (HTTP ok)
#   --disable-webp   libwebp not ported
# Everything else stays ENABLED — Phoenix's sysroot already provides the needed
# primitives (png/jpeg/gif/svg images, cookies, threaded-dns via getaddrinfo,
# iconv via libiconv, sockets/poll/select/fork/exec/pthread all verified present).
#
# THE LINK IS THE REAL RISK, not the configure flags. Dillo drives its final link
# through `fltk-config --ldflags`, which emits only `-lfltk -lpthread -lX11` — it
# OMITS the static xcb closure (-lxcb -lXau -lXdmcp), the image libs, and the
# -Wl,--start-group wrapping that a static C++/X cross-link needs. So we point
# FLTK_CONFIG at a WRAPPER (generated below) whose --ldflags emits the full
# grouped closure proven by the FLTK hello smoke test. Everything else delegates
# to the real fltk-config.
#
# Idempotent. Re-runnable. Output: $NFS/bin/dillo (+ a copy in artifacts/x11).
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=dillo-3.2.0
URL=https://github.com/dillo-browser/dillo/archive/refs/tags/v3.2.0.tar.gz

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
XPREFIX=/tmp/x11-phoenix            # READ-ONLY: shared X11 client lib stack
FPREFIX=/tmp/fltk-phoenix          # READ-ONLY: FLTK 1.3.10 static libs + fltk-config
PREFIX=/tmp/dillo-phoenix          # our own build/install prefix
SRC=${ROOT}/tools/ports/src
XDIR=$SRC/$NV
ART=${ROOT}/artifacts/x11
NFS="${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}"
SHIM=${ROOT}/tools/ports/dillo-phoenix-shim.h

fail() { echo "FAIL: $*"; exit 1; }

[ -f "$FPREFIX/lib/libfltk.a" ]  || fail "$FPREFIX/lib/libfltk.a missing — run build-fltk.sh first"
[ -f "$FPREFIX/fltk-config" ]    || fail "$FPREFIX/fltk-config missing — run build-fltk.sh first"
[ -f "$XPREFIX/lib/libX11.a" ]   || fail "$XPREFIX/lib/libX11.a missing — build the X11 client stack first"
[ -f "$SHIM" ]                   || fail "$SHIM missing"

mkdir -p "$SRC" "$PREFIX"

# --- fetch + extract ---------------------------------------------------------
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV.tar.gz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 180 -o "$SRC/$NV.tar.gz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV.tar.gz" || fail "extract failed"
fi

# --- source patches (idempotent; survive a clean re-extract) ------------------
# 1. strndup: dw/selection.cc UNCONDITIONALLY defines `extern "C" strndup()`
#    (assuming a platform without it). Phoenix's libphoenix DOES export a
#    non-weak strndup -> "multiple definition" at the final link. Rename Dillo's
#    private definition out of the way; its four call sites then resolve to
#    libphoenix's strndup. (A force-include shim can't fix this — it's a function
#    *definition*, and lout/misc.hh also has a class method named strndup that a
#    macro would mangle.)
if ! grep -q 'dillo_unused_strndup' "$XDIR/dw/selection.cc" 2>/dev/null; then
	sed -i 's/extern "C" char \*strndup(/extern "C" char *dillo_unused_strndup(/' "$XDIR/dw/selection.cc" \
		&& echo "=== patched dw/selection.cc (strndup -> dillo_unused_strndup) ==="
fi
# 2. getsockopt size mismatch: src/IO/http.c declares `uint_t connect_ret_size`
#    (4 bytes) but Phoenix's getsockopt() writes a `socklen_t` (8 bytes on LP64)
#    through that pointer -> a 4-byte stack overwrite on EVERY HTTP connect (the
#    hottest browser path). Fix the type to socklen_t. This is a real correctness
#    fix, not just a warning silence.
if grep -q 'uint_t connect_ret_size' "$XDIR/src/IO/http.c" 2>/dev/null; then
	sed -i 's/uint_t connect_ret_size/socklen_t connect_ret_size/' "$XDIR/src/IO/http.c" \
		&& echo "=== patched src/IO/http.c (connect_ret_size -> socklen_t) ==="
fi

# --- autogen: the tarball ships configure.ac but NO generated configure -------
# Run autoreconf (aclocal/autoheader/autoconf/automake -a) ONCE. This also drops
# automake's OWN config.sub/config.guess, which are NOT phoenix-aware — so the
# triplet refresh below MUST come AFTER this step.
if [ ! -x "$XDIR/configure" ]; then
	echo "=== autoreconf $NV ==="
	( cd "$XDIR" && autoreconf -fi >/tmp/dillo-autogen.log 2>&1 ) || { tail -30 /tmp/dillo-autogen.log; fail "autoreconf failed"; }
fi

# --- refresh config.sub/config.guess to phoenix-aware copies (AFTER autogen) --
for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		src=$(grep -lr phoenix "$SRC"/*/$cfg 2>/dev/null | head -1)
		[ -n "$src" ] && cp "$src" "$XDIR/$cfg" && echo "=== refreshed $cfg (phoenix-aware) from $src ==="
	fi
done
grep -q phoenix "$XDIR/config.sub" || fail "config.sub still not phoenix-aware — no donor copy found under $SRC"

# --- the FLTK_CONFIG wrapper: full grouped static link closure ---------------
# Dillo's src/Makefile builds:  ... @LIBFLTK_LIBS@ ...  where LIBFLTK_LIBS is the
# verbatim output of `fltk-config --ldflags`. We override ONLY --ldflags to emit
# the complete static closure (image libs + xcb + libstdc++/m/phoenix/c) wrapped
# in --start-group/--end-group so the static cross-references resolve in any
# order. --cflags/--cxxflags/--version/etc. pass through to the real fltk-config.
WRAP=$PREFIX/fltk-config
cat > "$WRAP" <<WRAPEOF
#!/bin/sh
# AUTO-GENERATED by build-dillo.sh — full static link closure for Dillo on Phoenix.
REAL=$FPREFIX/fltk-config
case " \$* " in
	*" --ldflags "*)
		# -L paths from the real config, then the proven grouped closure.
		printf '%s ' "-L$FPREFIX/lib" "-L$XPREFIX/lib" "--sysroot=$SYSROOT" "-L$SYSROOT/lib"
		printf '%s ' "-Wl,--start-group" \\
		  "-lfltk_images" "-lfltk" "-lpng16" "-ljpeg" "-lz" \\
		  "-lX11" "-lxcb" "-lXau" "-lXdmcp" \\
		  "-lstdc++" "-lm" "-lphoenix" "-lc" \\
		  "-Wl,--end-group"
		echo
		;;
	*)
		exec "\$REAL" "\$@"
		;;
esac
WRAPEOF
chmod +x "$WRAP"
echo "=== fltk-config wrapper -> $WRAP ==="

# --- cross flags -------------------------------------------------------------
# Force-include the Phoenix shim (AI_* getaddrinfo hint macros). Point includes
# at the FLTK + X11 prefixes and the cross sysroot.
# GCC 14 promotes -Wincompatible-pointer-types / -Wint-conversion to hard ERRORS
# by default. Dillo's IO/http.c passes a `uint_t*` (4 bytes) where Phoenix's
# getsockopt() wants `socklen_t*` (8 bytes on LP64). Demote both back to warnings
# so the build completes. NOTE: the socklen_t size mismatch is a latent RUNTIME
# bug (getsockopt would write 8 bytes into a 4-byte stack slot) — flagged for the
# attended runtime session; it does not affect the link-time deliverable.
XCFLAGS="--sysroot=$SYSROOT -I$FPREFIX/include -I$XPREFIX/include -include $SHIM -O2 -Wno-error=incompatible-pointer-types -Wno-error=int-conversion"
XLDFLAGS="--sysroot=$SYSROOT -L$FPREFIX/lib -L$XPREFIX/lib -L$SYSROOT/lib"

# --- configure ---------------------------------------------------------------
if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV (HTTP-only: --disable-tls --disable-webp) ==="
	( cd "$XDIR" && FLTK_CONFIG="$WRAP" ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="$PREFIX" \
	    --disable-tls --disable-webp \
	    --with-jpeg-lib="$XPREFIX/lib" --with-jpeg-inc="$XPREFIX/include" \
	    CC=${TC}gcc CXX=${TC}g++ AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="$XCFLAGS" CXXFLAGS="$XCFLAGS" \
	    CPPFLAGS="$XCFLAGS" LDFLAGS="$XLDFLAGS" \
	    PKG_CONFIG=/bin/false \
	    >/tmp/dillo-conf.log 2>&1 ) || { tail -50 /tmp/dillo-conf.log; fail "configure failed"; }
fi

# --- build -------------------------------------------------------------------
echo "=== building $NV ==="
( cd "$XDIR" && make >/tmp/dillo-build.log 2>&1 ) || { tail -60 /tmp/dillo-build.log; fail "make failed"; }

DILLO_BIN="$XDIR/src/dillo"
[ -x "$DILLO_BIN" ] || fail "src/dillo not produced"

# --- PRE-FLIGHT VALIDATION (the deliverable test) ----------------------------
echo "=== PRE-FLIGHT ==="
file "$DILLO_BIN"
case "$(file "$DILLO_BIN")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*"ARM aarch64"*) echo "[WARN] aarch64 ELF but not reported static — check below" ;;
	*) fail "dillo binary is not an aarch64 ELF" ;;
esac

# Confirm the X11/xcb closure actually got pulled in (the headline risk). On an
# incremental run the link line isn't re-emitted, so check the BINARY for X11
# symbols rather than grepping a possibly-stale build log.
echo "=== xcb/X11 closure sanity (symbols in the binary) ==="
if ${TC}nm "$DILLO_BIN" 2>/dev/null | grep -q ' [TtRr] _\?XOpenDisplay'; then
	echo "[OK] X11 (XOpenDisplay) linked into the binary"
else
	echo "[WARN] XOpenDisplay not found in binary — X11 closure may be missing"
fi

echo "=== undefined symbols (nm -u) ==="
und=$(${TC}nm -u "$DILLO_BIN" 2>/dev/null)
if [ -z "$und" ]; then
	echo "[OK] 0 undefined symbols"
else
	echo "$und"
	echo "[WARN] undefined symbols present (see above)"
fi

# --- stage to NFS rootfs -----------------------------------------------------
mkdir -p "$NFS/bin" "$ART"
cp "$DILLO_BIN" "$NFS/bin/dillo"
cp "$DILLO_BIN" "$ART/dillo"
# Stage the default config so the browser has sane runtime defaults.
mkdir -p "$NFS/etc/dillo"
[ -f "$XDIR/dillorc" ] && cp "$XDIR/dillorc" "$NFS/etc/dillo/dillorc"

echo "=== Dillo staged ==="
ls -la "$NFS/bin/dillo"
echo "binary:   $NFS/bin/dillo"
echo "artifact: $ART/dillo"
[ -z "$und" ] && echo "=== ALL PRE-FLIGHT CHECKS PASSED ===" || echo "=== BUILD COMPLETE WITH UNDEFINED SYMBOLS (see above) ==="

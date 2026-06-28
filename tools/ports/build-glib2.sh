#!/usr/bin/env bash
#
# Phoenix-RTOS — cross-build glib-2.56.4 for aarch64-phoenix as REUSABLE static
# libraries (libglib-2.0.a + libgobject-2.0.a + libgmodule-2.0.a + libgthread).
# This is the hard prerequisite for Midnight Commander (task #55); mc links
# glib-2.0 (GList/GHashTable/GString/g_malloc/...).
#
# Version: glib 2.56.x is the LAST autotools (./configure) glib series — glib went
# meson-only at 2.60. 2.56 also still bundles PCRE1 (--with-pcre=internal), so it
# needs no system pcre2.
#
# Dependency chain resolved here:
#   - pcre   : BUNDLED in glib/pcre  -> --with-pcre=internal (no external port)
#   - iconv  : stub libiconv.a (build-libiconv.sh) -> --with-libiconv=maybe + cache
#   - zlib   : ported (/tmp/x11-phoenix/lib/libz.a, also in sysroot)
#   - gettext: stubbed via --disable-nls + glib-phoenix-shim.h identity macros
#   - libffi : OPTIONAL, only needed by gobject's gclosure marshalling; if present
#              in the sysroot gobject builds fully, otherwise gobject is skipped.
#
# The AC_TRY_RUN cross-compile probes are pre-answered via glib2.cache (canonical
# aarch64 values) loaded with --cache-file, so configure doesn't error/hang.
#
# Output: $PREFIX/lib/lib{glib,gobject,gmodule,gthread}-2.0.a + headers, staged
# into the cross sysroot for downstream (mc).
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=glib-2.56.4
URL=https://download.gnome.org/sources/glib/2.56/$NV.tar.xz

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/phoenix-glib
HERE=/home/houp/phoenix-rpi/tools/ports
SRC=$HERE/src
XDIR=$SRC/$NV
SHIM=$HERE/glib-phoenix-shim.h
CACHE=$HERE/glib2.cache
ZLIB=/tmp/x11-phoenix     # ported zlib lives here (libz.a + zlib.h)

fail() { echo "FAIL: $*"; exit 1; }

# Ensure the iconv stub is built + staged.
[ -f "$SYSROOT/lib/libiconv.a" ] || { "$HERE/build-libiconv.sh" || fail "build-libiconv.sh failed"; }
# Ensure libffi is built + staged (glib-2.56 configure HARD-requires it).
[ -f "$SYSROOT/lib/libffi.a" ] || { "$HERE/build-libffi.sh" || fail "build-libffi.sh failed"; }
# Ensure zlib is reachable.
[ -f "$SYSROOT/lib/libz.a" ] || cp -a "$ZLIB/lib/libz.a" "$SYSROOT/lib/" 2>/dev/null || true
[ -f "$SYSROOT/usr/include/zlib.h" ] || cp -a "$ZLIB/include/zlib.h" "$ZLIB/include/zconf.h" "$SYSROOT/usr/include/" 2>/dev/null || true

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV.tar.xz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 300 -o "$SRC/$NV.tar.xz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV.tar.xz" || fail "extract failed"
fi

# Refresh config.sub/guess for "phoenix".
for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		s=$(grep -lr phoenix /home/houp/phoenix-rpi/tools/x11-port/src/*/$cfg 2>/dev/null | head -1)
		[ -n "$s" ] && cp "$s" "$XDIR/$cfg" && echo "=== refreshed $cfg ==="
	fi
done

# libffi is required by glib-2.56 (gobject gclosure marshalling). We supply it
# via LIBFFI_CFLAGS/LIBFFI_LIBS so configure never calls pkg-config for it.
[ -f "$SYSROOT/usr/include/ffi.h" ] || fail "ffi.h missing in sysroot (build-libffi.sh)"

# Stub <libintl.h> (identity gettext macros) — glib HARD-requires a gettext
# provider even with --disable-nls. Stage it so the configure header probe + the
# compile of glib's i18n call sites both resolve.
cp -a "$HERE/libintl-stub/libintl.h" "$SYSROOT/usr/include/libintl.h"

# Stub <arpa/nameser.h> (DNS class/type constants) — glib configure hard-requires
# C_IN to be defined; Phoenix ships no resolver headers.
mkdir -p "$SYSROOT/usr/include/arpa"
cp -a "$HERE/nameser-stub/arpa/nameser.h" "$SYSROOT/usr/include/arpa/nameser.h"

# Stub libresolv.a + <resolv.h> — glib configure LINK-tests res_query(); Phoenix
# has no resolver. The stub fails cleanly at runtime (gio gresolver isn't built
# for libglib-2.0). Install header + build the .a into the sysroot, and glib's
# configure picks it up via the "in -lresolv" branch.
cp -a "$HERE/resolv-stub/resolv.h" "$SYSROOT/usr/include/resolv.h"
if [ ! -f "$SYSROOT/lib/libresolv.a" ]; then
	${TC}gcc --sysroot="$SYSROOT" -O2 -c "$HERE/resolv-stub/resolv-stub.c" -I"$HERE/resolv-stub" -o /tmp/resolv-stub.o || fail "resolv stub compile failed"
	${TC}ar rcs "$SYSROOT/lib/libresolv.a" /tmp/resolv-stub.o || fail "resolv stub ar failed"
fi

CF="--sysroot=$SYSROOT -O2 -I$ZLIB/include -include $SHIM"
LD="--sysroot=$SYSROOT -static -L$SYSROOT/lib -L$ZLIB/lib"

if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	# Fresh cache each configure run (idempotent): copy the seed in.
	cp "$CACHE" "$XDIR/glib2.cache"
	( cd "$XDIR" && ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="$PREFIX" \
	    --cache-file=glib2.cache \
	    --enable-static --disable-shared --disable-nls --disable-libmount \
	    --disable-selinux --disable-dtrace --disable-systemtap --disable-coverage \
	    --disable-installed-tests --with-pcre=internal --with-libiconv=maybe \
	    --with-threads=posix \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="$CF" CPPFLAGS="--sysroot=$SYSROOT -I$ZLIB/include" \
	    LDFLAGS="$LD" LIBS="-liconv" \
	    PKG_CONFIG=/bin/false \
	    ZLIB_CFLAGS="-I$ZLIB/include" ZLIB_LIBS="-L$ZLIB/lib -lz" \
	    LIBFFI_CFLAGS="-I$SYSROOT/usr/include" LIBFFI_LIBS="-L$SYSROOT/lib -lffi" \
	    >/tmp/glib-conf.log 2>&1 ) || { tail -n 50 /tmp/glib-conf.log; fail "configure failed"; }
fi

echo "=== building $NV (glib core) ==="
# Build just the glib/ library first (the mc-critical deliverable), then attempt
# the rest. -k keeps going so gobject/gio tooling failures don't kill libglib.
( cd "$XDIR/glib" && make >/tmp/glib-build.log 2>&1 ) || { tail -n 60 /tmp/glib-build.log; fail "libglib build failed"; }

GLIBA="$XDIR/glib/.libs/libglib-2.0.a"
[ -f "$GLIBA" ] || fail "libglib-2.0.a not produced"

echo "=== building gobject/gmodule/gthread (best-effort) ==="
( cd "$XDIR/gthread" && make >/tmp/glib-gthread.log 2>&1 ) || echo "  [warn] gthread build issues (see /tmp/glib-gthread.log)"
( cd "$XDIR/gmodule" && make >/tmp/glib-gmodule.log 2>&1 ) || echo "  [warn] gmodule build issues (see /tmp/glib-gmodule.log)"
( cd "$XDIR/gobject" && make >/tmp/glib-gobject.log 2>&1 ) || echo "  [warn] gobject build issues (see /tmp/glib-gobject.log)"

# Stage libraries into PREFIX and the sysroot.
mkdir -p "$PREFIX/lib" "$PREFIX/lib/glib-2.0/include"
for la in glib/.libs/libglib-2.0.a gthread/.libs/libgthread-2.0.a \
          gmodule/.libs/libgmodule-2.0.a gobject/.libs/libgobject-2.0.a; do
	[ -f "$XDIR/$la" ] && cp -a "$XDIR/$la" "$PREFIX/lib/" && cp -a "$XDIR/$la" "$SYSROOT/lib/"
done

# Headers: reproduce glib's install layout by direct copy (glib's `make install`
# runs an install-data-local hook that fails in this cross env and aborts header
# recursion, so we stage explicitly + deterministically):
#   include/glib-2.0/glib.h, glib-object.h, gmodule.h
#   include/glib-2.0/glib/*.h  (+ glib/deprecated/*.h)
#   include/glib-2.0/gobject/*.h
#   lib/glib-2.0/include/glibconfig.h   (the generated config header)
GINC="$PREFIX/include/glib-2.0"
rm -rf "$GINC"
mkdir -p "$GINC/glib/deprecated" "$GINC/gobject" "$GINC/gmodule" "$PREFIX/lib/glib-2.0/include"
# Top-level public headers included as <glib.h>/<glib-unix.h>/<glib-object.h>
# (glibinclude_HEADERS) — these live directly under include/glib-2.0/, NOT glib/.
cp -a "$XDIR"/glib/glib.h "$XDIR"/glib/glib-unix.h "$XDIR"/glib/glib-object.h "$XDIR"/glib/gi18n.h "$XDIR"/glib/gi18n-lib.h "$GINC/" 2>/dev/null || true
cp -a "$XDIR"/gmodule/gmodule.h "$GINC/" 2>/dev/null || true
# all glib/*.h except the .in template; deprecated subdir too
for h in "$XDIR"/glib/*.h; do cp -a "$h" "$GINC/glib/"; done
cp -a "$XDIR"/glib/deprecated/*.h "$GINC/glib/deprecated/" 2>/dev/null || true
cp -a "$XDIR"/gobject/*.h "$GINC/gobject/" 2>/dev/null || true
# glibconfig.h is generated at $XDIR/glib/glibconfig.h (config_commands target).
GLIBCONFIG="$XDIR/glib/glibconfig.h"
[ -f "$GLIBCONFIG" ] && cp -a "$GLIBCONFIG" "$PREFIX/lib/glib-2.0/include/glibconfig.h"

# Mirror PREFIX headers into the sysroot the way downstream -I dirs expect.
mkdir -p "$SYSROOT/usr/include" "$SYSROOT/usr/lib/glib-2.0/include"
rm -rf "$SYSROOT/usr/include/glib-2.0"
cp -a "$GINC" "$SYSROOT/usr/include/"
cp -a "$PREFIX/lib/glib-2.0/include/glibconfig.h" "$SYSROOT/usr/lib/glib-2.0/include/glibconfig.h"

echo "=== glib2 build summary ==="
ls -la "$PREFIX"/lib/lib*.a 2>/dev/null
echo "--- libglib-2.0.a core symbols ---"
${TC}nm "$GLIBA" 2>/dev/null | grep -E " T (g_malloc|g_list_append|g_hash_table_new|g_string_new|g_strdup|g_main_loop_new)$" | sed 's/^/  /'
echo "--- undefined symbols (informational; many are libc, resolved at final link) ---"
${TC}nm -u "$GLIBA" 2>/dev/null | sort -u | head -40
echo "=== glib2 core OK (libglib-2.0.a) ==="

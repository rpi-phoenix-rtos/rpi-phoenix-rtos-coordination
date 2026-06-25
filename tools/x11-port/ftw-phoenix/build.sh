#!/usr/bin/env bash
# Build libftw.a (minimal nftw/ftw, see ftw.c) for the aarch64-phoenix X11 port
# and install it + ftw.h into the wmaker dependency prefix. libphoenix ships no
# <ftw.h>; Window Maker's WINGs toolkit needs nftw() in one helper. See
# ../WMAKER-PORT-STATUS.md (ftw/nftw listed as a known libphoenix gap).
set -euo pipefail

SR=${SYSROOT:-/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}
TC=${TOOLCHAIN_BIN:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin}
PREFIX=${DEPS_PREFIX:-/tmp/wmaker-deps}
HERE=$(cd "$(dirname "$0")" && pwd)

cd "$HERE"
"$TC/aarch64-phoenix-gcc" --sysroot="$SR" -I"$HERE" -O2 -Wall -c ftw.c -o ftw.o
"$TC/aarch64-phoenix-ar" rcs libftw.a ftw.o
"$TC/aarch64-phoenix-ranlib" libftw.a

install -d "$PREFIX/lib" "$PREFIX/include"
cp libftw.a "$PREFIX/lib/"
cp ftw.h "$PREFIX/include/"
cp wmaker-phoenix-compat.h "$PREFIX/include/"
echo "libftw.a + ftw.h + wmaker-phoenix-compat.h installed into $PREFIX"

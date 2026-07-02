#!/usr/bin/env bash
# Build libmd.a (public-domain SHA1, BSD libmd API) for the aarch64-phoenix X
# server port and install it into the X11 prefix. xorg-server's os/xsha1.c
# selects this backend via `./configure --with-sha1=libmd` (Phoenix has no
# openssl/libgcrypt/libnettle). See ../PROGRESS.md.
set -euo pipefail

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../../.." && pwd)"

SR=${SYSROOT:-${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}
TC=${TOOLCHAIN_BIN:-${ROOT}/.toolchain/aarch64-phoenix/bin}
PREFIX=${X11_PREFIX:-/tmp/x11-phoenix}
HERE=$(cd "$(dirname "$0")" && pwd)

cd "$HERE"
"$TC/aarch64-phoenix-gcc" --sysroot="$SR" -O2 -c sha1.c -o sha1.o
"$TC/aarch64-phoenix-ar" rcs libmd.a sha1.o
"$TC/aarch64-phoenix-ranlib" libmd.a

install -d "$PREFIX/lib" "$PREFIX/include"
cp libmd.a "$PREFIX/lib/"
cp sha1.h "$PREFIX/include/"
echo "libmd.a + sha1.h installed into $PREFIX"

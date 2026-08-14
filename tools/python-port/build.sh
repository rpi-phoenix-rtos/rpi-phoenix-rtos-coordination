#!/usr/bin/env bash
# Cross-build CPython 3.14.4 for Phoenix-RTOS / aarch64.  WORK IN PROGRESS.
# CPython is PSF-licensed (permissive, GPL-compatible) -> safe for Phoenix.
#
# Status (2026-08-14): configure SUCCEEDS; `make` reaches ~32 objects then walls
# on mimalloc (madvise/MADV_DONTNEED/struct rusage fields Phoenix lacks). Next:
# disable mimalloc (--without-mimalloc) or shim those, then continue make.
#
# Prereq: a HOST python3 of the SAME minor version (3.14) for --with-build-python.
set -euo pipefail
VER=3.14.4
REPO=/home/houp/phoenix-rpi
TC=$REPO/.toolchain/aarch64-phoenix/bin
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/python-port-build}
mkdir -p "$WORK"; cd "$WORK"
[ -f "Python-$VER.tar.xz" ] || curl -fsSL -o "Python-$VER.tar.xz" "https://www.python.org/ftp/python/$VER/Python-$VER.tar.xz"
rm -rf "Python-$VER"; tar xf "Python-$VER.tar.xz"
cd "Python-$VER"

# --- teach configure about Phoenix (two cross-build MACHDEP blocks that else
#     hard-error "cross build not supported for aarch64-unknown-phoenix") ---
perl -0pi -e 's/(\t\*-\*-wasi\*\)\n\t    ac_sys_system=WASI\n\t    ;;\n)/$1\t*-*-phoenix*)\n\t    ac_sys_system=Phoenix\n\t    ;;\n/' configure
perl -0pi -e 's/(\twasm32-\*-\* \| wasm64-\*-\*\)\n\t\t_host_ident=\$host_cpu\n\t\t;;\n)/$1\t*-*-phoenix*)\n\t\t_host_ident=\$host_cpu\n\t\t;;\n/' configure

export CONFIG_SITE="$HERE/config.site"
export PATH="$TC:$PATH"
export CC=aarch64-phoenix-gcc CXX=aarch64-phoenix-g++ AR=aarch64-phoenix-ar RANLIB=aarch64-phoenix-ranlib READELF=aarch64-phoenix-readelf
./configure --host=aarch64-phoenix --build=x86_64-pc-linux-gnu \
  --with-build-python=/usr/bin/python3 \
  --disable-ipv6 --without-ensurepip --disable-shared --disable-test-modules \
  --without-mimalloc \
  CFLAGS="-include $HERE/phoenix-py-compat.h"

# phoenix-py-compat.h (-include'd first in every TU) shims the libc gaps found so
# far: early sys/time.h+sys/resource.h (complete struct timeval/rusage for CPython
# internal headers), wide-char decls (wcstol/wcstok/wcstoul/wcstod/wcsstr/wcsspn/
# wcscspn/wcspbrk/... — declare so compile proceeds, link then reveals which need
# real defs), a clock_getres nominal-1ns shim, and O_NOFOLLOW=0.
make -j4   # advances to ~120 objects; next compile gap: _SC_TTY_NAME_MAX (see STATUS.md)
echo "see STATUS.md for the current wall + next steps."

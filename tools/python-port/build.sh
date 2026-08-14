#!/usr/bin/env bash
# Cross-build CPython 3.14.4 for Phoenix-RTOS / aarch64 -> a static `python3`.
# CPython is PSF-licensed (permissive, GPL-compatible) -> safe for Phoenix.
#
# RESULT (2026-08-14): runs on the Pi 4 over netboot — `python3 -c print(6*7)`=>42,
# selftest.py => ALL-OK (Python 3.14.4). See README.md / STATUS.md.
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

# 1. Teach configure about Phoenix (two cross-build MACHDEP blocks that otherwise
#    hard-error "cross build not supported for aarch64-unknown-phoenix").
perl -0pi -e 's/(\t\*-\*-wasi\*\)\n\t    ac_sys_system=WASI\n\t    ;;\n)/$1\t*-*-phoenix*)\n\t    ac_sys_system=Phoenix\n\t    ;;\n/' configure
perl -0pi -e 's/(\twasm32-\*-\* \| wasm64-\*-\*\)\n\t\t_host_ident=\$host_cpu\n\t\t;;\n)/$1\t*-*-phoenix*)\n\t\t_host_ident=\$host_cpu\n\t\t;;\n/' configure

export CONFIG_SITE="$HERE/config.site"     # cross cache: MANY ac_cv_func_*=yes for
                                           # funcs Phoenix has but the cross-check
                                           # missed (fork/execv/sysconf/timegm/clock/
                                           # ...), + py_cv_module_*=n/a to drop
                                           # external-lib modules (zlib/_ssl/_ctypes/
                                           # readline/_sqlite3/_zstd/resource/...).
export PATH="$TC:$PATH"
export CC=aarch64-phoenix-gcc CXX=aarch64-phoenix-g++ AR=aarch64-phoenix-ar RANLIB=aarch64-phoenix-ranlib READELF=aarch64-phoenix-readelf

# 2. Configure. --without-mimalloc (mimalloc needs madvise/rusage Phoenix lacks ->
#    pymalloc). phoenix-py-compat.h (-include) shims the remaining libc gaps:
#    early sys/time.h+resource.h+mman.h (complete struct timeval/rusage), _SC_*
#    sysconf names, clock_getres/msync no-ops, O_NOFOLLOW=0, SOMAXCONN=128.
./configure --host=aarch64-phoenix --build=x86_64-pc-linux-gnu \
  --with-build-python=/usr/bin/python3 \
  --disable-ipv6 --without-ensurepip --disable-shared --disable-test-modules \
  --without-mimalloc \
  CFLAGS="-include $HERE/phoenix-py-compat.h"

# 3. The .so extension linker defaults to the host `ld` (wrong arch). Point it at
#    the cross gcc (only matters if you later build shared modules; the static
#    interpreter below doesn't need them).
sed -i 's/^LDSHARED=\tld /LDSHARED=\taarch64-phoenix-gcc -shared /; s/^BLDSHARED=\tld /BLDSHARED=\taarch64-phoenix-gcc -shared /' Makefile

# 4. Build JUST the static interpreter (libpython + builtin modules baked in).
#    `make` (all) would also try to build the ~40 shared .so stdlib extensions;
#    we skip those for now (a static python with builtin modules + the pure-python
#    stdlib is enough to run). Re-enable individual .so modules later as needed.
make -j4 python
"$TC/aarch64-phoenix-readelf" -h python | grep Machine
echo "OK -> $PWD/python  (static aarch64 CPython 3.14.4)"
echo
echo "Deploy (netboot NFS root): stage the binary + the pure-python stdlib at the"
echo "compiled prefix so startup finds 'encodings' etc.:"
echo "  cp python            <nfsroot>/bin/python3"
echo "  cp -r Lib/*          <nfsroot>/usr/local/lib/python3.14/"
echo "Run on the Pi:  /bin/python3 -S /selftest.py   # => ALL-OK"

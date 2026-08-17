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

# 4. Build the STATIC extension modules into the interpreter (Setup.local lists
#    the pure-C, no-external-lib stdlib modules: _socket/array/struct/json/select/
#    math/mmap/pickle/csv/...). makesetup gives Setup.local priority over the
#    Setup.stdlib *shared* defs, so these link into `python` instead of as .so.
cp "$HERE/Setup.local" Modules/Setup.local

# 5. Optional: the `_sqlite3` module (Python + SQLite). Build libsqlite3.a from the
#    SQLite amalgamation and append the module to Setup.local. Set SKIP_SQLITE=1 to
#    skip. Downloads the SQLite amalgamation (same version as the official
#    phoenix-rtos-ports/sqlite3 port).
if [ "${SKIP_SQLITE:-0}" != 1 ]; then
	SQLVER=3530400
	SQLZIP="sqlite-amalgamation-$SQLVER.zip"
	SQLSHA=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
	SQLDIR="$WORK/sqlite-amalgamation-$SQLVER"
	if [ ! -d "$SQLDIR" ]; then
		( cd "$WORK"; [ -f "$SQLZIP" ] || curl -fsSL -o "$SQLZIP" "https://www.sqlite.org/2026/$SQLZIP"
		  echo "$SQLSHA  $SQLZIP" | sha256sum -c -; unzip -oq "$SQLZIP" )
	fi
	"$CC" -O2 -c "$SQLDIR/sqlite3.c" -o "$SQLDIR/sqlite3.o" \
		-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_JSON1 -DSQLITE_ENABLE_FTS5
	"$AR" rcs "$SQLDIR/libsqlite3.a" "$SQLDIR/sqlite3.o"; "$RANLIB" "$SQLDIR/libsqlite3.a"
	# MODULE_NAME is defined in Modules/_sqlite/module.h, so no -D needed.
	grep -q '^_sqlite3 ' Modules/Setup.local || \
	echo "_sqlite3 _sqlite/blob.c _sqlite/connection.c _sqlite/cursor.c _sqlite/microprotocols.c _sqlite/module.c _sqlite/prepare_protocol.c _sqlite/row.c _sqlite/statement.c _sqlite/util.c -I$SQLDIR -L$SQLDIR -lsqlite3" >> Modules/Setup.local
fi

# 5b. Optional: the `zlib` module (unlocks gzip / zipfile / zipimport). Cross-build
#     libz.a from zlib 1.2.11 (same version as phoenix-rtos-ports/zlib) and append
#     the module to Setup.local. Set SKIP_ZLIB=1 to skip.
if [ "${SKIP_ZLIB:-0}" != 1 ]; then
	ZVER=1.2.11
	ZTGZ="zlib-$ZVER.tar.gz"
	ZSHA=c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1
	ZDIR="$WORK/zlib-$ZVER"
	if [ ! -d "$ZDIR" ]; then
		( cd "$WORK"; [ -f "$ZTGZ" ] || curl -fsSL -o "$ZTGZ" "https://zlib.net/fossils/$ZTGZ"
		  echo "$ZSHA  $ZTGZ" | sha256sum -c -; tar xzf "$ZTGZ" )
	fi
	[ -f "$ZDIR/zconf.h" ] || cp "$ZDIR/zconf.h.in" "$ZDIR/zconf.h"
	ZOBJS=""
	# -DZ_HAVE_UNISTD_H: configure normally sets this; it makes the gz* sources pull
	# <unistd.h> for read/write/lseek (else implicit-decl errors on Phoenix).
	for zc in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil; do
		"$CC" -O2 -DZ_HAVE_UNISTD_H=1 -c "$ZDIR/$zc.c" -o "$ZDIR/$zc.o"
		ZOBJS="$ZOBJS $ZDIR/$zc.o"
	done
	"$AR" rcs "$ZDIR/libz.a" $ZOBJS; "$RANLIB" "$ZDIR/libz.a"
	grep -q '^zlib ' Modules/Setup.local || \
	echo "zlib zlibmodule.c -I$ZDIR -L$ZDIR -lz" >> Modules/Setup.local
fi

# 6. Build JUST the interpreter (skip the remaining .so extensions we didn't
#    make static). `make` (all) would try to link those as .so with the wrong
#    linker; `make python` links the static interpreter + the Setup.local modules.
make -j4 python
"$TC/aarch64-phoenix-readelf" -h python | grep Machine
echo "OK -> $PWD/python  (static aarch64 CPython 3.14.4)"
echo
echo "Deploy (netboot NFS root): stage the binary + the pure-python stdlib at the"
echo "compiled prefix so startup finds 'encodings' etc.:"
echo "  cp python            <nfsroot>/bin/python3"
echo "  cp -r Lib/*          <nfsroot>/usr/local/lib/python3.14/"
echo "Run on the Pi:  /bin/python3 -S /selftest.py   # => ALL-OK"

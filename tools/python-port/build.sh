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

# 5c. Optional: `_ssl` + `_hashlib` (TLS/HTTPS + OpenSSL-backed hashlib). Links the
#     official phoenix-rtos-ports/openssl111 (now built thread-safe). Needs a prior
#     `rebuild-rpi4b-fast.sh --with-ports` so libssl.a/libcrypto.a exist. SKIP_SSL=1 to skip.
if [ "${SKIP_SSL:-0}" != 1 ]; then
	OSSL="$REPO/.buildroot/_build/aarch64a72-generic-rpi4b/versioned-ports/openssl-1.1.1a"
	if [ -f "$OSSL/lib/libssl.a" ]; then
		grep -q '^_ssl ' Modules/Setup.local || \
		echo "_ssl _ssl.c -I$OSSL/include -L$OSSL/lib -lssl -lcrypto" >> Modules/Setup.local
		grep -q '^_hashlib ' Modules/Setup.local || \
		echo "_hashlib _hashopenssl.c -I$OSSL/include -L$OSSL/lib -lcrypto" >> Modules/Setup.local
	else
		echo "SKIP _ssl: official openssl111 not built (run rebuild-rpi4b-fast.sh --with-ports first)"
	fi
fi


# 5d. `_decimal` (arbitrary-precision Decimal / the `decimal` stdlib module).
#     CPython 3.14 still bundles libmpdec under Modules/_decimal/libmpdec, so this is
#     self-contained (no external download). Compile the module + all libmpdec sources
#     (minus the bench*.c timing helpers, which aren't part of the library) with the
#     64-bit config mpdecimal.h requires. Flags mirror the stock MODULE__DECIMAL_CFLAGS.
#     NB: config.site sets py_cv_module__decimal=n/a so configure does NOT emit its own
#     _decimal rules — otherwise they collide with this static line and corrupt the
#     Makefile (same disable-then-append pattern as _sqlite3/zlib). SKIP_DECIMAL=1 to skip.
if [ "${SKIP_DECIMAL:-0}" != 1 ]; then
	MPDEC="basearith constants context convolute crt difradix2 fnt fourstep io mpalloc mpdecimal mpsignal numbertheory sixstep transpose"
	DECSRCS="_decimal/_decimal.c"
	for m in $MPDEC; do DECSRCS="$DECSRCS _decimal/libmpdec/$m.c"; done
	# NB: no '=' in the -D flags. makesetup treats ANY Setup line containing '=' as a
	# Makefile variable definition and echoes it raw (breaking the Makefile), so use
	# bare -DCONFIG_64 (gcc defines it to 1) instead of -DCONFIG_64=1. The mpdecimal
	# headers test these with #if defined(...), so the value is irrelevant.
	grep -q '^_decimal ' Modules/Setup.local || \
	echo "_decimal $DECSRCS -IModules/_decimal/libmpdec -DCONFIG_64 -DANSI -DHAVE_UINT128_T" >> Modules/Setup.local
fi


# 5e. `_ctypes` (Python FFI: the `ctypes` module / CDLL). Needs libffi — build it via
#     the committed tools/ports/build-libffi.sh (idempotent: downloads + cross-builds
#     libffi 3.3 into /tmp/phoenix-ffi + the cross sysroot). Forward calls (load a lib,
#     call a C function) work; ffi closures/callbacks need executable mmap, which is
#     limited on Phoenix (callbacks may not work). The HAVE_FFI_* macros are normally
#     set by configure's libffi probe; we supply them directly (libffi 3.3 has all of
#     them). Same no-'=' rule for the -D flags as _decimal. SKIP_CTYPES=1 to skip.
if [ "${SKIP_CTYPES:-0}" != 1 ]; then
	FFIPREFIX=/tmp/phoenix-ffi
	[ -f "$FFIPREFIX/lib/libffi.a" ] || "$REPO/tools/ports/build-libffi.sh" || true
	if [ -f "$FFIPREFIX/lib/libffi.a" ]; then
		# _ctypes' callproc.c defines its own set_errno/get_errno, which clash with
		# Phoenix <errno.h>'s `static inline int set_errno(int)`. Rename ONLY _ctypes'
		# C functions (definitions + method-table references) to ctypes_{set,get}_errno.
		# The quoted "set_errno"/"get_errno" method names Python sees are left intact.
		# (A global -Dset_errno=... would rename errno.h's inline too -> same clash, and
		# a Setup.local -D can't contain '=' anyway: makesetup reads it as a Make var.)
		perl -0pi -e 's/\nset_errno\(PyObject \*self/\nctypes_set_errno(PyObject *self/;
		              s/\nget_errno\(PyObject \*self/\nctypes_get_errno(PyObject *self/;
		              s/\{"set_errno", set_errno,/{"set_errno", ctypes_set_errno,/;
		              s/\{"get_errno", get_errno,/{"get_errno", ctypes_get_errno,/' \
		              Modules/_ctypes/callproc.c
		CTSRCS="_ctypes/_ctypes.c _ctypes/callbacks.c _ctypes/callproc.c _ctypes/cfield.c _ctypes/malloc_closure.c _ctypes/stgdict.c"
		grep -q '^_ctypes ' Modules/Setup.local || \
		# -DUSING_MALLOC_CLOSURE_DOT_C: without it ctypes.h #defines Py_ffi_closure_free
		# -> ffi_closure_free, so malloc_closure.c's Py_ffi_closure_free definition
		# redefines libffi's symbol (multiple-definition link error + self-recursion).
		# With it, malloc_closure.c owns Py_ffi_closure_* and calls libffi's underneath.
		echo "_ctypes $CTSRCS -I$FFIPREFIX/include -L$FFIPREFIX/lib -lffi -DHAVE_FFI_PREP_CIF_VAR -DHAVE_FFI_PREP_CLOSURE_LOC -DHAVE_FFI_CLOSURE_ALLOC -DHAVE_ALLOCA_H -DUSING_MALLOC_CLOSURE_DOT_C" >> Modules/Setup.local
	else
		echo "SKIP _ctypes: libffi.a not available (build-libffi.sh failed)"
	fi
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

#!/usr/bin/env bash
# Build jq 1.7.1 (JSON processor) for Phoenix-RTOS / aarch64.
#
# jq is MIT-licensed (permissive; no GPL concern for Phoenix repos).
#
# Approach (SQLite-style direct compile, no autoconf on the target):
#   - The jq 1.7.1 RELEASE tarball ships the pre-generated parser.c / lexer.c
#     (so no bison/flex needed) and a bundled decNumber.
#   - jq has NO config.h; configure normally emits the HAVE_* feature macros as
#     -D flags. We bake a curated, Phoenix-valid set below (derived from a native
#     ./configure, minus the libm functions libphoenix lacks -- see NOTES).
#   - builtin.inc / config_opts.inc / version.h are BUILT_SOURCES; we generate
#     them here (builtin.inc is just a sed transform of builtin.jq).
#   - Regex builtins (test/match/sub/gsub/splits) need oniguruma and are compiled
#     out (gated on HAVE_LIBONIG, left undefined) -- core jq is unaffected.
#
# NOTES / dropped features (all obscure jq math builtins; core jq is complete):
#   libphoenix lacks these libm funcs, so their jq builtins are omitted:
#   drem exp10 gamma scalb significand lgamma_r lgamma tgamma remainder
#   nexttoward nextafter logb log1p expm1 frexp ldexp acosh asinh atanh cbrt ilogb
#   j0 j1 jn y0 y1 yn. KEPT: sqrt floor ceil round trunc fabs exp exp2 log log2
#   log10 pow sin cos tan asin acos atan atan2 sinh cosh tanh hypot fmin fmax
#   fma fmod copysign erf erfc rint nearbyint scalbn modf fdim (+ all non-math jq).
set -euo pipefail

VER=1.7.1
TARBALL=jq-${VER}.tar.gz
URL=https://github.com/jqlang/jq/releases/download/jq-${VER}/${TARBALL}
SHA256=478c9ca129fd2e3443fe27314b455e211e0d8c60bc8ff7df703873deeee580c2

REPO=/home/houp/phoenix-rpi
GCC=${GCC:-$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/jq-port-build}

mkdir -p "$WORK"; cd "$WORK"
if [ ! -f "$TARBALL" ]; then
	echo "downloading $URL"
	curl -fsSL -o "$TARBALL" "$URL"
fi
echo "$SHA256  $TARBALL" | sha256sum -c -
rm -rf "jq-${VER}"; tar xzf "$TARBALL"
cd "jq-${VER}"

# --- generate BUILT_SOURCES (no configure) ---
sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$/\\n"/' src/builtin.jq > src/builtin.inc
printf '#define JQ_CONFIG "phoenix-direct (no oniguruma)"\n' > src/config_opts.inc
printf '#define JQ_VERSION "%s"\n' "$VER" > src/version.h

# --- curated Phoenix HAVE_* macro set (libm funcs libphoenix provides) ---
DEFS='-DPACKAGE_NAME=\"jq\" -DPACKAGE_VERSION=\"1.7.1\" -DPACKAGE_STRING=\"jq\ 1.7.1\" -DPACKAGE=\"jq\" -DVERSION=\"1.7.1\" '
DEFS+='-D_GNU_SOURCE=1 -DHAVE_MEMMEM=1 -DUSE_DECNUM=1 -DHAVE_ALLOCA_H=1 -DHAVE_ALLOCA=1 -DHAVE_ISATTY=1 '
DEFS+='-DHAVE_STRPTIME=1 -DHAVE_STRFTIME=1 -DHAVE_SETENV=1 -DHAVE_TIMEGM=1 -DHAVE_GMTIME_R=1 -DHAVE_GMTIME=1 '
DEFS+='-DHAVE_LOCALTIME_R=1 -DHAVE_LOCALTIME=1 -DHAVE_GETTIMEOFDAY=1 -DHAVE_TM_TM_GMT_OFF=1 -DHAVE_SETLOCALE=1 -DHAVE_ATEXIT=1 '
DEFS+='-DHAVE_ACOS=1 -DHAVE_ASIN=1 -DHAVE_ATAN2=1 -DHAVE_ATAN=1 -DHAVE_CEIL=1 -DHAVE_COPYSIGN=1 -DHAVE_COS=1 -DHAVE_COSH=1 '
DEFS+='-DHAVE_ERF=1 -DHAVE_ERFC=1 -DHAVE_EXP2=1 -DHAVE_EXP=1 -DHAVE_FABS=1 -DHAVE_FDIM=1 -DHAVE_FLOOR=1 -DHAVE_FMA=1 '
DEFS+='-DHAVE_FMAX=1 -DHAVE_FMIN=1 -DHAVE_FMOD=1 -DHAVE_HYPOT=1 -DHAVE_LOG10=1 -DHAVE_LOG2=1 -DHAVE_LOG=1 -DHAVE_MODF=1 '
DEFS+='-DHAVE_NEARBYINT=1 -DHAVE_POW=1 -DHAVE_RINT=1 -DHAVE_ROUND=1 -DHAVE_SCALBN=1 -DHAVE_SIN=1 -DHAVE_SINH=1 -DHAVE_SQRT=1 '
DEFS+='-DHAVE_TAN=1 -DHAVE_TANH=1 -DHAVE_TRUNC=1 -DIEEE_8087=1'

SRCS="src/builtin.c src/bytecode.c src/compile.c src/execute.c src/jv.c src/jv_alloc.c src/jv_aux.c \
src/jv_dtoa.c src/jv_dtoa_tsd.c src/jv_file.c src/jv_parse.c src/jv_print.c src/jv_unicode.c src/lexer.c \
src/linker.c src/locfile.c src/main.c src/parser.c src/util.c src/jq_test.c \
src/decNumber/decNumber.c src/decNumber/decContext.c"

echo "cross-compiling jq for aarch64-phoenix ..."
# -Wno-incompatible-pointer-types: jq 1.7.1's cfunction dispatch table stores
# different-arity fn pointers in one slot (arity checked at runtime); GCC 14 makes
# the resulting cast an error by default. Benign for jq.
eval "$GCC" -O2 -static -Wno-incompatible-pointer-types -Isrc -I. "$DEFS" $SRCS -o jq -lm

"$GCC" --version | head -1
ls -l jq
cp jq "$HERE/jq"
echo "OK -> $HERE/jq  (stage into the netboot NFS root or an SD image as /bin/jq)"

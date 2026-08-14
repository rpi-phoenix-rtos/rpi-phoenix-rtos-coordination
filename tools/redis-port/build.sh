#!/usr/bin/env bash
# Build Redis 7.2.4 (redis-server + redis-cli) for Phoenix-RTOS / aarch64.
#
# Redis 7.2.x is BSD-3-Clause (the 7.4 SSPL/RSALv2 relicense is later) -> safe
# for the Phoenix repos.
#
# Approach: MALLOC=libc (skip jemalloc, which is hard to cross-compile; this
# relies on the standard malloc(0)!=NULL behavior -- see the libphoenix malloc(0)
# fix). Redis's event loop falls back to ae_select on non-Linux/BSD (Phoenix).
# Two small adaptations:
#   1. phoenix-compat.h (-include): errno constants (ESOCKTNOSUPPORT/ECANCELED),
#      pthread_setcanceltype no-op, setitimer/itimerval + dladdr/Dl_info + SI_USER
#      stubs (all only feed Redis's crash-report/watchdog diagnostics, not core).
#   2. src/Makefile link flags: Redis keys off the BUILD host's `uname` (Linux),
#      so it adds -rdynamic -ldl -pthread -lrt -- none valid/needed on Phoenix
#      (pthread/dl/rt/clock live in libphoenix; the toolchain has no -rdynamic).
# Needs libphoenix's floorl/ceill/llroundl (128-bit long double), added alongside.
#
# RUNTIME: start foreground, no persistence (redis.conf: `daemonize no`,
# `save ""`, `appendonly no`) so the fork()-based BGSAVE/AOF paths are never hit.
set -euo pipefail

VER=7.2.4
TARBALL=redis-${VER}.tar.gz
URL=https://download.redis.io/releases/${TARBALL}
SHA256=8d104c26a154b29fd67d6568b4f375212212ad41e0c2caa3d66480e78dbd3b59

REPO=/home/houp/phoenix-rpi
TC=$REPO/.toolchain/aarch64-phoenix/bin
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/redis-port-build}

mkdir -p "$WORK"; cd "$WORK"
[ -f "$TARBALL" ] || curl -fsSL -o "$TARBALL" "$URL"
echo "$SHA256  $TARBALL" | sha256sum -c -
rm -rf "redis-${VER}"; tar xzf "$TARBALL"
cd "redis-${VER}"

# --- patch the link flags for Phoenix (drop Linux-host -rdynamic/-ldl/-pthread/-lrt) ---
perl -0pi -e 's/\tFINAL_LDFLAGS\+= -rdynamic\n\tFINAL_LIBS\+=-ldl -pthread -lrt/\t# Phoenix-RTOS: pthread\/dl\/rt in libphoenix; no -rdynamic\n\tFINAL_LIBS+=/' src/Makefile

CF="-DBYTE_ORDER=1234 -DLITTLE_ENDIAN=1234 -DBIG_ENDIAN=4321 -DAF_LOCAL=AF_UNIX -include $HERE/phoenix-compat.h"

export PATH="$TC:$PATH"
make -C . \
	CC=aarch64-phoenix-gcc AR=aarch64-phoenix-ar RANLIB=aarch64-phoenix-ranlib \
	MALLOC=libc BUILD_TLS=no USE_SYSTEMD=no \
	OPTIMIZATION=-O2 LDFLAGS=-static \
	REDIS_CFLAGS="$CF" -j4

aarch64-phoenix-gcc --version | head -1
ls -l src/redis-server src/redis-cli
cp src/redis-server src/redis-cli "$HERE/"
echo "OK -> $HERE/redis-server , $HERE/redis-cli"
echo "Run on the Pi: /bin/redis-server /redis-min.conf   (daemonize no, save \"\", bind 0.0.0.0)"

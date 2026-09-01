#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Build the SQLite multi-process lock-test harnesses (Phase 0 + Phase 1) and a
# FRESH sqlite3 shell (linked against the current libphoenix, so it carries the
# fixed 64-bit fcntl() wrapper — the staged one predates the fix). Stages all
# three into the netboot NFS export.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CC="$ROOT/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
BR="$ROOT/.buildroot/_build/aarch64a72-generic-rpi4b"
SQLSRC="$BR/port-sources/sqlite3-3.53.4/sqlite-autoconf-3530400"
NFS="${NFS_ROOT:-/srv/phoenix-rpi4-nfs-gcc16}"

SQLITE_FEATURES="-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1 -DSQLITE_ENABLE_RTREE -DHAVE_READLINE=0"

echo "build.sh: CC=$CC"
echo "build.sh: sqlite src=$SQLSRC"
[ -f "$SQLSRC/sqlite3.c" ] || { echo "ERROR: sqlite3.c not found — run a --with-ports build first"; exit 1; }

mkdir -p "$NFS/bin" "$NFS/usr/bin"

echo "build.sh: Phase 0 (cross-open fcntl proof)"
"$CC" -O2 -static -Wall -o "$NFS/bin/sqlite-crossopen" "$HERE/phase0.c"

echo "build.sh: Phase 1 (SQLite lock hammer, links libsqlite3.a)"
"$CC" -O2 -static -Wall -I"$BR/include" -o "$NFS/bin/sqlite-lockhammer" \
	"$HERE/lockhammer.c" "$BR/lib/libsqlite3.a" -lm

echo "build.sh: fresh sqlite3 shell (new libphoenix wrapper) -> /usr/bin"
"$CC" -O2 -static -Wall $SQLITE_FEATURES -o "$NFS/usr/bin/sqlite3" \
	"$SQLSRC/shell.c" "$SQLSRC/sqlite3.c" -lm

echo "build.sh: done"
ls -la "$NFS/bin/sqlite-crossopen" "$NFS/bin/sqlite-lockhammer" "$NFS/usr/bin/sqlite3"

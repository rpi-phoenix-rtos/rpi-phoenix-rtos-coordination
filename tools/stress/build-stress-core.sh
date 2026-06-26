#!/usr/bin/env bash
# Cross-compile the CORE stress / micro-benchmark util (task #38) as a static
# aarch64-phoenix ELF and stage it onto the NFS rootfs export so the Pi can run
# /nfstest/bin/stress-core from psh. Host-side only — does NOT boot the Pi.
#
# Idempotent: recompiles, runs `file`, gates on `nm -u` (no undefined symbols in
# a static binary), and stages. Mirrors tools/x11-port/apps/build.sh.
set -euo pipefail

SR=${SYSROOT:-/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}
TC=${TOOLCHAIN:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}
NM=${NM:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm}
NFS=${NFS_EXPORT:-/srv/phoenix-rpi4-nfs}
HERE=$(cd "$(dirname "$0")" && pwd)

SRC="$HERE/stress-core.c"
OUT="$HERE/stress-core"

echo "== compiling stress-core =="
"$TC" --sysroot="$SR" -Wall -Wextra -O2 -static "$SRC" -o "$OUT"

echo "== file =="
file "$OUT"

echo "== nm -u (undefined symbols — must be empty for a static binary) =="
UNDEF=$("$NM" -u "$OUT" 2>/dev/null || true)
if [ -n "$UNDEF" ]; then
	echo "FAIL: unresolved symbols present:"
	echo "$UNDEF"
	exit 1
fi
echo "nm -u: clean (0 undefined symbols)"

echo "== stage =="
if [ -d "$NFS/bin" ]; then
	cp "$OUT" "$NFS/bin/stress-core"
	echo "staged -> $NFS/bin/stress-core"
else
	echo "WARN: $NFS/bin not present — skipping stage (binary at $OUT)"
fi

echo "== done =="

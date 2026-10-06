#!/usr/bin/env bash
# Host regression test for libnfs sync-call deadline handling (no Pi, ~5 s).
#
# Builds libnfs 6.0.2 for the HOST from the same tag the port uses, with the
# port's patches applied, and runs sync-deadline-stale-pdu.c against it. See that
# file for the bug it pins down (the build-39 "nfs" Data Abort after a reclaim).
#
#   tools/libnfs-hosttest/run.sh                 # patches from sources/phoenix-rtos-ports
#   PORTS=/path/to/ports-worktree tools/libnfs-hosttest/run.sh
#   SAN="-fsanitize=address,undefined" tools/libnfs-hosttest/run.sh
#     (UBSan then reports one "null pointer passed as argument 2" at
#     lib/libnfs-zdr.c:206: upstream memcpy()s an empty opaque from NULL. Benign.)
#
# Against the unfixed patch 04 it prints the Pi's exact corruption,
# "canary word ... overwritten: 0xfffffffc00000001", and FAILs.
#
# The libnfs tree is exported from the port build's git clone (the port fetches
# the libnfs-6.0.2 tag with git), so no network is needed once the port has been
# built once.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
ports="${PORTS:-$root/sources/phoenix-rtos-ports}"
san="${SAN:-}"
clone="${LIBNFS_CLONE:-$root/.buildroot/_build/aarch64a72-generic-rpi4b/port-sources/libnfs-6.0.2/libnfs-libnfs-6.0.2}"
work="$(mktemp -d "${TMPDIR:-/tmp}/libnfs-hosttest.XXXXXX")"
trap 'rm -rf "$work"' EXIT

if [ ! -d "$clone/.git" ]; then
	echo "LIBNFS-STALE-PDU: FAIL (no libnfs clone at $clone; build the libnfs port once, or set LIBNFS_CLONE)"
	exit 2
fi

mkdir -p "$work/src"
git -C "$clone" archive --format=tar libnfs-6.0.2 | tar -x -C "$work/src"
for p in "$ports"/libnfs/patches/*.patch; do
	patch -s -d "$work/src" -p1 --no-backup-if-mismatch < "$p"
done

make -s -j"$(nproc)" -f "$ports/libnfs/files/Makefile.phoenix" \
	CROSS= CC=gcc AR=ar CFLAGS="-O1 -g -fno-omit-frame-pointer -w $san" \
	LIBNFS_SRC="$work/src" CONFIG_H_DIR="$ports/libnfs/files" OUT="$work/o"

s="$work/src"
gcc -O1 -g -fno-omit-frame-pointer $san -Wall -Wextra -Wno-unused-parameter '-D_U_=__attribute__((unused))' \
	-I"$ports/libnfs/files" -I"$s/include" -I"$s/include/nfsc" -I"$s/nfs" -I"$s/nfs4" -I"$s/mount" -I"$s/portmap" \
	"$here/sync-deadline-stale-pdu.c" "$work/o/libnfs.a" -lpthread -o "$work/stale-pdu"
# The harness never frees what a killed sync call leaked inside libnfs.
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" "$work/stale-pdu"

#!/usr/bin/env bash
#
# Build the PROCESS / FILE-I/O / IPC stress utils for the Phoenix-RTOS Pi 4
# (task #39) as static aarch64-phoenix ELFs and stage them on the NFS export.
#
# Each util is a single self-contained C file that includes the shared contract
# header tools/stress/stress.h. Built static (no runtime .so deps), libc only
# (plus libphoenix for the msg API in stress-ipc — pulled in by the toolchain's
# default specs). After each build we print `file` + `nm -u` (undefined symbols)
# so a missing msg-API / libphoenix symbol is caught at build time, then stage to
# /srv/phoenix-rpi4-nfs/bin (mounted at /nfstest on the netbooted Pi).
#
# Host-side only — this does NOT boot the Pi.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt

set -euo pipefail

TC="${TC:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}"
SR="${SR:-/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}"
NM="${NM:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm}"
STAGE="${STAGE:-/srv/phoenix-rpi4-nfs/bin}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

UTILS="stress-proc stress-fs stress-ipc"

CFLAGS="-Wall -Wextra -O2 -static -I${HERE}"

if [ ! -x "$TC" ]; then
	echo "error: toolchain gcc not found/executable: $TC" >&2
	exit 1
fi
if [ ! -d "$SR" ]; then
	echo "error: sysroot not found: $SR" >&2
	exit 1
fi

mkdir -p "$STAGE"

for u in $UTILS; do
	src="${HERE}/${u}.c"
	out="${HERE}/${u}"
	if [ ! -f "$src" ]; then
		echo "skip: $src not present yet" >&2
		continue
	fi

	echo "=== building $u ==="
	# shellcheck disable=SC2086
	"$TC" --sysroot="$SR" $CFLAGS "$src" -o "$out"

	echo "--- file $u ---"
	file "$out" || true

	echo "--- nm -u $u (undefined symbols; should be empty for a static link) ---"
	"$NM" -u "$out" || true

	echo "--- stage -> ${STAGE}/${u} ---"
	cp -f "$out" "${STAGE}/${u}"
	echo
done

echo "all built + staged to ${STAGE}"

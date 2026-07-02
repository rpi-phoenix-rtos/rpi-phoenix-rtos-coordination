#!/usr/bin/env bash
# Cross-compile the 2048 demo game for aarch64-phoenix and stage it to the NFS
# rootfs export. Host-side, no Pi boot. See ../../docs/todo/userspace-demo-apps.md
# (Tier D). Final interactive play-test is on HW.
set -euo pipefail

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../../.." && pwd)"

SR=${SYSROOT:-${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}
TC=${TOOLCHAIN_BIN:-${ROOT}/.toolchain/aarch64-phoenix/bin}
NFS=${NFS_EXPORT:-/srv/phoenix-rpi4-nfs}
HERE=$(cd "$(dirname "$0")" && pwd)

cd "$HERE"
"$TC/aarch64-phoenix-gcc" --sysroot="$SR" -Wall -Wextra -O2 -static 2048.c -o 2048
file 2048

if [ -d "$NFS/bin" ]; then
	cp 2048 "$NFS/bin/2048"
	echo "staged -> $NFS/bin/2048"
else
	echo "NFS export $NFS/bin not present; built 2048 locally only"
fi

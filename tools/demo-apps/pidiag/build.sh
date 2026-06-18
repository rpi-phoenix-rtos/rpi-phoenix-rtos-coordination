#!/usr/bin/env bash
# Cross-compile the pidiag self-test tool for aarch64-phoenix and stage it to the
# NFS rootfs export. Host-side, no Pi boot. Runs from psh on HW with no X/network.
set -euo pipefail

SR=${SYSROOT:-/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}
TC=${TOOLCHAIN_BIN:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin}
NFS=${NFS_EXPORT:-/srv/phoenix-rpi4-nfs}
HERE=$(cd "$(dirname "$0")" && pwd)

cd "$HERE"
"$TC/aarch64-phoenix-gcc" --sysroot="$SR" -Wall -Wextra -O2 -static pidiag.c -o pidiag
file pidiag

if [ -d "$NFS/bin" ]; then
	cp pidiag "$NFS/bin/pidiag"
	echo "staged -> $NFS/bin/pidiag"
else
	echo "NFS export $NFS/bin not present; built pidiag locally only"
fi

#!/usr/bin/env bash
# Cross-compile the Pi-side TCP echo server (tools/stress/stress-net.c) for
# aarch64-phoenix and stage it to the NFS rootfs export. Host-side, no Pi boot.
#
# The server is the controlled Pi-as-server target for the host echo load
# generator (tools/stress/net/echo-load.py). See stress-net.c for the contract.
set -euo pipefail

SR=${SYSROOT:-/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}
TC=${TOOLCHAIN_BIN:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin}
NFS=${NFS_EXPORT:-/srv/phoenix-rpi4-nfs}
HERE=$(cd "$(dirname "$0")" && pwd)
STRESS_DIR=$(cd "$HERE/.." && pwd)

# Build into the net/ dir (gitignored) so the binary doesn't litter the shared
# tools/stress/ tree next to other agents' sources. stress.h + stress-net.c
# live in tools/stress/; -I"$STRESS_DIR" picks up the header.
OUT="$HERE/stress-net"
"$TC/aarch64-phoenix-gcc" --sysroot="$SR" -I"$STRESS_DIR" -Wall -Wextra -O2 -static \
	"$STRESS_DIR/stress-net.c" -o "$OUT"
file "$OUT"

if [ -d "$NFS/bin" ]; then
	cp "$OUT" "$NFS/bin/stress-net"
	echo "staged -> $NFS/bin/stress-net"
else
	echo "NFS export $NFS/bin not present; built stress-net locally only"
fi

#!/bin/bash
# Build one single-file diagnostic tool from tools/<name>/<name>.c against the
# CURRENT buildroot sysroot, and stage it into the live NFS export's /bin.
#
# These tools (fileperf, udprtt, ...) are throwaway instruments, not products:
# they have no port recipe on purpose, so nothing about the shipped image
# depends on them. What they DO need is to be relinked against the libphoenix
# that the running kernel was built with -- a tool left over from before a core
# rebuild measures the old libc and quietly reports nonsense.
#
# Usage: scripts/build-diag-tool.sh <name> [<name>...]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET=aarch64a72-generic-rpi4b
BUILD="$REPO/.buildroot/_build/$TARGET"
SYSROOT="$BUILD/sysroot"
CC="$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
# Resolve the LIVE export the same way make-pristine-nfs-export.sh does: the
# fsid=0 entry, scanning exports.d too. Hardcoding a path is how tools ended up
# staged into a dead directory while the Pi mounted a different one.
fsid0_export="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports /etc/exports.d/*.exports 2>/dev/null || true)"
EXPORT_ROOT="${RPI4B_NFS_EXPORT:-${fsid0_export:-}}"
[ -n "$EXPORT_ROOT" ] || { echo "no fsid=0 export found and RPI4B_NFS_EXPORT unset" >&2; exit 1; }

[ -x "$CC" ] || { echo "no toolchain at $CC" >&2; exit 1; }
[ -d "$SYSROOT" ] || { echo "no sysroot at $SYSROOT (build the project first)" >&2; exit 1; }
[ $# -ge 1 ] || { echo "usage: $0 <name> [<name>...]" >&2; exit 1; }

for name in "$@"; do
	src="$REPO/tools/$name/$name.c"
	out="$REPO/tools/$name/$name"
	[ -f "$src" ] || { echo "no such tool: $src" >&2; exit 1; }

	echo "==> $name"
	"$CC" -mcpu=cortex-a72 -mtune=cortex-a72 -fomit-frame-pointer -mstrict-align \
		-mno-outline-atomics --sysroot="$SYSROOT/" -B"$SYSROOT/lib/" \
		-iprefix "$SYSROOT/" -I"$BUILD/include/" \
		-Wl,-z,max-page-size=0x1000 -Wl,--no-warn-rwx-segments \
		-L"$BUILD/lib/" -std=gnu17 -O2 -Wall -Wextra -g \
		"$src" -o "$out"

	if [ -d "$EXPORT_ROOT/bin" ]; then
		sudo cp -f "$out" "$EXPORT_ROOT/bin/$name"
		echo "    staged -> $EXPORT_ROOT/bin/$name"
	else
		echo "    NOT staged: $EXPORT_ROOT/bin missing" >&2
	fi
done

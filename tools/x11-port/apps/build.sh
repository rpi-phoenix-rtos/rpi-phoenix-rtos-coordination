#!/usr/bin/env bash
# Cross-compile the self-contained native Xlib demo clients for aarch64-phoenix
# and stage them to the NFS rootfs export. Host-side, no Pi boot. Links against
# the X11 client libraries built by ../build-x11-phoenix.sh into $X11_PREFIX.
# See ../PROGRESS.md. Final visual run is on HW once the fbdev DDX server lands.
set -euo pipefail

SR=${SYSROOT:-/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot}
TC=${TOOLCHAIN_BIN:-/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin}
P=${X11_PREFIX:-/tmp/x11-phoenix}
NFS=${NFS_EXPORT:-/srv/phoenix-rpi4-nfs}
ART=${ARTIFACTS:-/home/houp/phoenix-rpi/artifacts/x11}
HERE=$(cd "$(dirname "$0")" && pwd)

cd "$HERE"
for app in xphxdemo; do
	"$TC/aarch64-phoenix-gcc" --sysroot="$SR" -Wall -Wextra -O2 -static \
		-I"$P/include" "$app.c" -o "$app" \
		-L"$P/lib" -lX11 -lxcb -lXau -lXdmcp
	file "$app"
	mkdir -p "$ART"
	cp "$app" "$ART/$app"
	if [ -d "$NFS/bin" ]; then
		cp "$app" "$NFS/bin/$app"
		echo "staged -> $NFS/bin/$app"
	fi
done

#!/usr/bin/env bash
#
# build-csd-matmul-daemon.sh - cross-build the v3d-server (rpi4-v3d), the client
# library (libv3d-client), and a DAEMON variant of the CSD matmul microbench
# (csd-matmul-daemon = the UNCHANGED tools/v3d-driver-port/csd_matmul.c linked
# against libv3d-client instead of the in-process winsys).
#
# This is the M1 step-2b build-verify: it proves the whole CSD path cross-compiles
# and links with 0 undefined symbols, and produces the three binaries to stage on
# the netboot NFS root for a Pi cycle.
#
# Usage: build-csd-matmul-daemon.sh [OUTPUT_DIR]   (default: ./.build-csd-daemon)
#
# Copyright 2026 Phoenix Systems  %LICENSE%
set -euo pipefail

TOP=/home/houp/phoenix-rpi
TC=$TOP/.toolchain/aarch64-phoenix/bin
GCC=$TC/aarch64-phoenix-gcc
AR=$TC/aarch64-phoenix-ar
NM=$TC/aarch64-phoenix-nm

DEV=$TOP/sources/phoenix-rtos-devices/gpu/rpi4-v3d   # server + client + vendored uapi
TOOLS=$TOP/tools/v3d-driver-port                     # csd_matmul.c (UNCHANGED) + its headers
# shim-include (sys/ioccom.h for the DRM UAPI) moved into the devices repo with the
# rest of the V3D/Mesa port glue; csd_matmul.c still needs it on the include path.
SHIM=$TOP/sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/shim-include
OUT=${1:-$TOOLS/.build-csd-daemon}

# The device tree builds with these flags (Makefile.common): -Wall -Werror etc.
# Build the server/client sources with the exact same gate.
DEVWF="-Wall -Wstrict-prototypes -Wundef -Wimplicit-fallthrough -Werror -fno-common"
DEVINC="-I$DEV -I$DEV/uapi"

mkdir -p "$OUT"
echo "== output dir: $OUT =="

echo "== compile GPU core + server + client (device tree, -Werror) =="
set -x
$GCC $DEVWF -O2 $DEVINC -c "$DEV/v3d_gpu.c"       -o "$OUT/v3d_gpu.o"
$GCC $DEVWF -O2 $DEVINC -c "$DEV/rpi4-v3d.c"      -o "$OUT/rpi4-v3d.o"
$GCC $DEVWF -O2 $DEVINC -c "$DEV/libv3d-client.c" -o "$OUT/libv3d-client.o"
$AR rcs "$OUT/libv3d-client.a" "$OUT/libv3d-client.o"
set +x

echo "== link server binary rpi4-v3d (-static) =="
set -x
$GCC -static "$OUT/rpi4-v3d.o" "$OUT/v3d_gpu.o" -o "$OUT/rpi4-v3d"
set +x

echo "== compile UNCHANGED csd_matmul.c (its own headers; no -Werror, matches today) =="
set -x
$GCC -O2 -Wall -I"$TOOLS" -I"$SHIM" -c "$TOOLS/csd_matmul.c" -o "$OUT/csd_matmul.o"
set +x

echo "== link csd-matmul-daemon (csd_matmul.o + libv3d-client.a + libphoenix + libm, -static) =="
set -x
$GCC -static "$OUT/csd_matmul.o" "$OUT/libv3d-client.a" -lm -o "$OUT/csd-matmul-daemon"
set +x

echo
echo "== undefined-symbol proof (must all be 0) =="
for bin in rpi4-v3d csd-matmul-daemon; do
	n=$("$NM" -u "$OUT/$bin" | wc -l)
	echo "[$bin] undefined symbols: $n"
	[ "$n" -eq 0 ] || { echo "FAIL: $bin has undefined symbols"; "$NM" -u "$OUT/$bin"; exit 1; }
done
echo "[libv3d-client.o] external undefined (must all be libphoenix externs):"
"$NM" -u "$OUT/libv3d-client.o"

echo
echo "BUILD OK. Binaries in $OUT :"
ls -la "$OUT/rpi4-v3d" "$OUT/csd-matmul-daemon"

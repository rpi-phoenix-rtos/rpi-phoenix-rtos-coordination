#!/usr/bin/env bash
# build-mlp-gpu-daemon.sh - cross-build mlp-gpu-daemon (GPU MNIST MLP inference,
# tools/v3d-driver-port/mlp_gpu.c) linked against libv3d-client (RPC to rpi4-v3d).
# Reuses the v3d-server + client built by build-csd-matmul-daemon.sh.
set -euo pipefail
TOP=/home/houp/phoenix-rpi
TC=$TOP/.toolchain/aarch64-phoenix/bin
GCC=$TC/aarch64-phoenix-gcc
AR=$TC/aarch64-phoenix-ar
DEV=$TOP/sources/phoenix-rtos-devices/gpu/rpi4-v3d
TOOLS=$TOP/tools/v3d-driver-port
MLPH=$TOP/tools/cnn-mnist          # mlp_data.h
OUT=${1:-$TOOLS/.build-csd-daemon}
mkdir -p "$OUT"

DEVWF="-Wall -Wstrict-prototypes -Wundef -Wimplicit-fallthrough -Werror -fno-common"
DEVINC="-I$DEV -I$DEV/uapi"
echo "== (re)build server + client =="
$GCC $DEVWF -O2 $DEVINC -c "$DEV/v3d_gpu.c"       -o "$OUT/v3d_gpu.o"
$GCC $DEVWF -O2 $DEVINC -c "$DEV/rpi4-v3d.c"      -o "$OUT/rpi4-v3d.o"
$GCC $DEVWF -O2 $DEVINC -c "$DEV/libv3d-client.c" -o "$OUT/libv3d-client.o"
$AR rcs "$OUT/libv3d-client.a" "$OUT/libv3d-client.o"
$GCC -static "$OUT/rpi4-v3d.o" "$OUT/v3d_gpu.o" -o "$OUT/rpi4-v3d"

echo "== compile mlp_gpu.c (+ mlp_data.h) =="
$GCC -O2 -Wall -I"$TOOLS" -I"$TOOLS/shim-include" -I"$MLPH" -c "$TOOLS/mlp_gpu.c" -o "$OUT/mlp_gpu.o"
echo "== link mlp-gpu-daemon =="
$GCC -static "$OUT/mlp_gpu.o" "$OUT/libv3d-client.a" -lm -o "$OUT/mlp-gpu-daemon"
"$TC/aarch64-phoenix-nm" -u "$OUT/mlp-gpu-daemon" | grep -c ' U ' | sed 's/^/mlp-gpu-daemon undefined syms: /'
echo "OK: $OUT/mlp-gpu-daemon"

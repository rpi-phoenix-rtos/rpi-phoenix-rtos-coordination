#!/usr/bin/env bash
#
# Build sand-wl (YUV design §5 step 2a probe: an NV12 SAND128 dma-buf as a Wayland buffer): a static
# aarch64-phoenix Wayland client linked against libwayland-client (wayland_phoenix) and libdrm_phoenix.
#
#   OUT=<dir> tools/gpu-lane/sand-wl/build.sh      (default: tools/gpu-lane/sand-wl/build-out)
#
# Nothing is written outside OUT; the ports must have been built. Run on the Pi inside the desktop
# session (labwc), e.g. from a session autostart: copy build-out/sand-wl.stripped to the export.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../../.." && pwd)"
MAIN="${PHX_MAIN:-${ROOT}}"
[ -d "${MAIN}/.buildroot" ] || MAIN="/home/houp/phoenix-rpi"
B="${MAIN}/.buildroot/_build/aarch64a72-generic-rpi4b"
TC="${MAIN}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-"
SYSROOT="${B}/sysroot"
LDP="${B}/versioned-ports/libdrm_phoenix-2.4.134"
WL="${B}/versioned-ports/wayland_phoenix-1.24.0"
PROTO="${WL}/prefix/share/wayland-protocols/stable"
OUT="${OUT:-${HERE}/build-out}"

for f in "${TC}gcc" "${SYSROOT}/lib/libphoenix.a" "${LDP}/lib/libdrm.a" "${WL}/prefix/lib/libwayland-client.a" \
	"${WL}/libwayland/deps/libffi/lib/libffi.a" "${PROTO}/xdg-shell/xdg-shell.xml"; do
	[ -e "${f}" ] || { echo "sand-wl/build: missing ${f}" >&2; exit 1; }
done
command -v wayland-scanner >/dev/null || { echo "sand-wl/build: host wayland-scanner missing" >&2; exit 1; }

TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${SYSROOT}/" -B"${SYSROOT}/lib/")
mkdir -p "${OUT}"

for p in xdg-shell:xdg-shell/xdg-shell.xml linux-dmabuf-v1:linux-dmabuf/linux-dmabuf-v1.xml; do
	n="${p%%:*}"
	wayland-scanner client-header "${PROTO}/${p#*:}" "${OUT}/${n}-client-protocol.h"
	wayland-scanner private-code "${PROTO}/${p#*:}" "${OUT}/${n}-protocol.c"
done

CF=(-O2 -g -std=gnu11 -Wall -Wextra "${TFLAGS[@]}" -I"${OUT}" -I"${WL}/prefix/include" -I"${LDP}/include" -I"${LDP}/include/libdrm")
"${TC}gcc" "${CF[@]}" -Werror -c "${HERE}/sand-wl.c" -o "${OUT}/sand-wl.o"
"${TC}gcc" "${CF[@]}" -c "${OUT}/xdg-shell-protocol.c" -o "${OUT}/xdg-shell-protocol.o"
"${TC}gcc" "${CF[@]}" -c "${OUT}/linux-dmabuf-v1-protocol.c" -o "${OUT}/linux-dmabuf-v1-protocol.o"
"${TC}gcc" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 \
	-Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=fcntl -Wl,--wrap=dup -Wl,--wrap=dup2 \
	-Wl,-u,__wrap_close -Wl,-u,__wrap_write -Wl,--wrap=close -Wl,--wrap=write \
	-o "${OUT}/sand-wl" "${OUT}/sand-wl.o" "${OUT}/xdg-shell-protocol.o" "${OUT}/linux-dmabuf-v1-protocol.o" \
	"${WL}/prefix/lib/libwayland-client.a" "${WL}/libwayland/deps/libffi/lib/libffi.a" \
	"${WL}/prefix/lib/libwlphx-compat.a" "${LDP}/lib/libdrm.a" -lm -lpthread
"${TC}strip" -o "${OUT}/sand-wl.stripped" "${OUT}/sand-wl"

u="$("${TC}nm" -u "${OUT}/sand-wl" || true)"
[ -z "${u}" ] || { echo "${u}" | head; echo "sand-wl/build: undefined symbols" >&2; exit 1; }
echo "sand-wl: ${OUT}/sand-wl.stripped ($(stat -c %s "${OUT}/sand-wl.stripped") bytes)"

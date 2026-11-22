#!/usr/bin/env bash
#
# Build kms-prop-race (KNOWN-ISSUES C21 reproducer): a static
# aarch64-phoenix program linked against libdrm_phoenix only.
#
#   OUT=<dir> tools/gpu-lane/kms-prop-race/build.sh      (default: tools/gpu-lane/kms-prop-race/build-out)
#
# Nothing is written outside OUT; the ports must have been built. Run on the Pi as root from the
# psh prompt with an overlay exposed (rpi4-kms -o 1): copy build-out/kms-prop-race.stripped to the export.
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
OUT="${OUT:-${HERE}/build-out}"

for f in "${TC}gcc" "${SYSROOT}/lib/libphoenix.a" "${LDP}/lib/libdrm.a"; do
	[ -e "${f}" ] || { echo "kms-prop-race/build: missing ${f}" >&2; exit 1; }
done

TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${SYSROOT}/" -B"${SYSROOT}/lib/")
mkdir -p "${OUT}"

"${TC}gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${LDP}/include" -I"${LDP}/include/libdrm" \
	-c "${HERE}/kms-prop-race.c" -o "${OUT}/kms-prop-race.o"
"${TC}gcc" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 \
	-Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=fcntl -Wl,--wrap=dup -Wl,--wrap=dup2 \
	-o "${OUT}/kms-prop-race" "${OUT}/kms-prop-race.o" "${LDP}/lib/libdrm.a" -lm
"${TC}strip" -o "${OUT}/kms-prop-race.stripped" "${OUT}/kms-prop-race"

u="$("${TC}nm" -u "${OUT}/kms-prop-race" || true)"
[ -z "${u}" ] || { echo "${u}" | head; echo "kms-prop-race/build: undefined symbols" >&2; exit 1; }
echo "kms-prop-race: ${OUT}/kms-prop-race.stripped ($(stat -c %s "${OUT}/kms-prop-race.stripped") bytes)"

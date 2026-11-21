#!/usr/bin/env bash
#
# Build mipcheck (mipmap levels read back through the FBO and the sampler, against a CPU
# reference): a static aarch64-phoenix program linked against the mesa_drm port's gles variant
# (EGL surfaceless + GLES), as tools/gpu-lane/sand-import does, plus libdrm_phoenix.
#
#   OUT=<dir> tools/gpu-lane/mipcheck/build.sh      (default: tools/gpu-lane/mipcheck/build-out)
#
# Nothing is written outside OUT; the ports must have been built (image or build-port.sh).
# Run on the Pi as root from the psh prompt: copy build-out/mipcheck.stripped to the export.
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
M="${B}/versioned-ports/mesa_drm-26.2.0"
LDP="${B}/versioned-ports/libdrm_phoenix-2.4.134"
OUT="${OUT:-${HERE}/build-out}"

for f in "${TC}gcc" "${SYSROOT}/lib/libphoenix.a" "${M}/gles/link-gles.txt" "${LDP}/lib/libdrm.a"; do
	[ -e "${f}" ] || { echo "mipcheck/build: missing ${f}" >&2; exit 1; }
done

TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${SYSROOT}/" -B"${SYSROOT}/lib/")
mkdir -p "${OUT}"

"${TC}gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${M}/gles/prefix/include" \
	-c "${HERE}/mipcheck.c" -o "${OUT}/mipcheck.o"

gallium="" MA=()
while IFS= read -r l; do
	case "${l}" in "--whole-archive "*) gallium="${l#--whole-archive }" ;; *) MA+=("${l}") ;; esac
done < "${M}/gles/link-gles.txt"
[ -f "${gallium}" ] || { echo "mipcheck/build: no libgallium in link-gles.txt" >&2; exit 1; }

"${TC}g++" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,-z,stack-size=8388608 \
	-Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=fcntl -Wl,--wrap=dup -Wl,--wrap=dup2 \
	-o "${OUT}/mipcheck" "${OUT}/mipcheck.o" \
	-Wl,--whole-archive "${gallium}" -Wl,--no-whole-archive \
	-Wl,--start-group "${MA[@]}" -Wl,--end-group -lm
"${TC}strip" -o "${OUT}/mipcheck.stripped" "${OUT}/mipcheck"

u="$("${TC}nm" -u "${OUT}/mipcheck" || true)"
[ -z "${u}" ] || { echo "${u}" | head; echo "mipcheck/build: undefined symbols" >&2; exit 1; }
echo "mipcheck: ${OUT}/mipcheck.stripped ($(stat -c %s "${OUT}/mipcheck.stripped") bytes)"

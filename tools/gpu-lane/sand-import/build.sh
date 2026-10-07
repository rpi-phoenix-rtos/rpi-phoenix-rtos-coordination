#!/usr/bin/env bash
#
# Build sand-import (checkpoint 1 of docs/gpu-new-lane/M10b-video-zero-copy.md): a static
# aarch64-phoenix program linked against the ports' build outputs, read-only:
#   - mesa_drm   gles variant (EGL surfaceless + GLES2, gles/link-gles.txt), as kmscube_drm links it
#   - libdrm_phoenix  libdrm.a (needs --wrap=mmap,ioctl,fcntl,dup,dup2)
#   - video_player    FFmpeg 6.1 with hevc_rpivid (libavformat/libavcodec/libswresample/libavutil)
# The rpivid hwaccel's picture pool is redirected into render-server BOs with
# --wrap=rpivid_geom,rpivid_dma_alloc_cached,rpivid_dma_free (see sand-import.c).
#
#   OUT=<dir> tools/gpu-lane/sand-import/build.sh      (default: tools/gpu-lane/sand-import/build-out)
#
# Nothing is written outside OUT; the ports must have been built (image or build-port.sh).
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../../.." && pwd)"
# a coordination-repo worktree has no .buildroot/.toolchain/sources of its own: use the main checkout's
MAIN="${PHX_MAIN:-${ROOT}}"
[ -d "${MAIN}/.buildroot" ] || MAIN="/home/houp/phoenix-rpi"
B="${MAIN}/.buildroot/_build/aarch64a72-generic-rpi4b"
TC="${MAIN}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-"
SYSROOT="${B}/sysroot"
M="${B}/versioned-ports/mesa_drm-26.2.0"
LDP="${B}/versioned-ports/libdrm_phoenix-2.4.134"
FF="${B}/versioned-ports/video_player-6.1/ffmpeg"
RV="${MAIN}/sources/phoenix-rtos-ports/video_player/files/rpivid/src"
GLUE="${MAIN}/sources/phoenix-rtos-ports/video_player/files/ffplay_phoenix_glue.c"
V3DH="${MAIN}/tools/gpu-lane/libdrm-phoenix/include"
OUT="${OUT:-${HERE}/build-out}"

for f in "${TC}gcc" "${SYSROOT}/lib/libphoenix.a" "${M}/gles/link-gles.txt" "${LDP}/lib/libdrm.a" "${FF}/lib/libavcodec.a" \
		"${RV}/rpivid_sand.h" "${GLUE}" "${V3DH}/v3d_drm.h"; do
	[ -e "${f}" ] || { echo "sand-import/build: missing ${f}" >&2; exit 1; }
done

TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${SYSROOT}/" -B"${SYSROOT}/lib/")
mkdir -p "${OUT}"

"${TC}gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${M}/gles/prefix/include" -I"${LDP}/include" \
	-I"${LDP}/include/libdrm" -I"${V3DH}" -I"${FF}/include" -I"${RV}" -c "${HERE}/sand-import.c" -o "${OUT}/sand-import.o"
"${TC}gcc" -O2 -std=gnu11 -Wall -Wextra "${TFLAGS[@]}" -c "${GLUE}" -o "${OUT}/glue.o"

gallium="" MA=()
while IFS= read -r l; do
	case "${l}" in "--whole-archive "*) gallium="${l#--whole-archive }" ;; *) MA+=("${l}") ;; esac
done < "${M}/gles/link-gles.txt"
[ -f "${gallium}" ] || { echo "sand-import/build: no libgallium in link-gles.txt" >&2; exit 1; }

"${TC}g++" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,-z,stack-size=16777216 \
	-Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,--wrap=fcntl -Wl,--wrap=dup -Wl,--wrap=dup2 -Wl,--wrap=pthread_create \
	-Wl,--wrap=rpivid_geom -Wl,--wrap=rpivid_dma_alloc_cached -Wl,--wrap=rpivid_dma_free \
	-Wl,-Map,"${OUT}/sand-import.map" -o "${OUT}/sand-import" "${OUT}/sand-import.o" "${OUT}/glue.o" \
	-Wl,--whole-archive "${gallium}" -Wl,--no-whole-archive \
	-Wl,--start-group "${FF}/lib/libavformat.a" "${FF}/lib/libavcodec.a" "${FF}/lib/libswresample.a" "${FF}/lib/libavutil.a" \
	"${MA[@]}" -Wl,--end-group -lm
"${TC}strip" -o "${OUT}/sand-import.stripped" "${OUT}/sand-import"

u="$("${TC}nm" -u "${OUT}/sand-import" || true)"
[ -z "${u}" ] || { echo "${u}" | head; echo "sand-import/build: undefined symbols" >&2; exit 1; }
syms="$("${TC}nm" "${OUT}/sand-import")"
for s in __wrap_rpivid_geom __wrap_rpivid_dma_alloc_cached __wrap_rpivid_dma_free __wrap_mmap __wrap_ioctl ff_hevc_rpivid_decoder \
		v3d_blit; do
	grep -qE " [TtWwDdRr] ${s}\$" <<<"${syms}" || { echo "sand-import/build: symbol ${s}: NO" >&2; exit 1; }
done
echo "sand-import: ${OUT}/sand-import.stripped ($(stat -c %s "${OUT}/sand-import.stripped") bytes), unstripped ${OUT}/sand-import"

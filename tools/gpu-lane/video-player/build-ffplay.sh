#!/usr/bin/env bash
#
# M10 (docs/gpu-new-lane/M10-video-player.md): ffplay -- ffmpeg's own SDL2 player -- for
# Phoenix-RTOS on the new GPU lane. Builds the ffmpeg 6.1 port's tarball with a PLAYER feature
# set (the port itself stays decode-only: libavutil/codec/format), plus libavfilter, libswscale
# and libswresample, then compiles fftools/ffplay.c and links it STATIC against an SDL 2.30.12
# build of the new lane:
#
#   --sdl drm  (default) tools/gpu-lane/sdl2-drm/build-out: SDL's KMSDRM video driver on Mesa
#              GBM/EGL (desktop-GL build) + libdrm-phoenix. Full screen only: run from psh, not
#              under a Wayland compositor (it would fight labwc for card0). -> ffplay-drm
#   --sdl wl   M8's SDL with the Wayland video driver (tools/gpu-lane/sdl2-wl, NOT BUILT YET):
#              windowed on the XFCE/labwc desktop. -> ffplay-wl. The link line of that build
#              (Mesa's EGL wayland platform + libwayland-client) is M8's; this script refuses
#              until its build-out exists.
#
# Only LGPL components: no --enable-gpl, no --enable-nonfree (ffplay.c, cmdutils.c and
# opt_common.c are LGPL-2.1-or-later). Audio goes through SDL's Phoenix driver (sdl2-drm
# overlay: /dev/audio0, 44100 Hz S16 stereo, SDL converts); decoding is on the CPU (the
# rpivid HEVC hardware path is the M10 doc's section 4, not built here).
#
#   <out>/ffmpeg-src/     ffmpeg-6.1 from the port tarball + patches/*.patch (ffplay's opt-in
#                         FFPLAY_STATLINE_MS / FFPLAY_AUTOKEYS knobs), configured in tree
#   <out>/ffplay-<v>      static, unstripped (addr2line); <out>/ffplay-<v>.stripped = stage this
#   <out>/ffplay-<v>.map, <out>/BUILD-INFO-<v>.txt, logs
#
# Reads (never writes): sources/phoenix-rtos-ports/ffmpeg (tarball), the SDL build-out named
# by --sdl (sdl-prefix + mesa-gl), the tree sysroot, the toolchain, the tree's libz.a, E7's
# phx-g++. No Pi, no rebuild-rpi4b-fast.sh, no /srv.
#
# Usage: tools/gpu-lane/video-player/build-ffplay.sh [--sdl drm|wl] [--out <dir>] [-j N]
#                                                    [--reconfigure] [--clean]
# Stage (coordinator only; a NEW path -- check it is absent first, then cmp after install):
#   sudo install -m 755 <out>/ffplay-drm.stripped <live NFS export>/usr/bin/ffplay-drm
#
# Copyright 2026 Phoenix Systems
#
# This file is part of Phoenix-RTOS.
#
# %LICENSE%
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
jobs="$(nproc)"
variant=drm
reconf=0
clean=0
while [ $# -gt 0 ]; do
	case "$1" in
		--sdl) shift; variant="${1:?--sdl needs drm or wl}" ;;
		--sdl=*) variant="${1#--sdl=}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--reconfigure) reconf=1 ;;
		--clean) clean=1 ;;
		-h|--help) sed -n '2,36p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-ffplay.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

log() { printf '[ffplay] %s\n' "$*"; }
die() { printf '[ffplay] ERROR: %s\n' "$*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -c1-16; }

case "${variant}" in
	drm) sdl_out="${root}/tools/gpu-lane/sdl2-drm/build-out" ;;
	wl)
		sdl_out="${root}/tools/gpu-lane/sdl2-wl/build-out"
		[ -d "${sdl_out}" ] || die "--sdl wl: ${sdl_out} does not exist yet (M8 builds SDL with the Wayland backend there)"
		die "--sdl wl: the link line for M8's SDL is not written yet -- add it from sdl2-wl's own link once it exists"
		;;
	*) die "--sdl must be drm or wl" ;;
esac

FF_VERSION=6.1
FF_TARBALL="${root}/sources/phoenix-rtos-ports/ffmpeg/ffmpeg-${FF_VERSION}.tar.gz"
FF_SHA256=938dd778baa04d353163ca5cb06c909c918850055f549205b29b1224e45a5316   # = the port's sha256
B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCXX="${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++"
SP="${sdl_out}/sdl-prefix"
M="${sdl_out}/mesa-gl"
MB="${M}/mesa-build"
GLUE="${here}/ffplay_phoenix_glue.c"
for p in "${FF_TARBALL}" "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-nm" "${TC}-strip" "${TC}-readelf" "${PHXCXX}" \
		"${B}/lib/libz.a" "${SP}/lib/libSDL2.a" "${SP}/include/SDL2/SDL_config.h" "${M}/libdrm-prefix/lib/libdrm.a" \
		"${M}/compat/libmesadrm-compat.a" "${GLUE}"; do
	[ -e "${p}" ] || die "missing ${p}"
done
[ "$(sha256sum "${FF_TARBALL}" | cut -d' ' -f1)" = "${FF_SHA256}" ] || die "${FF_TARBALL}: sha256 mismatch"
mkdir -p "${out}"

TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

# --- 1. source + 2. configure --------------------------------------------------------------------
FS="${out}/ffmpeg-src"
# Configure: the port's line (static, asm on, no autodetect, no programs, no network)
# with the player's components (components.sh, shared with the host control hosttest/run.sh).
# SDL itself is not probed (its link test would need the whole static Mesa group):
# fftools/ffplay.c is compiled and linked below by hand.
# shellcheck source=components.sh
. "${here}/components.sh"
ff_configure() {
	( cd "${FS}" && ./configure \
		--enable-cross-compile \
		--arch=aarch64 \
		--target-os=none \
		--cross-prefix="${TC}-" \
		--cc="${TC}-gcc" \
		--extra-cflags="${TFLAGS[*]} -O2 -g" \
		--extra-ldflags="${TFLAGS[*]}" \
		"${FF_COMMON[@]}" \
		--enable-asm \
		--disable-programs \
		--disable-shared \
		--enable-static \
		> "${out}/ff-configure.log" 2>&1 ) || { tail -30 "${out}/ff-configure.log"; die "ffmpeg configure failed"; }
}
stamp="$( (sha256sum "${FF_TARBALL}"; declare -f ff_configure; cat "${here}/components.sh" "${here}"/patches/*.patch) \
	| sha256sum | cut -c1-16)"
if [ "${reconf}" = 1 ] || [ "$(cat "${out}/ffmpeg-src.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
	log "ffmpeg ${FF_VERSION}: unpack the port tarball + $(ls "${here}"/patches/*.patch | wc -l) patch(es) (set ${stamp})"
	rm -rf "${FS}" "${out}/ff-tmp"
	mkdir -p "${out}/ff-tmp"
	tar -C "${out}/ff-tmp" -xzf "${FF_TARBALL}"
	mv "${out}/ff-tmp/ffmpeg-${FF_VERSION}" "${FS}"
	rmdir "${out}/ff-tmp"
	for p in "${here}"/patches/*.patch; do
		patch -d "${FS}" -p1 -s --no-backup-if-mismatch < "${p}" || die "patch failed: $(basename "${p}")"
	done
	ff_configure

	# The port's libm reconcile (port.def.sh p_prepare): a configure probe against a stale
	# libc would set these 0 and ffmpeg's static-inline fallbacks would clash with libphoenix.
	flipped=0
	for macro in HAVE_ERF HAVE_EXP2 HAVE_EXP2F HAVE_LOG2F; do
		if grep -q "^#define ${macro} 0$" "${FS}/config.h"; then
			sed -i "s/^#define ${macro} 0$/#define ${macro} 1/" "${FS}/config.h"
			flipped=$((flipped + 1))
		fi
	done
	log "  config.h: flipped ${flipped} libm HAVE_* flag(s) 0->1"
	echo "${stamp}" > "${out}/ffmpeg-src.stamp"
fi
for d in HAVE_PTHREADS HAVE_NEON CONFIG_AVFILTER CONFIG_SWSCALE CONFIG_SWRESAMPLE CONFIG_HEVC_DECODER CONFIG_H264_DECODER \
		CONFIG_AAC_DECODER CONFIG_MOV_DEMUXER CONFIG_SCALE_FILTER CONFIG_ARESAMPLE_FILTER; do
	cat "${FS}/config.h" "${FS}/config_components.h" | grep -qE "^#define ${d} 1$" || die "config: ${d} is not 1"
done
for d in CONFIG_GPL CONFIG_NONFREE CONFIG_SDL2; do
	grep -qE "^#define ${d} 0$" "${FS}/config.h" || die "config.h: ${d} is not 0"
done
log "  config.h: pthreads, NEON, avfilter/swscale/swresample, h264/hevc/aac, mov; LGPL (no GPL/nonfree)"

# --- 3. libraries + the ffplay objects ------------------------------------------------------------
log "ffmpeg libraries (-j${jobs})"
make -C "${FS}" -j"${jobs}" > "${out}/ff-make.log" 2>&1 || { grep -E -B2 -A6 'error' "${out}/ff-make.log" | head -60; die "ffmpeg build failed"; }
log "  $(grep -c 'warning:' "${out}/ff-make.log" || true) compiler warning line(s) (${out}/ff-make.log)"
# ffplay.c includes <SDL.h>: SDL's headers only for the fftools objects (ffmpeg's own Makefile
# does the same through CFLAGS-ffplay). The pattern rule builds them although CONFIG_FFPLAY=no.
make -C "${FS}" ECFLAGS="-I${SP}/include/SDL2" fftools/ffplay.o fftools/cmdutils.o fftools/opt_common.o \
	> "${out}/ff-fftools.log" 2>&1 || { tail -40 "${out}/ff-fftools.log"; die "fftools compile failed"; }
GLUE_O="${out}/ffplay_phoenix_glue.o"
"${TC}-gcc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}" -c "${GLUE}" -o "${GLUE_O}" || die "glue compile failed"

# --- 4. link ----------------------------------------------------------------------------------
# The quakespasm-drm shape (sdl2-drm/build.sh step 4): C++ driver (Mesa's compiler is C++),
# -static, --gc-sections, 4 KiB pages, --wrap=mmap for libdrm-phoenix's BO-token mmap, libgallium
# whole-archive, libSDL2 + Mesa + libdrm + libz in one group. Plus --wrap=pthread_create (the
# glue above: 8 MiB default thread stacks) and a 16 MiB main stack.
A=(src/egl/libEGL.a src/gbm/libgbm.a src/gbm/backends/dri/dri_gbm.a
	src/mesa/glapi/shared-glapi/libglapi.a src/gallium/drivers/v3d/libv3d.a
	src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a
	src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a src/broadcom/qpu/libbroadcom_qpu.a
	src/broadcom/libv3d_neon.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a
	src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/winsys/kmsro/drm/libkmsrowinsys.a
	src/gallium/winsys/v3d/drm/libv3dwinsys.a src/gallium/winsys/vc4/drm/libvc4winsys.a
	src/gallium/winsys/sw/kms-dri/libswkmsdri.a src/gallium/winsys/sw/dri/libswdri.a
	src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a
	src/c11/impl/libmesa_util_c11.a)
AA=()
for a in "${A[@]}"; do
	if [ -f "${MB}/${a}" ]; then AA+=("${MB}/${a}"); else log "  (archive not built: ${a})"; fi
done
GL_BRIDGE="${MB}/src/mesa/glapi/glapi/libglapi_bridge.a"
[ -f "${GL_BRIDGE}" ] || die "missing ${GL_BRIDGE}"
GALLIUM_A="$(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
FF_A=("${FS}/libavfilter/libavfilter.a" "${FS}/libavformat/libavformat.a" "${FS}/libavcodec/libavcodec.a"
	"${FS}/libswresample/libswresample.a" "${FS}/libswscale/libswscale.a" "${FS}/libavutil/libavutil.a")
for a in "${FF_A[@]}"; do [ -f "${a}" ] || die "missing ${a}"; done
BIN="${out}/ffplay-${variant}"
log "ffplay-${variant}: link"
"${PHXCXX}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,--wrap=mmap \
	-Wl,--wrap=pthread_create -Wl,-z,stack-size=16777216 -Wl,-Map,"${BIN}.map" -o "${BIN}" \
	"${FS}/fftools/ffplay.o" "${FS}/fftools/cmdutils.o" "${FS}/fftools/opt_common.o" "${GLUE_O}" \
	-Wl,--whole-archive "${GALLIUM_A}" -Wl,--no-whole-archive \
	-Wl,--start-group "${FF_A[@]}" "${SP}/lib/libSDL2.a" "${GL_BRIDGE}" "${AA[@]}" "${M}/libdrm-prefix/lib/libdrm.a" \
	"${M}/compat/libmesadrm-compat.a" "${B}/lib/libz.a" -Wl,--end-group -lm > "${out}/ffplay-link-${variant}.log" 2>&1 \
	|| { head -60 "${out}/ffplay-link-${variant}.log"; die "ffplay link failed"; }
[ -s "${out}/ffplay-link-${variant}.log" ] && sed 's/^/  link: /' "${out}/ffplay-link-${variant}.log" | head -20
"${TC}-strip" -o "${BIN}.stripped" "${BIN}"

# --- 5. verification --------------------------------------------------------------------------
log "verify"
bad=0
if "${TC}-readelf" -l "${BIN}" | grep -q INTERP; then log "  PT_INTERP present"; bad=1; fi
und="$("${TC}-nm" -u "${BIN}" || true)"
log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && { sed 's/^/    /' <<< "${und}" | head -20; bad=1; }
syms="$("${TC}-nm" "${BIN}")"
for s in main video_thread audio_thread read_thread sdl_audio_callback __wrap_pthread_create __wrap_mmap \
		KMSDRM_CreateDevice SDL_EGL_LoadLibrary SDL_PHOENIX_HID_Poll ff_hevc_decoder ff_h264_decoder ff_aac_decoder \
		ff_mov_demuxer ff_matroska_demuxer ff_vf_scale ff_af_aresample swr_convert sws_scale v3d_drm_screen_create_renderonly; do
	if grep -qE " [TtWwDdRr] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
done
strs="$(strings -a "${BIN}.stripped")"
for s in 'KMS/DRM Video Driver' '/dev/dri/' 'libdrm-phoenix:' '/dev/kbd0' '/dev/audio0' 'EGL_KHR_platform_gbm' 'V3D 4.2' \
		'FFPLAY_THREAD_STACK' 'Simple media player' 'ffplay-stat t=' 'FFPLAY_AUTOKEYS'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	log "  strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
for s in 'v3d-winsys:' 'v3da-winsys:' 'phxgl' '/dev/fb0' 'RPI4FB_GETMODE' 'phoenix_v3d_ioctl'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	log "  old-lane string '${s}': ${n}"
	[ "${n}" = 0 ] || bad=1
done
{
	echo "built:              $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "variant:            ${variant} (SDL from ${sdl_out})"
	echo "ffmpeg:             ${FF_VERSION} port tarball ($(sha "${FF_TARBALL}")), configure set ${stamp}, LGPL"
	echo "SDL:                $(sed -n 2p "${sdl_out}/BUILD-INFO.txt" | sed 's/^SDL: *//')"
	echo "libSDL2.a:          $(sha "${SP}/lib/libSDL2.a")"
	echo "Mesa:               $(cat "${M}/mesa-src.stamp") opengl=$(cat "${M}/mesa-opengl.txt" | sed 's/opengl=//')"
	echo "glue:               $(sha "${GLUE}") (--wrap=pthread_create, 8 MiB default thread stack)"
	echo "ffplay-${variant}:         $(sha256sum "${BIN}" | cut -d' ' -f1) $(stat -c %s "${BIN}") bytes"
	echo "ffplay-${variant}.stripped: $(sha256sum "${BIN}.stripped" | cut -d' ' -f1) $(stat -c %s "${BIN}.stripped") bytes"
} > "${out}/BUILD-INFO-${variant}.txt"
sed 's/^/  /' "${out}/BUILD-INFO-${variant}.txt"
[ "${bad}" = 0 ] || die "verification failed (see above)"
log "done: stage ${BIN}.stripped as /usr/bin/ffplay-${variant}"

#!/usr/bin/env bash
#
# FRAMEWORK PORT: sources/phoenix-rtos-ports yquake2_drm (body: sdl2_kmsdrm/gamedrm/relink-sdl-gl-game.subr),
# opt-in, not in the default image (docs/gpu-new-lane/MIGRATION.md section 4, "Ports (graphics)").
# Every patch/glue file this script uses is also a file of the port; the copies are kept
# identical by scripts/check-gpu-lane-ports-sync.sh -- a change here must be copied there.
#
# Build `yquake2-drm` + its launcher `quake2-drm`: a CLONE of the yquake2 port (yQuake2 8.71pre,
# single static ELF, ref_gl3 built as GLES3) on the FULL standard DRM stack -- SDL 2.30.12's
# stock KMSDRM video driver (tools/gpu-lane/sdl2-drm), Mesa 26.2 GBM + EGL + GLES (the sdl2-drm
# mesa-gl build) and libdrm-phoenix (build-out-m5b) -> rpi4-kms (card0) + rpi4-v3d-async
# (renderD128). The shipped /usr/bin/yquake2 and /usr/bin/quake2, the ports/yquake2 and
# ports/sdl2 ports, the old lane's Mesa fork and tools/.gpu-libs are never touched (PLAN rule 2).
#
# HOW: the port's own final link (from its build.log) re-run with the old SDL-GL glue objects
# and the old group (ports libSDL2.a, libGL-phoenix.a, libv3d-phoenix.a) swapped for the new
# stack in the stk-drm shape: yQuake2's ref_gl3 is a GLES3 renderer that loads every gl*
# through glad + SDL_GL_GetProcAddress (= eglGetProcAddress), so it links libGLESv2 + the
# shared glapi like stk-drm, not quakespasm-drm's desktop-GL bridge. Details and the proofs:
# gamedrm/relink-sdl-gl-game.sh. The launcher is tools/yquake2-port/quake2-launcher.c with
# only its exec target rewritten (/usr/bin/yquake2 -> /usr/bin/yquake2-drm): the same
# ram-stage-play of /usr/share/quake2 to /tmp/quake2 and the same video/demo arguments as the
# showcase gate's `/usr/bin/quake2` (its `+set vid_renderer gl1` is inert on the single-ELF
# gl3 binary, on both lanes).
#
# WRITES ONLY under $Q2DRM_OUT (default tools/gpu-lane/sdl2-drm/build-out/quake2-drm/). Reads the
# sdl2-drm build-out (libSDL2.a + mesa-gl), libdrm-phoenix build-out-m5b, the yquake2 port build
# tree + build.log, the toolchain and tools/.gpu-libs (control relink only). It does NOT run
# sdl2-drm/build.sh or mesa-drm/build.sh, and touches no /srv, TFTP loader, .buildroot output or
# sources/.
#
# Usage: tools/gpu-lane/sdl2-drm/build-quake2-drm.sh [--no-control] [--libdrm-prefix <dir>] [--variant <v>]
#   --variant <v>  link the libSDL2.a of an sdl2-drm `build.sh --out build-out-<v>` tree and name
#           everything quake2-drm-<v>: out build-out/quake2-drm-<v>/, yquake2-drm-<v> + launcher
#           quake2-drm-<v> (execs /usr/bin/yquake2-drm-<v>), banner/flipstat tag quake2-drm-<v>.
#           frame-pacing.md's A/B used `pace` (build.sh --extra-patches, the patch now adopted as
#           patches/0009) and its control `ctl` (the default set of the time, build-out-ctl).
# Env:   Q2DRM_PORT_SHADOW=<dir>  relink from a gamedrm/shadow-port-build.sh output instead of the
#        port's .buildroot tree (see that script)
# Env:   Q2DRM_OUT, TARGET (default aarch64a72-generic-rpi4b), RPI4B_BUILDROOT
# Stage (coordinator only; the live export is the fsid=0 one):
#   install -m 755 $OUT/yquake2-drm.stripped <export>/usr/bin/yquake2-drm
#   install -m 755 $OUT/quake2-drm           <export>/usr/bin/quake2-drm
#   (docs/gpu-new-lane/MIGRATION.md, "Pre-registered Pi cycles")
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
G_DO_CONTROL=1
G_LIBDRM_SRC="${repo_root}/tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix"
variant=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--no-control) G_DO_CONTROL=0 ;;
		--variant) shift; variant="${1:?--variant needs a name}" ;;
		--variant=*) variant="${1#--variant=}" ;;
		--libdrm-prefix) shift; G_LIBDRM_SRC="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) G_LIBDRM_SRC="${1#--libdrm-prefix=}" ;;
		-h|--help) sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-quake2-drm: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
G_LIBDRM_SRC="$(realpath -m "${G_LIBDRM_SRC}")"
G_APP=quake2-drm
G_OUT_DEFAULT="${here}/build-out/quake2-drm"
if [ -n "${variant}" ]; then
	case "${variant}" in *[!a-z0-9]*) echo "build-quake2-drm: --variant must be [a-z0-9]+" >&2; exit 2 ;; esac
	G_APP="quake2-drm-${variant}"
	G_OUT_DEFAULT="${here}/build-out/quake2-drm-${variant}"
	G_SDL_DIR="${here}/build-out-${variant}"
	G_SUFFIX="-drm-${variant}"
	[ -f "${G_SDL_DIR}/sdl-prefix/lib/libSDL2.a" ] || { echo "build-quake2-drm: no ${G_SDL_DIR}/sdl-prefix/lib/libSDL2.a (run build.sh --out build-out-${variant} first)" >&2; exit 1; }
fi
G_ENGINE=yquake2
G_PORTDIR=yquake2-8.71
G_GL=gles
G_API_TEXT=GLES
G_LAUNCHER_SRC="${repo_root}/tools/yquake2-port/quake2-launcher.c"
G_LAUNCHER=quake2
G_ENGINE_SYMS="GL3_Init GL3_EndFrame gladLoadGLES2Loader GetRefAPI Qcommon_Init"
G_OUT="$(realpath -m "${Q2DRM_OUT:-${G_OUT_DEFAULT}}")"
[ -z "${Q2DRM_PORT_SHADOW:-}" ] || G_PORT_SHADOW="$(realpath -m "${Q2DRM_PORT_SHADOW}")"

# shellcheck source=gamedrm/relink-sdl-gl-game.sh
. "${here}/gamedrm/relink-sdl-gl-game.sh"
g_main

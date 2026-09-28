#!/usr/bin/env bash
#
# FRAMEWORK PORT: sources/phoenix-rtos-ports quake3_drm (body: sdl2_kmsdrm/gamedrm/relink-sdl-gl-game.subr),
# opt-in, not in the default image (docs/gpu-new-lane/MIGRATION.md section 4, "Ports (graphics)").
# Every patch/glue file this script uses is also a file of the port; the copies are kept
# identical by scripts/check-gpu-lane-ports-sync.sh -- a change here must be copied there.
#
# Build `quake3e-drm` + its launcher `quake3-drm`: a CLONE of the quake3 port (quake3e "Q3 1.32e",
# single static ELF, opengl1 renderer, aarch64 QVM JIT) on the FULL standard DRM stack -- SDL
# 2.30.12's stock KMSDRM video driver (tools/gpu-lane/sdl2-drm), Mesa 26.2 GBM + EGL + desktop GL
# (the sdl2-drm mesa-gl build) and libdrm-phoenix (build-out-m5b) -> rpi4-kms (card0) +
# rpi4-v3d-async (renderD128). The shipped /usr/bin/quake3e and /usr/bin/quake3, the ports/quake3
# and ports/sdl2 ports, the old lane's Mesa fork and tools/.gpu-libs are never touched (PLAN rule 2).
#
# HOW: the port's own final link (from its build.log) re-run with the old SDL-GL glue objects and
# the old group (ports libSDL2.a, libGL-phoenix.a, libv3d-phoenix.a) swapped for the new stack in
# the quakespasm-drm shape: quake3e's opengl1 renderer is a desktop (compatibility) GL program, so
# it links Mesa's libglapi_bridge.a (the static desktop gl* entry points) instead of libGLESv2.a.
# quake3e resolves its qgl* pointers through SDL_GL_GetProcAddress; the bridge only matters for any
# gl* referenced at link time. Details and the proofs: gamedrm/relink-sdl-gl-game.sh. The launcher
# is tools/quake3-port/quake3-launcher.c with only its exec target rewritten (/usr/bin/quake3e ->
# /usr/bin/quake3e-drm): the same ram-stage-play of /usr/share/quake3 to /tmp/quake3 and the same
# fs_basepath/fs_game arguments; the gate appends `+map q3dm1`, as for `/usr/bin/quake3`.
#
# WRITES ONLY under $Q3DRM_OUT (default tools/gpu-lane/sdl2-drm/build-out/quake3-drm/). Reads the
# sdl2-drm build-out (libSDL2.a + mesa-gl), libdrm-phoenix build-out-m5b, the quake3 port build
# tree + build.log, the toolchain and tools/.gpu-libs (control relink only). It does NOT run
# sdl2-drm/build.sh or mesa-drm/build.sh, and touches no /srv, TFTP loader, .buildroot output or
# sources/.
#
# Usage: tools/gpu-lane/sdl2-drm/build-quake3-drm.sh [--no-control] [--libdrm-prefix <dir>]
# Env:   Q3DRM_OUT, TARGET (default aarch64a72-generic-rpi4b), RPI4B_BUILDROOT
# Stage (coordinator only; the live export is the fsid=0 one):
#   install -m 755 $OUT/quake3e-drm.stripped <export>/usr/bin/quake3e-drm
#   install -m 755 $OUT/quake3-drm           <export>/usr/bin/quake3-drm
#   (docs/gpu-new-lane/MIGRATION.md, "Pre-registered Pi cycles")
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
G_DO_CONTROL=1
G_LIBDRM_SRC="${repo_root}/tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--no-control) G_DO_CONTROL=0 ;;
		--libdrm-prefix) shift; G_LIBDRM_SRC="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) G_LIBDRM_SRC="${1#--libdrm-prefix=}" ;;
		-h|--help) sed -n '2,35p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-quake3-drm: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
G_LIBDRM_SRC="$(realpath -m "${G_LIBDRM_SRC}")"
G_APP=quake3-drm
G_ENGINE=quake3e
G_PORTDIR=quake3-1.32
G_GL=gl
G_API_TEXT="desktop GL"
G_LAUNCHER_SRC="${repo_root}/tools/quake3-port/quake3-launcher.c"
G_LAUNCHER=quake3
G_ENGINE_SYMS="GLimp_Init GLimp_EndFrame GetRefAPI Com_Init VM_Compile"
G_OUT="$(realpath -m "${Q3DRM_OUT:-${here}/build-out/quake3-drm}")"

# shellcheck source=gamedrm/relink-sdl-gl-game.sh
. "${here}/gamedrm/relink-sdl-gl-game.sh"
g_main

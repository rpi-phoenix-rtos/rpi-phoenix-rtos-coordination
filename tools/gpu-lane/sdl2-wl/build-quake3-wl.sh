#!/usr/bin/env bash
#
# Build `quake3e-wl` + its launcher `quake3-wl` (M8): a CLONE of the quake3e port (quake3e 1.32e, single static ELF, opengl1 renderer = desktop GL: libglapi_bridge, the quakespasm shape)
# for a WINDOW on the Wayland desktop -- SDL 2.30.12 Wayland (+ KMSDRM), Mesa 26.2 EGL wayland,
# the labwc-drm Wayland client stack and libdrm-phoenix build-out-low, i.e. the link group of
# tools/gpu-lane/sdl2-wl/build.sh (build-out/link-inputs.txt). The body, and the proofs it prints,
# are gamewl/relink-sdl-gl-game-wl.sh. The launcher is quake3-port/quake3-launcher.c with only its exec
# target rewritten (/usr/bin/quake3e -> /usr/bin/quake3e-wl); it forwards extra arguments, which is
# how /bin/game-window.sh asks for a window.
#
# WRITES ONLY under $Q3WL_OUT (default tools/gpu-lane/sdl2-wl/build-out/quake3-wl/). Reads the sdl2-wl
# build-out, the quake3e port build tree + build.log, the toolchain and tools/.gpu-libs (control
# relink only). Never runs sdl2-wl/build.sh or mesa-drm/build.sh; no /srv, TFTP, .buildroot output
# or sources/ is written.
#
# Usage: tools/gpu-lane/sdl2-wl/build-quake3-wl.sh [--no-control]
# Env:   Q3WL_OUT, Q3WL_PORT_SHADOW (a gamedrm/shadow-port-build.sh output), TARGET, RPI4B_BUILDROOT
# Stage (coordinator only; new names only, checked absent first):
#   install -m 755 $OUT/quake3e-wl.stripped <export>/usr/bin/quake3e-wl
#   install -m 755 $OUT/quake3-wl           <export>/usr/bin/quake3-wl
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${here}/../../.." && pwd)"
G_DO_CONTROL=1
while [ "$#" -gt 0 ]; do
	case "$1" in
		--no-control) G_DO_CONTROL=0 ;;
		-h|--help) sed -n '2,22p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-quake3-wl: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
G_APP=quake3-wl
G_ENGINE=quake3e
G_PORTDIR=quake3-1.32
G_GL=gl
G_API_TEXT="desktop GL"
G_LAUNCHER_SRC="${repo_root}/tools/quake3-port/quake3-launcher.c"
G_LAUNCHER=quake3
G_ENGINE_SYMS="GLimp_Init GLimp_EndFrame GetRefAPI Com_Init VM_Compile"
G_OUT="$(realpath -m "${Q3WL_OUT:-${here}/build-out/quake3-wl}")"
[ -z "${Q3WL_PORT_SHADOW:-}" ] || G_PORT_SHADOW="$(realpath -m "${Q3WL_PORT_SHADOW}")"

# shellcheck source=gamewl/relink-sdl-gl-game-wl.sh
. "${here}/gamewl/relink-sdl-gl-game-wl.sh"
g_main

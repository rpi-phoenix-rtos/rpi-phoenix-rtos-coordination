#!/usr/bin/env bash
#
# Build `yquake2-wl` + its launcher `quake2-wl` (M8): a CLONE of the yquake2 port (yQuake2 8.71pre, single static ELF, ref_gl3 built as GLES3: libGLESv2, the stk-drm shape)
# for a WINDOW on the Wayland desktop -- SDL 2.30.12 Wayland (+ KMSDRM), Mesa 26.2 EGL wayland,
# the labwc-drm Wayland client stack and libdrm-phoenix build-out-low, i.e. the link group of
# tools/gpu-lane/sdl2-wl/build.sh (build-out/link-inputs.txt). The body, and the proofs it prints,
# are gamewl/relink-sdl-gl-game-wl.sh. The launcher is yquake2-port/quake2-launcher.c with only its exec
# target rewritten (/usr/bin/yquake2 -> /usr/bin/yquake2-wl); it forwards extra arguments, which is
# how /bin/game-window.sh asks for a window.
#
# WRITES ONLY under $Q2WL_OUT (default tools/gpu-lane/sdl2-wl/build-out/quake2-wl/). Reads the sdl2-wl
# build-out, the yquake2 port build tree + build.log, the toolchain and tools/.gpu-libs (control
# relink only). Never runs sdl2-wl/build.sh or mesa-drm/build.sh; no /srv, TFTP, .buildroot output
# or sources/ is written.
#
# Usage: tools/gpu-lane/sdl2-wl/build-quake2-wl.sh [--no-control]
# Env:   Q2WL_OUT, Q2WL_PORT_SHADOW (a gamedrm/shadow-port-build.sh output), TARGET, RPI4B_BUILDROOT
# Stage (coordinator only; new names only, checked absent first):
#   install -m 755 $OUT/yquake2-wl.stripped <export>/usr/bin/yquake2-wl
#   install -m 755 $OUT/quake2-wl           <export>/usr/bin/quake2-wl
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
		*) echo "build-quake2-wl: unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
G_APP=quake2-wl
G_ENGINE=yquake2
G_PORTDIR=yquake2-8.71
G_GL=gles
G_API_TEXT="GLES"
G_LAUNCHER_SRC="${repo_root}/tools/yquake2-port/quake2-launcher.c"
G_LAUNCHER=quake2
G_ENGINE_SYMS="GL3_Init GL3_EndFrame gladLoadGLES2Loader GetRefAPI Qcommon_Init"
G_OUT="$(realpath -m "${Q2WL_OUT:-${here}/build-out/quake2-wl}")"
[ -z "${Q2WL_PORT_SHADOW:-}" ] || G_PORT_SHADOW="$(realpath -m "${Q2WL_PORT_SHADOW}")"

# shellcheck source=gamewl/relink-sdl-gl-game-wl.sh
. "${here}/gamewl/relink-sdl-gl-game-wl.sh"
g_main

#!/usr/bin/env bash
#
# check-gpu-lane-ports-sync.sh — are the new-GPU-lane framework ports' vendored copies still
# identical to their sources?
#
# The new lane was developed in tools/gpu-lane/*/ (standalone build.sh scripts) and is now also
# stored as phoenix-rtos-ports framework ports (docs/gpu-new-lane/MIGRATION.md section 4, "Ports
# (graphics)"). Every patch, glue source, launcher and config the ports carry is a COPY of a file
# here (or, for the game clones, of an old-lane port's file). Until the tools/ side is retired,
# both copies exist; this script proves they have not drifted apart:
#
#   scripts/check-gpu-lane-ports-sync.sh [<ports dir>]     (default sources/phoenix-rtos-ports)
#
# Mapping lines: "<source>|<port path>". <source> is relative to the coordination repo, or to the
# ports dir when it starts with "@"; a trailing "/*" (source) maps every file of that directory
# into the <port path> directory; otherwise a directory is compared recursively (diff -r) and a
# file with cmp. Exit status 1 on any difference or missing file.
#
# Files a port has that are NOT copies (the recipes, libdrm_phoenix/glue/newlane.subr,
# sdl2_kmsdrm/gamedrm/relink-sdl-gl-game.subr -- the ports adaptation of
# tools/gpu-lane/sdl2-drm/gamedrm/relink-sdl-gl-game.sh with the group of
# tools/gpu-lane/sdl2-wl/gamewl/relink-sdl-gl-game-wl.sh; the image's own session files
# derived from tools files with the image's program names: sdl2_kmsdrm/games/ (from sdl2-wl/pi
# and conf/labwc-xfce-m8), video_player/files/image/ (from video-player/pi/video-play2 and
# conf/labwc-xfce-m10)) are not listed.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ports="${1:-${repo_root}/sources/phoenix-rtos-ports}"
T=tools/gpu-lane

MAP=(
	# libdrm_phoenix
	"${T}/libdrm-phoenix/patches|libdrm_phoenix/patches"
	"${T}/libdrm-phoenix/src/*|libdrm_phoenix/glue/phoenix"
	"${T}/libdrm-phoenix/include/*|libdrm_phoenix/glue/phoenix"
	"${T}/v3d-async/v3da_proto.h|libdrm_phoenix/glue/phoenix/v3da_proto.h"
	"${T}/kms/kms_proto.h|libdrm_phoenix/glue/phoenix/kms_proto.h"
	"${T}/libdrm-phoenix/drmprobe/drmprobe.c|libdrm_phoenix/glue/drmprobe/drmprobe.c"
	"${T}/v3d-async/v3da_clgen.c|libdrm_phoenix/glue/drmprobe/v3da_clgen.c"
	"${T}/v3d-async/v3da_clgen.h|libdrm_phoenix/glue/drmprobe/v3da_clgen.h"
	# mesa_drm
	"${T}/mesa-drm/patches/mesa|mesa_drm/patches"
	"${T}/mesa-drm/compat|mesa_drm/glue/compat"
	"${T}/vulkan-drm/phxvk|mesa_drm/glue/phxvk"
	# sdl2_kmsdrm
	"${T}/sdl2-drm/patches/*|sdl2_kmsdrm/patches"
	"${T}/sdl2-drm/patches-sdl-vulkan|sdl2_kmsdrm/patches/vulkan"
	"${T}/sdl2-drm/overlay|sdl2_kmsdrm/overlay"
	"${T}/sdl2-drm/gamedrm/gamedrm_hooks.c|sdl2_kmsdrm/gamedrm/gamedrm_hooks.c"
	"${T}/sdl2-drm/gamedrm/check-swap-order.sh|sdl2_kmsdrm/gamedrm/check-swap-order.sh"
	# sdl2_kmsdrm: its Wayland video driver (the games in a window, M8)
	"${T}/sdl2-wl/patches/*|sdl2_kmsdrm/patches/wayland"
	# the game clones
	"tools/yquake2-port/quake2-launcher.c|yquake2_drm/glue/quake2-launcher.c"
	"tools/quake3-port/quake3-launcher.c|quake3_drm/glue/quake3-launcher.c"
	"tools/supertuxkart-port/stk-launcher.c|supertuxkart_drm/glue/stk-launcher.c"
	"${T}/sdl2-drm/stkdrm/stkdrm_hooks.c|supertuxkart_drm/glue/stkdrm_hooks.c"
	"${T}/sdl2-drm/patches-vkquake|vkquake_drm/patches"
	"${T}/sdl2-drm/vkqdrm|vkquake_drm/glue/vkqdrm"
	"${T}/vulkan-drm/patches/vkcube|vkcube_drm/patches"
	# wayland
	"${T}/weston-drm/patches/wayland|wayland/patches/wayland"
	"${T}/weston-drm/compat|wayland/glue/compat"
	"${T}/mesa-drm/compat/include|wayland/glue/mesa-compat/include"
	# shmsrv's wire header: the server is phoenix-rtos-devices misc/shmsrv, the ports carry
	# copies of its shm_proto.h (wlphx_memfd.c, libxshmfence_phoenix, labwc_desktop)
	"sources/phoenix-rtos-devices/misc/shmsrv/shm_proto.h|wayland/glue/shmsrv/shm_proto.h"
	"sources/phoenix-rtos-devices/misc/shmsrv/shm_proto.h|wayland_phoenix/files/shmsrv/shm_proto.h"
	# X11
	"${T}/x11-drm/patches/libxshmfence/*|libxshmfence_phoenix/patches"
	"${T}/xorg-drm/patches/libepoxy/*|libepoxy/patches"
	"${T}/xorg-drm/compat/include|libepoxy/glue/compat-include"
	"${T}/xorg-drm/patches/xorg-server/*|xorg_server_drm/patches"
	"${T}/xorg-drm/compat|xorg_server_drm/glue/compat"
	"${T}/xorg-drm/src|xorg_server_drm/glue/src"
	"${T}/xorg-drm/conf/xorg-drm.conf|xorg_server_drm/glue/conf/xorg-drm.conf"
	"${T}/xorg-drm/pi/startx|xorg_server_drm/glue/pi/startx"
	"${T}/x11-drm/src/eglx11_demo.c|xorg_server_drm/glue/eglx11/eglx11_demo.c"
	# video_player (M10)
	"${T}/video-player/patches|video_player/patches"
	"${T}/video-player/components.sh|video_player/files/components.sh"
	"${T}/video-player/ffplay_phoenix_glue.c|video_player/files/ffplay_phoenix_glue.c"
	"${T}/video-player/gen-clips.sh|video_player/files/gen-clips.sh"
	"${T}/video-player/gtk-video/gtk-video.c|video_player/files/gtk-video/gtk-video.c"
	"${T}/video-player/conf/applications|video_player/files/conf/applications"
)

bad=0 n=0
for m in "${MAP[@]}"; do
	src="${m%%|*}" dst="${ports}/${m#*|}"
	case "${src}" in @*) src="${ports}/${src#@}" ;; *) src="${repo_root}/${src}" ;; esac
	if [ "${src%/\*}" != "${src}" ]; then
		for f in "${src%/\*}"/*; do
			[ -f "${f}" ] || continue
			n=$((n + 1))
			cmp -s "${f}" "${dst}/$(basename "${f}")" || { echo "DIFFERS/MISSING: ${f#"${repo_root}"/} -> ${dst#"${ports}"/}/$(basename "${f}")"; bad=1; }
		done
	elif [ -d "${src}" ]; then
		n=$((n + $(find "${src}" -type f | wc -l)))
		if ! diff -rq "${src}" "${dst}" > /dev/null 2>&1; then
			echo "DIFFERS/MISSING: ${src#"${repo_root}"/}/ -> ${dst#"${ports}"/}/"
			diff -rq "${src}" "${dst}" 2>&1 | head -5 | sed 's/^/    /'
			bad=1
		fi
	else
		n=$((n + 1))
		cmp -s "${src}" "${dst}" || { echo "DIFFERS/MISSING: ${src#"${repo_root}"/} -> ${dst#"${ports}"/}"; bad=1; }
	fi
done
if [ "${bad}" = 0 ]; then
	echo "gpu-lane ports in sync: ${n} files in ${#MAP[@]} mappings identical (${ports})"
else
	echo "gpu-lane ports OUT OF SYNC (see above)" >&2
	exit 1
fi

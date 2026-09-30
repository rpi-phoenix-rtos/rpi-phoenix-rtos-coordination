#!/usr/bin/env bash
#
# check-rootfs-complete.sh — assert a staged rootfs contains everything the
# image is supposed to ship, and FAIL if it does not.
#
# Why this exists (2026-09-03): the exported SD image was complete in every
# binary and still shipped a broken Quake III, because
# usr/share/quake3/demoq3/pak1.pk3 (our ioquake3 VM pak, UI API 6) was absent.
# Without it the engine dies at startup with
#   ERROR: User Interface is version 3, expected 6
# The pak had been hand-staged into the live NFS export that morning, never into
# the rootfs-overlay, so recreating the export from the build tree correctly
# discarded it -- and nothing checked. The old completeness check printed "MISS"
# and exited 0, which is indistinguishable from success in a build log.
#
# One list, called from both the export path and the ext2 image path, because a
# list maintained in two places is how this class of bug survives.
#
#   ./scripts/check-rootfs-complete.sh <rootfs-dir>
#
# Exit 0 only when every required path is present and non-empty.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

root="${1:-}"
[ -n "${root}" ] && [ -d "${root}" ] || {
	printf 'check-rootfs-complete: usage: %s <rootfs-dir>\n' "$0" >&2
	exit 2
}

# Required = the image is broken or a headline feature is missing without it.
# Game DATA belongs here as much as the binaries: an engine with no data is not
# a shipped game, and that is precisely the failure this script was written for.
# The GPU stack's programs. The servers are started at boot from loader.disk; the
# rootfs copies are the ones psh can run.
GPU_REQUIRED=(
	sbin/rpi4-v3d-async
	sbin/rpi4-kms
	bin/shmsrv
	bin/Xorg-drm
	bin/startx
	etc/X11/xorg-drm.conf
	bin/eglx11-demo-x
	# the engines and their launchers (the commands)
	usr/bin/quakespasm-drm
	usr/bin/vkquake-drm
	usr/bin/yquake2-drm
	usr/bin/quake3e-drm
	usr/bin/supertuxkart-drm
	usr/bin/quakespasm
	usr/bin/vkquake
	usr/bin/quake2
	usr/bin/quake3
	bin/stk
	# the lower-resolution launcher (build-rootfs-helpers.sh)
	bin/game-res
	# the Wayland desktop (labwc_desktop, dbus, xfce_wayland) and the GPU smoke tests
	bin/xfce-session
	bin/xfce-desktop.sh
	bin/xfce-autostart.sh
	bin/labwc
	bin/foot
	bin/thunar
	bin/xfce4-panel
	bin/xfdesktop
	bin/dbus-daemon
	bin/kmscube
	bin/vkcube
	bin/drmprobe
)
# The desktop applications (docs/gpu-new-lane/desktop-apps-ports.md): the games' window
# launcher and session (sdl2_kmsdrm), the video players (video_player), the PDF viewer
# (atril_wayland) and their XFCE menu entries.
DESKTOP_REQUIRED=(
	bin/game-window.sh
	bin/game-window-autostart.sh
	bin/game-window-quit.sh
	etc/xdg/labwc-xfce-games/rc.xml
	etc/xdg/labwc-xfce-games/menu.xml
	etc/xdg/labwc-xfce-games/autostart
	etc/xdg/labwc-xfce-games/environment
	usr/bin/ffplay
	bin/video-play
	usr/bin/gtk-video
	etc/xdg/labwc-xfce-video/rc.xml
	etc/xdg/labwc-xfce-video/autostart
	usr/share/video-demo/h264-720p30-aac.mp4
	usr/share/video-demo/h264-1080p30-aac.mp4
	usr/share/video-demo/hevc-720p30-aac.mp4
	usr/share/video-demo/vp9-360p-opus.webm
	usr/bin/atril
	usr/share/atril/schemas/gschemas.compiled
	usr/share/doc/phoenix/sample.pdf
	usr/share/applications/quakespasm.desktop
	usr/share/applications/quake2.desktop
	usr/share/applications/quake3.desktop
	usr/share/applications/stk.desktop
	usr/share/applications/gtk-video.desktop
	usr/share/applications/video-demo.desktop
	usr/share/applications/atril.desktop
)

REQUIRED=(
	bin/psh
	bin/busybox
	"${GPU_REQUIRED[@]}"
	"${DESKTOP_REQUIRED[@]}"
	usr/share/quake/id1/pak0.pak
	# Shipped Quake settings live in autoexec.cfg, not config.cfg: quake.rc execs
	# default.cfg -> config.cfg -> autoexec.cfg, so autoexec is read every start and
	# wins without clobbering the game's own file (which a fresh rootfs correctly
	# does not have, and which vkQuake shadows with vkQuake.cfg anyway).
	usr/share/quake/id1/autoexec.cfg
	usr/share/quake2/baseq2/pak0.pak
	usr/share/quake3/demoq3/pak0.pk3
	usr/share/quake3/demoq3/pak1.pk3
	usr/share/quake3/demoq3/q3key
	usr/share/supertuxkart/data/stk_config.xml
	# Trusted root CA store (ca_certificates port). Without these two files every
	# TLS client we ship silently falls back to "no trust anchors": Dillo reports
	# "Trusting 0 TLS certificates." and refuses real https:// sites, and
	# openssl/python3's ssl module have no default CAfile. Two REAL copies, not a
	# symlink -- see ca_certificates/port.def.sh for which consumer reads which.
	etc/ssl/certs/ca-certificates.crt
	etc/ssl/cert.pem
	# WiFi: the daemon (also started from loader.disk), the user's client, the example
	# configuration and the vendor firmware (scripts/fetch-wifi-firmware.sh; an offline
	# build with an empty download cache fails here on purpose: the image's WiFi would
	# not come up) with its licence files.
	sbin/rpi4-wifi
	bin/wifi
	etc/wifi.conf.example
	lib/firmware/brcm/brcmfmac43455-sdio.bin
	lib/firmware/brcm/brcmfmac43455-sdio.clm_blob
	lib/firmware/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt
	lib/firmware/LICENSES/LICENCE.cypress
	lib/firmware/LICENSES/GPL-2.0
	lib/firmware/WHENCE
)

# Expected but not fatal: launchers and conveniences. Reported, never silent.
OPTIONAL=(
	bin/xterm
	bin/wmaker
	bin/ram-stage-play
	bin/python3
	bin/bash
	bin/nano
	bin/mc
)

missing_req=0
missing_opt=0

printf '== rootfs completeness: %s ==\n' "${root}"
for p in "${REQUIRED[@]}"; do
	if [ -s "${root}/${p}" ]; then
		printf '  OK    %s\n' "${p}"
	else
		printf '  ABSENT %s   <-- REQUIRED\n' "${p}"
		missing_req=$((missing_req + 1))
	fi
done
for p in "${OPTIONAL[@]}"; do
	if [ -s "${root}/${p}" ]; then
		printf '  ok    %s\n' "${p}"
	else
		printf '  absent %s   (optional)\n' "${p}"
		missing_opt=$((missing_opt + 1))
	fi
done

# stk-assets is a directory of ~149 MB, so check it is populated rather than
# naming one file inside it (which would rot).
if [ -d "${root}/usr/share/supertuxkart/stk-assets/karts" ] &&
	[ -n "$(ls -A "${root}/usr/share/supertuxkart/stk-assets/karts" 2>/dev/null)" ]; then
	printf '  OK    usr/share/supertuxkart/stk-assets/karts (populated)\n'
else
	printf '  ABSENT usr/share/supertuxkart/stk-assets/karts   <-- REQUIRED\n'
	missing_req=$((missing_req + 1))
fi

# The GL games and ffplay are ONE program each with SDL's KMSDRM AND Wayland drivers (full
# screen from psh, a window on the desktop): an engine without both is a stale or wrong build.
for p in usr/bin/quakespasm-drm usr/bin/yquake2-drm usr/bin/quake3e-drm usr/bin/supertuxkart-drm usr/bin/ffplay; do
	[ -s "${root}/${p}" ] || continue
	for s in 'KMS/DRM Video Driver' 'SDL Wayland video driver'; do
		if [ "$(grep -a -c -F -- "${s}" "${root}/${p}" 2>/dev/null || true)" -ge 1 ]; then
			printf '  OK    %s: %s\n' "${p}" "${s}"
		else
			printf '  NO    %s: %s   <-- REQUIRED (one dual-mode program)\n' "${p}" "${s}"
			missing_req=$((missing_req + 1))
		fi
	done
done
# The lab's credentials live only on the NFS export; one in a built rootfs would
# ship in every image made from it.
if [ -e "${root}/etc/wifi.conf" ]; then
	printf '  FOUND etc/wifi.conf   <-- must NOT be in a built rootfs (credentials)\n'
	missing_req=$((missing_req + 1))
fi

printf '\n'
if [ "${missing_req}" -gt 0 ]; then
	printf 'INCOMPLETE: %d required path(s) missing, %d optional.\n' "${missing_req}" "${missing_opt}"
	printf 'Game data missing? Run: ./scripts/stage-game-data.sh all   (local builds do NOT\n'
	printf 'run it -- only the Dockerfile does, so a stale rootfs-overlay persists silently.)\n'
	exit 1
fi
printf 'COMPLETE: all %d required paths present (%d optional missing).\n' "${#REQUIRED[@]}" "${missing_opt}"
exit 0

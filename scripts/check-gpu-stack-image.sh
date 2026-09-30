#!/usr/bin/env bash
#
# check-gpu-stack-image.sh — the image gate of the GPU migration
# (docs/gpu-new-lane/MIGRATION.md §7, P3-removal.md): does a built image carry the GPU
# stack, nothing of the first (deleted) stack, and start its servers at boot?
#
#   ./scripts/check-gpu-stack-image.sh [--root <rootfs-dir>] [--loader <loader.disk>]
#
# Defaults: the staged rootfs .buildroot/_fs/<target>/root and the image's
# .buildroot/_boot/<target>/rpi4b-bootfs/loader.disk (TFTP). Point --root at an NFS export (for
# example a pristine one, scripts/make-pristine-nfs-export.sh) to check what the Pi
# will actually boot. Reads only; exit 0 = every check passed.
#
# Checks, in order (each prints OK / FAIL lines, the summary counts FAILs):
#   1. loader.disk: the three servers are in the boot blob (their ready-line format
#      strings), the rpi4-kms build is the one with the -G wait and the M9 scaled
#      modes (stale-core hazard — an `auto` rebuild after a committed devices change
#      ships the old one), and rpi4-fb (/dev/fb0) is not.
#   2. rootfs: the servers, the GPU-stack programs under their command names and
#      /bin/game-res; startx and xfce-session are the session scripts themselves; the engines
#      carry the GPU-stack banner; the desktop applications (the games' window launcher + session, the
#      video players, Atril, their XFCE menu entries, the demo clips) and WiFi (daemon,
#      client, example configuration, vendor firmware + licences); every GL game engine
#      and ffplay ONE program with SDL's KMSDRM AND Wayland drivers.
#   3. rootfs: none of the first stack's program files, and none of the superseded
#      desktop-app builds or hand-staged test names (the -wl game clones, ffplay-drm/-wl,
#      video-play2, *-2 / *-low / -g<N> servers and sessions), and none of the retired names
#      (the *-drm launcher copies, startx-drm, startx_gpu, thunar-wl, gdbus-wl, vkcube-drm,
#      the xfce-session wrapper's target). Nothing builds them any
#      more, so one present is a stale file of an old build in the persistent staging
#      tree (or a hand-staged NFS export): delete it, or make a pristine export.
#   4. rootfs: the old stack's strings in any ELF under bin sbin usr/bin usr/sbin
#      usr/lib: `v3d-winsys:`, `phxgl`, `V3DV_PHOENIX`, `/dev/v3d-srv`,
#      `RPI4FB_GETMODE` and `/dev/fb0` = 0. `grep -a -c`, never -q (a binary
#      match with -q and pipefail reads as absent). Known exceptions are listed
#      below and reported, never silently skipped.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
root="${buildroot}/_fs/${target}/root"
loader="${buildroot}/_boot/${target}/rpi4b-bootfs/loader.disk"   # the one TFTP serves

while [ "$#" -gt 0 ]; do
	case "$1" in
		--root) root="${2:?--root needs a directory}"; shift 2 ;;
		--loader) loader="${2:?--loader needs a file}"; shift 2 ;;
		-h|--help) sed -n '2,30p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "check-gpu-stack-image: unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -d "${root}/bin" ] || { echo "check-gpu-stack-image: no rootfs at ${root}" >&2; exit 2; }

fails=0
ok()   { printf '  OK    %s\n' "$*"; }
fail() { printf '  FAIL  %s\n' "$*"; fails=$((fails + 1)); }
note() { printf '  NOTE  %s\n' "$*"; }
count() { grep -a -c -F -- "$1" "$2" 2>/dev/null || true; }

echo "== 1. boot blob: ${loader} =="
if [ ! -s "${loader}" ]; then
	fail "loader.disk missing or empty"
else
	for spec in \
		"V3DA srv ready dev=|rpi4-v3d-async" \
		"srv ready dev=/dev/%s buf=%s backend=|rpi4-kms" \
		"srv ready ns=%s port=|shmsrv" \
		"why=no_/dev/%s waited_ms=|rpi4-kms with the -G wait (not a stale core build)" \
		"modes=%u scaler=%s|rpi4-kms with the M9 scaled modes, g9 (not a stale core build)"; do
		n=$(count "${spec%%|*}" "${loader}")
		if [ "${n:-0}" -ge 1 ]; then ok "${spec#*|} in loader.disk"; else fail "${spec#*|} NOT in loader.disk ('${spec%%|*}')"; fi
	done
	n=$(count "rpi4-wifi" "${loader}")
	if [ "${n:-0}" -ge 1 ]; then ok "rpi4-wifi in loader.disk"; else fail "rpi4-wifi NOT in loader.disk"; fi
	n=$(count "registered /dev/fb0" "${loader}")
	if [ "${n:-0}" = 0 ]; then ok "rpi4-fb not in loader.disk"; else fail "rpi4-fb is in loader.disk (a stale boot script?)"; fi
fi

echo "== 2. the GPU stack in the rootfs: ${root} =="
for p in sbin/rpi4-v3d-async sbin/rpi4-kms bin/shmsrv \
	usr/bin/quakespasm-drm usr/bin/quakespasm usr/bin/yquake2-drm usr/bin/quake2 usr/bin/quake3e-drm \
	usr/bin/quake3 usr/bin/vkquake-drm usr/bin/vkquake usr/bin/supertuxkart-drm bin/stk \
	bin/Xorg-drm bin/startx bin/eglx11-demo-x etc/X11/xorg-drm.conf \
	bin/labwc bin/foot bin/labwc-desktop.sh bin/xfce-session bin/xfce-desktop.sh bin/xfce-autostart.sh \
	bin/thunar bin/gdbus bin/xfce4-panel bin/xfdesktop bin/dbus-daemon usr/lib/xfce-demo/bin/loginctl \
	bin/kmscube bin/vkcube bin/drmprobe bin/game-res; do
	if [ -s "${root}/${p}" ]; then ok "${p}"; else fail "${p} missing"; fi
done
# The session scripts are installed under their command names (not wrappers of another name).
for spec in "bin/startx|XDRM start mode=" "bin/xfce-session|XFCE-SESSION start"; do
	name="${spec%%|*}"
	if [ -s "${root}/${name}" ] && [ "$(count "${spec#*|}" "${root}/${name}")" -ge 1 ]; then
		ok "${name} is the session script"
	else
		fail "${name} is not the session script ('${spec#*|}'): a stale wrapper?"
	fi
done
# Each launcher runs its GPU-stack engine (an old-stack launcher of the same name would not).
for pair in usr/bin/quakespasm:quakespasm-drm usr/bin/quake2:yquake2-drm usr/bin/quake3:quake3e-drm \
	usr/bin/vkquake:vkquake-drm bin/stk:supertuxkart-drm; do
	name="${pair%%:*}"
	[ -s "${root}/${name}" ] || continue
	if [ "$(count "/usr/bin/${pair#*:}" "${root}/${name}")" -ge 1 ]; then
		ok "${name} runs /usr/bin/${pair#*:}"
	else
		fail "${name} does not run /usr/bin/${pair#*:} (a stale launcher?)"
	fi
done
# Engine banners: an old engine staged under a -drm name would have none.
for spec in usr/bin/quakespasm-drm:quakespasm-drm usr/bin/yquake2-drm:quake2-drm \
	usr/bin/quake3e-drm:quake3-drm usr/bin/vkquake-drm:vkquake-drm usr/bin/supertuxkart-drm:stk-drm; do
	f="${root}/${spec%%:*}"
	[ -s "${f}" ] || continue
	if [ "$(count "${spec#*:}: new GPU lane" "${f}")" -ge 1 ]; then ok "${spec%%:*} banner"; else fail "${spec%%:*} has no '${spec#*:}: new GPU lane' banner"; fi
done

echo "== 2b. the desktop applications and WiFi in the rootfs =="
for p in bin/game-window.sh bin/game-window-autostart.sh bin/game-window-quit.sh \
	etc/xdg/labwc-xfce-games/rc.xml etc/xdg/labwc-xfce-games/menu.xml etc/xdg/labwc-xfce-games/autostart \
	etc/xdg/labwc-xfce-games/environment usr/bin/ffplay bin/video-play usr/bin/gtk-video \
	etc/xdg/labwc-xfce-video/rc.xml etc/xdg/labwc-xfce-video/autostart \
	usr/share/video-demo/h264-720p30-aac.mp4 usr/share/video-demo/h264-1080p30-aac.mp4 \
	usr/share/video-demo/hevc-720p30-aac.mp4 usr/share/video-demo/vp9-360p-opus.webm \
	usr/bin/atril usr/share/atril/schemas/gschemas.compiled usr/share/doc/phoenix/sample.pdf \
	usr/share/applications/quakespasm.desktop usr/share/applications/quake2.desktop \
	usr/share/applications/quake3.desktop usr/share/applications/stk.desktop \
	usr/share/applications/gtk-video.desktop usr/share/applications/video-demo.desktop \
	usr/share/applications/atril.desktop \
	sbin/rpi4-wifi bin/wifi etc/wifi.conf.example \
	lib/firmware/brcm/brcmfmac43455-sdio.bin lib/firmware/brcm/brcmfmac43455-sdio.clm_blob \
	lib/firmware/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt \
	lib/firmware/LICENSES/LICENCE.cypress lib/firmware/LICENSES/GPL-2.0 lib/firmware/WHENCE; do
	if [ -s "${root}/${p}" ]; then ok "${p}"; else fail "${p} missing"; fi
done
if [ -e "${root}/etc/wifi.conf" ]; then fail "etc/wifi.conf present (credentials must not be in a built rootfs)"; else ok "etc/wifi.conf absent"; fi
# One program per game / player: full screen on KMS from psh, a window on the desktop.
for p in usr/bin/quakespasm-drm usr/bin/yquake2-drm usr/bin/quake3e-drm usr/bin/supertuxkart-drm usr/bin/ffplay; do
	[ -s "${root}/${p}" ] || continue
	for s in 'KMS/DRM Video Driver' 'SDL Wayland video driver' 'EGL_KHR_platform_wayland'; do
		if [ "$(count "${s}" "${root}/${p}")" -ge 1 ]; then ok "${p}: '${s}'"; else fail "${p} lacks '${s}' (not the dual-mode build)"; fi
	done
done
# Every menu entry's program is installed (Exec's first absolute path, after /bin/bash).
for f in "${root}"/usr/share/applications/{quakespasm,quake2,quake3,stk,gtk-video,video-demo,atril}.desktop; do
	[ -s "${f}" ] || continue
	e="$(sed -n 's/^Exec=//p' "${f}" | head -1)"
	e="${e#/bin/bash }"
	e="${e%% *}"
	if [ -s "${root}${e}" ]; then ok "${f#"${root}"/}: Exec ${e}"; else fail "${f#"${root}"/}: Exec ${e} not installed"; fi
done

echo "== 3. no file of the first GPU stack, of a superseded build or of a hand-staged test =="
# The kdrive X server, the old engines, the glamor X daemon, the GL-in-X client, xlaunch,
# the /dev/fb0 probe, the old GPU daemon and the /dev/fb0 server.
for p in usr/bin/Xphoenix usr/bin/yquake2 usr/bin/quake3e usr/bin/supertuxkart \
	bin/Xphoenix-glamor-daemon bin/gl-x11-window-daemon bin/pl_phoenix_xlaunch bin/fbprobe \
	sbin/rpi4-v3d sbin/rpi4-fb \
	usr/bin/quakespasm-wl usr/bin/yquake2-wl usr/bin/quake2-wl usr/bin/quake3e-wl usr/bin/quake3-wl \
	usr/bin/supertuxkart-wl bin/stk-wl usr/bin/ffplay-drm usr/bin/ffplay-wl usr/bin/ffplay-drm2 \
	usr/bin/ffplay-wl2 bin/video-play2 bin/atril-wl bin/xfce-desktop-atril.sh bin/foot-2 bin/labwc-2 \
	bin/fuzzel-2 bin/xfce-session-2 bin/xfce-desktop-2.sh bin/rpi4-v3d-async-low bin/rpi4-kms-g7 \
	bin/rpi4-kms-g8 bin/rpi4-kms-g9 bin/weston-simple-egl-low etc/xdg/labwc-xfce-m8 usr/share/m10 \
	bin/Xorg-drm-noshim bin/v3dmemprobe \
	bin/qs-drm usr/bin/quake2-drm usr/bin/quake3-drm bin/vkq-drm bin/stk-drm bin/startx-drm bin/startx_gpu \
	bin/thunar-wl bin/gdbus-wl bin/vkcube-drm usr/lib/xfce-demo/xfce-session; do
	if [ -e "${root}/${p}" ] || [ -L "${root}/${p}" ]; then fail "${p} present (a stale file of an old build: the first stack, a superseded build or a retired name; remove it)"; else ok "${p} absent"; fi
done

echo "== 4. old-stack strings in the rootfs ELFs =="
# Known exceptions: reported as NOTE, not counted. Each has a TD and a plan.
#   bin/hevc-play  (TD-27) rpivid decoder writing /dev/fb0; built by hand from
#                  tools/hevc-decode, not by the image build: port it to a KMS dumb
#                  buffer, then delete video/rpi4-fb.
allow_fb0=(bin/hevc-play)
elves=()
while IFS= read -r -d '' f; do
	# ELF magic only; scripts and data are not programs.
	if [ "$(head -c 4 "${f}" 2>/dev/null | od -An -c | tr -d ' ')" = "177ELF" ]; then
		elves+=("${f}")
	fi
done < <(find "${root}/bin" "${root}/sbin" "${root}/usr/bin" "${root}/usr/sbin" "${root}/usr/lib" \
	-type f -print0 2>/dev/null)
printf '  (%d ELF files scanned)\n' "${#elves[@]}"
for s in 'v3d-winsys:' 'phxgl' 'V3DV_PHOENIX' '/dev/v3d-srv' 'RPI4FB_GETMODE' '/dev/fb0'; do
	hits=0
	for f in "${elves[@]}"; do
		n=$(count "${s}" "${f}")
		[ "${n:-0}" -gt 0 ] || continue
		rel="${f#"${root}"/}"
		if [ "${s}" = '/dev/fb0' ] && printf '%s\n' "${allow_fb0[@]}" | grep -qxF "${rel}"; then
			note "'${s}' x${n} in ${rel} (known exception, TD-27)"
			continue
		fi
		fail "'${s}' x${n} in ${rel}"
		hits=$((hits + 1))
	done
	[ "${hits}" -gt 0 ] || ok "'${s}': 0"
done

echo
if [ "${fails}" = 0 ]; then
	echo "RESULT: PASS — the image carries the GPU stack only, servers in the boot blob"
	exit 0
fi
echo "RESULT: FAIL — ${fails} check(s) failed (above)"
exit 1

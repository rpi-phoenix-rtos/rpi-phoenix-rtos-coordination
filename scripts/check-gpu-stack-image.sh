#!/usr/bin/env bash
#
# check-gpu-stack-image.sh — the P1 image gate of the GPU migration
# (docs/gpu-new-lane/MIGRATION.md §7): does a built default image carry the GPU
# stack, only that stack, and start its servers at boot?
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
#      strings), the rpi4-kms build is the one with the -G wait (TD: stale-core
#      hazard — an `auto` rebuild after a committed devices change ships the old
#      one), and the legacy boot pieces (rpi4-fb) are not.
#   2. rootfs: the servers, the new-stack programs and the plain command names;
#      each plain name is the new-stack program (cmp) or its wrapper.
#   3. rootfs: no file only the legacy stack produces (the prune list of
#      build-showcase-apps.sh; a stale one means the prune did not run).
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
		"why=no_/dev/%s waited_ms=|rpi4-kms with the -G wait (not a stale core build)"; do
		n=$(count "${spec%%|*}" "${loader}")
		if [ "${n:-0}" -ge 1 ]; then ok "${spec#*|} in loader.disk"; else fail "${spec#*|} NOT in loader.disk ('${spec%%|*}')"; fi
	done
	n=$(count "registered /dev/fb0" "${loader}")
	if [ "${n:-0}" = 0 ]; then ok "rpi4-fb not in loader.disk"; else fail "rpi4-fb is in loader.disk (a legacy boot script?)"; fi
fi

echo "== 2. the GPU stack in the rootfs: ${root} =="
for p in sbin/rpi4-v3d-async sbin/rpi4-kms bin/shmsrv \
	usr/bin/quakespasm-drm usr/bin/yquake2-drm usr/bin/quake2-drm usr/bin/quake3e-drm \
	usr/bin/quake3-drm usr/bin/vkquake-drm bin/vkq-drm usr/bin/supertuxkart-drm bin/stk-drm \
	bin/Xorg-drm bin/Xorg-drm-noshim bin/startx-drm bin/eglx11-demo-x etc/X11/xorg-drm.conf \
	bin/labwc bin/foot bin/labwc-desktop.sh bin/xfce-desktop.sh bin/thunar-wl bin/xfce4-panel \
	bin/xfdesktop bin/dbus-daemon usr/lib/xfce-demo/xfce-session usr/lib/xfce-demo/bin/loginctl \
	bin/kmscube bin/vkcube-drm bin/drmprobe; do
	if [ -s "${root}/${p}" ]; then ok "${p}"; else fail "${p} missing"; fi
done
# The plain command names (TD-26): copies of the -drm programs, or wrappers.
for pair in usr/bin/quakespasm:usr/bin/quakespasm-drm usr/bin/quake2:usr/bin/quake2-drm \
	usr/bin/quake3:usr/bin/quake3-drm usr/bin/vkquake:bin/vkq-drm bin/stk:bin/stk-drm; do
	name="${pair%%:*}"; prog="${pair#*:}"
	if [ -s "${root}/${name}" ] && cmp -s "${root}/${name}" "${root}/${prog}"; then
		ok "${name} = ${prog}"
	else
		fail "${name} is not ${prog} (missing, or another program: the legacy launcher?)"
	fi
done
for spec in "bin/startx|exec /bin/bash /bin/startx-drm" "bin/startx_gpu|exec /bin/bash /bin/startx-drm" \
	"bin/xfce-session|exec /bin/bash /usr/lib/xfce-demo/xfce-session"; do
	name="${spec%%|*}"
	if [ -s "${root}/${name}" ] && [ "$(count "${spec#*|}" "${root}/${name}")" -ge 1 ]; then
		ok "${name} runs ${spec#*exec /bin/bash }"
	else
		fail "${name} is not the new-stack wrapper ('${spec#*|}')"
	fi
done
# Engine banners: an old engine staged under a -drm name would have none.
for spec in usr/bin/quakespasm-drm:quakespasm-drm usr/bin/yquake2-drm:quake2-drm \
	usr/bin/quake3e-drm:quake3-drm usr/bin/vkquake-drm:vkquake-drm usr/bin/supertuxkart-drm:stk-drm; do
	f="${root}/${spec%%:*}"
	[ -s "${f}" ] || continue
	if [ "$(count "${spec#*:}: new GPU lane" "${f}")" -ge 1 ]; then ok "${spec%%:*} banner"; else fail "${spec%%:*} has no '${spec#*:}: new GPU lane' banner"; fi
done

echo "== 3. no legacy-only file =="
# = build-showcase-apps.sh legacy_gpu_files (keep the two lists together).
for p in usr/bin/Xphoenix usr/bin/yquake2 usr/bin/quake3e usr/bin/supertuxkart \
	bin/Xphoenix-glamor-daemon bin/gl-x11-window-daemon bin/pl_phoenix_xlaunch bin/fbprobe \
	sbin/rpi4-v3d sbin/rpi4-fb; do
	if [ -e "${root}/${p}" ] || [ -L "${root}/${p}" ]; then fail "${p} present (legacy stack; the stage-phase prune did not run?)"; else ok "${p} absent"; fi
done

echo "== 4. old-stack strings in the rootfs ELFs =="
# Known exceptions: reported as NOTE, not counted. Each has a TD and a plan.
#   bin/hevc-play  (TD-27) rpivid decoder writing /dev/fb0; built by hand from
#                  tools/hevc-decode, not by the image build: port it to a KMS dumb
#                  buffer before P3.
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

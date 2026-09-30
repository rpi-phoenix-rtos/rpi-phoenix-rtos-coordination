#!/usr/bin/env bash
#
# check-wayland-ports-sync.sh — are the new-lane Wayland-desktop ports' vendored copies still
# identical to their tools/gpu-lane sources?
#
# The Wayland desktop of the new GPU lane (dbus, the Wayland base, GTK 3, XFCE, labwc) was
# developed in tools/gpu-lane/*/ (standalone build.sh scripts) and is now also stored as
# phoenix-rtos-ports framework ports (docs/gpu-new-lane/MIGRATION.md section 4, "Ports (Wayland
# desktop)"). Every patch, glue source, launcher and config those ports carry is a COPY of a file
# here. Until the tools/ side is retired both exist; this script proves they have not drifted:
#
#   scripts/check-wayland-ports-sync.sh [<ports dir>]     (default sources/phoenix-rtos-ports)
#
# Same mapping format as scripts/check-gpu-lane-ports-sync.sh (the graphics half):
# "<source>|<port path>"; a directory is compared recursively (diff -r), a file with cmp.
# It compares the WORKING TREE of the coordination repo: an uncommitted patch in a tools
# directory shows up as drift, which is what it is until the port gets the same file.
#
# Not copies, so not listed: the recipes; gtk3_wayland/files/egl-include (the Khronos EGL/KHR
# headers as Mesa 26.2.0 installs them, taken from a mesa-drm build prefix); the committed
# keymaps wayland_phoenix/files/keymap-us.xkb and labwc_desktop/files/keymap-us.xkb (build
# outputs the tools scripts regenerate on the build host); wayland_phoenix/files/
# input-event-codes.h (FreeBSD's, sha256-pinned in the recipe).
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ports="${1:-${repo_root}/sources/phoenix-rtos-ports}"
T=tools/gpu-lane

MAP=(
	# dbus
	"${T}/dbus/patches/dbus|dbus/patches"
	"${T}/dbus/conf|dbus/files/conf"
	# wayland_phoenix (the M6 Wayland base of weston-drm, + mesa-drm's generic gap headers)
	"${T}/weston-drm/patches/wayland|wayland_phoenix/patches/wayland"
	"${T}/weston-drm/patches/seatd|wayland_phoenix/patches/seatd"
	"${T}/weston-drm/compat|wayland_phoenix/files/compat"
	"${T}/weston-drm/shims|wayland_phoenix/files/shims"
	"${T}/weston-drm/shmsrv/shm_proto.h|wayland_phoenix/files/shmsrv/shm_proto.h"
	"${T}/mesa-drm/compat/include|wayland_phoenix/files/mesa-compat/include"
	"${T}/xorg-drm/src/phxhid_evdev_map.h|wayland_phoenix/files/phxhid/phxhid_evdev_map.h"
	# gtk3_wayland (+ xorg-drm's libepoxy patch and compat headers)
	"${T}/gtk3-wayland/patches/fribidi|gtk3_wayland/patches/fribidi"
	"${T}/gtk3-wayland/patches/gdk-pixbuf|gtk3_wayland/patches/gdk-pixbuf"
	"${T}/gtk3-wayland/patches/glib|gtk3_wayland/patches/glib"
	"${T}/gtk3-wayland/patches/gtk|gtk3_wayland/patches/gtk"
	"${T}/xorg-drm/patches/libepoxy|gtk3_wayland/patches/libepoxy-1.5.10"
	"${T}/xorg-drm/compat/include|gtk3_wayland/files/epoxy-compat/include"
	"${T}/gtk3-wayland/src|gtk3_wayland/files/src"
	"${T}/gtk3-wayland/conf/settings.ini|gtk3_wayland/files/conf/settings.ini"
	# xfce_wayland
	"${T}/xfce-wayland/patches|xfce_wayland/patches"
	"${T}/xfce-wayland/compat|xfce_wayland/files/compat"
	"${T}/xfce-wayland/conf/applications|xfce_wayland/files/conf/applications"
	"${T}/xfce-wayland/conf/labwc-xfce|xfce_wayland/files/conf/labwc-xfce"
	"${T}/xfce-wayland/conf/xfconf|xfce_wayland/files/conf/xfconf"
	"${T}/xfce-wayland/pi/xfce-desktop.sh|xfce_wayland/files/pi/xfce-desktop.sh"
	# the one-command session /bin/xfce-session (GPU migration P1)
	"${T}/xfce-wayland/pi/xfce-session|xfce_wayland/files/pi/xfce-session"
	"${T}/xfce-wayland/pi/xfce-demo-loginctl|xfce_wayland/files/pi/xfce-demo-loginctl"
	"${T}/xfce-wayland/conf/labwc-xfce-demo|xfce_wayland/files/conf/labwc-xfce-demo"
	"${T}/xfce-wayland/conf/xfce-demo|xfce_wayland/files/conf/xfce-demo"
	"${T}/xfce-wayland/bin/msgfmt|xfce_wayland/files/bin/msgfmt"
	"${T}/xfce-wayland/tools/pngify-icon-theme.py|xfce_wayland/files/tools/pngify-icon-theme.py"
	# labwc_desktop
	"${T}/labwc-drm/patches|labwc_desktop/patches"
	"${T}/labwc-drm/compat|labwc_desktop/files/compat"
	"${T}/labwc-drm/shims|labwc_desktop/files/shims"
	"${T}/labwc-drm/src|labwc_desktop/files/src"
	"${T}/labwc-drm/conf/applications|labwc_desktop/files/conf/applications"
	"${T}/labwc-drm/conf/foot|labwc_desktop/files/conf/foot"
	"${T}/labwc-drm/conf/fuzzel|labwc_desktop/files/conf/fuzzel"
	"${T}/labwc-drm/conf/rc.xml|labwc_desktop/files/conf/rc.xml"
	"${T}/labwc-drm/conf/menu.xml|labwc_desktop/files/conf/menu.xml"
	"${T}/labwc-drm/conf/autostart|labwc_desktop/files/conf/autostart"
	"${T}/labwc-drm/conf/environment|labwc_desktop/files/conf/environment"
	"${T}/labwc-drm/conf/backgrounds/make-wallpaper.py|labwc_desktop/files/conf/backgrounds/make-wallpaper.py"
	# atril_wayland (M7 m7j)
	"${T}/atril-wayland/patches|atril_wayland/patches"
	"${T}/atril-wayland/poppler-options.sh|atril_wayland/files/poppler-options.sh"
	"${T}/atril-wayland/conf/atril.desktop|atril_wayland/files/conf/atril.desktop"
	"${T}/atril-wayland/tools/make-sample-pdf.py|atril_wayland/files/tools/make-sample-pdf.py"
	"${T}/xfce-wayland/bin/msgfmt|atril_wayland/files/bin/msgfmt"
)

bad=0 n=0 files=0
for m in "${MAP[@]}"; do
	src="${repo_root}/${m%%|*}" dst="${ports}/${m#*|}"
	n=$((n + 1))
	if [ -d "${src}" ]; then
		[ -d "${dst}" ] || { echo "MISSING dir ${m#*|}"; bad=1; continue; }
		# __pycache__ and editor droppings are not part of either copy
		if ! diff -r -x __pycache__ -x '*.pyc' "${src}" "${dst}" >/dev/null; then
			echo "DIFFERS ${m}"
			diff -rq -x __pycache__ -x '*.pyc' "${src}" "${dst}" | sed 's/^/    /' || true
			bad=1
		fi
		files=$((files + $(find "${src}" -type f ! -path '*/__pycache__/*' | wc -l)))
	else
		[ -f "${dst}" ] || { echo "MISSING ${m#*|}"; bad=1; continue; }
		cmp -s "${src}" "${dst}" || { echo "DIFFERS ${m}"; bad=1; }
		files=$((files + 1))
	fi
done
if [ "${bad}" = 0 ]; then
	echo "wayland ports sync: ${files} files, ${n} mappings: identical (${ports})"
else
	echo "wayland ports sync: DRIFT (see above) -- copy the changed file into the port, or back"
fi
exit "${bad}"

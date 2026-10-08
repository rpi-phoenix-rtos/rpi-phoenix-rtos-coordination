#!/bin/bash
#
# stage-gtk-paint.sh -- put the GTK paint-watch gate on the Pi's NFS root (host side, no sudo):
#
#     tools/browser/webkitgtk/stage-gtk-paint.sh [--export DIR]
#
#   <export>/usr/share/gate/gtk-paint.sh          the runner (pi/gtk-paint.sh)
#   <export>/usr/share/browser-bench/bench.sh     the benchmark suite's runner (browser=gtk, stats=S)
# and checks what the gate needs: an image whose webkit-browser has the paint watch (ports branch
# gtk-frame-watch: launcher b10-r4, WebKit patch webkit-gtk/0105), the 1080p60 clip, b8.html, the
# media server for the HLS page. Default export: the live one, /srv/phoenix-rpi4-nfs-gcc16.
#
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../.." && pwd)
export_root=/srv/phoenix-rpi4-nfs-gcc16
while [ $# -gt 0 ]; do
	case "$1" in
		--export) export_root=$2; shift 2 ;;
		*) echo "usage: $0 [--export DIR]" >&2; exit 2 ;;
	esac
done
[ -d "${export_root}/usr/share" ] || { echo "stage: ${export_root}: not an NFS root" >&2; exit 1; }

install -D -m 755 "${here}/pi/gtk-paint.sh" "${export_root}/usr/share/gate/gtk-paint.sh"
echo "stage: ${export_root}/usr/share/gate/gtk-paint.sh"
if [ -d "${export_root}/usr/share/browser-bench" ]; then
	install -m 755 "${repo}/tools/browser/bench/pi/bench.sh" "${export_root}/usr/share/browser-bench/bench.sh"
	echo "stage: ${export_root}/usr/share/browser-bench/bench.sh"
else
	echo "stage: WARNING no ${export_root}/usr/share/browser-bench (tools/browser/bench/stage.sh first): no MotionMark arms"
fi

bad=0
check_strings() {  # check_strings <binary> <string...>
	local bin=$1 s
	shift
	if [ ! -f "${bin}" ]; then
		echo "stage: MISSING ${bin}"
		bad=1
		return
	fi
	for s in "$@"; do
		if ! grep -qaF -- "${s}" "${bin}"; then
			echo "stage: ${bin} lacks '${s}' (an image without the gtk-frame-watch branch?)"
			bad=1
		fi
	done
}
check_strings "${export_root}/usr/bin/webkit-browser" 'b10-r4' 'gtk-paint %s' 'frame-watch-web pid=%d' 'frame-watch-ui %s' \
	'WPEB-WEBKIT gtk-paint import pid=%d' 'frame-trace'
check_strings "${export_root}/usr/bin/wpe-browser" 'present frames=%u fps=%.1f'
for f in usr/share/video-demo/rpivid-check/real-peertube-1080.mp4 usr/share/wpe-browser/b8.html; do
	[ -f "${export_root}/${f}" ] || { echo "stage: MISSING /${f}"; bad=1; }
done
if curl -s -m 5 -o /dev/null http://10.42.0.1:8091/phx-ping; then
	echo "stage: media server up (http://10.42.0.1:8091)"
else
	echo "stage: media server DOWN: tools/browser/media/serve-for-pi.sh start (the hls30 arms need it)"
fi
if curl -s -m 5 -o /dev/null http://10.42.0.1:8090/phx-ping; then
	echo "stage: bench server up (http://10.42.0.1:8090)"
else
	echo "stage: bench server DOWN: tools/browser/bench/host/serve-for-pi.sh start (the MotionMark arms need it)"
fi
[ "${bad}" = 0 ] && echo "stage: ok" || { echo "stage: NOT READY"; exit 1; }

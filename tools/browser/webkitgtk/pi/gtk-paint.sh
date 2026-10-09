#!/bin/bash
#
# gtk-paint.sh -- where WebKitGTK's frames go: the paint-watch gate (B10 G6 follow-up, step 1),
# one psh command (psh: no pipes, no quotes -- plain words only):
#
#     /bin/bash /usr/share/gate/gtk-paint.sh [arms|preset] [key=value...]
#
# Staged on the NFS root by tools/browser/webkitgtk/stage-gtk-paint.sh. One XFCE session
# (/bin/xfce-session) whose autostart runs the arms in turn, each a fresh browser at the same
# window size, killed after the arm's time:
#
#   webkit-browser --autoplay=allow --size=WxH --present-stats=S --frame-trace=N@T <url>
#   wpe-browser    --autoplay=allow --size=WxH --present-stats=S <url>
#
# arms (comma list):
#   gtk-hls30   webkit-browser, the B10 G5/G6 page: our HEVC 1080p30 HLS ladder (b8-hls.html, muted)
#   wpe-hls30   wpe-browser, the same page
#   gtk-p60     webkit-browser, the PeerTube HEVC 1080x1920 59.94 fps file (b8.html, local file)
#   wpe-p60     wpe-browser, the same
#   gtk-hls30-g6  gtk-hls30 at webkit-browser's default window (1280x960: the G6 run's condition)
#   <gtk arm>-ahead          the same with webkit-browser --frame-ahead (step 2, WebKit patch
#                            webkit-gtk/0106: FrameDone when a frame is received, the web process
#                            renders frame N+1 while GTK paints frame N), e.g. gtk-p60-ahead
#   <gtk arm>-opaque         ... --opaque-frames (step 3, webkit-gtk/0107: an opaque view's frames
#                            imported as XB24, GDK skips the upload + blend of the window below)
#   <gtk arm>-ahead-opaque   both
#   presets: video  = gtk-hls30,wpe-hls30,gtk-p60,wpe-p60 (default; hold 470 s, one Pi cycle)
#            ahead  = gtk-hls30,gtk-hls30-ahead,gtk-p60,gtk-p60-ahead (step 2's A/B, one boot)
#            opaque = gtk-p60,gtk-p60-opaque,gtk-p60-ahead,gtk-p60-ahead-opaque (step 3's A/B)
#            all    = video + gtk-hls30-g6 + gtk-hls30-ahead,gtk-p60-ahead,gtk-p60-opaque,
#                     gtk-p60-ahead-opaque
#   (an image with ports branch gtk-frame-overlap: launcher b10-r5; the -ahead/-opaque arms log
#   "WPEB-WEBKIT frame-pacing ... ahead=1 opaque=0|1", the others ahead=0 opaque=0)
# key=value: base=URL (the media server, default http://10.42.0.1:8091)  size=WxH (default 1280x800)
#            secs=S (each arm, default 75: start-up + the 60 s clips)  stats=S (default 5)
#            trace=N@T (webkit-browser's per-frame lines: N frames from T s; default 150@30,
#            for the p60 arms 180@30)  hold=S (the session's limit)
#
# MotionMark (its own XFCE session, so not an arm here; bench.sh's gtk arms take the same flags,
# "ahead" -> --frame-ahead, "opaque" -> --opaque-frames, and name the run after the arm):
#     /bin/bash /usr/share/browser-bench/bench.sh motionmark-quick gpu,gpu-ahead,gpu-opaque,gpu-ahead-opaque browser=gtk stats=5
#
# Lines: ours "GFW ...", the browsers' "WKGB ..." (webkit-browser: present-stats, gtk-paint,
# frame-watch-ui, frame-watch-web, frame) and "WPEB ..." (wpe-browser: present), the media
# player's "WPEB-MEDIA ... stat ...", WebKit's "WPEB-WEBKIT swap-chain / gtk-paint import". After
# each arm "GFW arm=<a> end rc=<browser exit> t=<s>". Grade on the host:
#     tools/browser/webkitgtk/grade-gtk-paint.py <uart log> [<bench logs...>]
#
# SPDX-License-Identifier: BSD-3-Clause

exec 2>&1
SELF=/usr/share/gate/gtk-paint.sh
GTK=${GFW_GTK_BROWSER:-/usr/bin/webkit-browser}
WPE=${GFW_WPE_BROWSER:-/usr/bin/wpe-browser}
export HOME=${HOME:-/root}
export PHX_TRACE_ABORT=1
export THUNAR_START=${THUNAR_START:-0}
# the media player's stat line (decoded/presented/painted/dropped) every 2 s, in both browsers
export WPE_PHOENIX_MEDIA_STAT_MS=${WPE_PHOENIX_MEDIA_STAT_MS:-2000}

if [ -n "${GFW_INNER:-}" ]; then
	# shellcheck disable=SC2086 # plain words
	set -- ${GFW_ARGS}
fi
ARMS=${1:-video}
[ $# -gt 0 ] && shift
case "${ARMS}" in
	video) ARMS=gtk-hls30,wpe-hls30,gtk-p60,wpe-p60 ;;
	ahead) ARMS=gtk-hls30,gtk-hls30-ahead,gtk-p60,gtk-p60-ahead ;;
	opaque) ARMS=gtk-p60,gtk-p60-opaque,gtk-p60-ahead,gtk-p60-ahead-opaque ;;
	all) ARMS=gtk-hls30,wpe-hls30,gtk-p60,wpe-p60,gtk-hls30-g6,gtk-hls30-ahead,gtk-p60-ahead,gtk-p60-opaque,gtk-p60-ahead-opaque ;;
esac
BASE=http://10.42.0.1:8091
SIZE=1280x800
SECS_DEFAULT=75
STATS=5
TRACE=""
HOLD=""
for kv in "$@"; do
	case "${kv}" in
		base=*) BASE=${kv#base=} ;;
		size=*) SIZE=${kv#size=} ;;
		secs=*) SECS_DEFAULT=${kv#secs=} ;;
		stats=*) STATS=${kv#stats=} ;;
		trace=*) TRACE=${kv#trace=} ;;
		hold=*) HOLD=${kv#hold=} ;;
		*) echo "GFW bad argument ${kv}" ;;
	esac
done

HLS30="${BASE}/pages/b8-hls.html?src=%2Fladders%2Fhevc-fmp4%2Fmaster.m3u8&muted=1"
P60CLIP=/usr/share/video-demo/rpivid-check/real-peertube-1080.mp4
P60="file:///usr/share/wpe-browser/b8.html?src=file://${P60CLIP}"

spec() {  # spec <arm>: sets CMD (the browser and its words), SECS
	local trace=${TRACE:-150@30} base=$1 flags=()
	SECS=${SECS_DEFAULT}
	# webkit-browser's pacing flags, from the arm's suffixes (gtk arms only)
	# Since launcher b10-r6 both are ON by default, so a gtk arm states both choices explicitly
	local ahead=0 opaque=0
	case "${base}" in *-opaque) opaque=1; base=${base%-opaque} ;; esac
	case "${base}" in *-ahead) ahead=1; base=${base%-ahead} ;; esac
	case "${base}" in
		wpe-*) [ "${ahead}${opaque}" = 00 ] || return 1 ;;
		*)
			if [ "${ahead}" = 1 ]; then flags=(--frame-ahead); else flags=(--no-frame-ahead); fi
			if [ "${opaque}" = 1 ]; then flags+=(--opaque-frames); else flags+=(--no-opaque-frames); fi
			;;
	esac
	case "${base}" in
		gtk-hls30) CMD=("${GTK}" --autoplay=allow --size="${SIZE}" --present-stats="${STATS}" --frame-trace="${trace}" "${flags[@]}" "${HLS30}&run=$1") ;;
		gtk-hls30-g6) CMD=("${GTK}" --autoplay=allow --present-stats="${STATS}" --frame-trace="${trace}" "${flags[@]}" "${HLS30}&run=$1") ;;
		wpe-hls30) CMD=("${WPE}" --autoplay=allow --size="${SIZE}" --present-stats="${STATS}" "${HLS30}&run=$1") ;;
		gtk-p60) CMD=("${GTK}" --autoplay=allow --size="${SIZE}" --present-stats="${STATS}" --frame-trace="${TRACE:-180@30}" "${flags[@]}" "${P60}") ;;
		wpe-p60) CMD=("${WPE}" --autoplay=allow --size="${SIZE}" --present-stats="${STATS}" "${P60}") ;;
		*) return 1 ;;
	esac
}

pid=""
PIDFILE=/tmp/gfw-arm.pid
RCFILE=/tmp/gfw-arm.rc
pause() {  # pause <seconds>, interruptible by the session's SIGTERM
	sleep "$1" &
	wait $!
}
stop_all() {
	[ -z "${pid}" ] && [ -s "${PIDFILE}" ] && pid=$(cat "${PIDFILE}")
	[ -z "${pid}" ] || kill -TERM "${pid}" 2>/dev/null
	wait
	echo "GFW stopped by the session t=${SECONDS}"
	exit 0
}

arm() {  # arm <name>
	local name=$1 rc i
	if ! spec "${name}"; then
		echo "GFW bad arm ${name}"
		return
	fi
	echo "GFW arm=${name} start secs=${SECS} cmd=${CMD[*]} t=${SECONDS}"
	rm -f "${PIDFILE}" "${RCFILE}"
	(
		"${CMD[@]}" 2>&1 &
		echo "$!" > "${PIDFILE}"
		wait "$!"
		echo "$?" > "${RCFILE}"
	) &
	pause "${SECS}"
	pid=$(cat "${PIDFILE}" 2>/dev/null)
	[ -z "${pid}" ] || kill -TERM "${pid}" 2>/dev/null
	i=0
	while [ ! -s "${RCFILE}" ] && [ "${i}" -lt 30 ]; do
		pause 1
		i=$((i + 1))
	done
	if [ ! -s "${RCFILE}" ] && [ -n "${pid}" ]; then
		echo "GFW arm=${name} kill -KILL ${pid} (no exit 30 s after SIGTERM)"
		kill -KILL "${pid}" 2>/dev/null
		pause 2
	fi
	pid=""
	rc=$(cat "${RCFILE}" 2>/dev/null)
	echo "GFW arm=${name} end rc=${rc:-?} t=${SECONDS}"
	# the children (web, network) leave within their 3 s orphan grace; the decoder block is free
	pause 8
}

inner() {
	trap stop_all TERM
	echo "GFW start t=${SECONDS} arms=${ARMS} size=${SIZE} stats=${STATS} base=${BASE} display=${WAYLAND_DISPLAY:-unset}"
	local a IFS=,
	for a in ${ARMS}; do
		unset IFS
		arm "${a}"
	done
	echo "GFW done t=${SECONDS}"
	# end the session now rather than at HOLD (the panel's Log Out path)
	[ -n "${XFCE_LOGOUT_FLAG:-}" ] && : > "${XFCE_LOGOUT_FLAG}"
}

if [ -n "${GFW_INNER:-}" ]; then
	inner
	exit 0
fi

# --- at psh: check the inputs, then the session with the arms as its autostart -----------------
[ -f "${P60CLIP}" ] || echo "GFW WARNING ${P60CLIP} missing: the p60 arms show an error page"
[ -f /usr/share/wpe-browser/b8.html ] || echo "GFW WARNING /usr/share/wpe-browser/b8.html missing"
if curl -s -m 5 -o /dev/null "${BASE}/phx-ping"; then
	echo "GFW media server ok base=${BASE}"
else
	echo "GFW WARNING media server ${BASE} unreachable (host: tools/browser/media/serve-for-pi.sh start): the hls30 arms fail"
fi
if [ -z "${HOLD}" ]; then
	HOLD=90
	IFS=, read -ra list <<< "${ARMS}"
	for a in "${list[@]}"; do
		spec "${a}" && HOLD=$((HOLD + SECS + 20))
	done
fi
export GFW_INNER=1 GFW_ARGS="${ARMS} $*" HOLD
export XFCE_AUTOSTART="/bin/bash=${SELF}"
echo "GFW session hold=${HOLD}s arms=${ARMS} base=${BASE} size=${SIZE}"
/bin/bash /bin/xfce-session
echo "GFW end rc=$?"

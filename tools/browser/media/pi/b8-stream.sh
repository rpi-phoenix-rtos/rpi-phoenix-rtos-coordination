#!/bin/bash
#
# b8-stream.sh -- the streaming-video gates on the Pi (docs/browser/MSE-DESIGN.md §10), one psh
# command (psh: no pipes, no quotes -- plain words only):
#
#     /bin/bash /usr/share/browser-media/b8-stream.sh <arms|preset> [key=value...]
#
# Staged on the NFS root by tools/browser/media/stage.sh; the pages and the media come from the
# host's serve-media.py (tools/browser/media/serve-for-pi.sh start) at base=http://10.42.0.1:8091.
# One XFCE session (/bin/xfce-session) whose autostart plays the arms in turn, each a fresh
#     wpe-browser --autoplay=allow --size=1000x620 <base>/pages/<page>?...&run=<arm>
# killed after the arm's time. arms (comma list):
#
#   stage 0 (native HLS; §10.1 row)            stage 1 (MSE; §10.2 row)
#   probe       1  type answers only           mse-basic    2  hevc-1080 + AAC to the end
#   hevc-fmp4   2,3 the main case (HEVC 1080p) mse-seek     3  seek to 40 s, unbuffered
#   hevc-ts     4  TS segments                 mse-switch   4  720p -> 1080p init, changeType -> H.264
#   h264-only   5  the 720p cap                mse-evict    5  remove(0,40) + 120 s of appends
#   main10      6  policy picks H.264 720p     mse-underrun 6  appends stop: waiting -> playing
#   main10-forced 6 WPE_PHOENIX_HLS_VARIANT=0  mse-eos      7  endOfStream -> ended
#   seek        7  hevc-fmp4, seek to 40 at 5  mse-offset      timestampOffset + sequence mode
#   byterange   8  EXT-X-BYTERANGE             hlsjs-hevc   8  hls.js on hevc-fmp4 (HEVC level)
#   aes         8  AES-128                     hlsjs-h264   8  hls.js on h264-only (no 1080p level)
#   live        9  /live/hevc-fmp4, 90 s       mse-off      9  hls.js page with MSE off -> native
#   live-disc   9  ?disc=10 (resets)           mse-memory  11  (attended: 4 MSE players, see the doc)
#   hlsjs      10  hls.js page: branch native
#   audio-only     AAC/Opus variants
#   memory     12  7 idle players, then play one
#
#   presets, each one Pi cycle (hold <= 450 s, under the cycle's --max-cmd-secs 540):
#     stage0a = probe,hevc-fmp4,hevc-ts,h264-only      stage1a = mse-basic,mse-seek,mse-switch,mse-underrun
#     stage0b = main10,main10-forced,seek,byterange,aes stage1b = mse-evict,mse-eos,mse-offset
#     stage0c = live,live-disc,hlsjs                    stage1c = probe,hlsjs-hevc,hlsjs-h264,mse-off
#     stage0d = memory,audio-only
#   key=value: base=URL (default http://10.42.0.1:8091)  hold=S (session limit; default 90 + the arms' times + 15 s each)
#              args=<extra wpe-browser words, comma-separated> (e.g. args=--cpu-rendering)
#              mseoff=<the browser words that turn MSE off, comma-separated; default --mse=off>
#
# Lines: ours "B8S ...", the pages' "B8HLS / B8HLSJS / B8MSE / B8HLSMEM ..." (console and titles),
# the browser's "WPEB ...", the media player's "WPEB-MEDIA ...". After each arm:
#   B8S arm=<a> end rc=<browser exit> t=<s>
#   B8S arm=<a> page=<DONE result|none> <the page's summary fields>
# Grade with: grep -a -E '^(B8S |WPEB-MEDIA |B8HLS|B8MSE|WPEB )|Exception #'. The host's
# serve.log has every request of the arm (check-media.py requests <serve.log> --run <arm>).
# First Pi run: stage0a on build 54 (2026-10-07): hevc-fmp4, hevc-ts and h264-only played to the end.
#
# SPDX-License-Identifier: BSD-3-Clause

exec 2>&1
SELF=/usr/share/browser-media/b8-stream.sh
BROWSER=${B8S_BROWSER:-/usr/bin/wpe-browser}
export HOME=${HOME:-/root}
export PHX_TRACE_ABORT=1
export THUNAR_START=${THUNAR_START:-0}
export WPE_PHOENIX_MEDIA_STAT_MS=${WPE_PHOENIX_MEDIA_STAT_MS:-2000}

if [ -n "${B8S_INNER:-}" ]; then
	# shellcheck disable=SC2086 # plain words
	set -- ${B8S_ARGS}
fi
ARMS=${1:-stage0a}
[ $# -gt 0 ] && shift
case "${ARMS}" in
	stage0a) ARMS=probe,hevc-fmp4,hevc-ts,h264-only ;;
	stage0b) ARMS=main10,main10-forced,seek,byterange,aes ;;
	stage0c) ARMS=live,live-disc,hlsjs ;;
	stage0d) ARMS=memory,audio-only ;;
	stage1a) ARMS=mse-basic,mse-seek,mse-switch,mse-underrun ;;
	stage1b) ARMS=mse-evict,mse-eos,mse-offset ;;
	stage1c) ARMS=probe,hlsjs-hevc,hlsjs-h264,mse-off ;;
esac
BASE=http://10.42.0.1:8091
HOLD=""
EXTRA=()
MSEOFF=(--mse=off)
for kv in "$@"; do
	case "${kv}" in
		base=*) BASE=${kv#base=} ;;
		hold=*) HOLD=${kv#hold=} ;;
		args=*) IFS=, read -ra EXTRA <<< "${kv#args=}" ;;
		mseoff=*) IFS=, read -ra MSEOFF <<< "${kv#mseoff=}" ;;
		*) echo "B8S bad argument ${kv}" ;;
	esac
done
PAGES=${BASE}/pages

# arm table: <page + query> <seconds> [env...]; url-encoded src values
ladder() { echo "%2Fladders%2F$1%2Fmaster.m3u8"; }
spec() {  # spec <arm>: sets PAGE, SECS, ENVS, ARGS
	PAGE="" SECS=0 ENVS=() ARGS=()
	case "$1" in
		probe) PAGE="b8-hls.html?src=$(ladder hevc-fmp4)&autoplay=0&limit=25" SECS=40 ;;
		hevc-fmp4) PAGE="b8-hls.html?src=$(ladder hevc-fmp4)" SECS=85 ;;
		hevc-ts) PAGE="b8-hls.html?src=$(ladder hevc-ts)" SECS=85 ;;
		h264-only) PAGE="b8-hls.html?src=$(ladder h264-only)" SECS=85 ;;
		main10) PAGE="b8-hls.html?src=$(ladder hevc-main10)&stop=30" SECS=55 ;;
		main10-forced) PAGE="b8-hls.html?src=$(ladder hevc-main10)&stop=30" SECS=55 ENVS=(WPE_PHOENIX_HLS_VARIANT=0) ;;
		seek) PAGE="b8-hls.html?src=$(ladder hevc-fmp4)&seek=40@5" SECS=55 ;;
		byterange) PAGE="b8-hls.html?src=$(ladder byterange)&stop=30" SECS=55 ;;
		aes) PAGE="b8-hls.html?src=$(ladder aes)&stop=30" SECS=55 ;;
		live) PAGE="b8-hls.html?src=%2Flive%2Fhevc-fmp4%2Fmaster.m3u8&stop=90" SECS=115 ;;
		live-disc) PAGE="b8-hls.html?src=%2Flive%2Fhevc-fmp4%2Fmaster.m3u8%3Fdisc%3D10&stop=90" SECS=115 ;;
		hlsjs) PAGE="b8-hlsjs.html?src=$(ladder hevc-fmp4)&stop=20" SECS=50 ;;
		audio-only) PAGE="b8-hls.html?src=$(ladder audio-only)&stop=20" SECS=45 ;;
		memory) PAGE="b8-hls-memory.html?n=7&idle=60&play=30" SECS=110 ;;
		mse-basic) PAGE="b8-mse.html?mode=basic" SECS=85 ;;
		mse-seek) PAGE="b8-mse.html?mode=seek" SECS=60 ;;
		mse-switch) PAGE="b8-mse.html?mode=switch" SECS=85 ;;
		mse-evict) PAGE="b8-mse.html?mode=evict&loops=3" SECS=170 ;;
		mse-underrun) PAGE="b8-mse.html?mode=underrun&stop=30" SECS=60 ;;
		mse-eos) PAGE="b8-mse.html?mode=eos" SECS=90 ;;
		mse-offset) PAGE="b8-mse.html?mode=offset" SECS=55 ;;
		hlsjs-hevc) PAGE="b8-hlsjs.html?src=$(ladder hevc-fmp4)&stop=30" SECS=60 ;;
		hlsjs-h264) PAGE="b8-hlsjs.html?src=$(ladder h264-only)&stop=30" SECS=60 ;;
		mse-off) PAGE="b8-hlsjs.html?src=$(ladder hevc-fmp4)&stop=20" SECS=50 ARGS=("${MSEOFF[@]}") ;;
		*) return 1 ;;
	esac
}

pid=""
EVENTS=/tmp/b8s-arm.events
PIDFILE=/tmp/b8s-arm.pid
RCFILE=/tmp/b8s-arm.rc
pause() {  # pause <seconds>, interruptible by the session's SIGTERM
	sleep "$1" &
	wait $!
}
stop_all() {
	[ -z "${pid}" ] && [ -s "${PIDFILE}" ] && pid=$(cat "${PIDFILE}")
	[ -z "${pid}" ] || kill -TERM "${pid}" 2>/dev/null
	wait
	echo "B8S stopped by the session t=${SECONDS}"
	exit 0
}

summary() {  # summary <arm>: the page's DONE result and summary fields (bash only: no forks per line)
	local line done=none fields=""
	[ -f "${EVENTS}" ] || : > "${EVENTS}"
	while IFS= read -r line; do
		case "${line}" in
			*"-DONE result="*" r=$1 "*)
				done=${line##*-DONE result=}
				done=${done%% *} ;;
			*" r=$1 summary "*)
				fields=${line##* summary } ;;
		esac
	done < "${EVENTS}"
	echo "B8S arm=$1 page=${done} ${fields}"
}

arm() {  # arm <name>
	local name=$1 rc tee i url
	if ! spec "${name}"; then
		echo "B8S bad arm ${name}"
		return
	fi
	url="${PAGES}/${PAGE}&run=${name}"
	echo "B8S arm=${name} start secs=${SECS} env=${ENVS[*]:-none} args=${ARGS[*]:-none} url=${url} t=${SECONDS}"
	rm -f "${EVENTS}" "${PIDFILE}" "${RCFILE}"
	(
		[ "${#ENVS[@]}" = 0 ] || export "${ENVS[@]}"
		"${BROWSER}" --autoplay=allow --size=1000x620 "${EXTRA[@]}" "${ARGS[@]}" "${url}" 2>&1 &
		echo "$!" > "${PIDFILE}"
		wait "$!"
		echo "$?" > "${RCFILE}"
	) | tee "${EVENTS}" &
	tee=$!
	pause "${SECS}"
	pid=$(cat "${PIDFILE}" 2>/dev/null)
	[ -z "${pid}" ] || kill -TERM "${pid}" 2>/dev/null
	i=0
	while [ ! -s "${RCFILE}" ] && [ "${i}" -lt 60 ]; do
		pause 1
		i=$((i + 1))
	done
	i=0
	while kill -0 "${tee}" 2>/dev/null && [ "${i}" -lt 10 ]; do
		pause 1
		i=$((i + 1))
	done
	kill -TERM "${tee}" 2>/dev/null
	wait "${tee}" 2>/dev/null
	pid=""
	rc=$(cat "${RCFILE}" 2>/dev/null)
	echo "B8S arm=${name} end rc=${rc:-?} t=${SECONDS}"
	summary "${name}"
	# the audio device and the decoder block are free again before the next arm
	pause 5
}

inner() {
	trap stop_all TERM
	echo "B8S start t=${SECONDS} arms=${ARMS} base=${BASE} display=${WAYLAND_DISPLAY:-unset}"
	local a IFS=,
	for a in ${ARMS}; do
		unset IFS
		arm "${a}"
	done
	echo "B8S done t=${SECONDS}"
}

if [ -n "${B8S_INNER:-}" ]; then
	inner
	exit 0
fi

# --- at psh: the session with the check as its autostart ------------------------------------------
if [ -z "${HOLD}" ]; then
	HOLD=90
	IFS=, read -ra list <<< "${ARMS}"
	for a in "${list[@]}"; do
		spec "${a}" && HOLD=$((HOLD + SECS + 15))
	done
fi
export B8S_INNER=1 B8S_ARGS="${ARMS} $*" HOLD
export XFCE_AUTOSTART="/bin/bash=${SELF}"
echo "B8S session hold=${HOLD}s arms=${ARMS} base=${BASE}"
/bin/bash /bin/xfce-session
echo "B8S end rc=$?"

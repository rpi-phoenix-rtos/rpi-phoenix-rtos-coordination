#!/bin/bash
#
# run-host-pages.sh -- the streaming-video test pages in the host's headless browsers (Playwright's
# Chromium, Firefox and WebKit = the WPE MiniBrowser with GStreamer), before a Pi cycle: proves the
# pages, the server and the media against engines that already play them. Same pages, same server
# (serve-media.py on 127.0.0.1, a free port), the bench's pw-run.mjs as the browser driver
# (external/browser-bench/host-tools: tools/browser/bench/host/setup-host-tools.sh).
#
#     tools/browser/media/run-host-pages.sh [--root DIR] [--out DIR] [--browsers "chromium firefox webkit"]
#                                           [--cases "name ..."] [--list]
#
# Each case is one page in one browser, muted (headless autoplay), until its "<TAG>-DONE" title.
# Output: DIR/<browser>-<case>.log (pw-run's lines + the page's console) and DIR/summary.txt, one
# line per run:
#   HOST browser=<b> case=<c> result=<DONE result|NO-DONE> check=<PASS|FAIL|-> <the page's summary>
#       [branch= levels= switched=] [checks=<mode checks>]
# check: the case's expectation (a regex over the log), when it has one for that browser.
# Known host limits (not page bugs; 2026-10-07, Playwright 1.63): Chromium has no HEVC at all (hls.js
# drops the HEVC levels, hvc1 SourceBuffers are refused) but plays native HLS in <video> (its own
# variant choice); Firefox and WebKit play HEVC (system FFmpeg / GStreamer) but not native HLS.
#
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../.." && pwd)
tools=${BENCH_HOST_TOOLS:-${repo}/external/browser-bench/host-tools}
[ -d "${tools}/node_modules" ] || tools=/home/houp/phoenix-rpi/external/browser-bench/host-tools
root=${MEDIA_ROOT:-${repo}/artifacts/media}
out=""
browsers="chromium firefox webkit"
only=""
list=0
while [ $# -gt 0 ]; do
	case "$1" in
		--root) root=$2; shift 2 ;;
		--out) out=$2; shift 2 ;;
		--browsers) browsers=$2; shift 2 ;;
		--cases) only=$2; shift 2 ;;
		--list) list=1; shift ;;
		*) echo "usage: $0 [--root DIR] [--out DIR] [--browsers LIST] [--cases LIST] [--list]" >&2; exit 2 ;;
	esac
done
out=${out:-${root}/host-$(date +%Y%m%d-%H%M%S)}
enc() { python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$1"; }

# name | browsers | page + query (src= is URL-encoded below) | timeout s | expectation per browser (b=regex;...)
hevc_hd='level-switched i=[0-9]+ size=1920x1080 bitrate=[0-9]+ codecs=hvc1'
cases=(
	"hls-native-hevc-fmp4|chromium firefox webkit|b8-hls.html?src=$(enc /ladders/hevc-fmp4/master.m3u8)&stop=10|60|chromium=B8HLS-DONE result=stopped"
	"hlsjs-hevc-fmp4|chromium firefox webkit|b8-hlsjs.html?src=$(enc /ladders/hevc-fmp4/master.m3u8)&stop=15|90|firefox=${hevc_hd};webkit=${hevc_hd};chromium=manifest-parsed levels=2 "
	"hlsjs-h264-only|chromium firefox webkit|b8-hlsjs.html?src=$(enc /ladders/h264-only/master.m3u8)&stop=10|80|chromium=level-switched .*size=1920x1080"
	"hlsjs-hevc-ts|firefox webkit|b8-hlsjs.html?src=$(enc /ladders/hevc-ts/master.m3u8)&stop=10|80|firefox=manifest-parsed levels=2 .*B8HLSJS-DONE result=stopped;webkit=manifest-parsed levels=2 .*B8HLSJS-DONE result=stopped"
	"hlsjs-main10|firefox webkit|b8-hlsjs.html?src=$(enc /ladders/hevc-main10/master.m3u8)&stop=10|80|"
	"hlsjs-byterange|chromium firefox webkit|b8-hlsjs.html?src=$(enc /ladders/byterange/master.m3u8)&stop=10|80|chromium=size=1280x720;firefox=size=1280x720;webkit=size=1280x720"
	"hlsjs-aes|chromium firefox webkit|b8-hlsjs.html?src=$(enc /ladders/aes/master.m3u8)&stop=10|80|chromium=size=1280x720;firefox=size=1280x720;webkit=size=1280x720"
	"hlsjs-audio-only|chromium firefox webkit|b8-hlsjs.html?src=$(enc /ladders/audio-only/master.m3u8)&stop=10|80|chromium=B8HLSJS-DONE result=stopped;firefox=B8HLSJS-DONE result=stopped;webkit=B8HLSJS-DONE result=stopped"
	"hlsjs-live|chromium firefox webkit|b8-hlsjs.html?src=$(enc /live/h264-only/master.m3u8)&stop=30|90|chromium=level-loaded .*live=1;firefox=level-loaded .*live=1;webkit=level-loaded .*live=1"
	"hlsjs-live-disc|firefox webkit|b8-hlsjs.html?src=$(enc '/live/hevc-fmp4/master.m3u8?disc=5')&stop=30|90|firefox=level-loaded .*live=1;webkit=level-loaded .*live=1"
	"mse-basic|firefox webkit|b8-mse.html?mode=basic&stop=12|90|firefox=B8MSE-DONE result=stopped;webkit=B8MSE-DONE result=stopped"
	"mse-basic-h264|chromium|b8-mse.html?mode=basic&rep=h264-720&stop=12|90|chromium=B8MSE-DONE result=stopped"
	"mse-seek|chromium firefox webkit|b8-mse.html?mode=seek&rep=h264-720|100|chromium=seek-check ok=1 .*pass=1;firefox=seek-check ok=1 .*pass=1;webkit=seek-check ok=1 .*pass=1"
	"mse-switch|firefox webkit|b8-mse.html?mode=switch|100|firefox=resize size=1280x720.*resize size=1920x1080;webkit=resize size=1280x720.*resize size=1920x1080"
	"mse-evict|chromium firefox webkit|b8-mse.html?mode=evict&rep=h264-720|90|chromium=evict-check .*pass=1;firefox=evict-check .*pass=1;webkit=evict-check .*pass=1"
	"mse-offset|chromium firefox webkit|b8-mse.html?mode=offset&rep=h264-720|90|chromium=offset-check what=sequence.*pass=1;firefox=offset-check what=sequence.*pass=1;webkit=offset-check what=sequence.*pass=1"
	"mse-underrun|chromium firefox webkit|b8-mse.html?mode=underrun&rep=h264-720&stop=25|90|chromium=underrun-check .*pass=1;firefox=underrun-check .*pass=1;webkit=underrun-check .*pass=1"
	"mse-eos|firefox webkit|b8-mse.html?mode=eos|100|firefox=ended-check .*pass=1;webkit=ended-check .*pass=1"
	"mse-opus|chromium firefox webkit|b8-mse.html?mode=basic&rep=none&audio=opus&stop=8|60|chromium=B8MSE-DONE result=stopped;firefox=B8MSE-DONE result=stopped;webkit=B8MSE-DONE result=stopped"
	"memory|firefox webkit|b8-hls-memory.html?n=7&idle=8&play=6|40|"
)
if [ "${list}" = 1 ]; then
	for c in "${cases[@]}"; do IFS='|' read -r name bs page _ <<< "${c}"; echo "${name} [${bs}] ${page}"; done
	exit 0
fi
[ -d "${tools}/node_modules/playwright" ] || { echo "run-host-pages: no Playwright in ${tools} (tools/browser/bench/host/setup-host-tools.sh)" >&2; exit 1; }
[ -f "${root}/pages/b8-hls.html" ] || { echo "run-host-pages: no pages in ${root} (stage.sh)" >&2; exit 1; }
mkdir -p "${out}"
export NODE_PATH=${tools}/node_modules PLAYWRIGHT_BROWSERS_PATH=${tools}/browsers PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
python3 "${here}/serve-media.py" --root "${root}" --port "${port}" > "${out}/serve.log" 2>&1 &
server=$!
trap 'kill ${server} 2>/dev/null' EXIT
for _ in $(seq 50); do curl -sf "http://127.0.0.1:${port}/phx-ping" > /dev/null && break; sleep 0.2; done
echo "# host pages $(date '+%Y-%m-%d %H:%M %z') root=${root} $(head -1 "${root}/VERSIONS.txt" 2>/dev/null)" > "${out}/summary.txt"

for browser in ${browsers}; do
	for c in "${cases[@]}"; do
		IFS='|' read -r name bs page timeout expects <<< "${c}"
		case " ${bs} " in *" ${browser} "*) ;; *) continue ;; esac
		if [ -n "${only}" ]; then case " ${only} " in *" ${name} "*) ;; *) continue ;; esac; fi
		tag=$(case "${page}" in b8-hlsjs*) echo B8HLSJS ;; b8-hls-memory*) echo B8HLSMEM ;; b8-hls*) echo B8HLS ;; b8-mse*) echo B8MSE ;; esac)
		log=${out}/${browser}-${name}.log
		url="http://127.0.0.1:${port}/pages/${page}&muted=1&run=host-${browser}-${name}"
		node "${repo}/tools/browser/bench/host/pw-run.mjs" --browser "${browser}" --timeout "${timeout}" \
			--done "${tag}-DONE" "${url}" > "${log}" 2>&1
		done_line=$(grep -a -o -E "${tag}-DONE result=[a-z]+" "${log}" | head -1)
		summary=$(grep -a -o -E "${tag} t=[0-9]+ r=[^ ]+ summary .*" "${log}" | head -1 | sed -E 's/^.* summary //')
		extra=""
		if [ "${tag}" = B8HLSJS ]; then
			extra="$(grep -a -o -E 'branch branch=[^ ]+' "${log}" | head -1 | sed 's/branch branch=/branch=/')"
			extra+=" levels=$(grep -a -o -E 'manifest-parsed levels=[0-9]+' "${log}" | head -1 | sed 's/.*=//')"
			extra+=" switched=$(grep -a -o -E 'level-switched i=[0-9]+ size=[0-9x]+' "${log}" | sed -E 's/.*size=//' | paste -sd, -)"
		elif [ "${tag}" = B8MSE ]; then
			extra="checks=$(grep -a -o -E '(seek|evict|offset|underrun|ended)-check [^ ]+ [^ ]+ .*pass=[01]' "${log}" | sed -E 's/^([a-z]+-check).*pass=([01])/\1:\2/' | paste -sd, -)"
		fi
		check=-
		expect=""
		IFS=';' read -ra pairs <<< "${expects}"
		for kv in "${pairs[@]}"; do [ "${kv%%=*}" = "${browser}" ] && expect=${kv#*=}; done
		if [ -n "${expect}" ]; then
			# -z: the whole log is one record, so an expectation may span lines (no pipe: pipefail)
			if grep -a -q -z -E "${expect}" "${log}"; then check=PASS; else check=FAIL; fi
		fi
		line="HOST browser=${browser} case=${name} result=${done_line#*result=} check=${check} ${summary} ${extra}"
		[ -n "${done_line}" ] || line="HOST browser=${browser} case=${name} result=NO-DONE check=${check} ${extra}"
		echo "${line}" | tee -a "${out}/summary.txt"
	done
done
echo "run-host-pages: ${out}/summary.txt"

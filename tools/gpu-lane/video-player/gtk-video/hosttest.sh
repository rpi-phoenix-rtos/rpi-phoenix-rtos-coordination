#!/usr/bin/env bash
#
# Host control for gtk-video: the same gtk-video.c built natively against the host's GTK 3 and
# the host-control ffmpeg (../hosttest/run.sh builds it), run HEADLESS on GDK's broadway
# backend (a private broadwayd display; no window on the host desktop), sound to /dev/null.
# Scripted controls (GTK_VIDEO_AUTOKEYS) are graded from the GTK-VIDEO stat lines: playback
# at the clip's rate, pause holds the clock, a seek jumps, stop returns to 0 paused, play
# resumes, quit exits 0; then every clip plays its first seconds. (Broadway has no
# fullscreen window state: the fullscreen key is exercised on the Pi only.)
#
# Usage: tools/gpu-lane/video-player/gtk-video/hosttest.sh
#
# Copyright 2026 Phoenix Systems
#
# This file is part of Phoenix-RTOS.
#
# %LICENSE%
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
vp="$(cd "${here}/.." && pwd)"
H="${vp}/build-out/host/ffmpeg-src"
out="${vp}/build-out/host-gtk"
log() { printf '[gtk-video-host] %s\n' "$*"; }
die() { printf '[gtk-video-host] ERROR: %s\n' "$*" >&2; exit 1; }
[ -f "${H}/libavcodec/libavcodec.a" ] || die "no host ffmpeg: run ${vp}/hosttest/run.sh first"
command -v broadwayd > /dev/null || die "broadwayd not found (GTK 3's broadway backend)"
mkdir -p "${out}"
# shellcheck disable=SC2046
gcc -O2 -g -std=gnu11 -Wall -Wextra -Werror $(pkg-config --cflags gtk+-3.0) -I"${H}" -o "${out}/gtk-video" \
	"${here}/gtk-video.c" "${H}/libavformat/libavformat.a" "${H}/libavcodec/libavcodec.a" "${H}/libswscale/libswscale.a" \
	"${H}/libswresample/libswresample.a" "${H}/libavutil/libavutil.a" $(pkg-config --libs gtk+-3.0) -lm -lpthread
log "built ${out}/gtk-video"

disp=:$(( (RANDOM % 50) + 40 ))
broadwayd "${disp}" > "${out}/broadwayd.log" 2>&1 &
bpid=$!
trap 'kill "${bpid}" 2> /dev/null || true' EXIT
sleep 1
run() {  # log clip autokeys [args...]
	local l="$1" clip="$2" keys="$3"
	shift 3
	GDK_BACKEND=broadway BROADWAY_DISPLAY="${disp}" GTK_VIDEO_AUDIO_DEV=/dev/null GTK_VIDEO_STAT_MS=500 \
		GTK_VIDEO_AUTOKEYS="${keys}" timeout 90 "${out}/gtk-video" "$@" "${clip}" > "${l}" 2>&1
}
field() {  # log t_from t_to key -> the values of key in stat lines within (t_from, t_to)
	awk -v a="$2" -v b="$3" -v k="$4" '/^GTK-VIDEO stat/ { for (i = 3; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
		if (v["t"] > a && v["t"] < b) printf "%s ", v[k] }' "$1"
}

l="${out}/controls.log"
run "${l}" "${vp}/build-out/clips/m10-h264-720p30-aac.mp4" "3:pause,5:pause,6:right,8:stop,9.5:pause,11:quit"
rc=$?
fps="$(field "${l}" 1 2.9 fps)"
pclock="$(field "${l}" 3.3 4.9 clock)"
paused="$(field "${l}" 3.3 4.9 paused)"
sclock="$(field "${l}" 6.4 7.9 clock)"
stop="$(field "${l}" 8.3 9.4 clock)"
spaused="$(field "${l}" 8.3 9.4 paused)"
resumed="$(field "${l}" 10.2 10.9 clock)"
log "controls: rc=${rc} fps=[${fps}] paused=[${paused}] clock_while_paused=[${pclock}] after_seek=[${sclock}]" \
	"after_stop=[${stop}] stop_paused=[${spaused}] resumed=[${resumed}]"
ok=1
[ "${rc}" = 0 ] || ok=0
awk -v s="${fps}" 'BEGIN { n = split(s, a, " "); for (i = 1; i <= n; i++) if (a[i] < 27 || a[i] > 33) exit 1; exit !(n >= 2) }' || ok=0
case " ${paused}" in *" 0"*|" ") ok=0 ;; esac
awk -v s="${pclock}" 'BEGIN { n = split(s, a, " "); for (i = 2; i <= n; i++) if (a[i] != a[1]) exit 1; exit !(n >= 2) }' || ok=0
# right = +10 s from ~4 s, keyframe-aligned (2 s GOP): 12..17 in the stat lines after it
awk -v s="${sclock}" 'BEGIN { split(s, a, " "); exit !(a[1] >= 12 && a[1] <= 17) }' || ok=0
awk -v s="${stop}" 'BEGIN { n = split(s, a, " "); for (i = 1; i <= n; i++) if (a[i] != 0) exit 1; exit !(n >= 1) }' || ok=0
case " ${spaused}" in *" 0"*|" ") ok=0 ;; esac
awk -v s="${resumed}" 'BEGIN { split(s, a, " "); exit !(a[1] > 0.1 && a[1] < 2) }' || ok=0
grep -q '^GTK-VIDEO done$' "${l}" || ok=0
[ "${ok}" = 1 ] || die "controls FAILED (${l})"
log "PASS controls: 30 fps, pause holds the clock, right seeks, stop = 0 + paused, play resumes, quit"

fail=0
for clip in "${vp}"/build-out/clips/m10-*; do
	l="${out}/$(basename "${clip}").log"
	run "${l}" "${clip}" "4:quit" || true
	f="$(field "${l}" 1.5 4 fps)"
	snd="$(grep -m1 -oE '^GTK-VIDEO audio [^ ]+ [0-9]+ Hz' "${l}" || echo 'no audio line')"
	if awk -v s="${f}" 'BEGIN { n = split(s, a, " "); for (i = 1; i <= n; i++) if (a[i] < 20) exit 1; exit !(n >= 1) }' \
			&& grep -q '^GTK-VIDEO done$' "${l}"; then
		log "PASS $(basename "${clip}"): fps=[${f}] ${snd}"
	else
		log "FAIL $(basename "${clip}"): fps=[${f}] ${snd} (${l})"
		fail=1
	fi
done
[ "${fail}" = 0 ] || die "a clip failed"
log "all clips play"

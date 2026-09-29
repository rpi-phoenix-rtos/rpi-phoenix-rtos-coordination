#!/usr/bin/env bash
#
# Host reproduction of the M10 ffplay hang (m10a0: no return after key=quit at the end of the
# file; m10a1: -autoexit never fires, the last frame stays up). The host ffplay of run.sh (the
# same tarball, components and patch 0001, against the host's SDL 2) runs headless with
# phx-condclock.c preloaded:
#
#   stock    no shim (glibc: an attribute-less condvar is on CLOCK_REALTIME, SDL's clock)
#   phx      the condvar default of libphoenix (CLOCK_MONOTONIC) under the host's SDL, i.e.
#            SDL_CondWaitTimeout's CLOCK_REALTIME deadline decades ahead, as on the Pi
#   phx+fix  phx + SDL_CondWaitTimeout on CLOCK_MONOTONIC, as sdl-patches/0011
#
# Clip: m10-h264-720p30-aac.mp4 from 36 s (-ss), so the end of the file comes after ~9 s.
# Predicted: stock and phx+fix return (rc 0) for -autoexit and for a quit after the end;
# phx does not (killed by timeout, rc 124), with its stat lines still coming after the end
# (-autoexit) or stopping at the key (quit). A quit in the middle of the file returns even under
# phx: ffplay's read thread is then woken by a decoder whose packet queue ran empty; at the end
# of the file nobody signals it any more.
#
# Usage: tools/gpu-lane/video-player/hosttest/condclock.sh   (after hosttest/run.sh)
#
# Copyright 2026 Phoenix Systems
#
# This file is part of Phoenix-RTOS.
#
# %LICENSE%
#
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
vp="$(cd "${here}/.." && pwd)"
out="${vp}/build-out/host"
FFPLAY="${out}/ffmpeg-src/ffplay"
clip="${vp}/build-out/clips/m10-h264-720p30-aac.mp4"
log() { printf '[condclock] %s\n' "$*"; }
die() { printf '[condclock] ERROR: %s\n' "$*" >&2; exit 1; }
[ -x "${FFPLAY}" ] || die "no host ffplay: run hosttest/run.sh first"
[ -f "${clip}" ] || die "no clip: run gen-clips.sh"

cc -O2 -Wall -Werror -shared -fPIC -o "${out}/phx-condclock.so" "${here}/phx-condclock.c" -ldl || die "shim build failed"
cc -O2 -Wall -Werror -shared -fPIC -DWITH_SDL_FIX -o "${out}/phx-condclock-fix.so" "${here}/phx-condclock.c" -ldl \
	|| die "shim build failed"

# run <name> <preload or -> <timeout s> <ffplay args...>: prints "rc=<rc> t=<wall s>"
run() {
	local name="$1" pre="$2" to="$3" t0 rc
	shift 3
	t0=$(date +%s.%N)
	if [ "${pre}" = - ]; then
		FFPLAY_STATLINE_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
			timeout "${to}" "${FFPLAY}" -hide_banner -loglevel info -ss 36 "$@" "${clip}" > "${out}/condclock-${name}.log" 2>&1
	else
		LD_PRELOAD="${pre}" FFPLAY_STATLINE_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
			timeout "${to}" "${FFPLAY}" -hide_banner -loglevel info -ss 36 "$@" "${clip}" > "${out}/condclock-${name}.log" 2>&1
	fi
	rc=$?
	printf 'rc=%s t=%.1f' "${rc}" "$(echo "$(date +%s.%N) - ${t0}" | bc)"
}
# stat lines after the last frame: shown= no longer growing
frozen() { awk '/^ffplay-stat/ { split($5, s, "="); if (s[2] == last && s[2] > 0) n++; last = s[2] } END { print n + 0 }' "$1"; }
# the lowest audio-queue depth while frames were still being shown (t >= 2 s)
aqmin() { awk '/^ffplay-stat/ { split($2, t, "="); split($5, s, "="); split($9, a, "="); sub("KB", "", a[2])
	if (t[2] >= 2 && s[2] != last) { if (m == "" || a[2] + 0 < m) m = a[2] + 0 } last = s[2] } END { print m }' "$1"; }

fail=0
expect() {   # expect <label> <result> <want rc>
	local rc="${2#rc=}"; rc="${rc%% *}"
	if [ "${rc}" = "$3" ]; then log "PASS $1: $2 (want rc $3)"; else log "FAIL $1: $2 (want rc $3)"; fail=1; fi
}

r="$(run stock-autoexit - 30 -autoexit)"
expect "stock   -autoexit       " "${r} aq_min=$(aqmin "${out}/condclock-stock-autoexit.log")KB" 0
r="$(FFPLAY_AUTOKEYS=14:quit run stock-quit - 30)"
expect "stock   quit at 14 s    " "${r}" 0
r="$(run phx-autoexit "${out}/phx-condclock.so" 25 -autoexit)"
expect "phx     -autoexit       " "${r} frozen_stat_lines=$(frozen "${out}/condclock-phx-autoexit.log") aq_min=$(aqmin "${out}/condclock-phx-autoexit.log")KB" 124
r="$(FFPLAY_AUTOKEYS=14:quit run phx-quit-end "${out}/phx-condclock.so" 25)"
expect "phx     quit at 14 s    " "${r} key=$(grep -c 'key=quit' "${out}/condclock-phx-quit-end.log") stat_after_key=$(awk '/key=quit/ { k = 1; next } k && /^ffplay-stat/ { n++ } END { print n + 0 }' "${out}/condclock-phx-quit-end.log")" 124
r="$(FFPLAY_AUTOKEYS=4:quit run phx-quit-mid "${out}/phx-condclock.so" 25)"
log "info    phx quit at 4 s (mid-file): ${r}"
r="$(run fix-autoexit "${out}/phx-condclock-fix.so" 30 -autoexit)"
expect "phx+fix -autoexit       " "${r} aq_min=$(aqmin "${out}/condclock-fix-autoexit.log")KB" 0
r="$(FFPLAY_AUTOKEYS=14:quit run fix-quit-end "${out}/phx-condclock-fix.so" 30)"
expect "phx+fix quit at 14 s    " "${r}" 0
[ "${fail}" = 0 ] || die "a case did not behave as predicted (logs: ${out}/condclock-*.log)"
log "PASS: the libphoenix condvar clock reproduces both hangs with the host's SDL; 0011's monotonic deadline removes them"

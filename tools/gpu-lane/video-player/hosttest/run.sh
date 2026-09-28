#!/usr/bin/env bash
#
# Host control for the M10 ffplay build: the SAME ffmpeg 6.1 tarball and component set
# (../components.sh) built natively for x86-64 against the host's SDL2, then every M10 clip
# played headless (SDL dummy video + dummy audio drivers) for a few seconds. It answers,
# without a Pi, whether the component set is complete for ffplay: the demuxer opens each
# container, each decoder is present, and ffplay's filter graphs configure (the auto-inserted
# scale / aresample), with the video and audio stats advancing. Pi-side questions (KMSDRM,
# EGL, /dev/audio0, speed) are the Pi cycles' job.
#
# Usage: tools/gpu-lane/video-player/hosttest/run.sh [--secs N] [-j N]
#   needs ../build-out/clips (../gen-clips.sh) and the host's SDL2 development files.
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
root="$(cd "${vp}/../../.." && pwd)"
out="${vp}/build-out/host"
secs=6
jobs="$(nproc)"
while [ $# -gt 0 ]; do
	case "$1" in
		--secs) shift; secs="${1:?}" ;;
		-j) shift; jobs="${1:?}" ;;
		*) echo "run.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
log() { printf '[ffplay-host] %s\n' "$*"; }
die() { printf '[ffplay-host] ERROR: %s\n' "$*" >&2; exit 1; }

TARBALL="${root}/sources/phoenix-rtos-ports/ffmpeg/ffmpeg-6.1.tar.gz"
# shellcheck source=../components.sh
. "${vp}/components.sh"
stamp="$( (sha256sum "${TARBALL}"; cat "${vp}/components.sh" "${vp}"/patches/*.patch; echo x86asm-off) | sha256sum | cut -c1-16)"
FS="${out}/ffmpeg-src"
if [ "$(cat "${out}/stamp" 2>/dev/null || true)" != "${stamp}" ]; then
	log "host ffmpeg: unpack + configure (set ${stamp})"
	rm -rf "${out}"
	mkdir -p "${out}"
	tar -C "${out}" -xzf "${TARBALL}"
	mv "${out}/ffmpeg-6.1" "${FS}"
	for p in "${vp}"/patches/*.patch; do
		patch -d "${FS}" -p1 -s --no-backup-if-mismatch < "${p}" || die "patch failed: $(basename "${p}")"
	done
	( cd "${FS}" && ./configure "${FF_COMMON[@]}" --enable-sdl2 --disable-x86asm --disable-programs --enable-ffplay \
		> "${out}/configure.log" 2>&1 ) || { tail -20 "${out}/configure.log"; die "configure failed"; }
	echo "${stamp}" > "${out}/stamp"
fi
grep -qE '^#define CONFIG_FFPLAY 1$' "${FS}/config_components.h" "${FS}/config.h" 2>/dev/null \
	|| die "host configure did not enable ffplay (SDL2 development files missing?)"
make -C "${FS}" -j"${jobs}" ffplay > "${out}/make.log" 2>&1 || { tail -30 "${out}/make.log"; die "build failed"; }
FFPLAY="${FS}/ffplay"
log "host ffplay built: ${FFPLAY}"

fail=0
for clip in "${vp}"/build-out/clips/m10-*; do
	[ -f "${clip}" ] || die "no clips: run ${vp}/gen-clips.sh"
	name="$(basename "${clip}")"
	l="${out}/${name}.log"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout $((secs + 20)) \
		"${FFPLAY}" -hide_banner -loglevel info -stats -t "${secs}" -autoexit "${clip}" > "${l}" 2>&1 || true
	# the stats line is \r-terminated: split it; take the last one
	last="$(tr '\r' '\n' < "${l}" | grep -E '^ *[0-9.-]+ (A-V|M-V|M-A)' | tail -1 || true)"
	vdec="$(grep -m1 -oE 'Video: [a-z0-9]+' "${l}" || true)"
	adec="$(grep -m1 -oE 'Audio: [a-z0-9]+' "${l}" || true)"
	# expected on this host and not counted: the dummy video driver has no accelerated renderer
	# (ffplay falls back to SDL's software one), and --disable-x86asm has no fast swscale path
	errs="$(tr '\r' '\n' < "${l}" | grep -vE 'Failed to initialize a hardware accelerated renderer|No accelerated colorspace conversion' \
		| grep -cE 'Error|error|Failed|failed|Invalid' || true)"
	clock="$(awk '{print $1}' <<< "${last}")"
	ok=1
	[ -n "${last}" ] || ok=0
	awk -v c="${clock:-0}" -v s="${secs}" 'BEGIN { exit !(c + 0 >= s - 1.5) }' || ok=0
	[ "${errs}" = 0 ] || ok=0
	[ "${ok}" = 1 ] || fail=1
	log "$([ "${ok}" = 1 ] && echo PASS || echo FAIL) ${name}: ${vdec:-no video} / ${adec:-no audio}, errors=${errs}, last stats: ${last:-none}"
done
[ "${fail}" = 0 ] || die "a clip failed (logs in ${out})"
log "all clips play headless on the host with this component set"

# The control knobs of patches/0001 (the Pi cycles use them): a scripted pause / resume / seek
# / fullscreen / quit through ffplay's own event loop, graded from the newline stat lines.
clip="${vp}/build-out/clips/m10-h264-720p30-aac.mp4"
l="${out}/autokeys.log"
FFPLAY_STATLINE_MS=500 FFPLAY_AUTOKEYS="2:pause,4:pause,5:right,7:fs,8:fs,9.5:quit" \
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 40 "${FFPLAY}" -hide_banner -loglevel info "${clip}" > "${l}" 2>&1
rc=$?
keys="$(grep -c '^ffplay-auto t=' "${l}" || true)"
cr="$(tr -cd '\r' < "${l}" | wc -c)"
# paused=1 between the two pauses; the clock stands still while paused; +10 s after "right"
paused="$(awk '/^ffplay-stat/ { for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
	if (v["t"] > 2.6 && v["t"] < 3.9) { n++; if (v["paused"] == 1) p++; c[n] = v["clock"] } }
	END { printf "%d/%d still=%s", p, n, (n >= 2 && c[1] == c[n]) ? "yes" : "no" }' "${l}")"
# (the seek's jump: the clock gained over the key's clock, minus the wall time played since)
seek="$(awk '/^ffplay-auto .*key=right/ { split($2, kt, "="); split($4, kv, "="); before = kv[2] }
	/^ffplay-stat/ && before != "" && !done { for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
	if (v["t"] > kt[2] + 0.5) { printf "%.1f", v["clock"] - before - (v["t"] - kt[2]); done = 1 } }' "${l}")"
fs="$(awk '/^ffplay-stat/ { for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
	if (v["t"] > 7.3 && v["t"] < 7.9) f = f v["fs"]; if (v["t"] > 8.5 && v["t"] < 9.3) g = g v["fs"] }
	END { printf "%s>%s", substr(f, 1, 1), substr(g, 1, 1) }' "${l}")"
fps="$(awk '/^ffplay-stat/ { for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
	if (v["t"] > 0.9 && v["t"] < 1.9) printf "%s ", v["fps"] }' "${l}")"
log "autokeys: rc=${rc} keys=${keys}/6 cr_chars=${cr} paused=${paused} seek_delta=${seek:-none} fs=${fs} fps(1-2s)=${fps}"
ok=1
[ "${rc}" = 0 ] && [ "${keys}" = 6 ] && [ "${cr}" = 0 ] || ok=0
case "${paused}" in 0/*|*still=no) ok=0 ;; esac
# ffplay seeks to a keyframe: the clips' 2 s GOP makes the jump 10 s +- 2 s
awk -v d="${seek:-0}" 'BEGIN { exit !(d >= 8 && d <= 12.5) }' || ok=0
[ "${fs}" = "1>0" ] || ok=0
[ "${ok}" = 1 ] || die "autokeys control FAILED (${l})"
log "PASS autokeys: pause holds the clock, right seeks +10 s, f toggles fullscreen, q quits"

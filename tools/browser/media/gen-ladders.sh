#!/bin/bash
#
# gen-ladders.sh -- the streaming-video test media (docs/browser/MSE-DESIGN.md §9.2), made on the
# host with its own ffmpeg (libx265, libx264, aac, libopus). Data, never committed: everything goes
# under the media root (default artifacts/media/, git-ignored), which serve-media.py serves.
#
#     tools/browser/media/gen-ladders.sh [--root DIR] [--secs N] [--force] [--only "name ..."]
#
# 1. renditions/  every picture/sound stream is encoded ONCE (the same 2 s closed GOPs everywhere)
#                 and every ladder below is a stream copy of these, so a variant decodes to the
#                 same pictures in every packaging (fMP4, TS, byte ranges, AES-128, DASH segments).
#                 Picture: testsrc2 + light temporal noise (so the bit rates are real), burned-in
#                 "<codec> <size>" label (which variant is on screen: the HDMI snapshot proves the
#                 player's choice), frame number + time (dropped frames are visible), and a white
#                 square on the first 3 frames of every second; sound: a 440 Hz tone with a beep at
#                 every second (A/V offset is visible and audible).
#                   hevc-1080 hevc-720 hevc-480   HEVC Main 8-bit (hvc1), 4.5 / 2.5 / 1.2 Mb/s
#                   hevc10-1080                   HEVC Main10, 4.5 Mb/s
#                   h264-1080 h264-720 h264-480 h264-360   H.264 High 4.0 / High 3.1 / Main 3.0 / Main 3.0
#                   aac (AAC-LC 128k stereo 48 kHz), opus (96k stereo)
# 2. ladders/<name>/master.m3u8 (+ <variant>/index.m3u8, init + segments), VOD, 2 s segments:
#                   hevc-fmp4   h264-720, hevc-480, hevc-1080, h264-480, hevc-720 + AAC group (fMP4)
#                   hevc-ts     h264-720, hevc-1080, each with AAC muxed in (MPEG-TS)
#                   hevc-main10 hevc10-1080, h264-720 + AAC group (fMP4)
#                   h264-only   h264-1080, h264-360, h264-720 + AAC group (fMP4)
#                   byterange   h264-720 + AAC in ONE file, EXT-X-BYTERANGE (fMP4, single_file)
#                   aes         h264-720 + AAC, MPEG-TS, AES-128 (key aes.key, explicit IV)
#                   audio-only  aac, opus (fMP4)
#                 The variant order is deliberate: the right choice is never simply the first or
#                 the last line, so a player that does not choose shows it. manifest.json says what
#                 the stage-0 policy (§6.4) must pick ("expect").
# 3. mse/<rep>/init.mp4 + seg-00001.m4s ... (DASH muxer, one track each): hevc-1080 hevc-720
#                 h264-720 h264-480 aac opus; mse/manifest.json lists them with codec strings.
#
# ffmpeg's hls muxer writes no CODECS attribute for HEVC variants and no FRAME-RATE at all:
# ladder-manifest.py rewrites every master with the RFC 6381 strings read from the streams
# themselves (hvcC/avcC/esds) and writes manifest.json (variants, codecs, segment counts, sha256).
#
# Idempotent: a rendition or ladder already made with the same settings is kept (--force remakes).
# Encodes run one after another (x265 at 1080p in parallel is a memory risk on the build host).
#
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../.." && pwd)
root=${MEDIA_ROOT:-${repo}/artifacts/media}
secs=60
force=0
only=""
while [ $# -gt 0 ]; do
	case "$1" in
		--root) root=$2; shift 2 ;;
		--secs) secs=$2; shift 2 ;;
		--force) force=1; shift ;;
		--only) only=$2; shift 2 ;;
		*) echo "usage: $0 [--root DIR] [--secs N] [--force] [--only \"name ...\"]" >&2; exit 2 ;;
	esac
done
font=/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf
[ -f "${font}" ] || { echo "gen-ladders: ${font} missing (fonts-dejavu-core)" >&2; exit 1; }
encoders=$(ffmpeg -hide_banner -encoders 2>/dev/null)
for enc in libx265 libx264 aac libopus; do
	case "${encoders}" in
		*" ${enc} "*) ;;
		*) echo "gen-ladders: the host ffmpeg has no ${enc} encoder" >&2; exit 1 ;;
	esac
done
ffver=$(ffmpeg -hide_banner -version | head -1 | cut -d' ' -f3)
rend=${root}/renditions
mkdir -p "${rend}" "${root}/ladders" "${root}/mse"
ff=(ffmpeg -hide_banner -nostdin -loglevel error -y)
keys=(-force_key_frames "expr:gte(t,n_forced*2)")
x265keys="keyint=60:min-keyint=60:scenecut=0:open-gop=0:log-level=error"

wanted() {  # wanted <name>: in --only (or no --only)
	[ -z "${only}" ] && return 0
	case " ${only} " in *" $1 "*) return 0 ;; esac
	return 1
}

fresh() {  # fresh <file> <settings string>: the file exists and was made with these settings
	[ "${force}" = 0 ] && [ -s "$1" ] && [ -f "$1.settings" ] && [ "$(cat "$1.settings")" = "$2" ]
}

# --- 1. renditions ---------------------------------------------------------------------------------
video() {  # video <name> <WxH> <label> <encoder args...>
	local name=$1 size=$2 label=$3 out=${rend}/$1.mp4 fs
	shift 3
	local settings="v3 ${secs}s ${size} ${label} $* ffmpeg-${ffver}"
	if fresh "${out}" "${settings}"; then
		echo "gen-ladders: rendition ${name} (kept)"
		return
	fi
	echo "gen-ladders: rendition ${name} ${size} $*"
	fs=$(( ${size#*x} / 14 ))
	printf '%s' "${label}" > "${rend}/${name}.label"
	printf 'frame %%{frame_num}  t=%%{pts:hms}' > "${rend}/${name}.clock"
	local vf="noise=alls=5:allf=t"
	vf+=",drawbox=x=iw-ih/6-20:y=20:w=ih/6:h=ih/6:color=white:t=fill:enable='lt(mod(n,30),3)'"
	vf+=",drawtext=fontfile=${font}:textfile=${rend}/${name}.label:x=20:y=20:fontsize=${fs}:fontcolor=white:box=1:boxcolor=black@0.7:boxborderw=8"
	vf+=",drawtext=fontfile=${font}:textfile=${rend}/${name}.clock:x=20:y=$((fs * 2 + 20)):fontsize=${fs}:fontcolor=yellow:box=1:boxcolor=black@0.7:boxborderw=8"
	"${ff[@]}" -f lavfi -i "testsrc2=size=${size}:rate=30:duration=${secs}" -vf "${vf}" -an "${keys[@]}" "$@" "${out}"
	echo "${settings}" > "${out}.settings"
}

audio() {  # audio <name> <encoder args...>
	local name=$1 out=${rend}/$1.mp4
	shift
	local settings="a2 ${secs}s $* ffmpeg-${ffver}"
	if fresh "${out}" "${settings}"; then
		echo "gen-ladders: rendition ${name} (kept)"
		return
	fi
	echo "gen-ladders: rendition ${name} $*"
	"${ff[@]}" -f lavfi -i "sine=frequency=440:beep_factor=4:sample_rate=48000:duration=${secs}" -ac 2 -vn "$@" "${out}"
	echo "${settings}" > "${out}.settings"
}

x265() {  # x265 <kbps> [extra args]: the HEVC encoder settings
	local kbps=$1
	shift
	echo -c:v libx265 -preset fast -b:v "${kbps}k" -maxrate "$((kbps * 11 / 10))k" -bufsize "$((kbps * 2))k" \
		-tag:v hvc1 -x265-params "${x265keys}" "$@"
}
x264() {  # x264 <kbps> <profile> <level>
	echo -c:v libx264 -preset fast -profile:v "$2" -level:v "$3" -b:v "$1k" -maxrate "$(($1 * 11 / 10))k" \
		-bufsize "$(($1 * 2))k" -g 60 -keyint_min 60 -sc_threshold 0 -pix_fmt yuv420p
}

# shellcheck disable=SC2046 # the encoder settings are plain words
{
	video hevc-1080 1920x1080 "HEVC 1080p" $(x265 4500 -pix_fmt yuv420p -profile:v main)
	video hevc-720 1280x720 "HEVC 720p" $(x265 2500 -pix_fmt yuv420p -profile:v main)
	video hevc-480 854x480 "HEVC 480p" $(x265 1200 -pix_fmt yuv420p -profile:v main)
	video hevc10-1080 1920x1080 "HEVC Main10 1080p" $(x265 4500 -pix_fmt yuv420p10le -profile:v main10)
	video h264-1080 1920x1080 "H.264 1080p" $(x264 6000 high 4.0)
	video h264-720 1280x720 "H.264 720p" $(x264 3000 high 3.1)
	video h264-480 854x480 "H.264 480p" $(x264 1500 main 3.0)
	video h264-360 640x360 "H.264 360p" $(x264 800 main 3.0)
}
audio aac -c:a aac -b:a 128k
audio opus -c:a libopus -b:a 96k

# --- 2. HLS ladders --------------------------------------------------------------------------------
# ladder <name> <spec>: spec = the variant names in master order; renditions are inputs in that
# order, "audio" adds the AAC group (fMP4) -- see the case below for each ladder's muxer options.
ladder_settings() {  # the settings a ladder is made from (its renditions' settings + this script's)
	local f s="l5 $1 ffmpeg-${ffver}"
	for f in "${rend}"/*.settings; do s+=" $(basename "${f}"):$(cat "${f}")"; done
	echo "${s}"
}

hls_group() {  # hls_group <name> <segment type> <video renditions...>: video variants + an AAC group
	local name=$1 type=$2 dir=${root}/ladders/$1 inputs=() maps=() tags=() vsm="" i=0 r ext=m4s
	shift 2
	[ "${type}" = mpegts ] && ext=ts
	for r in "$@"; do
		inputs+=(-i "${rend}/${r}.mp4")
		maps+=(-map "${i}:v")
		case "${r}" in hevc*) tags+=("-tag:v:${i}" hvc1) ;; esac
		vsm+="v:${i},agroup:aud,name:${r} "
		i=$((i + 1))
	done
	inputs+=(-i "${rend}/aac.mp4")
	maps+=(-map "${i}:a")
	vsm+="a:0,agroup:aud,name:audio,default:yes"
	"${ff[@]}" "${inputs[@]}" "${maps[@]}" -c copy "${tags[@]}" -f hls -hls_time 2 -hls_playlist_type vod \
		-hls_segment_type "${type}" -hls_fmp4_init_filename init.mp4 \
		-hls_segment_filename "${dir}/%v/seg-%05d.${ext}" -master_pl_name master.m3u8 \
		-var_stream_map "${vsm}" "${dir}/%v/index.m3u8"
}

hls_muxed() {  # hls_muxed <name> <segment type> <video renditions...>: each variant video + AAC
	local name=$1 type=$2 dir=${root}/ladders/$1 inputs=() maps=() tags=() vsm="" i=0 r ext=m4s
	shift 2
	[ "${type}" = mpegts ] && ext=ts
	for r in "$@"; do
		inputs+=(-i "${rend}/${r}.mp4")
		i=$((i + 1))
	done
	inputs+=(-i "${rend}/aac.mp4")
	i=0
	for r in "$@"; do
		maps+=(-map "${i}:v" -map "$#:a")
		case "${r}" in hevc*) tags+=("-tag:v:${i}" hvc1) ;; esac
		vsm+="v:${i},a:${i},name:${r} "
		i=$((i + 1))
	done
	"${ff[@]}" "${inputs[@]}" "${maps[@]}" -c copy "${tags[@]}" -f hls -hls_time 2 -hls_playlist_type vod \
		-hls_segment_type "${type}" -hls_fmp4_init_filename init.mp4 \
		-hls_segment_filename "${dir}/%v/seg-%05d.${ext}" -master_pl_name master.m3u8 \
		-var_stream_map "${vsm% }" "${dir}/%v/index.m3u8"
}

make_ladder() {  # make_ladder <name>
	local name=$1 dir=${root}/ladders/$1 settings
	wanted "${name}" || return 0
	settings=$(ladder_settings "${name}")
	if [ "${force}" = 0 ] && [ -s "${dir}/manifest.json" ] && [ -f "${dir}/.settings" ] &&
		[ "$(cat "${dir}/.settings")" = "${settings}" ]; then
		echo "gen-ladders: ladder ${name} (kept)"
		return
	fi
	echo "gen-ladders: ladder ${name}"
	rm -rf "${dir}"
	mkdir -p "${dir}"
	case "${name}" in
		hevc-fmp4) hls_group "${name}" fmp4 h264-720 hevc-480 hevc-1080 h264-480 hevc-720 ;;
		hevc-ts) hls_muxed "${name}" mpegts h264-720 hevc-1080 ;;
		hevc-main10) hls_group "${name}" fmp4 hevc10-1080 h264-720 ;;
		h264-only) hls_group "${name}" fmp4 h264-1080 h264-360 h264-720 ;;
		byterange)
			"${ff[@]}" -i "${rend}/h264-720.mp4" -i "${rend}/aac.mp4" -map 0:v -map 1:a -c copy -f hls -hls_time 2 \
				-hls_playlist_type vod -hls_segment_type fmp4 -hls_flags single_file \
				-hls_segment_filename "${dir}/%v/stream.mp4" -master_pl_name master.m3u8 \
				-var_stream_map "v:0,a:0,name:h264-720" "${dir}/%v/index.m3u8"
			;;
		aes)
			# a fixed test key and IV (not a secret: the point is the decryption path). The IV is
			# explicit so the live simulation's renumbered segments still decrypt.
			printf '\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff' > "${dir}/aes.key"
			printf '../aes.key\n%s\n0f0e0d0c0b0a09080706050403020100\n' "${dir}/aes.key" > "${dir}/aes.keyinfo"
			"${ff[@]}" -i "${rend}/h264-720.mp4" -i "${rend}/aac.mp4" -map 0:v -map 1:a -c copy -f hls -hls_time 2 \
				-hls_playlist_type vod -hls_segment_type mpegts -hls_key_info_file "${dir}/aes.keyinfo" \
				-hls_segment_filename "${dir}/%v/seg-%05d.ts" -master_pl_name master.m3u8 \
				-var_stream_map "v:0,a:0,name:h264-720" "${dir}/%v/index.m3u8"
			rm -f "${dir}/aes.keyinfo"
			;;
		audio-only)
			"${ff[@]}" -i "${rend}/aac.mp4" -i "${rend}/opus.mp4" -map 0:a -map 1:a -c copy -f hls -hls_time 2 \
				-hls_playlist_type vod -hls_segment_type fmp4 -hls_fmp4_init_filename init.mp4 \
				-hls_segment_filename "${dir}/%v/seg-%05d.m4s" -master_pl_name master.m3u8 \
				-var_stream_map "a:0,name:aac a:1,name:opus" "${dir}/%v/index.m3u8"
			;;
		*) echo "gen-ladders: unknown ladder ${name}" >&2; exit 1 ;;
	esac
	python3 "${here}/ladder-manifest.py" hls "${dir}" --renditions "${rend}" --secs "${secs}" --ffmpeg "${ffver}"
	echo "${settings}" > "${dir}/.settings"
}

for l in hevc-fmp4 hevc-ts hevc-main10 h264-only byterange aes audio-only; do
	make_ladder "${l}"
done

# --- 3. MSE segment sets ---------------------------------------------------------------------------
if wanted mse; then
	settings=$(ladder_settings mse)
	if [ "${force}" = 0 ] && [ -s "${root}/mse/manifest.json" ] && [ -f "${root}/mse/.settings" ] &&
		[ "$(cat "${root}/mse/.settings")" = "${settings}" ]; then
		echo "gen-ladders: mse (kept)"
	else
		rm -rf "${root}/mse"
		for r in hevc-1080 hevc-720 h264-720 h264-480 aac opus; do
			echo "gen-ladders: mse ${r}"
			mkdir -p "${root}/mse/${r}"
			tag=()
			case "${r}" in hevc*) tag=(-tag:v hvc1) ;; esac
			"${ff[@]}" -i "${rend}/${r}.mp4" -map 0 -c copy "${tag[@]}" -f dash -dash_segment_type mp4 -seg_duration 2 -use_template 1 \
				-use_timeline 0 -init_seg_name init.mp4 -media_seg_name 'seg-$Number%05d$.m4s' \
				"${root}/mse/${r}/manifest.mpd"
		done
		python3 "${here}/ladder-manifest.py" mse "${root}/mse" --renditions "${rend}" --secs "${secs}" --ffmpeg "${ffver}"
		echo "${settings}" > "${root}/mse/.settings"
	fi
fi

echo "gen-ladders: done ${root} ($(du -sh "${root}" | cut -f1); renditions $(du -sh "${rend}" | cut -f1))"

#!/usr/bin/env bash
#
# The rpivid tool-coverage stream set: HEVC streams that each exercise coding tools beyond
# the set the Pi 4's rpivid block was first proved bit-exact on, with a per-frame md5
# reference, for `hevc-rpivid-check <dir>` on the Pi (ports video_player).
#
#   gen-set.sh --out <dir> --source <video> [--conf <dir>] [--ref-tool <hevc-rpivid-check>] [--stage <dir>]
#
#   --source    natural-content video the encodes are made from (the PeerTube upload of
#               2026-10-07, 1080x1920 HEVC); also copied into the set as real-peertube-1080.mp4
#   --conf      the HEVC v1 conformance bitstreams (rsync://fate-suite.ffmpeg.org/fate-suite/
#               hevc-conformance/); the Main / Main 10 ones FFmpeg's FATE runs are copied in
#   --ref-tool  a HOST hevc-rpivid-check built against FFmpeg 6.1 (the port's hosttest/run.sh
#               builds one: <work>/hevc-rpivid-check): the references are then that version's
#               CPU decode, the very decoder the Pi compares with; without it the host ffmpeg's
#               -f framemd5. With it the host ffmpeg's md5s are computed too and compared
#               (MANIFEST: ref8=same|DIFF)
#   --stage     copy the set there afterwards (the NFS export's
#               /usr/share/video-demo/rpivid-check/)
#
# Every stream <s> gets <s>.md5 (framemd5 lines, display order) next to it. Encoders: the
# host ffmpeg's libx265, hevc_vaapi and hevc_vulkan (AMD VCN here), whichever are present.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

out="" source="" conf="" reftool="" stage=""
while [ $# -gt 0 ]; do
	case "$1" in
		--out) out="$2"; shift ;;
		--source) source="$2"; shift ;;
		--conf) conf="$2"; shift ;;
		--ref-tool) reftool="$2"; shift ;;
		--stage) stage="$2"; shift ;;
		-h|--help) sed -n '2,27p' "$0"; exit 0 ;;
		*) echo "gen-set.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
[ -n "${out}" ] && [ -f "${source}" ] || { echo "gen-set.sh: --out <dir> --source <video> are required" >&2; exit 2; }
mkdir -p "${out}"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
ff=(ffmpeg -nostdin -hide_banner -v error -y)

# --- source frames: 120 frames from 5 s in, three geometries ------------------------------
"${ff[@]}" -ss 5 -i "${source}" -an -frames:v 120 -vf scale=720:1280:flags=bicubic -pix_fmt yuv420p -f rawvideo "${tmp}/p720.yuv"
"${ff[@]}" -ss 5 -i "${source}" -an -frames:v 120 -vf 'crop=1080:608:0:656,scale=1920:1080:flags=bicubic' -pix_fmt yuv420p \
	-f rawvideo "${tmp}/l1080.yuv"
"${ff[@]}" -ss 5 -i "${source}" -an -frames:v 30 -vf scale=360:640:flags=bicubic -pix_fmt yuv420p -f rawvideo "${tmp}/p360.yuv"
raw() { echo -f rawvideo -pix_fmt yuv420p -s "$1" -framerate 60 -i "${tmp}/$2.yuv"; }
# (not `ffmpeg -encoders | grep -q`: under pipefail grep's early exit fails the pipeline)
encoders="$(ffmpeg -hide_banner -encoders 2>/dev/null)"

# --- libx265: one tool each on top of the preset (hash=1: an MD5 picture hash SEI too) ------
x265() { # <name> <geometry> <yuv> <preset> <x265-params> [ffmpeg args...]
	local name="$1" geom="$2" yuv="$3" preset="$4" params="$5"
	shift 5
	# shellcheck disable=SC2046
	"${ff[@]}" $(raw "${geom}" "${yuv}") "$@" -c:v libx265 -preset "${preset}" -crf 26 \
		-x265-params "log-level=error:hash=1:keyint=60:${params}" "${out}/${name}.mp4" 2>"${tmp}/x265.err" ||
		{ echo "gen-set.sh: ${name}: x265 refused ${params}: $(grep -m1 'x265 \[error\]' "${tmp}/x265.err")" >&2; rm -f "${out}/${name}.mp4"; }
}
if [[ "${encoders}" == *libx265* ]]; then
	x265 x265-medium 720x1280 p720 medium ""
	x265 x265-tu-intra2 720x1280 p720 medium "tu-intra-depth=2"
	x265 x265-tu-inter2 720x1280 p720 medium "tu-inter-depth=2"
	x265 x265-tu-4-4 720x1280 p720 medium "tu-intra-depth=4:tu-inter-depth=4:limit-tu=0"
	x265 x265-amp 720x1280 p720 medium "rect=1:amp=1"
	x265 x265-nosignhide 720x1280 p720 medium "signhide=0"
	x265 x265-tskip 720x1280 p720 medium "tskip=1:rdoq-level=2"
	x265 x265-qg64 720x1280 p720 medium "qg-size=64"
	x265 x265-qg16 720x1280 p720 medium "qg-size=16"
	x265 x265-cqp-noaq 720x1280 p720 medium "aq-mode=0:cutree=0:qp=30"
	x265 x265-scaling 720x1280 p720 medium "scaling-list=default"
	x265 x265-ctu32 720x1280 p720 medium "ctu=32"
	x265 x265-ctu16 720x1280 p720 medium "ctu=16:max-tu-size=16"
	x265 x265-mincu16 720x1280 p720 medium "min-cu-size=16"
	x265 x265-maxtu16 720x1280 p720 medium "max-tu-size=16"
	x265 x265-ultrafast 720x1280 p720 ultrafast ""
	x265 x265-veryslow 720x1280 p720 veryslow ""
	x265 x265-slices4 720x1280 p720 medium "slices=4"
	x265 x265-deblock-m2 720x1280 p720 medium "deblock=-2,-2"
	x265 x265-nodeblock 720x1280 p720 medium "no-deblock=1"
	x265 x265-chromaqp 720x1280 p720 medium "cbqpoffs=-3:crqpoffs=2"
	x265 x265-nostrongsmooth 720x1280 p720 medium "strong-intra-smoothing=0"
	x265 x265-constrained-intra 720x1280 p720 medium "constrained-intra=1"
	x265 x265-lossless 360x640 p360 medium "lossless=1"
	x265 x265-10bit-tu 720x1280 p720 medium "tu-intra-depth=2:tu-inter-depth=2:amp=1:rect=1" -pix_fmt yuv420p10le
	x265 x265-1080p-tu-amp 1920x1080 l1080 slow "tu-intra-depth=2:tu-inter-depth=2"
fi
# the PeerTube stream's own tool mix (intra TU depth 1, CU QP delta depth 0, no WPP) at 1080x1920
if [[ "${encoders}" == *libx265* ]]; then
	"${ff[@]}" -ss 5 -i "${source}" -an -frames:v 120 -c:v libx265 -preset medium -crf 26 \
		-x265-params "log-level=error:hash=1:keyint=60:tu-intra-depth=2:qg-size=64:wpp=0" "${out}/x265-peertube-like-1080.mp4"
fi

# --- hardware encoders of the host (VA-API, Vulkan video) --------------------------------
if [ -e /dev/dri/renderD128 ]; then
	va=(-vaapi_device /dev/dri/renderD128 -vf format=nv12,hwupload)
	# shellcheck disable=SC2046
	"${ff[@]}" $(raw 1920x1080 l1080) "${va[@]}" -c:v hevc_vaapi -qp 26 "${out}/vaapi-1080p.mp4" || rm -f "${out}/vaapi-1080p.mp4"
	# shellcheck disable=SC2046
	"${ff[@]}" $(raw 1920x1080 l1080) "${va[@]}" -c:v hevc_vaapi -qp 26 -slices 4 "${out}/vaapi-1080p-slices4.mp4" ||
		rm -f "${out}/vaapi-1080p-slices4.mp4"
	# shellcheck disable=SC2046
	"${ff[@]}" $(raw 720x1280 p720) "${va[@]}" -c:v hevc_vaapi -rc_mode VBR -b:v 3M "${out}/vaapi-720x1280-vbr.mp4" ||
		rm -f "${out}/vaapi-720x1280-vbr.mp4"
	# shellcheck disable=SC2046
	"${ff[@]}" $(raw 1920x1080 l1080) -init_hw_device vulkan -vf format=nv12,hwupload -c:v hevc_vulkan -qp 26 \
		"${out}/vulkan-1080p.mp4" ||
		rm -f "${out}/vulkan-1080p.mp4"
fi

# --- the real upload and the conformance bitstreams -------------------------------------
cp "${source}" "${out}/real-peertube-1080.mp4"
if [ -n "${conf}" ]; then
	# FFmpeg FATE's 8- and 10-bit Main lists (tests/fate/hevc.mak), as .265 (the raw demuxer)
	for b in AMP_A_Samsung_6 AMP_B_Samsung_6 AMP_D_Hisilicon AMP_E_Hisilicon AMP_F_Hisilicon_3 AMVP_A_MTK_4 AMVP_B_MTK_4 \
		AMVP_C_Samsung_6 BUMPING_A_ericsson_1 CAINIT_A_SHARP_4 CAINIT_B_SHARP_4 CAINIT_C_SHARP_3 CAINIT_D_SHARP_3 \
		CAINIT_E_SHARP_3 CAINIT_F_SHARP_3 CAINIT_G_SHARP_3 CAINIT_H_SHARP_3 CIP_A_Panasonic_3 cip_B_NEC_3 CIP_C_Panasonic_2 \
		CONFWIN_A_Sony_1 DBLK_A_SONY_3 DBLK_B_SONY_3 DBLK_C_SONY_3 DBLK_D_VIXS_2 DBLK_E_VIXS_2 DBLK_F_VIXS_2 DBLK_G_VIXS_2 \
		DBLK_A_MAIN10_VIXS_3 DELTAQP_A_BRCM_4 DELTAQP_B_SONY_3 DELTAQP_C_SONY_3 DSLICE_A_HHI_5 DSLICE_B_HHI_5 DSLICE_C_HHI_5 \
		ENTP_A_Qualcomm_1 ENTP_B_Qualcomm_1 ENTP_C_Qualcomm_1 ENTP_A_LG_2 ENTP_B_LG_2 ENTP_C_LG_3 EXT_A_ericsson_4 \
		FILLER_A_Sony_1 HRD_A_Fujitsu_3 INITQP_A_Sony_1 INITQP_B_Sony_1 ipcm_A_NEC_3 ipcm_B_NEC_3 ipcm_C_NEC_3 ipcm_D_NEC_3 \
		ipcm_E_NEC_2 IPRED_A_docomo_2 IPRED_B_Nokia_3 IPRED_C_Mitsubishi_3 LS_A_Orange_2 LS_B_ORANGE_4 LTRPSPS_A_Qualcomm_1 \
		MAXBINS_A_TI_4 MAXBINS_B_TI_4 MAXBINS_C_TI_4 MERGE_A_TI_3 MERGE_B_TI_3 MERGE_C_TI_3 MERGE_D_TI_3 MERGE_E_TI_3 \
		MERGE_F_MTK_4 MERGE_G_HHI_4 MVCLIP_A_qualcomm_3 MVDL1ZERO_A_docomo_3 MVEDGE_A_qualcomm_3 NoOutPrior_A_Qualcomm_1 \
		NoOutPrior_B_Qualcomm_1 NUT_A_ericsson_5 OPFLAG_A_Qualcomm_1 OPFLAG_B_Qualcomm_1 OPFLAG_C_Qualcomm_1 PICSIZE_A_Bossen_1 \
		PICSIZE_B_Bossen_1 PICSIZE_C_Bossen_1 PICSIZE_D_Bossen_1 PMERGE_A_TI_3 PMERGE_B_TI_3 PMERGE_C_TI_3 PMERGE_D_TI_3 \
		PMERGE_E_TI_3 POC_A_Bossen_3 PPS_A_qualcomm_7 PS_B_VIDYO_3 RAP_A_docomo_4 RAP_B_Bossen_1 RPLM_A_qualcomm_4 \
		RPLM_B_qualcomm_4 RPS_A_docomo_4 RPS_B_qualcomm_5 RPS_C_ericsson_5 RPS_D_ericsson_6 RPS_E_qualcomm_5 RPS_F_docomo_1 \
		RQT_A_HHI_4 RQT_B_HHI_4 RQT_C_HHI_4 RQT_D_HHI_4 RQT_E_HHI_4 RQT_F_HHI_4 RQT_G_HHI_4 SAO_A_MediaTek_4 SAO_B_MediaTek_5 \
		SAO_C_Samsung_5 SAO_D_Samsung_5 SAO_E_Canon_4 SAO_F_Canon_3 SAO_G_Canon_3 SDH_A_Orange_3 SLICES_A_Rovi_3 SLIST_A_Sony_4 \
		SLIST_B_Sony_8 SLIST_C_Sony_3 SLIST_D_Sony_9 SLPPLP_A_VIDYO_2 STRUCT_A_Samsung_5 STRUCT_B_Samsung_6 TILES_A_Cisco_2 \
		TILES_B_Cisco_1 TMVP_A_MS_3 TSCL_A_VIDYO_5 TSCL_B_VIDYO_4 TSKIP_A_MS_3 TUSIZE_A_Samsung_1 \
		VPSID_A_VIDYO_2 WP_A_Toshiba_3 WP_B_Toshiba_3 WP_A_MAIN10_Toshiba_3 WP_MAIN10_B_Toshiba_3 WPP_A_ericsson_MAIN_2 \
		WPP_B_ericsson_MAIN_2 WPP_C_ericsson_MAIN_2 WPP_D_ericsson_MAIN_2 WPP_E_ericsson_MAIN_2 WPP_F_ericsson_MAIN_2 \
		WPP_A_ericsson_MAIN10_2 WPP_B_ericsson_MAIN10_2 WPP_C_ericsson_MAIN10_2 WPP_D_ericsson_MAIN10_2 WPP_E_ericsson_MAIN10_2 \
		WPP_F_ericsson_MAIN10_2; do
		if [ -f "${conf}/${b}.bit" ]; then
			cp "${conf}/${b}.bit" "${out}/conf-${b}.265"
		else
			echo "gen-set.sh: no ${conf}/${b}.bit" >&2
		fi
	done
fi

# --- references -------------------------------------------------------------------------
# ffmpeg's framemd5 of the decoded frames, display order, no frame dropped or repeated
md5_ffmpeg() { "${ff[@]}" -i "$1" -map 0:v:0 -fps_mode passthrough -f framemd5 "$2"; }
# the FFmpeg 6.1 CPU decoder of hevc-rpivid-check (-cpu -md5: "MD5 hevc <i> <pts> <md5>")
md5_61() {
	"${reftool}" -cpu -md5 -q "$1" >"${tmp}/md5.out" 2>&1 || true
	{
		echo "#format: frame md5 of the decoded frames, display order (hevc-rpivid-check -cpu -md5, FFmpeg 6.1)"
		awk '$1 == "MD5" && $2 == "hevc" { printf "0, %s, %s, 1, 0, %s\n", $3, $4, $5 }' "${tmp}/md5.out"
	} >"$2"
}
manifest="${out}/MANIFEST"
printf '# stream bytes frames ref ref8\n' >"${manifest}"
for f in "${out}"/*.mp4 "${out}"/*.265; do
	[ -f "${f}" ] || continue
	if [ -n "${reftool}" ]; then
		md5_61 "${f}" "${f}.md5"
		md5_ffmpeg "${f}" "${tmp}/ff.md5" 2>/dev/null || : >"${tmp}/ff.md5"
		if cmp -s <(grep -v '^#' "${f}.md5" | awk -F', *' '{print $NF}') <(grep -v '^#' "${tmp}/ff.md5" | awk -F', *' '{print $NF}'); then
			r8=same
		else
			r8=DIFF
		fi
		ref=ffmpeg6.1
	else
		md5_ffmpeg "${f}" "${f}.md5"
		ref="ffmpeg$(ffmpeg -version | awk 'NR == 1 { print $3 }')" r8=-
	fi
	printf '%s %s %s %s %s\n' "$(basename "${f}")" "$(stat -c %s "${f}")" "$(grep -vc '^#' "${f}.md5")" "${ref}" "${r8}" >>"${manifest}"
done
echo "gen-set.sh: $(grep -vc '^#' "${manifest}") streams, $(du -sh "${out}" | cut -f1) in ${out}"
if [ -n "${stage}" ]; then
	mkdir -p "${stage}"
	cp "${out}"/* "${stage}/"
	echo "gen-set.sh: staged in ${stage}"
fi

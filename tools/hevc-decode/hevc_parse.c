/* hevc_parse.c — see hevc_parse.h. Runtime HEVC/H.265 param extractor for the
 * rpivid HW decoder, matching the build-time python generators bit-for-bit.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "hevc_parse.h"
#include <string.h>

/* ------------------------------------------------------------------ errors */
static char g_err[128];

const char *hevc_err(void) { return g_err; }

static int fail(const char *msg)
{
	/* keep it short; caller prints on negative return */
	size_t n = strlen(msg);
	if (n >= sizeof(g_err))
		n = sizeof(g_err) - 1;
	memcpy(g_err, msg, n);
	g_err[n] = 0;
	return -1;
}

/* --------------------------------------------------------------- bitreader
 * Raw reader: does NOT strip emulation-prevention bytes, so the bit position
 * equals the raw NAL byte position — which is exactly what data_byte_offset
 * must be measured in (the HW consumes raw bytes). The slice headers in this
 * subset contain no 00 00 03 sequence (verified — see the test harness), so
 * raw reading is bit-identical to the RBSP for the header. SPS/PPS are parsed
 * from an EPB-stripped copy where bit position is irrelevant. */
typedef struct {
	const uint8_t *d;
	uint32_t nbytes;
	uint32_t bitpos;   /* absolute bit index from d[0] bit7 */
	int overflow;
} br_t;

static void br_init(br_t *b, const uint8_t *d, uint32_t n)
{
	b->d = d;
	b->nbytes = n;
	b->bitpos = 0;
	b->overflow = 0;
}

static uint32_t br_pos(const br_t *b) { return b->bitpos; }

static uint32_t br_u(br_t *b, uint32_t n)
{
	uint32_t v = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t byte = b->bitpos >> 3;
		uint32_t bit = 7u - (b->bitpos & 7u);
		uint32_t x = 0;
		if (byte < b->nbytes)
			x = (b->d[byte] >> bit) & 1u;
		else
			b->overflow = 1;
		v = (v << 1) | x;
		b->bitpos++;
	}
	return v;
}

/* ue(v) Exp-Golomb (H.265 9.2). */
static uint32_t br_ue(br_t *b)
{
	int zeros = 0;
	while (br_u(b, 1) == 0 && !b->overflow) {
		if (++zeros > 31) {        /* codeNum would overflow 32 bits: reject */
			b->overflow = 1;
			return 0;
		}
	}
	uint32_t rest = zeros ? br_u(b, zeros) : 0;
	return ((1u << zeros) - 1u) + rest;
}

/* se(v) signed Exp-Golomb. codeNum k -> (-1)^(k+1) * ceil(k/2). */
static int32_t br_se(br_t *b)
{
	uint32_t k = br_ue(b);
	return (k & 1u) ? (int32_t)((k + 1u) >> 1) : -(int32_t)(k >> 1);
}

/* --------------------------------------------------- emulation-prevention */
/* Strip 00 00 03 -> 00 00 into dst; returns stripped length (0 on overflow). */
static uint32_t strip_epb(const uint8_t *s, uint32_t n, uint8_t *dst, uint32_t cap)
{
	uint32_t o = 0, zeros = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (zeros >= 2 && s[i] == 0x03 && i + 1 < n && s[i + 1] <= 0x03) {
			zeros = 0;       /* drop this emulation_prevention_three_byte */
			continue;
		}
		if (o >= cap)
			return 0;
		dst[o++] = s[i];
		zeros = (s[i] == 0) ? zeros + 1 : 0;
	}
	return o;
}

/* Count 00 00 03 sequences in a byte range (for the EPB report / guard). */
uint32_t hevc_count_epb(const uint8_t *s, uint32_t n)
{
	uint32_t c = 0, zeros = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (zeros >= 2 && s[i] == 0x03) { c++; zeros = 0; continue; }
		zeros = (s[i] == 0) ? zeros + 1 : 0;
	}
	return c;
}

/* ---------------------------------------------------------- NAL splitting */
int hevc_nal_next(hevc_nal_iter_t *it, hevc_nal_t *out)
{
	const uint8_t *d = it->buf;
	uint32_t n = it->size, i = it->pos;

	/* Find this NAL's start code (4-byte checked first). */
	uint32_t start = 0;
	int found = 0;
	while (i + 3 <= n) {
		if (i + 4 <= n && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 && d[i + 3] == 1) {
			start = i + 4; found = 1; break;
		}
		if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
			start = i + 3; found = 1; break;
		}
		i++;
	}
	if (!found)
		return 0;

	/* Find the next start code to bound the payload. */
	uint32_t j = start, end = n;
	while (j + 3 <= n) {
		if (d[j] == 0 && d[j + 1] == 0 && d[j + 2] == 1) {
			/* a trailing 0x00 belongs to the *next* start code, not us */
			end = (j > start && d[j - 1] == 0) ? j - 1 : j;
			break;
		}
		j++;
	}
	if (start >= end)
		return 0;

	out->data = d + start;
	out->len = end - start;
	out->type = (d[start] >> 1) & 0x3f;
	it->pos = end;   /* resume scan; next start code is at/after end */
	return 1;
}

/* -------------------------------------------------------- profile_tier_level
 * For sps_max_sub_layers_minus1 == 0 (subset), PTL is a fixed 96-bit block:
 * 2+1+5 (space/tier/profile) + 32 compat + 4 (progressive/interlaced/non_packed/
 * frame_only) + 43 reserved + 1 = 88 profile bits, then general_level_idc u(8). */
static void skip_ptl_no_sublayers(br_t *b)
{
	br_u(b, 32); br_u(b, 32); br_u(b, 24);   /* 88 profile bits */
	br_u(b, 8);                              /* general_level_idc */
}

/* short_term_ref_pic_set (subset: non-predicted only; predicted => reject). */
static int parse_st_rps(br_t *b, uint32_t idx, uint32_t num_rps)
{
	if (idx != 0) {
		if (br_u(b, 1))            /* inter_ref_pic_set_prediction_flag */
			return fail("predicted short_term_ref_pic_set (out of subset)");
	}
	uint32_t nneg = br_ue(b);
	uint32_t npos = br_ue(b);
	if (nneg > 16 || npos > 16)
		return fail("implausible RPS pic counts");
	for (uint32_t i = 0; i < nneg; i++) { br_ue(b); br_u(b, 1); }
	for (uint32_t i = 0; i < npos; i++) { br_ue(b); br_u(b, 1); }
	(void)num_rps;
	return 0;
}

/* pred_weight_table (H.265 7.3.6.3); ChromaArrayType==1 for 4:2:0. */
static void parse_pred_weight_table(br_t *b, uint32_t nrefl0, int is_b, uint32_t nrefl1)
{
	int chroma = 1;
	br_ue(b);                       /* luma_log2_weight_denom */
	if (chroma)
		br_se(b);               /* delta_chroma_log2_weight_denom */
	for (int list = 0; list <= (is_b ? 1 : 0); list++) {
		uint32_t nref = list ? nrefl1 : nrefl0;
		uint8_t lflag[16] = {0}, cflag[16] = {0};
		for (uint32_t i = 0; i < nref; i++)
			lflag[i] = (uint8_t)br_u(b, 1);
		if (chroma)
			for (uint32_t i = 0; i < nref; i++)
				cflag[i] = (uint8_t)br_u(b, 1);
		for (uint32_t i = 0; i < nref; i++) {
			if (lflag[i]) { br_se(b); br_se(b); }
			if (cflag[i]) { br_se(b); br_se(b); br_se(b); br_se(b); }
		}
	}
}

/* --------------------------------------------------------------- SPS parse */
static int sps_parse_internal(const uint8_t *nal, uint32_t len, hevc_sps_t *out)
{
	uint8_t buf[1024];
	uint32_t n = strip_epb(nal, len, buf, sizeof(buf));
	if (!n)
		return fail("SPS too large / EPB strip overflow");

	br_t b; br_init(&b, buf, n);
	br_u(&b, 16);                              /* NAL header */
	br_u(&b, 4);                               /* sps_video_parameter_set_id */
	uint32_t max_sub = br_u(&b, 3);            /* sps_max_sub_layers_minus1 */
	br_u(&b, 1);                               /* sps_temporal_id_nesting_flag */
	if (max_sub != 0)
		return fail("sps_max_sub_layers_minus1 != 0 (out of subset)");
	skip_ptl_no_sublayers(&b);

	br_ue(&b);                                 /* sps_seq_parameter_set_id */
	out->chroma_format_idc = br_ue(&b);
	if (out->chroma_format_idc == 3)
		br_u(&b, 1);                       /* separate_colour_plane_flag */
	out->width = br_ue(&b);
	out->height = br_ue(&b);
	if (br_u(&b, 1)) {                         /* conformance_window_flag */
		br_ue(&b); br_ue(&b); br_ue(&b); br_ue(&b);
	}
	out->bit_depth_luma_minus8 = br_ue(&b);
	out->bit_depth_chroma_minus8 = br_ue(&b);
	uint32_t log2_poc_m4 = br_ue(&b);
	out->log2_max_poc_lsb = log2_poc_m4 + 4;

	int sub_present = br_u(&b, 1);             /* sps_sub_layer_ordering_info_present */
	uint32_t lo = sub_present ? 0 : max_sub;   /* == 0 either way here */
	for (uint32_t i = lo; i <= max_sub; i++) {
		br_ue(&b); br_ue(&b); br_ue(&b);
	}
	br_ue(&b);  /* log2_min_luma_coding_block_size_minus3 */
	br_ue(&b);  /* log2_diff_max_min_luma_coding_block_size */
	br_ue(&b);  /* log2_min_luma_transform_block_size_minus2 */
	br_ue(&b);  /* log2_diff_max_min_luma_transform_block_size */
	br_ue(&b);  /* max_transform_hierarchy_depth_inter */
	br_ue(&b);  /* max_transform_hierarchy_depth_intra */
	if (br_u(&b, 1))                           /* scaling_list_enabled_flag */
		return fail("scaling_list_enabled (out of subset)");
	br_u(&b, 1);                               /* amp_enabled_flag */
	out->sao_enabled = (int)br_u(&b, 1);       /* sample_adaptive_offset_enabled */
	if (br_u(&b, 1))                           /* pcm_enabled_flag */
		return fail("pcm_enabled (out of subset)");
	out->num_short_term_rps = br_ue(&b);
	for (uint32_t i = 0; i < out->num_short_term_rps; i++)
		if (parse_st_rps(&b, i, out->num_short_term_rps) < 0)
			return -1;
	if (br_u(&b, 1))                           /* long_term_ref_pics_present */
		return fail("long_term_ref_pics_present (out of subset)");
	out->temporal_mvp_enabled = (int)br_u(&b, 1);

	if (b.overflow)
		return fail("SPS parse ran past end of NAL");
	return 0;
}

int hevc_parse_sps(const uint8_t *sps_nal, uint32_t len, hevc_sps_t *out)
{
	memset(out, 0, sizeof(*out));
	return sps_parse_internal(sps_nal, len, out);
}

int hevc_parse_sps_dims(const uint8_t *sps_nal, uint32_t len, uint32_t *w, uint32_t *h)
{
	hevc_sps_t s;
	int r = hevc_parse_sps(sps_nal, len, &s);
	if (r < 0)
		return r;
	*w = s.width;
	*h = s.height;
	return 0;
}

/* --------------------------------------------------------------- PPS parse */
int hevc_parse_pps(const uint8_t *pps_nal, uint32_t len, hevc_pps_t *out)
{
	memset(out, 0, sizeof(*out));
	uint8_t buf[1024];
	uint32_t n = strip_epb(pps_nal, len, buf, sizeof(buf));
	if (!n)
		return fail("PPS too large / EPB strip overflow");

	br_t b; br_init(&b, buf, n);
	br_u(&b, 16);                              /* NAL header */
	br_ue(&b);                                 /* pps_pic_parameter_set_id */
	br_ue(&b);                                 /* pps_seq_parameter_set_id */
	out->dependent_slice_segments_enabled = (int)br_u(&b, 1);
	out->output_flag_present = (int)br_u(&b, 1);
	out->num_extra_slice_header_bits = br_u(&b, 3);
	br_u(&b, 1);                               /* sign_data_hiding_enabled_flag */
	out->cabac_init_present = (int)br_u(&b, 1);
	out->num_ref_idx_l0_default_active_minus1 = br_ue(&b);
	out->num_ref_idx_l1_default_active_minus1 = br_ue(&b);
	out->init_qp_minus26 = br_se(&b);
	br_u(&b, 1);                               /* constrained_intra_pred_flag */
	br_u(&b, 1);                               /* transform_skip_enabled_flag */
	if (br_u(&b, 1))                           /* cu_qp_delta_enabled_flag */
		br_ue(&b);                         /* diff_cu_qp_delta_depth */
	br_se(&b);                                 /* pps_cb_qp_offset */
	br_se(&b);                                 /* pps_cr_qp_offset */
	out->pps_slice_chroma_qp_offsets_present = (int)br_u(&b, 1);
	out->weighted_pred = (int)br_u(&b, 1);
	out->weighted_bipred = (int)br_u(&b, 1);
	br_u(&b, 1);                               /* transquant_bypass_enabled_flag */
	out->tiles_enabled = (int)br_u(&b, 1);
	out->entropy_coding_sync = (int)br_u(&b, 1);
	if (out->tiles_enabled)
		return fail("tiles_enabled (out of subset)");
	out->pps_loop_filter_across_slices = (int)br_u(&b, 1);
	out->deblocking_filter_control_present = (int)br_u(&b, 1);
	if (out->deblocking_filter_control_present) {
		out->deblocking_filter_override_enabled = (int)br_u(&b, 1);
		if (br_u(&b, 1) == 0) {            /* pps_deblocking_filter_disabled_flag */
			br_se(&b);                 /* pps_beta_offset_div2 */
			br_se(&b);                 /* pps_tc_offset_div2 */
		}
	}
	if (br_u(&b, 1))                           /* pps_scaling_list_data_present */
		return fail("pps scaling_list_data (out of subset)");
	out->lists_modification_present = (int)br_u(&b, 1);
	/* remaining PPS fields unused by the slice parser */

	if (b.overflow)
		return fail("PPS parse ran past end of NAL");
	return 0;
}

/* ------------------------------------------------------------- slice parse */
int hevc_parse_slice(const uint8_t *nal, uint32_t len, int nal_type,
                     const hevc_sps_t *sps, const hevc_pps_t *pps, hevc_slice_t *out)
{
	/* Subset defaults when SPS/PPS are unavailable. */
	uint32_t log2_poc = sps ? sps->log2_max_poc_lsb : 8;
	int sao = sps ? sps->sao_enabled : 0;
	int tmvp_en = sps ? sps->temporal_mvp_enabled : 0;
	uint32_t num_rps = sps ? sps->num_short_term_rps : 0;
	int32_t init_qp = pps ? pps->init_qp_minus26 : 0;
	uint32_t extra_bits = pps ? pps->num_extra_slice_header_bits : 0;
	int output_flag = pps ? pps->output_flag_present : 0;
	int wp = pps ? pps->weighted_pred : 0;
	int wbp = pps ? pps->weighted_bipred : 0;
	int chroma_qp_off = pps ? pps->pps_slice_chroma_qp_offsets_present : 0;
	int deb_override_en = pps ? pps->deblocking_filter_override_enabled : 0;
	int loop_filter_flag = pps ? pps->pps_loop_filter_across_slices : 1;
	int cabac_init = pps ? pps->cabac_init_present : 0;
	int lists_mod = pps ? pps->lists_modification_present : 0;
	uint32_t nrl0_def = pps ? pps->num_ref_idx_l0_default_active_minus1 : 0;
	uint32_t nrl1_def = pps ? pps->num_ref_idx_l1_default_active_minus1 : 0;

	if (nal_type != HEVC_NAL_TRAIL_R && nal_type != HEVC_NAL_IDR_W_RADL &&
	    nal_type != HEVC_NAL_IDR_N_LP)
		return fail("slice NAL type outside {1,19,20} (out of subset)");

	/* Guard: this subset's slice headers contain no emulation-prevention bytes,
	 * so raw reading is valid and the bit position feeds data_byte_offset. */
	uint32_t hdr_scan = len < 24 ? len : 24;
	if (hevc_count_epb(nal, hdr_scan))
		return fail("emulation-prevention byte inside slice header region");

	br_t b; br_init(&b, nal, len);
	br_u(&b, 16);                              /* NAL header */
	uint32_t first_slice = br_u(&b, 1);
	if (!first_slice)
		return fail("!first_slice_segment_in_pic_flag (out of subset)");
	int is_irap = (nal_type >= 16 && nal_type <= 23);
	int is_idr = (nal_type == HEVC_NAL_IDR_W_RADL || nal_type == HEVC_NAL_IDR_N_LP);
	if (is_irap)
		br_u(&b, 1);                       /* no_output_of_prior_pics_flag */
	br_ue(&b);                                 /* slice_pic_parameter_set_id */
	for (uint32_t i = 0; i < extra_bits; i++)
		br_u(&b, 1);                       /* slice_reserved_flag */
	uint32_t slice_type = br_ue(&b);           /* 0=B,1=P,2=I */
	if (slice_type > 2)
		return fail("invalid slice_type");
	if (output_flag)
		br_u(&b, 1);                       /* pic_output_flag */

	uint32_t poc = 0;
	int slice_tmvp = 0;
	if (!is_idr) {
		poc = br_u(&b, log2_poc);          /* slice_pic_order_cnt_lsb */
		uint32_t st_sps = br_u(&b, 1);     /* short_term_ref_pic_set_sps_flag */
		if (st_sps) {
			/* index into SPS RPS list; ceil(log2(num_rps)) bits */
			uint32_t bits = 0;
			while ((1u << bits) < num_rps)
				bits++;
			if (bits)
				br_u(&b, bits);
		} else {
			if (parse_st_rps(&b, num_rps, num_rps) < 0)
				return -1;
		}
		if (tmvp_en)
			slice_tmvp = (int)br_u(&b, 1); /* slice_temporal_mvp_enabled_flag */
	}
	if (sao) {
		br_u(&b, 1);                       /* slice_sao_luma_flag */
		br_u(&b, 1);                       /* slice_sao_chroma_flag */
	}

	if (slice_type == 1 || slice_type == 0) {  /* P or B */
		int is_b = (slice_type == 0);
		uint32_t nrl0 = nrl0_def, nrl1 = nrl1_def;
		if (br_u(&b, 1)) {                 /* num_ref_idx_active_override_flag */
			nrl0 = br_ue(&b);
			if (is_b)
				nrl1 = br_ue(&b);
		}
		if (lists_mod)
			return fail("lists_modification_present (out of subset)");
		if (is_b)
			br_u(&b, 1);               /* mvd_l1_zero_flag */
		if (cabac_init)
			br_u(&b, 1);               /* cabac_init_flag */
		if (slice_tmvp)
			return fail("slice temporal_mvp collocated syntax (out of subset)");
		if ((wp && slice_type == 1) || (wbp && is_b))
			parse_pred_weight_table(&b, nrl0 + 1, is_b, nrl1 + 1);
		br_ue(&b);                         /* five_minus_max_num_merge_cand */
	}

	int32_t qp_delta = br_se(&b);              /* slice_qp_delta */
	if (chroma_qp_off) { br_se(&b); br_se(&b); }
	if (deb_override_en) {
		if (br_u(&b, 1)) {                 /* deblocking_filter_override_flag */
			if (br_u(&b, 1) == 0) {    /* slice_deblocking_filter_disabled */
				br_se(&b); br_se(&b);
			}
		}
	}
	if (loop_filter_flag)
		br_u(&b, 1);                       /* slice_loop_filter_across_slices_enabled_flag */
	/* No tiles/WPP => no entry_point_offsets. No header extension (subset). */

	if (b.overflow)
		return fail("slice header parse ran past end of NAL");

	/* byte_alignment(): the slice_segment_data starts at the next byte boundary
	 * after the stop bit. With E = bits consumed from NAL byte 0 (incl. the
	 * 16-bit NAL header), this reduces to dbo = E/8 + 1 (matches the python). */
	uint32_t E = br_pos(&b);
	out->slice_type = (int)slice_type;
	out->poc = poc;
	out->slice_qp = 26 + init_qp + qp_delta;
	out->data_byte_offset = E / 8u + 1u;
	out->bfnum = len - out->data_byte_offset;
	return 0;
}

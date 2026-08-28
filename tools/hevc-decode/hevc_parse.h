/* hevc_parse.h — minimal runtime HEVC/H.265 bitstream parser for the rpivid
 * hardware decoder. Extracts the per-frame parameters hevc-m2.c needs (geometry
 * from the SPS; slice_type / POC / slice_qp / data_byte_offset / bfnum from each
 * slice header) at RUNTIME, replacing the build-time python generators
 * (gen-frame-header.py / gen-clip-header.py / gen-ippp-header.py).
 *
 * Scope: the fixed x265 subset the player targets — 8-bit 4:2:0, single-slice,
 * single-tile, no WPP, no SAO, no temporal-MVP, no scaling lists, no PCM, CTB 64.
 * IDR (type 19/20) I-slices + TRAIL_R (type 1) P-slices referencing the
 * immediately-previous frame. Anything outside the subset is rejected (a negative
 * return + hevc_err()), never mis-handled — a silently wrong value corrupts decode.
 *
 * No external dependencies; freestanding C (uint*_t + memcmp only).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef HEVC_PARSE_H
#define HEVC_PARSE_H

#include <stdint.h>

/* NAL unit types we care about (H.265 Table 7-1). */
enum {
	HEVC_NAL_TRAIL_N = 0,   /* non-IRAP, non-reference coded slice (non-ref B-slices) */
	HEVC_NAL_TRAIL_R = 1,   /* non-IRAP coded slice (our P-slices, ref B-slices) */
	HEVC_NAL_BLA_W_LP = 16,
	HEVC_NAL_IDR_W_RADL = 19,
	HEVC_NAL_IDR_N_LP = 20,
	HEVC_NAL_CRA_NUT = 21,
	HEVC_NAL_SPS = 33,
	HEVC_NAL_PPS = 34,
};

/* One NAL unit, start-code stripped. `data` points at the 2-byte NAL header;
 * `len` spans header + payload (RBSP with emulation-prevention bytes still in). */
typedef struct {
	int type;
	const uint8_t *data;
	uint32_t len;
} hevc_nal_t;

/* Cursor for hevc_nal_next(); zero-initialise before first call. */
typedef struct {
	const uint8_t *buf;
	uint32_t size;
	uint32_t pos;       /* byte offset of the next start code to scan from */
	int started;
} hevc_nal_iter_t;

/* One parsed short-term RPS (H.265 7.3.7 / 7.4.8): running DeltaPocSX + used flags. */
typedef struct {
	uint32_t num_neg, num_pos;
	int32_t  delta_s0[16];            /* DeltaPocS0[i], running NEGATIVE */
	int32_t  delta_s1[16];            /* DeltaPocS1[i], running POSITIVE */
	uint8_t  used_s0[16], used_s1[16];
} hevc_st_rps_t;

/* Sequence parameter set — only the fields the slice parser / geometry need. */
typedef struct {
	uint32_t width, height;           /* pic_{width,height}_in_luma_samples */
	uint32_t chroma_format_idc;       /* 1 == 4:2:0 (required) */
	uint32_t bit_depth_luma_minus8;
	uint32_t bit_depth_chroma_minus8;
	uint32_t log2_max_poc_lsb;        /* log2_max_pic_order_cnt_lsb_minus4 + 4 */
	uint32_t max_dec_pic_buffering;   /* sps_max_dec_pic_buffering_minus1 + 1 (DPB size) */
	uint32_t max_num_reorder;         /* sps_max_num_reorder_pics (display reorder depth) */
	uint32_t num_short_term_rps;      /* num_short_term_ref_pic_sets */
	hevc_st_rps_t sps_rps[16];        /* the SPS-level RPS list (st_ref_pic_set_sps_flag path) */
	int sao_enabled;                  /* sample_adaptive_offset_enabled_flag */
	int temporal_mvp_enabled;         /* sps_temporal_mvp_enabled_flag */
} hevc_sps_t;

/* Picture parameter set — the flags that steer slice-header syntax. */
typedef struct {
	int32_t init_qp_minus26;
	uint32_t num_extra_slice_header_bits;
	uint32_t num_ref_idx_l0_default_active_minus1;
	uint32_t num_ref_idx_l1_default_active_minus1;
	int dependent_slice_segments_enabled;
	int output_flag_present;
	int cabac_init_present;
	int weighted_pred;                /* P slices carry pred_weight_table() */
	int weighted_bipred;              /* B slices carry pred_weight_table() */
	int pps_slice_chroma_qp_offsets_present;
	int deblocking_filter_control_present;
	int deblocking_filter_override_enabled;
	int pps_loop_filter_across_slices;
	int tiles_enabled;
	int entropy_coding_sync;
	int lists_modification_present;
} hevc_pps_t;

/* Per-frame output — matches the python generators' per-frame fields exactly. */
typedef struct {
	int slice_type;             /* 0=B, 1=P, 2=I */
	uint32_t poc;               /* slice_pic_order_cnt_lsb (0 for IDR) */
	int slice_qp;               /* 26 + init_qp_minus26 + slice_qp_delta */
	uint32_t data_byte_offset;  /* NAL-start byte offset of slice_segment_data */
	uint32_t bfnum;             /* len - data_byte_offset (bytes the HW consumes) */
	/* Inter-prediction fields the HW command buffer needs (P/B). These are
	 * STREAM-SEMANTIC (they steer the block's CABAC engine) so they must be
	 * extracted, never assumed. Zero/1 defaults for I. */
	uint32_t nb_refs_l0;        /* num_ref_idx_l0_active (1 for our subset) */
	uint32_t nb_refs_l1;        /* num_ref_idx_l1_active (B only; 0 for P) */
	int mvd_l1_zero_flag;       /* B: sets slice_reg_const BIT(16) */
	int cabac_init_flag;        /* must be 0 for init_type = 2 - slice_type */
	uint32_t max_num_merge_cand;/* 5 - five_minus_max_num_merge_cand (3 for our subset) */
	/* Reference-picture lists for a general (b-pyramid) DPB. POC values, ordered,
	 * default construction (H.265 8.3.4; no list modification in-subset). The
	 * player maps these POCs to DPB buffers. */
	uint32_t ref_poc_l0[16];    /* RefPicListL0[0..nb_refs_l0-1] */
	uint32_t ref_poc_l1[16];    /* RefPicListL1[0..nb_refs_l1-1] (B only) */
	/* Retention union: EVERY POC in this slice's RPS (Before ∪ After, incl used=0).
	 * The DPB must keep exactly these (+ self if a reference) — NOT just the active
	 * lists (used=0 entries are future candidates; §8.3.2 marking). */
	uint32_t rps_poc[16];
	uint32_t rps_n;
	/* Temporal-MVP (collocated-MV). The collocated picture is
	 * RefPicList[collocated_from_l0?0:1][collocated_ref_idx] (resolved to POC). */
	int slice_temporal_mvp_enabled;
	int collocated_from_l0;
	uint32_t collocated_ref_idx;
	uint32_t collocated_poc;
	/* SAO: RPI_SLICE BIT14/BIT15 (the HW CABAC-decodes the per-CTB sao() params). */
	int slice_sao_luma;
	int slice_sao_chroma;
	/* WPP: num_entry_point_offsets>0 selects the wavefront command sequence. The
	 * offset VALUES are not needed (the HW derives the wavefront from CTB geometry);
	 * they're parsed only to advance the bit position for data_byte_offset. */
	uint32_t num_entry_point_offsets;
	/* Weighted prediction (H.265 §7.3.6.3/§7.4.7.3). Populated only when weighted!=0;
	 * the player emits a 6-word weight block per active ref. Indexed [list 0/1][ref].
	 * COMPUTED values (not raw deltas), with defaults filled for unflagged refs. */
	int weighted;                       /* (weighted_pred && P) || (weighted_bipred && B) */
	uint32_t luma_log2_weight_denom;    /* 0..7 */
	uint32_t chroma_log2_weight_denom;  /* luma_denom + delta_chroma_denom, 0..7 */
	int32_t luma_weight[2][16];         /* LumaWeightLX[i]      = (1<<lden)+delta / default 1<<lden */
	int32_t luma_offset[2][16];         /* luma_offset_lX[i]    (8-bit) / default 0 */
	int32_t chroma_weight[2][16][2];    /* ChromaWeightLX[i][j] = (1<<cden)+delta / default 1<<cden */
	int32_t chroma_offset[2][16][2];    /* ChromaOffsetLX[i][j] computed §7.4.7.3 / default 0 */
} hevc_slice_t;

/* Human-readable reason for the most recent negative return, for diagnostics. */
const char *hevc_err(void);

/* Count emulation-prevention (00 00 03) sequences in a byte range. */
uint32_t hevc_count_epb(const uint8_t *s, uint32_t n);

/* Iterate NAL units in a whole .265 file buffer. Returns 1 and fills *out for
 * each unit (4-byte start codes checked before 3-byte to avoid double counting;
 * a 0x00 immediately before the next start code is trimmed off this payload);
 * returns 0 at end of buffer. */
int hevc_nal_next(hevc_nal_iter_t *it, hevc_nal_t *out);

/* Required entry point: parse the SPS just far enough for the frame geometry. */
int hevc_parse_sps_dims(const uint8_t *sps_nal, uint32_t len,
                        uint32_t *w, uint32_t *h);

/* Full SPS parse (dims + the slice-steering fields). Rejects non-subset streams. */
int hevc_parse_sps(const uint8_t *sps_nal, uint32_t len, hevc_sps_t *out);

/* PPS parse (the flags the slice header depends on). */
int hevc_parse_pps(const uint8_t *pps_nal, uint32_t len, hevc_pps_t *out);

/* Parse a first-slice IDR or TRAIL_R slice_segment_header.
 *
 * DEVIATION from the task's 4-arg signature: this takes the already-parsed SPS
 * and PPS. The task explicitly permits "parse the PPS" / "parse it from the SPS",
 * and the ip128 vector is weighted-pred (needs weighted_pred_flag + the SPS
 * log2_max_poc_lsb) — so a correct, general parser must see both. Pass NULL for
 * either to fall back to the documented subset defaults (log2_max_poc_lsb=8,
 * init_qp_minus26=0, no weighted pred, loop-filter flag present). */
int hevc_parse_slice(const uint8_t *nal, uint32_t len, int nal_type,
                     const hevc_sps_t *sps, const hevc_pps_t *pps,
                     hevc_slice_t *out);

#endif /* HEVC_PARSE_H */

/*
 * idr64_frame.h — host-parsed parameters + slice payload for the M2 test vector
 * tools/hevc-decode/testdata/idr64.265 (one IDR frame, 64x64, 8-bit, 4:2:0,
 * single-tile, no-WPP, no-temporal-mvp, no-SAO, I-slice).
 *
 * Values from ffmpeg trace_headers (testdata/README.md). The three marked
 * PROVISIONAL fields (DATA_BYTE_OFFSET, DATA_LEN, slice_data[]) come from the
 * host slice extraction and are refreshed there — a wrong data_byte_offset
 * silently produces garbage, so they are the load-bearing values to confirm.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef IDR64_FRAME_H
#define IDR64_FRAME_H

/* Geometry. */
#define FRAME_WIDTH        64u
#define FRAME_HEIGHT       64u
#define FRAME_LOG2_CTB     6u    /* log2_min_cb(3) + diff_max_min_cb(3) */
#define FRAME_CTB_WIDTH    1u    /* ceil(64/64) */
#define FRAME_CTB_HEIGHT   1u

/* SPS-derived (already-decoded log2 values, i.e. the "+3"/"+2" applied). */
#define FRAME_LOG2_MIN_CB  3u    /* log2_min_luma_coding_block_size_minus3(0) + 3 */
#define FRAME_LOG2_MIN_TB  2u    /* log2_min_luma_transform_block_size_minus2(0) + 2 */
#define FRAME_LOG2_MAX_TB  5u    /* min_tb(2) + diff_max_min_tb(3) */
#define FRAME_MAX_TRAFO_INTRA 1u /* PROVISIONAL: SPS max_transform_hierarchy_depth_intra (confirm) */
#define FRAME_MAX_TRAFO_INTER 1u /* PROVISIONAL: SPS max_transform_hierarchy_depth_inter (confirm) */
#define FRAME_CHROMA_FORMAT_IDC 1u
#define FRAME_STRONG_INTRA_SMOOTH 1u
#define FRAME_BIT_DEPTH_LUMA_MINUS8 0u

/* PPS-derived. */
#define FRAME_DIFF_CU_QP_DELTA_DEPTH 0u
#define FRAME_CU_QP_DELTA_ENABLED    0u  /* PROVISIONAL */
#define FRAME_TRANSFORM_SKIP         0u  /* PROVISIONAL */
#define FRAME_SIGN_DATA_HIDING       1u  /* PROVISIONAL (x265 default on) */
#define FRAME_CONSTRAINED_INTRA_PRED 0u
#define FRAME_PPS_CB_QP_OFFSET       0
#define FRAME_PPS_CR_QP_OFFSET       0
#define FRAME_SLICE_CB_QP_OFFSET     0
#define FRAME_SLICE_CR_QP_OFFSET     0

/* Slice header. */
#define FRAME_SLICE_TYPE   2u    /* I */
#define FRAME_SLICE_QP     25    /* 26 + init_qp_minus26(0) + slice_qp_delta(-1) */

/* Phase-2 CONFIG2 (mk_config2, h265.c:1524) — precomputed for this frame:
 * BitDepthY 8 | BitDepthC 8<<4 | log2_ctb 6<<10 | strong_smooth<<14
 * | (log2_parallel_merge_level_minus2(0)+2)<<16 = 0x25888. */
#define FRAME_CONFIG2      0x25888u

/* PROVISIONAL — from the host slice extraction (idr64.265 IDR slice NAL):
 * DATA_BYTE_OFFSET = bytes from slice-segment RBSP start to slice DATA;
 * DATA_LEN = bitstream length the HW reads; slice_data[] = the bytes DMA'd
 * (whole slice NAL payload, emulation-prevention bytes kept). Replace with the
 * extracted values before the HW run. */
#define FRAME_DATA_BYTE_OFFSET 3u   /* PROVISIONAL — confirm from extraction */
#define FRAME_DATA_LEN         (sizeof(slice_data))  /* PROVISIONAL */

static const unsigned char slice_data[] = {
	/* PROVISIONAL placeholder — real slice NAL payload dropped in from extraction. */
	0x26, 0x01, 0xaf, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#endif /* IDR64_FRAME_H */

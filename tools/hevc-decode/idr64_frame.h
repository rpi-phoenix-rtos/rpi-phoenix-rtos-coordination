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
#define FRAME_MAX_TRAFO_INTRA 0u /* SPS max_transform_hierarchy_depth_intra (trace) */
#define FRAME_MAX_TRAFO_INTER 0u /* SPS max_transform_hierarchy_depth_inter (trace) */
#define FRAME_CHROMA_FORMAT_IDC 1u
#define FRAME_STRONG_INTRA_SMOOTH 1u
#define FRAME_BIT_DEPTH_LUMA_MINUS8 0u

/* PPS-derived (all confirmed via ffmpeg trace_headers + manual decode). */
#define FRAME_DIFF_CU_QP_DELTA_DEPTH 1u
#define FRAME_CU_QP_DELTA_ENABLED    1u
#define FRAME_TRANSFORM_SKIP         0u
#define FRAME_SIGN_DATA_HIDING       1u
#define FRAME_CONSTRAINED_INTRA_PRED 0u
#define FRAME_PPS_CB_QP_OFFSET       0
#define FRAME_PPS_CR_QP_OFFSET       0
#define FRAME_SLICE_CB_QP_OFFSET     0
#define FRAME_SLICE_CR_QP_OFFSET     0
/* Deblocking: pps_beta/tc_offset_div2 = -6, slice inherits (no per-slice override).
 * Not on the phase-1/2 register path we program → affects loop-filter output only,
 * not decode completion; revisit for exact-pixel parity (M4). */

/* Slice header. */
#define FRAME_SLICE_TYPE   2u    /* I */
#define FRAME_SLICE_QP     25    /* 26 + init_qp_minus26(0) + slice_qp_delta(-1) */

/* Golden output (ffmpeg SW decode of this vector): the solid gray frame decodes
 * to a UNIFORM luma 126 and neutral chroma 128 everywhere. The HW decode, once
 * unpacked from SAND/COL128, must match exactly. */
#define FRAME_EXPECT_Y   126u
#define FRAME_EXPECT_C   128u

/* Phase-2 CONFIG2 (mk_config2, h265.c:1524) — precomputed for this frame:
 * BitDepthY 8 | BitDepthC 8<<4 | log2_ctb 6<<10 | strong_smooth<<14
 * | (log2_parallel_merge_level_minus2(0)+2)<<16 = 0x25888. */
#define FRAME_CONFIG2      0x25888u

/* From the host slice extraction (idr64.265 IDR slice NAL), cross-verified via
 * ffmpeg trace_headers + a manual byte-level decode:
 *   slice_data[] = the 12-byte IDR slice NAL (start code stripped):
 *     [0..1] NAL header (28 01)  [2..3] slice header (ad e0)  [4..11] slice DATA
 *   data_byte_offset = 4  -> slice DATA begins at buffer byte 4
 *   BFNUM (data_len) = 8  -> the 8 slice-data bytes (bit_size 64)
 *   no 00 00 03 emulation-prevention bytes present; use_emu kept 1 (driver parity)
 * The block reads BFNUM bytes from (buf_pa + data_byte_offset). Buffer is page-
 * aligned (dma_alloc) so (buf_pa & 63) == 0 -> BFCONTROL low6 == 4. */
#define FRAME_DATA_BYTE_OFFSET 4u
#define FRAME_DATA_LEN         8u   /* BFNUM */

static const unsigned char slice_data[] = {
	0x28, 0x01, 0xad, 0xe0, 0x6c, 0x77, 0xdf, 0xf3, 0xaa, 0xbe, 0x5a, 0x78,
};

#endif /* IDR64_FRAME_H */

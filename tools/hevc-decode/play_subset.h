/* play_subset.h — the FIXED HEVC/H.265 stream constants the rpivid register
 * values bake in for the x265 subset the player targets (4:2:0, CTB 64,
 * single-slice/tile, no scaling-lists/PCM/AMP, strong-intra=1). Bit depth is
 * runtime now (8-bit or 10-bit, g_bd_minus8) — FRAME_BIT_DEPTH_LUMA_MINUS8 is
 * only the 8-bit default for the build-time (non-PLAY_TOOL) modes.
 *
 * This is the PLAY_TOOL counterpart to the generated *_frame.h headers: it
 * defines ONLY the resolution-independent FRAME_* constants that build_command_
 * buffer()/decode_one() emit into the SPS/PPS registers. The geometry that DOES
 * vary per stream — width, height, CTBs-across/down — is runtime (g_frame_w/h,
 * g_ctb_w/h), filled from the SPS by hevc_parse_sps(). Per-frame slice params
 * (type/POC/qp/data_byte_offset/bfnum) come from hevc_parse_slice() at run time.
 *
 * Matches the constants the python generators emit (gen-ippp-header.py). A stream
 * whose SPS/PPS disagrees with any of these is rejected by hevc_parse.c, so these
 * bakes are always valid for a stream the player accepts.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef PLAY_SUBSET_H
#define PLAY_SUBSET_H

#define FRAME_LOG2_CTB            6u   /* CTB size 64 */
#define FRAME_LOG2_MIN_CB         3u
#define FRAME_LOG2_MIN_TB         2u
#define FRAME_LOG2_MAX_TB         5u
#define FRAME_MAX_TRAFO_INTRA     0u
#define FRAME_MAX_TRAFO_INTER     0u
#define FRAME_CHROMA_FORMAT_IDC   1u   /* 4:2:0 */
#define FRAME_STRONG_INTRA_SMOOTH 1u
#define FRAME_BIT_DEPTH_LUMA_MINUS8 0u
#define FRAME_DIFF_CU_QP_DELTA_DEPTH 1u
#define FRAME_CU_QP_DELTA_ENABLED 1u
#define FRAME_TRANSFORM_SKIP      0u
#define FRAME_SIGN_DATA_HIDING    1u
#define FRAME_CONSTRAINED_INTRA_PRED 0u
#define FRAME_SLICE_TYPE          2u   /* I (the P/B const is passed explicitly) */
#define FRAME_PPS_CB_QP_OFFSET    0
#define FRAME_PPS_CR_QP_OFFSET    0
#define FRAME_SLICE_CB_QP_OFFSET  0
#define FRAME_SLICE_CR_QP_OFFSET  0
#define FRAME_CONFIG2             0x25888u

#endif /* PLAY_SUBSET_H */

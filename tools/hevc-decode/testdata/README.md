# HEVC M2 test vector — single-IDR 64×64

`idr64.265` is the canonical M2 bring-up test vector: **one IDR frame, 64×64, 8-bit,
4:2:0, single-tile, no WPP, no temporal-MVP, no SAO** — the smallest decode the rpivid
block can do, chosen to exercise the minimum register set (no colMV buffer, no tiles,
no scaling list, no references beyond the current frame).

The M2 harness hardcodes the fields below (host-parsed here so the on-device decode
needs no HEVC parser). Regenerate with `gen-idr64.sh`, but **commit the exact bytes**:
x265 output varies by version, and the harness's `data_byte_offset`/`bit_size` and
CABAC state must match the shipped bitstream.

## Parsed fields (ffmpeg trace_headers)

NAL units: VPS(32), SPS(33), PPS(34), SEI_PREFIX(39), **IDR_N_LP(20)** slice.

| Field | Value | Notes |
|---|---|---|
| pic_width_in_luma_samples | 64 | → RPI_FRAMESIZE, ctb_width = 1 (CTB 64) |
| pic_height_in_luma_samples | 64 | → RPI_NUMROWS = pic_height_in_ctbs_y = 1 |
| chroma_format_idc | 1 | 4:2:0 (required) |
| bit_depth_luma/chroma_minus8 | 0 | 8-bit → output must be a COL128 SAND variant |
| log2_min_luma_coding_block_size_minus3 | 0 | |
| log2_diff_max_min_luma_coding_block_size | 3 | → CTB = 2^(3+3) = 64 |
| log2_min_luma_transform_block_size_minus2 | 0 | |
| log2_diff_max_min_luma_transform_block_size | 3 | |
| amp_enabled_flag | 0 | |
| pcm_enabled_flag | 0 | |
| sps_temporal_mvp_enabled_flag | **0** | → MVBASE/COLBASE = 0, no colMV buffer |
| strong_intra_smoothing_enabled_flag | 1 | |
| tiles_enabled_flag | **0** | single-tile `decode_slice` path |
| entropy_coding_sync_enabled_flag | **0** | no WPP |
| init_qp_minus26 (PPS) | 0 | init_qp = 26 |
| slice_type | 2 | I-slice (nb_refs L0/L1 = 0, max_merge = 0) |
| slice_qp_delta | -1 | slice_qp = 26 + 0 + (-1) = **25** |

Still to extract on the host for the harness (M2): `data_byte_offset` (start of slice
DATA past the slice header — the HW consumes data, not header) and `bit_size`, plus the
exact SPS/PPS-derived register words. See `docs/inprogress/2026-08-28-hevc-m2-register-spec.md`.

## License

`idr64.265` is a compressed gray 64×64 frame generated locally — encoded *data*, not a
derivative of the encoder; no upstream code or third-party content. Safe to commit.

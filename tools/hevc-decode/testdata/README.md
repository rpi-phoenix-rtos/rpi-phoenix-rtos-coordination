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
exact SPS/PPS-derived register words. See `docs/misc/2026-08-28-hevc-m2-register-spec.md`.

## License

`idr64.265` is a compressed gray 64×64 frame generated locally — encoded *data*, not a
derivative of the encoder; no upstream code or third-party content. Safe to commit.

## `*.trace.txt`

`dflt.trace.txt` / `wp.trace.txt` are ffmpeg `-trace_headers` dumps of the
matching `.265` stream, kept as reference fixtures because the decoder tests
compare slice-header parameters (`data_byte_offset` varies per frame). Regenerate
with:

```
ffmpeg -v trace -hide_banner -i dflt.265 -f null - 2> dflt.trace.txt
```

## Generated demo assets are NOT committed

⚠ **Corrected 2026-09-17 — that is no longer the whole picture.** 35 `.265` files are tracked,
totalling **13.3 MB**, and two of them are *not* small conformance vectors:

| file | size | what it is |
|---|---|---|
| `showcase1080.265` | 6.8 MB | 1080p demo clip, deliberately **non-personal** content — made as the alternative to the owner's phone footage for the showcase reel |
| `reel-motion720.265` | 5.8 MB | 720p high-motion clip used to exercise the decoder's inter path |

Everything else is genuinely small (next largest 286 kB, most ~12 kB). Both large clips are
**generated**, by `gen-clip-header.py` / `transcode-for-phoenix.sh` from sources outside this
repo; if repo size ever matters they are the two to drop, not the vectors.

The owner's **personal** footage stays untracked, which is the line that matters:
`IMG_8331-phoenix.265` is 19.7 MB and is **not committed** — reproducible in one command from
the source the owner supplied:

```
./tools/hevc-decode/transcode-for-phoenix.sh ~/Downloads/IMG_8331.MOV
```

That stages it to both the buildroot rootfs and the live NFS export. It plays with
`video-play /usr/share/demo/IMG_8331-phoenix.265` (FFmpeg's `hevc_rpivid`). (Measured with
the retired `/dev/fb0` hevc-play: 1080p, 1058 frames, 21.7 fps, 0 faults, ~90% of each frame
in the framebuffer blit, not the decode.)

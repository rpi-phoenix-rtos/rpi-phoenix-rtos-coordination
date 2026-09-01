# HEVC P/B (inter-frame) decode plan — completing "video decode" to general (non-all-intra) streams

Intra (single-frame, bit-exact) + all-intra video playback are DONE (tools/hevc-decode/).
P/B adds inter prediction + a DPB so real (non-all-intra) video decodes. Analysis below
shows a P-frame decode is CLOSE to the I-frame path — a focused delta, not a rewrite.

## Test vector
`tools/hevc-decode/testdata/ip128.265` — 2 frames, 128×128, 8-bit 4:2:0, no-B / no-tmvp /
no-SAO / no-WPP, ref=1 (x265 `keyint=48:min-keyint=48:bframes=0:no-temporal-mvp=1:sao=0:wpp=0:ref=1`):
- Frame 0: IDR_N_LP(20), slice_type I(2), qp 33 (slice_qp_delta 7). Decode as today (all refs=self).
- Frame 1: TRAIL_R(1), slice_type P(1), max_num_merge_cand 3 (five_minus=2), qp 33, 1 L0 ref = frame 0.

## What P-decode changes vs the I path (from Linux hevc_d_h265.c)

1. **DPB / two output buffers.** Decode frame 0 (I) into buffer A (lumaA/chromaA); KEEP it.
   Decode frame 1 (P) into buffer B, with reference slots pointing at A.

2. **Reference slots** (phase-2 REFYBASE0.., 16×16B). Default all 16 = current frame (fallback,
   h265.c:1726). Then override the DPB entries: for P, `ref_addrs[0] = {lumaA_pa, chromaA_pa}`
   (the I frame), slots 1..15 stay = current (B). (h265.c:1872-1903.)

3. **Slice messages** (program_slicecmds → SLICECMDS + msg array @0x4000+4*i). I-slice emits 0
   messages; P-slice emits (pre_slice_decode, h265.c:725-838):
   - `cmd_slice = 2 | (nb_refs_L0<<2) | (nb_refs_L1<<6) | (max_merge<<11) | (no_backward_pred<<10) | (collocated_from_l0<<14)`.
     For 1 L0 ref (POC 0, cur POC 1): no_backward_pred=1 (cur_poc 1 !< ref_poc 0), collocated_from_l0=1.
     = `2 | (1<<2) | (3<<11) | (1<<10) | (1<<14)` = **0x5c06**.
   - per L0 ref: `msg_slice(dpb_no | LTR?1<<4 | weighted?3<<5)` then `msg_slice(POC & 0xffff)`.
     For ref (dpb 0, no-LTR, no-weight, POC 0): `msg_slice(0)`, `msg_slice(0)`.
   - ⇒ **num_slice_msgs = 3**: {0x5c06, 0, 0}. SLICECMDS = 3 | (sliceid 0 <<8) = 3.

4. **slice_const** (slice_reg_const, h265.c:593): `max_merge | nb_L0<<4 | nb_L1<<8 | slice_type<<12`
   (+ SAO/MVD bits). For P: `3 | (1<<4) | (1<<12)` = **0x1013** (I was 0x2000).

5. **CURRPOC** = P's slice_pic_order_cnt = **1** (I was 0).

6. **CONFIG2 / FRAMESIZE / SPS0/SPS1/PPS / QP** — unchanged shape; QP from the P slice header (33);
   temporal-mvp stays off ⇒ MVBASE/COLBASE = 0, no colMV buffer, mk_aux=0 (CONFIG2 unchanged).

7. **Bitstream**: frame 1's TRAIL_R NAL, data_byte_offset (parse — P slice header is longer than
   I's; likely still small but MUST extract, not assume 4).

## Harness changes (tools/hevc-decode/hevc-m2.c)
- `build_command_buffer`: accept slice params struct {slice_type, num_msgs, msgs[], slice_const,
  currpoc}; emit the msg array + SLICECMDS from it; use slice_const in RPI_SLICE; CURRPOC from it.
- `decode_one`: accept the ref-slot addresses (array of {luma_pa, chroma_pa}) instead of always
  current-frame; program REF slots from it; take currpoc.
- New `ip_frame.h` (hand-authored or a gen-ip-header.py): both frames' params + the P msg list.
- New test mode: alloc bufferA + bufferB; decode I→A; decode P→B with ref[0]=A; verify B vs ffmpeg.

## Risks
1. **P slice-header parse** (data_byte_offset, num_ref_idx, ref_pic_list_modification, POCs) — more
   fields than I; extract via trace_headers, cross-check.
2. **Message encoding** (cmd_slice bit layout) — port pre_slice_decode byte-for-byte.
3. **Ref address correctness** — REF[0] must be the I frame's SAND/COL128 output at the right stride.
4. **POC / no_backward_pred derivation** for multi-ref later.

First milestone: the single P frame decodes bit-exact vs `ffmpeg -i ip128.265` frame 1. Then extend
to a multi-frame P sequence (rolling DPB), then B-frames.

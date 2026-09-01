# BCM2711 rpivid / hevc_dec — M2 single-IDR-frame decode register spec

Derived from a full read of the in-tree Linux driver
`external/linux/drivers/media/platform/raspberrypi/hevc_dec/` (hevc_d_hw.{c,h},
hevc_d_h265.c, hevc_d_video.c) + `bcm2711.dtsi`. All register offsets are **byte
offsets from the block base**; `apb_read/apb_write = readl/writel(base_h265 + offset)`
(plain, no scaling — confirmed hevc_d_hw.h:91-108, matches M0). Value scaling exists
only for address/length registers (§4: addr = `phys>>6`, len/stride = `(bytes+63)>>6`).

This is the roadmap for **M2** (the decode core). M0 (reachability) and M1 (DMA
allocator + IRQ path) are HW-validated (`tools/hevc-probe`, `tools/hevc-decode/hevc-m1.c`).

## 0. Architecture (drives everything)

The decode is **two hardware phases**; phase 1 is **not** direct APB writes — the driver
builds a **DMA command buffer** of 64-bit entries `addr | (data << 32)` (`p1_apb_write`,
h265.c:289-294) that the hardware executes itself after being kicked. Three register classes:
- **Class A — cmd-FIFO (phase-1 SPS/PPS/slice regs):** data in the command buffer, executed
  by HW, never poked directly.
- **Class B — direct APB (phase-1 kick + all of phase 2):** real `apb_write` at run time.
- **Class C — ARGON INTC:** only offset 0 (`ARG_IC_ICTRL`) used.

## 1. Register map

### 1a. Phase-1 registers — emitted into the command buffer (Class A), offsets hevc_d_hw.h:25-54
| Name | Off | Programs | Site |
|---|---|---|---|
| RPI_SPS0 | 0 | log2 CB sizes, log2 TB min/max, luma/chroma bit-depth, max transform hier depth intra/inter | h265.c:619 |
| RPI_SPS1 | 4 | PCM bit depths, PCM CB sizes, chroma_format_idc, AMP/PCM/scaling-list/strong-intra-smoothing | h265.c:633 |
| RPI_PPS | 8 | log2_ctb - diff_cu_qp_delta_depth, cu_qp_delta/transquant-bypass/transform-skip/sign-data-hiding, cb/cr qp offsets, constrained-intra | h265.c:652 |
| RPI_SLICE | 12 | slice const (merge cand, nb_refs L0/L1, slice_type, SAO, MVD-L1-zero) \| per-tile last-CTB w/h | h265.c:894 / :593 |
| RPI_TILESTART | 16 | `col_bd[tx] \| (row_bd[ty]<<16)` | h265.c:927 |
| RPI_TILEEND | 20 | `endx \| (endy<<16)` | h265.c:929 |
| RPI_SLICESTART | 24 | `(ctb_col)\|(ctb_row<<16)` of slice_segment_addr | h265.c:683 |
| RPI_MODE | 28 | pause_mode (WPP=1/TILE=0xffff) + last-col(bit17)/last-row(bit18) | h265.c:943 |
| RPI_QP | 48 | `6*bit_depth_luma_minus8 + slice_qp` | h265.c:940 |
| RPI_CONTROL | 52 | `(ctb_col)\|(ctb_row<<16)` entry point | h265.c:948 |
| RPI_STATUS | 56 | end-of-slice / entry markers (write); also read for P1 status | h265.c:1203 |
| RPI_BFBASE | 64 | bitstream base `(addr>>6)` | h265.c:582 |
| RPI_BFNUM | 68 | bitstream length in bytes | h265.c:583 |
| RPI_BFCONTROL | 72 | `(addr&63)` + Stop(bit7), then `(addr&63)+(use_emu<<6)` | h265.c:584 |
| RPI_SLICECMDS | 96 | `num_slice_msgs + (sliceid<<8)` | h265.c:702 |
| RPI_TRANSFER | 104 | PROB_BACKUP/PROB_RELOAD CABAC ctx save/restore | h265.c:553 |
| RPI_BEGINTILEEND | 100 | first-tile end marker (do_bte) | h265.c:932 |
| prob array | 0x1000+i | 40 words CABAC init probs | h265.c:541 |
| scaling array | 0x2000+i | scaling factors (only if scaling_list) | h265.c:562 |
| slice-msg array | 0x4000+4*i | per-slice ref/weight/qp-offset msgs | h265.c:705 |

### 1b. Phase-1 kick — direct APB (Class B), phase1_claimed h265.c:2192
| Name | Off | Value |
|---|---|---|
| RPI_PUWBASE | 80 | `pu_base>>6` |
| RPI_PUWSTRIDE | 84 | `(pu_stride+63)>>6` |
| RPI_COEFFWBASE | 88 | `coeff_base>>6` |
| RPI_COEFFWSTRIDE | 92 | `(coeff_stride+63)>>6` |
| RPI_CFNUM | 112 | `cmd_len` (count of u64 entries, NOT >>6) |
| **RPI_CFBASE** | **108** | `cmd.addr>>6` — **apb_write_final STARTS PHASE 1** |

### 1c. Phase-2 registers — direct APB (Class B), phase2_claimed h265.c:2025; offsets h265_hw.h:58-85
| Name | Off | Value/programs |
|---|---|---|
| RPI_PURBASE | 0x8000 | `pu_base>>6` |
| RPI_PURSTRIDE | 0x8004 | pu_stride (vc_len) |
| RPI_COEFFRBASE | 0x8008 | `coeff_base>>6` |
| RPI_COEFFRSTRIDE | 0x800C | coeff_stride (vc_len) |
| RPI_CONFIG2 | 0x8014 | mk_config2(): bit depths, log2_ctb, constrained-intra, strong-smoothing, mk_aux(bit15), parallel-merge-level, temporal-mvp(bit19), pcm-loop-filter-disable, cb/cr qp offsets (h265.c:1524) |
| RPI_OUTYBASE | 0x8018 | `frame_luma>>6` |
| RPI_OUTYSTRIDE | 0x801C | luma col stride (vc_len) — **column stride, SAND** |
| RPI_OUTCBASE | 0x8020 | `frame_chroma>>6` |
| RPI_OUTCSTRIDE | 0x8024 | chroma stride (vc_len) |
| RPI_FRAMESIZE | 0x802C | `(height<<16)\|width` luma samples |
| RPI_MVBASE | 0x8030 | `col.addr>>6` or **0** if no aux |
| RPI_MVSTRIDE | 0x8034 | colmv_stride |
| RPI_COLBASE | 0x8038 | `col_aux.addr>>6` or **0** |
| RPI_COLSTRIDE | 0x803C | colmv_stride |
| RPI_CURRPOC | 0x8040 | `slice_pic_order_cnt` (0 for IDR) |
| RPI_REFYBASE0.. | 0x9000 | 16 slots × 16B: REFYBASE/REFYSTRIDE(+4)/REFCBASE(+8)/REFCSTRIDE(+0xc); slot pitch 16. **IDR: all 16 = current frame** (h265.c:1726) |
| **RPI_NUMROWS** | **0x8010** | `pic_height_in_ctbs_y` — **apb_write_final STARTS PHASE 2** |

### 1d. ARGON INTC (Class C) — hevc_d_hw.h:151
`ARG_IC_ICTRL` off **0**. EN: ACTIVE1_EN BIT(2), ACTIVE2_EN BIT(6). Latched IRQ: ACTIVE1_INT
BIT(0), ACTIVE2_INT BIT(4). `SET_ZERO_MASK = (0xff<<12)|BIT(11)`. (M0/M1 exercise this.)

### 1e. Liveness: RPI_VERSION off **60** == **0x202** (hw_setup, hw.c:346).

## 2. Decode-run sequence (single frame)

**Setup once:** VERSION==0x202; `ARG_IC_ICTRL=ACTIVE1_EN|ACTIVE2_EN`; read back; write back to clear.

**Phase-0 (host CPU builds command buffer, `decode_slice` last_slice path, single-tile IDR):**
1. pre_slice_decode → slice-msg array (h265.c:1173)
2. write_bitstream → BFBASE, BFNUM, BFCONTROL×2 (:1174)
3. write_prob → 40 prob words @0x1000+, RPI_TRANSFER=PROB_BACKUP (:1183)
4. program_slicecmds → SLICECMDS + msg array @0x4000+ (:1185)
5. new_slice_segment → SPS0, SPS1, PPS, (scaling), SLICESTART (:1186)
6. new_entry_point → TILESTART, TILEEND, BEGINTILEEND(do_bte), SLICE, QP, MODE, CONTROL (:1187)
7. last_slice → final **RPI_STATUS = `1 | ((ctb_w-1)<<5) | ((ctb_h-1)<<18)`** (:1203)

**Phase-1 kick (direct APB):** PUWBASE/PUWSTRIDE/COEFFWBASE/COEFFWSTRIDE, `CFNUM=cmd_len`,
then **`CFBASE=cmd>>6` (final) — starts P1**.
**P1 done:** ARGON **ACTIVE1** (BIT0). Success ⇔ **`RPI_CFSTATUS(116) == RPI_CFNUM(112)`**.
Else RPI_STATUS(56) & (PU_EXHAUSTED=16 | COEFF_EXHAUSTED=8) → grow buffers + retry; else hard error.

**Phase-2 kick (direct APB):** PURBASE…COLSTRIDE + CONFIG2/FRAMESIZE/CURRPOC + 16 ref slots,
then **`NUMROWS=pic_height_in_ctbs_y` (final) — starts P2**.
**P2 done:** ARGON **ACTIVE2** (BIT4). No status poll — ACTIVE2 = frame done, output valid.

## 3. IRQ
DT `hevc_dec: codec@7eb10000 { interrupts = <GIC_SPI 98 LEVEL_HIGH>; }` (bcm2711.dtsi:616).
**abs IRQ = 98 + 32 = 130** (Phoenix convention confirmed: bcm-genet.c:237 SPI157=abs189).
ISR (hw.c:209): `ictrl=irq_read(0)`; spurious if no ALL_IRQ_MASK bit; ack `irq_write(0, ictrl & ~SET_ZERO_MASK)`;
ACTIVE2(BIT4)→P2 done (serviced first), ACTIVE1(BIT0)→P1 done. Bits are write-1-clear.
**First-light simplification (spec §6):** POLL `ARG_IC_ICTRL` for ACTIVE1/ACTIVE2 instead of
wiring IRQ 130 (M0 proved ICTRL R/W); wire the real IRQ once decode works.

## 4. DMA buffers — PLAIN PHYSICAL (no 0xc alias; scb has no dma-ranges, like genet)
addr reg = `phys>>6` (apb_write_vc_addr hw.h:122); len/stride = `(bytes+63)>>6` (apb_write_vc_len hw.h:136); AXI_BASE64=0.
| Buffer | Size | Align | Register(s) |
|---|---|---|---|
| Command buffer | init 64 KiB (8192×8B), grow pow2 ≤1 MiB | 64B | CFBASE(108)/CFNUM(112) |
| Bitstream | `min(bit_size/8+1, bytesused-off)` | byte(off in BFCONTROL) | BFBASE/BFNUM/BFCONTROL |
| PU scratch | `round_up_size(w*h/4)`; stride `ALIGN_DOWN(pu_size/ctbs_h,64)` | 64B | PUW*(80/84)→PUR*(0x8000/4) |
| Coeff scratch | `round_up_size(w*h)`; stride `ALIGN_DOWN(coeff/ctbs_h,64)` | 64B | COEFFW*(88/92)→COEFFR*(0x8008/C) |
| Output luma | SAND COL128 sizeimage | 128-col | OUTYBASE(0x8018)/STRIDE(0x801C) |
| Output chroma | SAND COL128 | 128-col | OUTCBASE(0x8020)/STRIDE(0x8024) |
| Ref frames ×16 | = output frame; **IDR: all=current** | 128-col | REFYBASE0(0x9000, pitch 16) |
| Collocated MV | `ALIGN(w,64)*(ALIGN(h,64)>>4)` | 64B | MV*(0x8030/4), COL*(0x8038/C) or 0 |

`round_up_size(x)` (video.c:38): x≥256 → n=ilog2(x); `x≥3<<n ? 4<<n : 3<<n`.
**Output = SAND / NV12_COL128, NOT linear** (h265.c:1633): 128-byte vertical columns;
`luma_stride=bytesperline*128`; chroma at `luma+height*128`; `width=ALIGN(w,128)`,`height=ALIGN(h,16)`.
OUTYSTRIDE = column-to-column stride. **SAND→linear unpack required before any pixel compare** (M4).

## 5. Minimal slice/param set for one IDR I-slice
- **SPS→SPS0/SPS1+CONFIG2/FRAMESIZE:** log2 CB/TB sizes, `bit_depth_*_minus8` (0 or 2, equal),
  `chroma_format_idc==1` (4:2:0), max transform hier depth, AMP/PCM/strong-smoothing, pic w/h.
  Set `SPS_TEMPORAL_MVP_ENABLED=0` → no colMV buffer needed.
- **PPS→PPS+CONFIG2:** init_qp_minus26, diff_cu_qp_delta_depth, cu_qp/transform-skip/transquant-bypass/
  sign-hiding/constrained-intra flags, cb/cr qp offsets, log2_parallel_merge_level. TILES/WPP disabled.
- **Slice→SLICE/QP/SLICECMDS/msgs/SLICESTART:** slice_type=I(2), slice_segment_addr=0,
  slice_qp=`26+init_qp_minus26+slice_qp_delta`, cb/cr qp offsets, beta/tc offsets, SAO+deblock flags,
  `data_byte_offset` (HW consumes slice *data*, not header), bit_size. dependent=0.
- **decode_params:** IDR → num_active_dpb_entries=0, empty DPB, slice_pic_order_cnt=0→CURRPOC.
- **CABAC (write_prob h265.c:514):** I-slice init_type=`2-slice_type=0`; per-ctx
  `pre=2*((m*q>>4)+n)-127`, `m=(v>>4)*5-45`, `n=((v&15)<<3)-16`, `q=clamp(slice_qp,0,51)`; clamp/mirror
  [0,124]; 40 words @0x1000+, then RPI_TRANSFER=`(20<<12)|(20<<6)`.
- **Ignore for M1/M2-first:** scaling_matrix, pred_weight_table, ref_idx/DPB/collocated/LTR.

## 6. Minimal M2 plan
Pick a tiny single-slice **IDR, 8-bit, 4:2:0, one tile, no WPP, no scaling, temporal-mvp off**
(64×64 or 128×128). Pre-parse SPS/PPS/slice on host (ffmpeg), hardcode derived fields + data_byte_offset,
ship raw slice bytes as a blob. Then: M0 preamble → alloc buffers (§4) → build command buffer in
`decode_slice` last_slice order (§2) → phase-1 kick (CFBASE last) → **poll ICTRL ACTIVE1**, check
CFSTATUS==CFNUM → phase-2 kick (NUMROWS last) → **poll ICTRL ACTIVE2** → verify output non-zero →
SAND→linear unpack Y, byte-compare vs ffmpeg SW decode.

**Top-3 risks:** (1) slice-header pre-parse / data_byte_offset (wrong → silent garbage; cross-check vs
ffmpeg); (2) CABAC prob-table + QP transcription (no HW error on error; port write_prob exactly);
(3) phase-1 emission-order / end-marker dependency (copy decode_slice last_slice path byte-for-byte).
Addressing is resolved (plain physical, >>6). Use uncached allocs to sidestep cache-clean sequencing.

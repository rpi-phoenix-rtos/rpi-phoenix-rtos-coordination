# HEVC / H.265 hardware decode on the Raspberry Pi 4 (BCM2711 rpivid)

A from-scratch HEVC decoder driving the Pi 4's **rpivid / hevc_dec** hardware block
directly over MMIO + the VideoCore mailbox — **no VCHIQ, no V4L2, no firmware decode
blob**. Ported register-by-register from the Linux `hevc_d_h265.c` driver.

## What works (all HW-verified, bit-exact vs ffmpeg unless noted)

| Capability | Status |
|---|---|
| Block reachability (clock, version 0x202, INTC) | ✅ M0 (`hevc-probe`) |
| DMA allocators + GIC SPI-98/irq-130 IRQ path | ✅ M1 (`hevc-m1.c`) |
| **Intra (I) single-frame** decode | ✅ bit-exact 64×64 → 640×480; runs 1920×1080 full-screen |
| Multi-CTB, multi-COL128-column-block, partial CTBs | ✅ (128×128, 320×240, 640×480, 1080p) |
| **All-intra video** playback (per-frame QP, on HDMI) | ✅ 48-frame 320×240 clip, 288/288 |
| **Inter (P) single-frame** — motion compensation | ✅ bit-exact (weighted + non-weighted) |
| **Multi-frame inter (IPPP, rolling DPB)** | ✅ bit-exact 128×128 (8/8) + 640×480 (4/4); runs 1080p |
| **Inter-coded video → HDMI playback** | ✅ 32-frame 320×240 IPPP |
| **Bidirectional (B) inter** — 2 ref lists (past L0 + future L1) | ✅ bit-exact, ANY count of consecutive non-reference B (bframes 1/2/3+, b-adapt ok) |
| **B-pyramid (hierarchical reference-B)** — general POC-indexed DPB | ✅ bit-exact (reference-B pics, 2-ref lists, RPS ref-lists, DPB eviction — x265 default) |
| **Real HD default-x265 content** (720p 240 CTBs, 1080p 510 CTBs) | ✅ bit-exact — b-pyramid + multi-ref + tmvp + WPP combined at HD (testdata/hd720.265, hd1080b.265) |
| **10-bit (Main10)** decode + **HDMI display** | ✅ bit-exact luma+chroma, NV12_10_COL128 packed output; renders on /dev/fb0 (10→8 downshift). all-intra Rext profile still out-of-subset |
| **Runtime `.265` file player (M3)** | ✅ `hevc-play <file.265>` — parse + decode + display I/P/B, no rebuild |
| **`.mp4`/`.mov` container demux (M3)** | ✅ `hevc-play <file.mp4>` — in-tool ISOBMFF→Annex-B (no ffmpeg); video track read by sample tables so **audio tracks are skipped** (normal a/v files play); >1 video track / fragmented rejected loudly |
| **`hevc-play` bit-exact conformance verify** | ✅ `hevc-play <f.265> <golden.nv12>` → VERIFY BIT-EXACT (ibp, mandelbrot, bframes=2/3, b-pyramid all 0 bad px) |
| Decode → SAND/COL128 unpack → NV12→RGB → /dev/fb0 → HDMI | ✅ |
| Intermittent decode corruption under memory-fabric contention | ⚠️ OPEN (SoC-level) — non-deterministic, worse under heavy DMA (complex clips / on-HDMI); simple clips clean; decoder-side software causes ruled out (see gotcha 8) |
| **Multi-ref (ref>1)** | ✅ bit-exact (free via the general DPB + resolve_reflist) |
| **Temporal-MVP (tmvp)** — collocated-MV path | ✅ bit-exact (per-DPB-slot colMV, x265 default-on; 64/128/320 verified) |
| **SAO (Sample Adaptive Offset)** — in-loop filter | ✅ bit-exact (RPI_SLICE bit14/15; HW CABAC-decodes per-CTB sao(); x265 default-on) |
| **WPP (wavefront, entropy_coding_sync)** | ✅ bit-exact (single-submit HW wavefront + entry-point sequence; x265 default-on) |
| **All default-on tools COMBINED** (SAO+WPP+tmvp+b-pyramid+multi-ref) | ✅ bit-exact on a near-default x265 clip (testdata/allfeat.265) |
| **Weighted prediction** (x265 `--weightp` default-on) | ✅ bit-exact (per-ref 8-word weight descriptor; luma + 2 chroma planes, computed §7.4.7.3 offsets; testdata/wp.265). Fully-default clip (tmvp+wpp+weightp) `dflt.265` → BIT-EXACT |
| tiles, nonzero deblock offsets, amp, EPB-in-header (large frames) | ⏳ out of subset |
| HW H.264 | ⛔ VCHIQ/firmware-walled (banked) |

## Layout

- `hevc-m2.c` — the decode engine + all test modes (selected by the `-DFRAME_HEADER=...`
  header, which sets compile-time geometry + slice params). Modes: single-frame verify,
  `CLIP_NFRAMES` (all-intra video), `IP_TEST` (single I+P), `IPPP_TEST` (rolling-DPB
  sequence, golden or `nogolden` playback).
- `hevc_regs.h` — the decode-path register map (cited to the Linux driver).
- `build-hevc-m2.sh` — builds the default single-frame binary (static, links libvcmbox).
- Frame headers (generated): `idr64_frame.h`, `detail{64,128,320}_frame.h`, `show640_frame.h`
  (intra); `ip_frame.h` / `ipnw_frame.h` (single I+P); `ippp128.h` / `ippp640.h` (golden
  IPPP); `ipppplay320.h` (playback). Each defines FRAME_* geometry + the slice payload.
- Generators: `gen-frame-header.py` (single intra), `gen-clip-header.py` (all-intra clip),
  `gen-ippp-header.py` (IPPP inter sequence; `nogolden` for long playback clips). They
  extract slice params via ffmpeg `trace_headers` (data_byte_offset varies per frame —
  derived from the last slice-header element's bit position).
- `testdata/*.265` — the committed test vectors + `gen-idr64.sh` (regenerate).
- Register spec: `docs/inprogress/2026-08-28-hevc-m2-register-spec.md`.

## Build + run a mode

```sh
GCC=.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc
VCM=sources/phoenix-rtos-devices/misc/rpi4-vcmbox
$GCC -O2 -static -Wall -Wextra -std=gnu11 -I$VCM -Itools/hevc-decode \
    -DFRAME_HEADER='"ippp128.h"' -o /tmp/hevc-ippp tools/hevc-decode/hevc-m2.c $VCM/libvcmbox.c
# stage to the NFS export /bin and run over netboot (needs /dev/vcmbox, started by plo)
```

## Hard-won gotchas (each cost a debug cycle)

1. **DMA doorbell ordering needs `dsb sy`, not `dmb ish`** — after writing the uncached
   command-buffer/bitstream, before the CFBASE/NUMROWS kick, use a full system barrier
   (`hevc_dma_fence()` = `dsb sy`), not `__sync_synchronize()` (which emits inner-shareable
   `dmb ish`). The rpivid is a non-coherent SYSTEM DMA master outside the CPU inner domain,
   so `dmb ish` does not order the Normal-NC input writes against it; else the block reads a
   stale buffer → stall / wrong decode.
2. **Per-frame QP** — x265 rate-control varies `slice_qp` per frame; a wrong QP feeds the
   wrong CABAC init table → entropy desync → stall (CFSTATUS one-short).
3. **CABAC init_type** = `2 − slice_type` (I=0, P=1, B=2) — needs the full `prob_init[3]`.
4. **MVSTRIDE/COLSTRIDE = 0** when temporal-MVP is off (match the driver; non-zero stride
   with base 0 is a combination the driver never emits).
5. **Slice-message stream must be complete** — `pre_slice_decode` always emits 2 trailing
   messages (deblock 0x200 + CMD_QPOFF) after the ref descriptors; omitting them hangs
   phase 2 (only bites once `SLICECMDS>0`, so an all-intra frame with 0 messages is fine).
6. **x265 `wpp=0`** — WPP auto-enables past ~256px wide; the harness is single-tile/no-WPP.
7. **Output is SAND / NV12_COL128 tiled** — unpack (pixel(x,y)=buf[(x/128)*stride + y*128 +
   x%128]) before any pixel compare / display.
8. **Residual intermittent decode corruption under memory-fabric contention (OPEN, SoC-level).**
   A small number of wrong output pixels appear non-deterministically in a fraction of decodes.
   It is **worse under heavier memory traffic** — a concurrent `fb_blit` during on-HDMI
   playback, and (independently) high-complexity clips that do more PU/coeff/reference DMA —
   and it is **content- and process-independent** (the same clip is bit-exact one run and
   corrupt the next; even an IDR I-frame occasionally corrupts). Simple/low-traffic clips are
   effectively always clean (e.g. an `ultrafast`-preset clip verified 15/15 back-to-back).
   NOTE: an earlier belief that this was "fb_blit-only, bit-exact headless" was **corrected** —
   it manifests headless too; it is amplified, not caused, by `fb_blit`.

   The decoder-side software causes have been **exhaustively ruled out**:
   - **Barriers are architecturally complete** — `dsb sy` before each doorbell (gotcha 1),
     the ARGON INTC init-flush, and a `dsb sy` after the completion poll before the CPU reads
     the Normal-NC output (mirrors the Linux driver's `readl`/`__iormb` ordering). The last one
     measurably helped (an `ultrafast` clip went 3/10 → 15/15 clean) but did not close it.
   - **Not PU/coeff buffer exhaustion** — `RPI_STATUS` never sets the `PU_EXHAUSTED`/
     `COEFF_EXHAUSTED` bits on a corrupt frame, and `CFSTATUS == CFNUM` always.
   - **Not CPU polling/bus contention** — switching completion from ICTRL hot-polling to true
     IRQ-driven (`condWait` on SPI-98; the CPU blocks like Linux) left the rate unchanged.
   - **No missing init** — a survey of the Linux `hevc_d` driver + BCM2711 DT found it programs
     nothing beyond clock-enable + INTC-enable + version-check (no QoS/AXI-arbitration/reset/
     power/2nd-clock/firmware tag); we already set the HEVC clock to the firmware **max** and
     the DMA buffers are low-PA (no 36-bit truncation).

   ⇒ the residual is a genuine SoC memory-fabric interaction under decode DMA load, not a
   decoder-side software omission. The decisive next step is a Linux-on-the-same-Pi4 `hevc_d`
   side-by-side on the same clips. Impact: core decode + the `hevc-play` file player work for
   simple/low-traffic content; complex clips and on-HDMI playback show occasional glitched
   pixels. Repro with the `IPPP_STRESS` harness (`-DIPPP_STRESS`, `-DSTRESS_SLEEP[_MS]`,
   `-DNO_FRAME_SLEEP`) or any high-complexity clip via `hevc-play <clip> <golden>`.

## M3: runtime `.265` file player (done)

`hevc-play <file.265>` decodes + displays any I/P H.265 file in the x265 subset above with
no rebuild. It parses geometry from the SPS (`hevc_parse.*`) into runtime globals
(`g_frame_w/h`, `g_ctb_w/h`) — the fixed subset constants stay compile-time
(`play_subset.h`, e.g. `CONFIG2`, CTB log2, bit-depth) since the register values bake them
in. Per-frame params (type/POC/qp/data_byte_offset/bfnum) come from `hevc_parse_slice()`.
The bitstream DMA buffer is sized to the file's largest slice NAL (not a fixed cap), so a
high-bitrate frame can't silently overflow it. Build + run:

```sh
$GCC -O2 -static -Wall -Wextra -std=gnu11 -I$VCM -Itools/hevc-decode -DPLAY_TOOL \
    -o /tmp/hevc-play tools/hevc-decode/hevc-m2.c tools/hevc-decode/hevc_parse.c $VCM/libvcmbox.c
# stage hevc-play to /bin and a .265 next to it, then: hevc-play /root/clip.265
```

`hevc-play` also accepts an `.mp4`/`.mov` container directly (`hevc_mp4.{h,c}`): if
the file opens with an `ftyp` box it is demuxed to an in-memory Annex-B stream (no
ffmpeg / libavformat), then the existing raw pipeline runs unchanged. The demux
locates the HEVC video track's coded samples via its `stsc` → chunk → `stco`/`co64`
mapping (sizes from `stsz`) and reads each sample from its **true file offset**, so a
normal audio+video `.mp4` works — the audio track's interleaved chunks are simply not
visited. Each sample is a run of length-prefixed NALs (emitted as Annex-B); VPS/SPS/PPS
come from `hvcC`. Deliberately narrow ("reject don't mis-handle"): exactly one HEVC
video track, non-fragmented — no-video / >1-video-track / fragmented (`moof`) files are
**rejected loudly** (`hevc_err()`). HW-proven: `hevc-play wp.mp4`/`dflt.mp4`
(single-track) + `wpaudio.mp4`/`hd720aud.mp4` (audio+video, interleaved) all → VERIFY
BIT-EXACT against the raw-`.265` goldens. Regenerate fixtures with `testdata/gen-mp4.sh`.

**10-bit (Main10).** The decoder handles 10-bit 4:2:0 as well as 8-bit, keyed off the
SPS bit depth at runtime (`g_bd_minus8`). Four things change vs 8-bit (per the Linux
`hevc_d` driver): CONFIG2 low 10 bits `0x088→0x3AA` (BitDepthY/C nibbles 8→10 + the
"depth≠8" flags), RPI_SPS0 bit-depth fields `+0x220000`, RPI_QP gains QpBdOffsetY
(`6·2 = +12`), and the output is **NV12_10_COL128** — 3 samples packed LSB-first into
each 32-bit little-endian word, so a 128-byte SAND column holds 96 samples (the stride
register values are unchanged; the column *count* grows ×4/3). CABAC prob tables,
PU/coeff/colMV strides are bit-depth-invariant. The `--verify` golden for 10-bit is
`yuv420p10le` (16-bit LE, right-aligned 0..1023 — what the HW emits), not `p010le`
(which is `<<6`); the verify checks both luma and chroma planes. HW-proven bit-exact
on a 256×256 Main10 clip (intra + inter P/B), and it **displays on HDMI** (the SAND
unpack — shared `sand10()` — downshifts 10→8 before the NV12→RGB blit; verified
on-screen). Regenerate with `testdata/gen-10bit.sh`. (All-intra 10-bit x265 selects the
Range-Extensions "Rext" profile with extra tools outside this subset — still rejected/
mismatched; Main10 inter GOPs, which carry IDR I-frames, cover 10-bit intra.)

Weighted prediction is applied purely via the per-ref slice-message descriptor: a
weighted ref grows from 2 words to 8 — word0 gains marker bits `[6:5]=3`, then 6
weight words (luma denom|weight + 8-bit offset, then the same for each chroma plane).
The parser computes LumaWeight/ChromaWeight/ChromaOffset (§7.4.7.3, defaults for
unflagged refs); no cmd_slice/CONFIG2/slice_const change. `-DHEVC_NO_WEIGHT` builds
the non-weighted-descriptor negative control (`hevc-play-noweight`).

Out of scope / next: resolutions/params beyond the x265 subset (tiles, nonzero
deblock offsets, amp, EPB-in-header for large frames), and wiring the decoder into
the ffmpeg port so arbitrary `.265` files feed through libavcodec.

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
| **Runtime `.265` file player (M3)** | ✅ `hevc-play <file.265>` — parse + decode + display I/P/B, no rebuild |
| **`hevc-play` bit-exact conformance verify** | ✅ `hevc-play <f.265> <golden.nv12>` → VERIFY BIT-EXACT (ibp, mandelbrot, bframes=2/3 all 0 bad px) |
| Decode → SAND/COL128 unpack → NV12→RGB → /dev/fb0 → HDMI | ✅ |
| Intermittent inter corruption during on-HDMI playback (~10%) | ⚠️ known, decoder bit-exact HEADLESS in isolation (see gotcha 8) |
| b-pyramid (reference B), multi-ref (ref>1), SAO, WPP, tmvp | ⏳ out of subset — need a general POC-indexed DPB / feature work |
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
8. **Residual intermittent inter (P) corruption during on-HDMI playback (OPEN).** The
   decoder is reliable in isolation: **600/600 bit-exact back-to-back** (`-DIPPP_STRESS`,
   no display). The corruption appears only when `fb_blit` runs between frames (real video
   playback): the first corrupt frame fails its **own** golden (decode-input corruption),
   then errors compound down the P-chain; frame 0 (I) is always exact. Systematically
   REFUTED: phase-1→phase-2 PU/COEFF drain (RPI_STATUS read-back, no help); idle-gap
   duration (40/100/200 ms with no fb all clean); framebuffer/DMA physical overlap (PAs
   disjoint); MAP_UNCACHED cacheable alias (pmap: Normal-NC, no alias; Linux uses no
   `dma_sync`); buffer-specific effect (a scratch-buffer `fb_blit` corrupts identically →
   it's the fb *write* burst, not touching the output). Upgrading the pre-doorbell barrier
   to `dsb sy` (gotcha 1) + a fence before the input memcpy helped only marginally
   (~78% → ~90% pass, within n=24 noise) — so the residual is a deeper SoC memory-fabric /
   write-combine interaction between `fb_blit`'s heavy Normal-NC store burst and the
   subsequent decode, not closed by the architecturally-correct barrier. Repro with the
   `IPPP_STRESS` harness (`-DIPPP_STRESS`, `-DSTRESS_SLEEP[_MS]`, `-DNO_FRAME_SLEEP`).
   Impact: core decode + the `hevc-play` file player work; on-HDMI playback shows an
   occasional glitched frame (~10%).

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

Out of scope / next: B-frames, weighted-P in the player (non-weighted only today),
resolutions/params beyond the x265 subset, and wiring the decoder into the ffmpeg port.

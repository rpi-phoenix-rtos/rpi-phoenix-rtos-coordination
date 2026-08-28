# HEVC / H.265 hardware decode on the Raspberry Pi 4 (BCM2711 rpivid)

A from-scratch HEVC decoder driving the Pi 4's **rpivid / hevc_dec** hardware block
directly over MMIO + the VideoCore mailbox — **no VCHIQ, no V4L2, no firmware decode
blob**. Ported register-by-register from the Linux `hevc_d_h265.c` driver.

## What works (all HW-verified, bit-exact vs ffmpeg unless noted)

| Capability | Status |
|---|---|
| Block reachability (clock, version 0x202, INTC) | ✅ M0 (`hevc-probe`) |
| DMA allocators + GIC SPI-98/irq-130 IRQ path | ✅ M1 (`hevc-m1.c`) |
| **Intra (I) single-frame** decode | ✅ bit-exact 64×64 → 640×480 |
| Multi-CTB, multi-COL128-column-block, partial CTBs | ✅ (128×128, 320×240, 640×480) |
| **All-intra video** playback (per-frame QP, on HDMI) | ✅ 48-frame 320×240 clip, 288/288 |
| **Inter (P) single-frame** — motion compensation | ✅ bit-exact (weighted + non-weighted) |
| **Multi-frame inter (IPPP, rolling DPB)** | ✅ bit-exact 128×128 (8/8) + 640×480 (4/4) |
| **Inter-coded video → HDMI playback** | ✅ 32-frame 320×240 IPPP |
| Decode → SAND/COL128 unpack → NV12→RGB → /dev/fb0 → HDMI | ✅ |
| B-frames, runtime `.265` file parser (M3), >VGA | ⏳ not yet |
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

1. **DMA ordering** — `__sync_synchronize()` after writing the uncached command-buffer/
   bitstream, BEFORE the CFBASE kick; else the block DMA-reads a stale buffer → stall.
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

## Next (M3): a runtime `.265` file player

Today each vector is parsed at *build* time (the python generators) and compiled in. A
usable player needs a **runtime** minimal slice-header parser (the fields the generators
extract) + runtime geometry from the SPS, so `hevc-play <file.265>` decodes + displays any
I/P H.265 file (x265 params above) without a rebuild. The decode engine (`decode_one` +
`build_command_buffer`) already takes per-frame params — the missing piece is moving the
FRAME_* geometry macros to runtime variables + the bitstream parser.

# HDMI colour and the wallpaper banding (owner question, 2026-09-27)

Owner: "Are we rendering in True Color? The gradient in the background looks a bit strange, not fully smooth.
Could be the grabber compression, resolution scaling, the image, or the colour depth." This file separates those
stages. Measurements on the host use PIL on the files below; the Pi part is pre-registered here before any data.

## What is known (2026-09-27 21:10)

| stage | finding | evidence |
|---|---|---|
| framebuffer / plane | **true colour**: 1920x1080, `XB24` (XBGR8888, 8 bits per channel, 32-bit pixels); swaybg's buffer 8 294 400 B = 1920·1080·4; foot "using 8-bit RGB surfaces" | `KMS fw … fmt=XB24`, `SHMSRV truncate … size=8294400` in m7c/m7h logs |
| **the wallpaper file itself** | **8-bit gradient with no dithering**: along row 1000, 176 distinct colours for 1920 px, each colour a flat run of mean **11 px** (max 29). That bands on any display | `phoenix-gradient-1920x1080.png` (M7 stage 1, host-generated) |
| grabber resolution | our snapshots are **3840x2160**: `ffmpeg -f v4l2 -i /dev/video4` with no size takes the grabber's default mode; 1867/1920 captured pixel pairs are identical = a **2x nearest-neighbour upscale** of the 1080p signal inside the grabber | `scripts/test-cycle-psh-interact.sh` `hdmi_grab_one`; `v4l2-ctl --list-formats-ext` |
| grabber pixel format | **YUYV 4:2:2** (chroma at half horizontal resolution), reported `Colorspace sRGB, Transfer Rec.709, YCbCr encoding ITU-R 601, Quantization default → limited range` | `v4l2-ctl --all` |
| colour shift | captured colours differ from the file: e.g. (212,92,25) → (165,118,40) at the right edge (less red, more green), dark end lifted (90,49,49) → (97,61,63); mean |Δ| 13, max 47. A scale does not do that; a **YCbCr matrix / range mismatch** does | host comparison, m7c frame `20260927-203751` |
| no JPEG blocking | edge energy at 8-px boundaries 0.21 vs mid-block 0.19 | same row |
| HDMI link | `config.txt`: `hdmi_group=1 hdmi_mode=16` (CEA 1080p60), no `hdmi_pixel_encoding` ⇒ the firmware sends **limited-range RGB (16–235)** for CEA modes; whether the grabber expands it depends on the AVI InfoFrame | `.buildroot/_boot/…/config.txt` |

Host result: a **dithered** version of the same wallpaper (`phoenix-gradient-dither-1920x1080.png`: float smooth +
error-feedback dither) has mean flat run **1.4 px** (max 14) instead of 11 (max 29), so it looks smooth at 8 bpc.

## Pre-registered: `hdmi-calib` A/B (chain63, after build 19)

A calibration image (`/usr/share/backgrounds/phoenix/phoenix-calib-1920x1080.png`: 8 colour bars, a 256-step grey
ramp, R/G/B ramps) shown full-screen by swaybg under labwc-2. The cycle's periodic snapshots are off; during the
hold the queue grabs **1920x1080 YUYV raw + PNG**, **1920x1080 MJPEG**, and the usual **3840x2160 default**, into
`artifacts/hdmi/calib/<arm>-*`. Arm **A** = the firmware default; arm **B** = `hdmi_pixel_encoding=2` (full-range RGB)
added temporarily to the TFTP `config.txt` (restored by trap).

Readings (fixed now):
- grey ramp in the raw YUYV: **Y spans 16–235 in both arms** ⇒ the grabber always sends limited-range YCbCr (normal
  for V4L2); what matters is whether ramp input 0/255 maps to Y 16/235 (**full-range link understood**) or to
  ~30/~218 (**limited-range link expanded twice / not at all**). Arm B should give 0→16, 255→235 exactly if the
  grabber honours the infoframe; if arm A already does, the link range is fine.
- colour bars: fit Cb/Cr of the 100 % bars against BT.601 and BT.709 matrices ⇒ which matrix the grabber uses. If
  it is 709 while it reports 601 (or ffmpeg assumes 601), the **snapshots' hue shift is a capture-side artefact**
  and the fix is in `hdmi_grab_one` (request 1920x1080, tell ffmpeg `-colorspace bt709 -color_range tv`).
- grey ramp steps: every one of the **220** limited-range Y codes used, monotonic ⇒ the Pi scans out 8 bits per
  channel end to end; **~64** ⇒ 6 bpc somewhere on the Pi side (plane/HVS/HDMI) — a real rendering defect.
  (Corrected 21:45, before any data: this line first said "256 distinct steps", which a limited-range Y channel
  cannot show.)

Grader: `scripts/hdmi-calib-analyse.py <arm>-1080-yuyv.raw`. It reads the raw YUYV, so no ffmpeg colour
conversion sits between the card and the numbers. Self-test on frames synthesised by ffmpeg from the calibration
PNG: BT.709 frame → 709 chroma rms 0.4 vs 601 7.3; BT.601 frame → 601 0.4 vs 709 7.2; limited-range RGB read as
full → `Y(0)=29 Y(255)=217`, classified "LIMITED-range link read as full".

Fix plan independent of the result: stage the dithered wallpaper for the demo (swaybg + xfdesktop), capture at
native 1920x1080 for documentation screenshots, and correct the capture colour matrix once measured.

## Result — `hdmi-calib` A/B (chain63, 2026-09-27 22:28 / 22:37)

Graded with `scripts/hdmi-calib-analyse.py` on the raw YUYV frames (`artifacts/hdmi/calib/`).

| reading | pre-registered | measured | verdict |
|---|---|---|---|
| link range | ramp 0/255 → Y 16/235 = full range understood | **B: Y(0)=16.0, Y(255)=235.0** | ✓ the card receives full-scale 0–255 |
| arm A vs arm B | differ if `hdmi_pixel_encoding=2` changes the link | ffmpeg's PNG of A and of B are **byte-identical** | no difference: the link was already right |
| precision | all 220 limited-range Y codes, monotonic | **200 distinct Y means over 256 ramp steps** in the raw; the BT.709 decode of the ramp has **220 distinct levels** | ✓ **8 bits per channel end to end**; nothing near the 64 of a 6-bit path |
| capture matrix | fit BT.601 vs BT.709 | chroma rms **BT.709 2.2**, BT.601 7.1 (bars) | the card encodes **BT.709** while it reports BT.601 |
| ffmpeg default decode | — | mean \|Δ\| RGB vs the source (12.3, 6.1, 8.3); **with `in_color_matrix=bt709`: (6.9, 4.1, 5.7)** | the snapshots' hue shift is a **capture-side decode error** |

Arm A's raw frame is not a picture (every bar Y≈14, the ramp half height): it was the first frame the card delivered
after switching from its default 4K mode to 1080p. Arm A's PNG, grabbed seconds later, is byte-identical to arm B's.

**Answer to the owner's question.**
- **True colour: yes.** The plane is XB24 (8 bits per channel) and the grey ramp survives the whole path (plane → HVS
  → HDMI → card) with every level the capture format can hold. No 6-bit truncation, no dithering loss on the Pi.
- **The banding was in the wallpaper file**: an undithered 8-bit gradient, 176 colours over 1920 px (flat runs of
  11 px). Any display shows that as bands. The dithered version (flat runs 1.4 px) is now the demo wallpaper
  (`/bin/xfce-session`, xfdesktop default backdrop).
- **The screenshots added two artefacts of their own**:
  - they were 4K grabs, a 2× nearest-neighbour upscale inside the card;
  - they were decoded with the wrong matrix (BT.601 for a BT.709 signal): less red, more green, lifted blacks.
- **HDMI range setting: no change needed.** `hdmi_pixel_encoding=2` changes nothing the card can see; `config.txt` stays
  as it is.

**Fix (this commit).** `hdmi_grab_one` in `scripts/test-cycle-psh-interact.sh` and `scripts/test-cycle-netboot.sh` now asks for
`yuyv422` at 1920x1080 and decodes with `scale=in_color_matrix=bt709:in_range=tv`, falling back to the card's default mode
if the native one is refused. The remaining error (~5 per channel, std ~15–20) sits at bar edges: 4:2:2 chroma is half
horizontal resolution, a limit of this card's YUYV mode.

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
- grey ramp steps: 256 distinct steps visible in the Y channel ⇒ the Pi scans out 8 bits per channel end to end; far
  fewer ⇒ precision is lost on the Pi side (plane/HVS/HDMI) — would be a real rendering defect.

Fix plan independent of the result: stage the dithered wallpaper for the demo (swaybg + xfdesktop), capture at
native 1920x1080 for documentation screenshots, and correct the capture colour matrix once measured.

# Pi 4 learnings backlog, addendum 2 (2026-11-20)

Written 2026-11-20. **Documentation only: no Phoenix code, image or Pi was touched to write this.**

Continues [pi4-learnings-backlog.md](pi4-learnings-backlog.md) (tasks 1-32) and
[the 2026-11-17 addendum](pi4-learnings-backlog-addendum-2026-11-17.md) (tasks 33-42); numbering continues from 43.
Same scales (High / Medium / Low; S = under a day, M = 1-3 days, L = a week or more). The findings were measured
by the other OS port on the same board in its last two days of video work, and relayed on 2026-11-20. Phoenix
evidence below comes from reading our source; nothing was run.

**Where Phoenix stands.** `rpi4-kms` drives the firmware's `SET_PLANE` for **RGB scan-out only**: the plane
backend fills `num_planes = 1` and never sets `color_encoding` (`video/rpi4-kms/kms_backend.c:170`, `:331`), and
`kms_scanout.h:58` refuses Broadcom UIF/SAND/T-tiled modifiers. Hardware-decoded HEVC reaches the screen either
through the CPU (SAND → planar, then ffplay) or, in the browser, through the GPU (M10b path B: a SAND blit,
3.9 ms/frame). So tasks 43-46 are the plan for a **YUV video plane**, which [M10b](../gpu-new-lane/M10b-video-zero-copy.md)
lists as "not started: the players"; 47 is a cheap browser A/B; 48 feeds task 36 (H.264).

## Index

| # | Priority | Task | Effort |
|---|---|---|---|
| 43 | Medium | YUV video plane for the players: SAND straight to the firmware plane (NV12_COL128, below 1 GiB) | M |
| 44 | Medium | Plane colour encoding from the stream's tag, not from the picture height | S |
| 45 | Medium | A boot-time reserved pool below 1 GiB for scan-out pictures | S-M |
| 46 | Low | Browser video on a YUV plane with a colour key; no window redraw when only the video changed | L |
| 47 | Medium | Build Skia (WPE's CPU raster) with clang and compare | S |
| 48 | — | H.264 via VCHIQ + MMAL: five operational limits (folded into task 36) | reference |
| 49 | Low | Bench: an agent's ssh helper must check it reached its own Pi session | S |

## Medium

### 43. YUV video plane for the players: SAND straight to the firmware plane

**Measured by the second port** (HEVC on the hardware block, a media-player-style plane path, no CPU SAND →
planar, every frame shown, output bit-exact against the CPU decoder):

| HEVC | CPU de-tile + RGB path | SAND on the plane |
|---|---|---|
| 1080p30 | 23 % of a core | 4.4-6.5 % |
| 1080p30 Main 10 | 44 % | 5-6 % |
| 720p | 12 % | 3-4 % |
| 4K (with task 45's pool) | 78.6 %, ~360 frames dropped | 5.7-7 %, 0 dropped |

**Two hardware conditions.**
- **Layout.** The firmware accepts SAND only when luma and chroma share the column layout: **NV12_COL128**, one
  buffer, column stride C × 128, chroma at row H16 inside each 128-byte column. Separate per-plane SAND buffers
  are refused. Check what `hevc_rpivid` writes (`sources/phoenix-rtos-ports/video_player/files/rpivid/src`) before
  wiring `ADDFB2` NV12 + SAND128 into `rpi4-kms`.
- **Address.** The display fetches only the low 1 GiB (30 address bits); we know this already for RGB
  (`kms_scanout.h:27`, `kms_fw.c:128-130`). The HEVC block can write anywhere (M10b round 4), but a picture meant for
  scan-out must be allocated below 1 GiB, which leads to task 45.

**Phoenix work.** Accept the SAND128 modifier for NV12 in `kms_scanout.h`, fill `num_planes`/`planes[]`/`is_vu`
for it in the plane backend, let ffplay (SDL KMSDRM) or a small player hand the decoder's buffer over as a dma-buf
(M10b §3 already shares the buffers), and keep the CPU path as the fallback.
**Verify on the Pi:** `video-play` 720p/1080p/Main 10 CPU % against today's, frames shown = frames decoded,
same-scene screenshot against the CPU path.

### 44. Plane colour encoding from the stream's tag

**Measured by the second port**, probing the HVS through the firmware's `SET_PLANE` `color_encoding` byte:

| value | matrix and range |
|---|---|
| 1 | BT.601 limited |
| 2 | BT.709 limited |
| 3 | BT.601 **full** |
| 9 | BT.2020 |
| 0 and every other value | BT.709 (limited) |

There is no full-range BT.709 and no full-range BT.2020: convert those to limited range first, or leave them on
the GPU path. BT.2020 on the plane was up to 11 code values off a software reference, so the second port keeps it
off the plane. Passing the stream's real tag gave 11 test clips (SD/HD, 601/709, 601-tagged HD, full range, HEVC
8/10-bit) within ±3 of the RGB path. Guessing by height (709 from 720 lines up), which is what they did before,
gave a hue shift on 601-tagged HD.

**Phoenix work:** with task 43, map `AVFrame.colorspace`/`color_range` to this byte and fall back to the GPU path
for the two combinations the plane cannot show. **Verify:** a 601-tagged HD clip, same-scene against the RGB path.

### 45. A boot-time reserved pool below 1 GiB for scan-out pictures

**Measured by the second port:** a lowest-address-first allocator plus pinned pages scattered through low memory
left **no free contiguous 12.7 MiB below 1 GiB after ~20 browser runs**, so even 1080p fell back from the plane.
Retries and compaction did not help; a pool reserved at boot did. Sizes: **192 MiB** holds 4K 8-bit (~190 MB of
pictures); 4K Main 10 needs ~255 MiB. With it, 4K HEVC went to 5.7-7 % of a core with 0 frames dropped, and 1080p
stayed on the plane after heavy use.

**Trap:** never return a picture's memory to the pool while any other mapping of it exists. Otherwise the display
scans out reused memory, and another client can read the next stream's frames.

**Phoenix today:** `rpi4-kms` already has a scan-out pool below 1 GiB for RGB buffers (`srv.pool_ok`,
`kms_backend.c:189`; "our pool buffers scanned below 1 GiB", `:23`). Size it for pictures too (or add a second
pool), and make the free path wait until the last mapping is gone (the same rule as task 42's buffer reuse).
**Verify:** 20 consecutive 1080p plays, then a 1080p play still on the plane.

### 47. Build Skia with clang and compare

**Measured by the second port:** Firefox's CPU compositor (SWGL) built by clang 21 instead of GCC 16 composited
in 14 % less CPU at 720p and 25 % less at 1080p, and 1080p dropped frames fell from 33 % to 5 %: GCC misses its
vector code paths. **Phoenix:** WPE composites with Skia's CPU raster (M10b status, build 60), and Skia upstream
states GCC builds are much slower (its raster pipeline's vector code is tuned for clang). Building Skia alone with
clang is a cheap A/B: the browser's scroll/video benchmarks, CPU % and dropped frames, same pages. Needs a clang
cross toolchain for aarch64-phoenix first (not checked whether ours has one; inferred effort S if it does).

## Low

### 46. Browser video on a YUV plane with a colour key

**Measured by the second port:** instead of a GPU blit (our M10b path B), the browser put the decoded video on a
YUV plane behind a colour key. Per 10 s of 720p, compositor CPU went 8.2 s → 0.9 s, and 1080p start-up drops
120 → ~33. The biggest single win was **not redrawing the window when only the video changed**: the browser engine
marked the video rectangle dirty every frame, and excluding it took the window-system copies to 0.
Rules they needed: translucent content over the video (captions, controls) is drawn on the CPU/GPU instead; only a
fully visible, still rectangle goes on the plane (none while scrolling or partly hidden); the plane is released
when the tab is hidden or the window minimised; screenshots show black there.
**Phoenix:** depends on 43-45 and on a compositor that can place a plane; the "do not mark the video rectangle
dirty" part may pay off on path B too, and is worth measuring first.

### 49. Bench: an agent's ssh helper must check it reached its own Pi session

The second port's ssh helper once reached the other agent's running Pi session because its own session start had
been refused. It now checks the session label first. Phoenix's cycles use the UART and the bench lock, not ssh, but
any helper that logs in over the network should check the boot's label (for example `/etc/build-versions` or a
per-cycle token) before acting.

## Reference

### 48. H.264 via VCHIQ + MMAL: five operational limits (folded into task 36)

Measured by the second port on the BSD-licensed route (FreeBSD's vchiq + Broadcom's BSD-3 libmmal/VCOS); recorded
in [the L36 design note](../misc/2026-11-18-h264-vchiq-mmal-design.md) §"Field notes 2026-11-20":
1. Build VCOS with `NDEBUG`: otherwise `vcos_assert` calls `abort()` inside the client.
2. A second decoder fails with `ENOSPC` at the default `gpu_mem=76` (one 1080p decoder fits), and its error flood
   can wedge the firmware's MMAL service until reboot: allow one H.264 decoder system-wide (a file lock) and fall
   back to software.
3. The decoder manages ~69 fps at 1080p, so 1080p60 dropped 216-538 frames, while software decode played 1080p60
   with 0-11 of ~1500 dropped at ~2 cores: choose by pixel rate (above 1080p at 45 fps → CPU).
4. A decode and an encode at once need `gpu_mem=128`.
5. `h264_mmal`'s `initial_qp > 0` silently disables the encoder's rate control.

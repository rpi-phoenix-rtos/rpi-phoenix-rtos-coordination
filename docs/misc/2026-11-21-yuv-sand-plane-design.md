# YUV video plane: HEVC SAND pictures straight to the firmware plane (backlog tasks 43-45)

2026-11-21. Research by code reading. **Status 2026-11-22: plan step 1 done and on HDMI** (rpi4-kms NV12 + SAND128 on an
overlay, devices `f214377d`; libdrm_phoenix `cf07327`; `-o 1` project `e7932af`; test `tools/gpu-lane/sand-plane`, manifest
`2026-11-22-learnings-p`). **Step 2 done 2026-11-22** (ports `2aa2ce8`, `/usr/bin/video-plane`, manifest `2026-11-22-learnings-q`): 1080p HEVC at
30 fps on the overlay for 1.9 % of a core. **Task 44 and players step 1 done** (manifest `2026-11-22-learnings-t`):
COLOR_ENCODING / COLOR_RANGE on the overlays; ffplay on the overlay with `VIDEO_PLANE=1`. Next: §5.2 (Wayland), §5.3 (WPE). Tasks and the measurements that motivate them:
[`pi4-learnings-backlog-addendum-2026-11-20.md`](../inprogress/pi4-learnings-backlog-addendum-2026-11-20.md),
tasks 43 (SAND on the plane), 44 (colour encoding from the stream tag), 45 (a scan-out pool for pictures). Measured
on another port: 1080p30 HEVC 23 % of a core on the CPU de-tile + RGB path against 4.4-6.5 % with SAND on the plane;
4K went from 78.6 % and ~360 dropped frames to 5.7-7 % and none, given the pool of task 45.

## 1. What we have

### 1.1 The decoder already writes the one layout the firmware accepts

The firmware takes SAND only when luma and chroma share the column layout (NV12_COL128: one buffer, column stride
C × 128, chroma at row H16 inside each 128-byte column). Separate per-plane SAND buffers are refused.

- Default geometry, `rpivid_geom()` (`ports video_player/files/rpivid/src/rpivid_cmd.c:658`): luma and chroma are
  **two buffers**, column stride `H16 × 128` for luma and half that for chroma. This is what the CPU path de-tiles
  (`rpivid_sand.c:102` `rpivid_sand8_to_planar`, `:142` the 10-bit variant). Not plane-compatible.
- **`rpivid_geom_col128()` (`rpivid_cmd.c:683`) is exactly NV12_COL128**: `col_height = H16 + H16/2`, one buffer,
  `chroma_offset = H16 × 128`, both strides `col_height × 128`.
- It is already used by the decoder's **`drm_prime` output** (`rpivid_hevc.c:1251`, log line at `:1316`): one buffer
  per picture (`:100`), chroma pointer = luma + `chroma_offset` (`:280-282`), exported as an AVDRMFrameDescriptor
  with modifier `RPIVID_DRM_SAND128(col_height)` (`:290`) and plane 1 at `chroma_offset` (`:298`).
- 10-bit: the column holds 96 samples (3 per 32-bit word, `rpivid_cmd.c:666-671`); that is the firmware's
  `VC_IMAGE_YUV10COL` (59). Main 10 needs that image type and the same single-buffer layout.

So task 43's first question is answered: **no decoder layout change is needed**, only the drm_prime output path.

### 1.2 What stops the decoder's pictures from reaching the plane

- **Address.** Pictures come from `dma_map()` (`rpivid_hw.c:465`), `MAP_CONTIGUOUS | MAP_ANONYMOUS`, accepted up to
  `RPIVID_DMA_LIMIT` = 16 GiB (`rpivid_hw.h:32`). The display fetches nothing at or above 1 GiB
  (`rpi4-kms kms_scanout.h:27` `KMS_SCANOUT_LIMIT`). A picture meant for the plane must come from below 1 GiB.
- **Import rules.** `kms_import_fb_why()` (`kms_scanout.h:48`) refuses every modifier but linear ("modifier"), and
  requires `pitch >= width × 4`, i.e. RGB only.

### 1.3 rpi4-kms today

- Formats: XRGB/ARGB/XBGR/ABGR8888 only (`kms_proto.h:229-233`); `vc_type()` (`kms_backend.c:216`) maps to
  `VC_IMAGE_XRGB8888`/`ARGB8888` (44/43).
- SET_PLANE (`kms_backend.c:158-174`, 60 bytes, matches Linux `vc4_firmware_kms.c:64-96`): we always send
  `num_planes = 1`, `planes[0]`, `is_vu = 0`, `color_encoding = 0` (`kms_backend.c:331`).
- Linux's programming of the same message for SAND (`external/linux/drivers/gpu/drm/vc4/vc4_firmware_kms.c:574-650`):
  for a two-plane NV12 + `DRM_FORMAT_MOD_BROADCOM_SAND128` framebuffer it sends `vc_image_type = VC_IMAGE_YUV_UV`
  (19), `planes[0]` = luma, `planes[1]` = chroma, and **`pitch` = the modifier's parameter, the column height in
  lines, not bytes**. Values from `vc_image_types.h` (compiled and printed): `YUV420SP` 52, `YUV_UV` 19,
  `YUV_UV_16` 57, `YUV10COL` 59; colour encoding `ITUR_BT601` 1, `ITUR_BT709` 2, `JPEG_JFIF` (601 full) 3,
  `REC_2020` 9, identical to the task 44 probe table.
- Pool (task 45): one contiguous pool reserved at start, retried until it ends below 1 GiB (`kms_bo.c:55-104`),
  **32 MiB by default** for the plane backend (`kms_main.c:2169`, `-p`). A 1080p NV12_COL128 picture is
  15 columns × 128 B × 1 632 lines (H16 1 088 + chroma 544) = 3 133 440 B ≈ 3.1 MB; a decoder needs ~6-8 references + display, so
  1080p wants ~30 MB and 4K ~190 MB (another port's figure: 192 MiB holds 4K 8-bit).
- **Lifetime rule (task 45):** a picture's pool range must not be reused while any other mapping of it exists.
  Check the current free path (`kms_bo_unref()`, `kms_bo.c:353`, and `pool_find`, `:126`) against that rule before
  pictures share the pool; details of the check are tracked privately.

## 2. Plan (smallest path first)

1. **kms: accept NV12 SAND128 imports** (`kms_proto.h`, `kms_scanout.h`, `kms_backend.c`):
   - `KMS_FMT_NV12` ('N','V','1','2'), and P030-style for 10-bit later;
   - `kms_import_fb_why()`: allow `DRM_FORMAT_MOD_BROADCOM_SAND128(h)` for NV12 only, with plane 1 at
     `offset + h16 × 128` in the same buffer, column count × column bytes ≤ buffer size;
   - plane update: `vc_image_type` 19 (59 for 10-bit), `num_planes` 2, `planes[1]` = luma bus + chroma offset,
     `pitch` = column height in lines, `color_encoding` from a new per-plane property (task 44 below).
2. **Pictures below 1 GiB**: rather than a second allocator in the decoder, let the decoder ask rpi4-kms for its
   output pictures (dumb-buffer-like `KMS_CREATE` from the pool, which is already below 1 GiB and exported by
   `memExport`), and decode into them (the decoder needs only the physical address and a CPU mapping, which the
   export gives). Size the pool for pictures: `-p 64` for 1080p, 192-256 for 4K (task 45); measure free pool after
   20 runs.
3. **Enforce the lifetime rule** (item 1.3) before pictures share the pool. Same rule as task 42's buffer reuse.
4. **Colour encoding (task 44):** map `AVFrame.colorspace` / `color_range`: 601 limited → 1, 709 limited → 2,
   601 full → 3, 2020 limited → 9; 709 full and 2020 full are not available → stay on the CPU/GPU path. Another
   port measured 2020 on the plane up to 11 code values off; keep 2020 off the plane until measured here.
5. **Player:** ffplay's SDL KMSDRM path does not do planes; a small `video-plane` player (decode with
   `-hwaccel drm`/our drm_prime output, `ADDFB2` NV12+SAND128 on the imported buffer, SETPLANE an overlay, flip on
   vblank) is the shortest route, with the CPU path as the fallback when import is refused.

## 3. Verification (from the tasks)

- 720p / 1080p / Main 10: CPU % of `video-plane` against `video-play` today, frames shown = frames decoded,
  same-scene screenshot against the CPU path (HDMI capture).
- A 601-tagged HD clip: same-scene against the RGB path (no hue shift).
- 20 consecutive 1080p plays, then a 1080p play still on the plane (pool fragmentation).
- Lifetime: a test for the rule in item 1.3.

## 4. Risks

- The firmware may also want the luma buffer start aligned to the column (128 B) and the column count consistent
  with `width`; the pool's 64-byte rows (`KMS_IMPORT_ALIGN`) are not enough for SAND, align SAND BOs to 4 KiB.
- Firmware plane scaling of YUV is fine for overlays; the primary plane stays RGB (desktop), the video goes to an
  overlay (`-o` overlays, default 0 exposed: expose at least one).
- Effort: kms import + plane fields S-M; decoder output into kms BOs M; lifetime fix M; player S-M.

## 5. Players on the plane (plan, 2026-11-22)

`video-plane` proves the path but has no audio, seeking or keys. The players people use:

1. **ffplay without a desktop (`video-play`, drm mode): the next step.** SDL's KMSDRM driver owns the primary
   plane; the overlay is free. Plan:
   - move `video-plane`'s rpi4-kms dumb-buffer allocator into a small library beside `rpivid_bo_drm.c`
     (`rpivid_bo_kms.c`, `rpivid_bo_kms_install(card_fd)`), so any player can hand the decoder plane-reachable
     buffers;
   - in ffplay (our patch set), when `VIDEO_PLANE=1` and a frame is `AV_PIX_FMT_DRM_PRIME`: skip the SDL texture
     upload in `video_image_display()` and ADDFB2 + SETPLANE the picture on the overlay at the same moment ffplay
     would present it (its pacing, audio sync, pause and seek stay as they are); SDL's DRM fd is the card;
   - decode with `-rpivid_out drm_prime` (a codec private option on the command line) and hold the last three
     frames as `video-plane` does;
   - `video-play` sets both for HEVC in drm mode, and falls back to today's path when the overlay refuses a frame
     (TEST_ONLY), e.g. full-range BT.709.
   Verify: CPU % of ffplay HEVC 1080p30 against today's drm mode, A/V sync by ffplay's own stat line, seek/pause.
   ✅ Done 2026-11-22 (ports `cf07327`…`d85d135`, manifest `2026-11-22-learnings-t`): `VIDEO_PLANE=1 video-play`
   in drm mode; colour encoding from the stream (task 44).
2. **Under labwc (Wayland).** The compositor would have to put the client's NV12 dma-buf on the overlay (wlroots
   direct scan-out of a linux-dmabuf surface). SDL's Wayland driver draws with EGL and never offers its decoded
   frames as dma-bufs, so this needs a player that does (a dmabuf video sink), and wlroots' overlay use on top of
   rpi4-kms planes. Larger; after 1.

   **Plan (2026-11-22), in three steps, each useful alone:**
   - **2a. The client sends the decoder's picture as an NV12 SAND128 dma-buf; labwc composites it on the GPU.**
     What makes this possible: Mesa's V3D imports `DRM_FORMAT_MOD_BROADCOM_SAND128` NV12 as an external-only
     texture and blits it to UIF (`v3d_resource.c:1032`, `v3d_blit.c:647`, the "sand8" blit), and libdrm_phoenix
     already resolves a `/kmsbuf` dma-buf (rpivid_plane.c's buffers) for the render server. In ffplay (our patch
     set): when `VIDEO_PLANE=1` under Wayland, give the window a `wl_subsurface` of its own and attach each
     `AV_PIX_FMT_DRM_PRIME` frame there as a `zwp_linux_buffer_params_v1` buffer (fd from the frame's descriptor,
     modifier SAND128 with the column height), at ffplay's presentation time; SDL's surface keeps the OSD/black
     background. Check first that labwc advertises NV12 + SAND128 in its linux-dmabuf feedback (it lists what the
     renderer imports, external-only included). ✅ **Checked on the Pi 2026-11-22** (`tools/gpu-lane/sand-wl`, image
     Z = masters): labwc offers NV12 with `0x0700000000000004` (SAND128) among four modifiers, accepts sand-plane's
     1280x720 picture as a `zwp_linux_buffer_params_v1` buffer (one rpi4-kms dumb buffer, two planes, modifier
     `SAND128_COL_HEIGHT(1080)`) and composites it correctly: eight chroma bars, luma ramp, no column tearing
     (HDMI `20261122-165314-sandwl-tick.png`), 30 s, no protocol error. So 2a needs no compositor change.
     **First player version (2026-11-22, NOT landed):** ports branch `video-wl-subsurface` `3bd562f` (on the
     publish fork only): ffplay patch 0003 + `files/rpivid/wl/rpivid_wl.c` (desync subsurface of SDL's window,
     one linux-dmabuf `wl_buffer` per decoder buffer, `wp_viewporter` scaling, a picture held until its
     `wl_buffer.release`); `video-play` passes the rpivid options in wl mode. Gate AA (HEVC 720p30 windowed under
     labwc): the picture is right (HDMI `20261122-172731-learnAA-vwl-tick.png`), no errors, but **16.3 fps with
     ~14 late drops/s, against 30.0 fps and 1 drop for today's CPU path** in the same session (`…-172801-…`).
     The decoder is not the limit (2.5 ms per picture). Suspects, unmeasured: labwc converts the SAND buffer to
     UIF on the GPU every frame (Mesa's sand8 blit, `v3d_blit.c:647`) besides compositing; SDL's present on the
     parent waits for a frame callback that the extra composition delays. Next, if resumed: time labwc's frame
     (wlroots `WLR_RENDER` / the render server's per-job times) with and without the subsurface, then decide
     between 2b (scan-out, no GPU work) and a cheaper import.
     Gain: no CPU NV12→RGB conversion or texture upload (today's
     windowed path); the GPU blit remains. Verify: ffplay CPU % HEVC 1080p30 windowed vs today; colours against
     the drm-mode overlay (same scene, task 44's clips); C18-style session x12.
   - **2b. Fullscreen: direct scan-out.** wlroots' scene already scans a single fullscreen buffer out on the primary
     plane when the output accepts the buffer's format/modifier (check `wlr_scene` 0.20's conditions: buffer
     size vs output size, src/dst boxes). Needs rpi4-kms's primary plane to accept NV12 SAND128 (the firmware
     plane takes it on the overlay already; same SET_PLANE call) and to say so in its IN_FORMATS. Gain: no GPU
     work at all in fullscreen (`f` in ffplay).
   - **2c. Windowed on the overlay.** wlroots 0.20 places surfaces on extra planes only through output layers
     (`wlr_output_layer`, DRM backend with libliftoff; our build has `-Dlibliftoff=disabled`) and labwc would
     have to use them. Port libliftoff (MIT, small), enable it, and add output-layer use for one video
     subsurface in labwc. Largest of the three; only if 2a's GPU blit costs real frame rate.
3. **The browser (WPE).** The usual embedded route is a "hole punch": the page leaves a transparent rectangle and
   the media backend puts the video on a plane beneath (backlog task 46: colour key, no redraw for video-only
   changes). After 1 and 2.

   **Plan (2026-11-22):** WPE plays media through our FFmpeg player (webkit_wpe patch 0030; no GStreamer), so the
   work is in that player, not in a GStreamer sink.
   - **3a. HEVC through rpivid in the WebKit player** (decoder `hevc_rpivid`, software output first): the CPU win
     of hardware decode before any plane work; the player's frame path stays as it is.
   - **3b. Hole punch.** WebKit's `MediaPlayerPrivateHolePunch` draws the video rectangle transparent; the player
     puts `drm_prime` frames on the overlay *below* the primary (needs rpi4-kms zpos below primary, and the
     primary's alpha to reach the HVS: labwc must keep the window's transparent pixels transparent on an ARGB
     primary). The client does not know its window's screen position under Wayland, so start with fullscreen
     video (position 0,0) and only then a position protocol or the compositor-side route of 2c.
   - Verify each: CPU % and dropped frames of a 1080p HEVC `<video>` (the B8 page), colours, seek/pause, a
     scroll while playing (the hole must track the page).

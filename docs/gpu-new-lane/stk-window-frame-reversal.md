# Windowed SuperTuxKart shows older frames after newer ones

**Reported by the owner, 2026-09-30,** from the showcase reel: in windowed SuperTuxKart (on XFCE,
under labwc), the race "steps forward, then back a frame or two, then forward again". Fullscreen
STK and every Quake, windowed or fullscreen, look smooth.

## 1. Measurement: it is a frame-order defect, and it is specific to windowed STK

**The race timer, read by OCR.** The clip is `artifacts/hdmi-video/20260930-043504-xfce-stk.mp4`
(30 fps capture). The timer is at the top right of the STK window. Glyphs were segmented and
clustered; the clusters are the digits 0–9 plus a few blended glyphs, which count as unreadable.

60 s of racing, 240–300 s into the clip:

| capture frames | readable | forward | same | **backward** |
|---|---|---|---|---|
| 1800 | 1513 | 984 | 76 | **447** |

The pattern is always A, B, A. A newer frame is followed by the one before it, then by a new
frame:

    00:20.092  00:20.142  00:20.092  00:20.183  00:20.233  00:20.275  00:20.233  00:20.325 ...

The old frame comes back **whole**: only 5 of 1800 timers were unreadable blends. So it is a
complete older buffer shown again, not tearing.

**The same test without a timer.** `scripts/count-frame-reversals.py` counts capture frames that
are much closer to frame k−2 than to frame k−1 while the picture moves. It agrees with the OCR:
355 reversals against the OCR's 447. It needs no on-screen clock, so it can be run on every other
clip of the reel:

| clip (reel segment) | moving frames | reversals |
|---|---|---|
| windowed STK (`xfce-stk` 250–275 s) | 717 | **183 (25.5 %)** |
| windowed Quake III (`xfce-apps` 228–253 s, `--move 0.3`) | 576 | 0 |
| video player window (`xfce-apps` 288–318 s) | 358 | 0 |
| fullscreen STK (`fs-stk` 180–205 s) | 523 | 0 |
| fullscreen Quake II / Quakespasm / vkQuake | 620 / 604 / 579 | 0 / 0 / 0 |

**Not a regression.** Windowed games are new this week (M8), and this is the only windowed STK
recording.

## 2. What is known about the path

- **One swap per frame.** STK swaps once per frame: `supertuxkart_drm/glue/stkdrm_hooks.c` wraps
  `SDL_GL_SwapWindow` and only counts calls. The compositor followed the game: 1340 STK swaps,
  1379 labwc KMS flips (`flipstat client=1`). So labwc is not re-composing at 60 Hz between STK
  frames.
- **STK's swap interval.** It asks for adaptive vsync (`SDL_GL_SetSwapInterval(-1)`) only when
  its `swap-interval` setting is above 0. The default is 0 (`user_config.hpp:970`), so it runs
  **unthrottled**.
- **GPU job ordering.** The render server runs with `-m serial -r 1`: one hardware job at a time,
  in submission order. G6 cross-process implicit sync passed on the Pi (`g6-sync2`), so a
  compositor job that samples a client buffer waits for the client's pending write.
- **How windowed STK differs from windowed Quake III:**
  - frame rate: ~20 fps against 56–74 fps, so labwc flips about once per client frame, not on
    every vblank;
  - GPU cost per frame: ~45 ms against ~14 ms;
  - STK is unthrottled at swap interval 0.

## 3. Experiments (pre-registered)

**W1 — the same compositor path at STK's frame rate.** One XFCE session runs windowed Quakespasm
capped at 20 fps (`host_maxfps 20` in the export's `autoexec.cfg`, cycle `--skip-server-up`),
then windowed STK as the positive control. Clip `w1-qs20-stk`; both graded with
`count-frame-reversals.py`.
- Quakespasm above 5 %: the compositor or display path breaks at low frame rates. That points
  at labwc/wlroots output buffers, rpi4-kms flips or buffer age.
- Quakespasm at about 0 % while STK still reverses: the defect is on STK's client side. That
  points at an unthrottled swap-interval-0 client, Mesa's EGL Wayland buffer reuse or release
  timing, or STK's heavy frame.
- STK at 0 %: the defect did not reproduce on the release image; rerun before concluding.

### W1 result (2026-09-30 09:25, release build 1, clip `20260930-072520-w1-qs20-stk.mp4`): the STK client side

| segment | moving frames | reversals |
|---|---|---|
| Quakespasm windowed, capped at 20 fps (205–285 s) | 1461 | **1 (0.1 %)** |
| SuperTuxKart windowed, same session (385–445 s) | 1403 | **343 (24.4 %)** |

At STK's frame rate, the same compositor, KMS path and window size stay in order for Quakespasm.
STK still reverses on the release image. So the defect is in how STK presents, not in
labwc/wlroots/rpi4-kms at low frame rates.

**W2 — STK with swap interval 1.** Same session with `GAME_LIST=stk-race:150`. `SUPERTUXKART_SAVEDIR=/root/stkw2`
points STK at a hand-staged `config-0.10/config.xml` with `<GFX swap-interval="1" />`. The launcher
does not overwrite an already-set SAVEDIR, and STK first tries adaptive vsync (-1), then 1. The
`stk-drm: first swap … swap_interval N` line shows what took effect.
- Reversals near 0: an unthrottled (interval 0) client on Mesa's EGL Wayland platform reuses or
  presents a buffer the compositor still shows. The fix goes in the throttled path, or in the
  interval-0 buffer handling.
- Reversals unchanged: the cause is STK's own frame (its render targets or GPU cost), not throttling.

### W2 result (09:35, clip `20260930-073527-w2-stk-swap1.mp4`): not throttling

STK took the config: `stk-drm: first swap … swap_interval -1`, meaning adaptive vsync was accepted. Reversals:
**213 of 1014 moving frames (21.0 %)**, against 24–26 % at interval 0. The swap count barely moved
(1316 against 1340 in 139 s): at about 20 fps, vsync hardly throttles anyway. **Throttling does not
cause it.**

What is left: fullscreen STK renders the same 1280×720 frame through GBM and is clean. Windowed
Quake is clean through the same Wayland EGL path. A presented buffer that holds the frame from
two swaps ago is exactly an **undrawn back buffer**. So the leading hypothesis is that STK's
final pass to the default framebuffer sometimes does not land in the buffer that Mesa's Wayland
platform then presents. Examples: a draw against a stale back-buffer validation, or a
discard/invalidate hint that drops the pass. Code reading is under way.

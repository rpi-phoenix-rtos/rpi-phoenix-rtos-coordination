# GPU regressions (owner-reported, 2026-08-22 manual-test session)

Owner uses the **HDMI grabber live-preview as the monitor** (no separate display), so a
broken HDMI/scanout output = the owner cannot see the app at all. Three distinct
regressions, all GPU-related, believed introduced by recent big GPU work (Mesa 26.2.0
rebase, SDL2 migration of all Quake ports, v3d-server multiprocess GPU, tools→ports).
Confirmed visually from today/yesterday grabs. vkQuake (Vulkan/V3DV) is the control: its
render path is clean, so this is NOT a general GPU break.

## Issue 1 — GL-Quake HDMI/scanout output is SCANLINE-GARBAGE (BLOCKER) — ✅ FIXED + HW-VERIFIED 2026-08-22
VERIFIED: quakespasm-sdl relinked against the fixed libv3d, netbooted 1920x1080 fullscreen, +map start —
HDMI grab 20260822-193816-qs-sdl-v3d-tilefix-tick.png shows a CLEAN coherent GLQuake frame (textured
walls, armor pickup, candle flame, viewmodel, HUD), zero scanline shred, 0 GPU faults. Committed:
coord 571d57f (pushed publish), external/mesa 34a448d6a29 (local — mesa clone has no publish remote).
Remaining seam-tearing on moving demo = benign inter-frame page-flip tearing (separate, acceptable).

Affects all GL-based games (quakespasm Q1, quake2, quake3 — all now on the SDL2 port).
Was clean in the past (older GL grabs correct). vkQuake (own present path) is clean.
- Evidence: artifacts/hdmi/20260822-183358-q2fix-tick.png (dense horizontal scanline garbage).
- Reframe: the RENDER is fine (frame-dump SSIM 0.993 reads the render FBO via phxgl_capture's
  glReadPixels) — the regression is in the **scanout→/dev/fb0 present** path, which the
  frame-dump bypasses. So SSIM masked it.
- Mechanism split (the one hard difference): GL = TRIPLE-BUFFER **page-flip** (winsys
  v3d_phoenix_flip → v3d_phoenix_fb_flip pans display by buf*phys_h; scanout_init picks nbuf
  from the plo/driver-allocated 3x virtual fb height, NOT config.txt which is 1x). vkQuake =
  single-buffer render-to-scanout (clean). Scanline *shearing* smells like a pitch/pan-offset
  mismatch or presenting a mid-render buffer (missing fence/vsync).
- Memory caveat: single-buffering historically caused FLICKER (fixed via double-buffer through
  /dev/vcmbox — [[project_pi4_quake_flicker_vcmbox]]). So forcing single-buffer is a diagnostic
  + maybe-flickery stopgap, NOT the real fix. The real fix is in the page-flip/pan (or the
  render→scanout buffer coherency) — find what recent change broke it.
- **ROOT CAUSE CONFIRMED (2026-08-22, by code inspection — not the pan/flip):** the scanout RT is
  UIF-TILED, and the HVS display can only scan a LINEAR surface → it reads tiled memory as linear →
  horizontal shred. Proof chain:
  1. `external/mesa/src/mesa/main/renderbuffer.c:276` adds `PIPE_BIND_SAMPLER_VIEW` to EVERY
     renderbuffer unconditionally. The SDL2 scanout FBO color attachment is a user renderbuffer
     (`sdl_phoenix_glctx.c` glRenderbufferStorage), so its bind = RENDER_TARGET | SAMPLER_VIEW.
  2. `v3d_resource.c` force-RASTER gate (added by commit `4363822955b`, the q3dm7 lightmap fix)
     excluded `SAMPLER_VIEW` to keep the sampled lightmap atlas tiled — but that exclusion ALSO
     catches the scanout renderbuffer → `should_tile` stays true → scanout RT tiled → shred.
  - The q3 grab (20260822-184037-q3verify-tick.png) shows a coherent dark-red Quake3 frame sheared
    into scanlines (content present, horizontally shredded) = tiled-read-as-linear signature.
  - Explains ALL controls: frame-dump SSIM 0.993 (GPU-blit readback is tiling-aware → correct),
    vkQuake clean (V3DV uses src/broadcom/vulkan, not gallium v3d_resource.c), X11 GL app clean
    (DRAM FBO + glReadPixels, never scanned out by HVS). Regression window = when `4363822955b`
    landed. NOT the page-flip/pan (all pa/pitch/nbuf/virt_h values verified consistent; no wedge).
- **FIX (implemented, pending HW grab):** distinguish the scanout RT (must be RASTER) from a sampled
  atlas (must stay tiled) via the winsys `next_scanout` one-shot, which is set immediately before the
  scanout renderbuffer's `glRenderbufferStorage`. Added `v3d_phoenix_peek_next_scanout()` to the
  winsys; the gate now forces RASTER when `peek_next_scanout() || !(bind & SAMPLER_VIEW)`. Sampled
  textures never set next_scanout → stay tiled → q3dm7 fix preserved. Files: tools/v3d-driver-port/
  v3d_phoenix_winsys.c + external/mesa/.../v3d_resource.c. Rebuild libv3d + relink a GL game + grab.

## Issue 2 — vkQuake: the 2 start-map torches are MISSING (resurfaced #67/torch bug)
vkQuake render otherwise clean (Vulkan), but the flaming torches flanking the "QUAKE" archway
are gone; scene darker. This is the long-fought #67 torch/alpha bug resurfaced.
- Evidence: artifacts/hdmi/20260822-143739-vkq-semafix-final.png (no torches at the archway).
- The SLCACTL #67 ordering fix IS still present in the winsys (v3d_phoenix_winsys.c:939) — so
  that specific fix was NOT removed. #67 had MULTIPLE root causes historically (SLCACTL timing,
  VBO-crossing-4KB-page, single-buffer/vcmbox) with a history of false "fixed" claims — identify
  WHICH mechanism regressed. Advisor flag: 457a650 is a gallium-GL fix; vkQuake is V3DV — confirm
  the torch render path (alpha-tested/additive sprite/dlight) on V3DV and what the Mesa rebase or
  616f114 (CSD cache-flush) changed there. Do NOT blind-reapply. Lower priority (render mostly OK).

## Issue 3 — X11 windowed GL app: content Y-OFFSET inside its frame
The "Phoenix V3D GL" window renders the spinning-triangles demo at correct SIZE but shifted
DOWN inside the frame (big black band at top; content in lower ~2/3). Border/title correct,
content position wrong.
- Evidence: artifacts/hdmi/20260822-073928-m3c-gpudesk-tick.png (+ subsequent m3c-gpudesk).
- Likely the same present-path coordinate/origin family as the glamor O1 vertical-flip already
  fixed (PHX_READBACK_FLIP_Y). Suspect the m3c gl-x11-window present (glReadPixels→XPutImage dst-y,
  GL bottom-left vs X top-left origin). Lowest urgency (niche windowed-GL demo, doesn't block viewing).

## Status
Confirmed + triaged 2026-08-22. All uncommitted-investigation. Fixes are multi-turn GPU work;
priority order 1 → 3. Advisor-guided plan: bisect via grab-archive + git brackets, don't
re-derive by code-read; lead with the Issue-1 single-buffer diagnostic (stopgap + culprit-confirm).

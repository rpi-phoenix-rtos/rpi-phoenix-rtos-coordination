# GPU regressions (owner-reported, 2026-08-22 manual-test session)

Owner uses the **HDMI grabber live-preview as the monitor** (no separate display), so a
broken HDMI/scanout output = the owner cannot see the app at all. Three distinct
regressions, all GPU-related, believed introduced by recent big GPU work (Mesa 26.2.0
rebase, SDL2 migration of all Quake ports, v3d-server multiprocess GPU, tools→ports).
Confirmed visually from today/yesterday grabs. vkQuake (Vulkan/V3DV) is the control: its
render path is clean, so this is NOT a general GPU break.

## Issue 1 — GL-Quake HDMI/scanout output is SCANLINE-GARBAGE (BLOCKER)
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
- NEXT: (a) diagnostic — force nbuf=1 (winsys scanout_init) + re-grab: clean(-ish) confirms the
  page-flip present is the culprit. (b) bisect: newest-clean vs oldest-broken GL grab → git
  brackets across sdl2 port (glue/, overlay/src/video/phoenix/ — SwapWindow/present), winsys
  (v3d_phoenix_flip/fb_flip/scanout), and v3d_phoenix_power fb pan. SDL2 migration ~Aug 11
  (bc5e7ae de-Quake, c1494fc glBindFramebuffer→scanout FBO, 94ee607 video fixes) is a prime
  suspect but CONFIRM via the bracket. Priority #1 (owner blocked on all GL games).

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

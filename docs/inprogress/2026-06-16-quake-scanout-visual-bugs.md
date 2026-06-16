# Quake render-to-scanout visual bugs — diagnosis (2026-06-16)

After render-to-scanout landed (~42fps@1080p, coord 2d9e246), two visual bugs
appeared on the HDMI output. Both are DIAGNOSED here; neither is fixed yet. The
build is reverted to the stable 42fps state (RGBA8 renderbuffer color RT).

## Bug 1 — "everything is very blue" (red/blue channel swap)

**Symptom:** brown Quake stone renders teal/cyan; orange particles render blue.
Quantitatively (HDMI grab, brightest 64px region): a stone wall is RGB ≈
(52,89,108) → B>R, i.e. a clean R↔B swap.

**Root cause (precise):** the swap is in the SCANOUT (display) path only, NOT the
render. Proof: the visual-regression harness captures via glReadPixels (which
converts to logical RGBA) and matched the host at SSIM 0.958 — so the render is
logically correct (brown). The V3D's native 32-bit store order is BGRA (byte0=B),
but the firmware framebuffer is RGB-order (byte0=R, the order the old
glReadPixels→/dev/fb0 CPU-present path matched). With render-to-scanout the GPU
writes the RT's native bytes straight to the fb → byte0=B shown as red → teal.

**Confirmed dead ends:**
- plo `tag_setpxlordr` 1(RGB)→0(BGR) (sources/plo/.../generic/video.c): NO effect
  on the displayed colors (still teal, quantitatively). The firmware ignores the
  pixel-order tag under the `vc4-fkms-v3d` overlay — the fb order is fixed RGB.
- Making the FBO color a **B8G8R8A8 texture** (pl_phoenix_glctx.c, smoke-test
  st_context_teximage pattern) — the principled fix (Mesa maps B8G8R8A8 to the V3D
  RGBA8 HW format with swizzle ZYXW + `swap_color_rb`, storing byte0=R, keeping
  glReadPixels correct): **it STALLED the GPU** (bin never starts: ct0ca=0,
  int_sts=0; finish=2514ms timeout). should_tile correctly forces the RT raster
  (linear) and scanout still engaged ("RT scanout PA ... 2025/2026 pages"), so the
  stall is something else in the texture/SAMPLER_VIEW resource path — UNRESOLVED.

**Fix options (next):**
1. Re-investigate the B8G8R8A8-texture stall (the clean fix). Suspects: the
   SAMPLER_VIEW bind, st_context_teximage wrapping, or a format/stride mismatch in
   the scanout-backed BO. Add per-RT format/pitch/size logging in the winsys
   SCANOUT hook; compare the renderbuffer (works) vs texture (stalls) BO geometry.
2. Force `v3d->swap_color_rb` for the scanout RT (keep the stable RGBA8
   renderbuffer): mark `rsc->scanout` in v3d_resource.c, set
   `v3d->swap_color_rb |= 1<<i` in v3dx_state.c set_framebuffer_state for that cbuf.
   Lower GPU risk (renderbuffer is proven stable). CAVEAT: needs verifying whether
   it also keeps glReadPixels correct (the harness capture); if not, compensate R↔B
   in scripts/quake-visual-compare.py.

The render is correct, so this is display-only and does not affect the fps result
or the harness's render-correctness verdict.

## Bug 2 — monocolor square around explosions (particle alpha)

**Symptom:** a solid lavender square around explosion/particle effects
(artifacts/hdmi/20260616-185823-perf-scanout-tick.png), with the bright flash in
the middle. Appears with `r_particles` on (default); the harness forces
`r_particles 0` so it does NOT show there.

**Root cause (diagnosed from code):** `gl_flashblend` defaults to 0, so it is NOT
dlight coronas. It is the **particle quads** (r_part.c R_DrawParticles, GL_QUADS).
Each particle is a textured quad using `particletexture` (a 64×64 soft radial dot,
`SRC_RGBA` + `TEXPREF_ALPHA` — bright center, transparent edges). The vertex alpha
is `color[3]=255` (opaque); the soft-glow shape comes entirely from the TEXTURE's
alpha channel under alpha blending. R_DrawParticles does `glEnable(GL_BLEND)` +
`GL_MODULATE` but sets NO `glBlendFunc` (it inherits the last-set func). So the
square = the particle texture's transparent edges rendering OPAQUE → the full quad
shows as a solid block. Cause is one of: (a) the V3D isn't sampling the particle
texture's alpha (returns alpha=1), or (b) the inherited blend func at particle-draw
time isn't `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`.

**Fix path (next):** capture an `r_particles 1` frame and a code trace of the GL
blend-func state when R_DrawParticles runs (what was the last glBlendFunc?); verify
the particle texture's alpha channel reaches the V3D sampler descriptor (relates to
the prior "V3D sampler atom" investigations). Likely a small state/sampler fix.

## Validation harness (user ask — still to run on the scanout build)

The harness (scripts/quake-{host-capture.sh,capture-sink.py,visual-compare.py},
.venv-quakecmp; doc 2026-06-15-quake-visual-regression-harness.md) captures via
glReadPixels = the RENDER, so it will (a) confirm render-to-scanout did not regress
the render, (b) NOT show Bug 1 (scanout-only) or Bug 2 (r_particles 0). Re-run it
on the current build to re-establish the SSIM/blacktex baseline; treat Bugs 1 and 2
as separate display/particle items above.

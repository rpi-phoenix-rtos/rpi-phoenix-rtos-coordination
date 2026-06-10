# GLQuake path decision — the shader fork (2026-06-11)

## Where we are

The V3D 4.2 GPU is fully bring-up'd and the **render pipeline works**: powered on → MMU →
control-list execution → binner → **render-target clear with verified pixel output** (4096/4096
px, 2/2 netboots; devices `22eb868`). The next step toward a triangle — and ultimately GLQuake —
is **shaders** (coordinate + vertex + fragment QPU programs). That is the fork.

## The problem with the obvious next move (blind hand-encoding)

Hand-encoding V3D-4.2 QPU shaders directly in the scout is a trap for two reasons:
1. **No off-device oracle** — wrong shaders fault/hang on HW with little visibility; each blind
   build→boot is ~6 min. Likely many non-converging iterations.
2. **It doesn't reach GLQuake.** Quake's rendering needs *many* shaders; you cannot hand-encode
   them. GLQuake fundamentally needs a shader *compiler*. A hand-encoded flat triangle is, at
   best, a throwaway demo — not a step toward the goal.

The render-clear breakthrough came from **faithful-Mesa + a validation oracle**. The shader
equivalent is: get shaders **from Mesa**, **validated off the Pi**.

## Probe result: off-device Mesa is VIABLE on this host

- `meson`, `ninja`, `gcc`, `python3` all present.
- `external/mesa` has the full tree: the **v3d NIR→QPU compiler** (`src/broadcom/compiler`), a
  **v3d simulator** (`src/broadcom/simulator/` — executes CLs + shaders on the host CPU), and
  `qpu_validate` (instruction-sequence checker). So we can compile + simulate + validate shaders
  and control lists entirely off-device, then embed validated bytecode.

## The fork (your call)

**A. Host-Mesa-compiled shaders + off-device validation → the GPU triangle (then GLQuake via Mesa).**
   Build Mesa's v3d driver+simulator on this Linux host; run a trivial GL clear/triangle under the
   simulator with `V3D_DEBUG=cl,qpu` to dump the *exact* control lists + shader bytecode Mesa
   generates; embed/validate those in the scout → a real, debuggable triangle on HW.
   - Cost: a Mesa host build (meson; moderate — may need dev packages) + harnessing.
   - Odds: high (correct-by-construction, validatable).
   - GLQuake: this is the on-ramp. The real long-term GLQuake question it surfaces — **how do
     Quake's shaders get compiled?** → either *port Mesa's v3d driver+compiler to run on Phoenix*
     (big, but THE "full GPU based on Mesa" path) or ship a *pre-compiled shader set*.
   - This is the "reuse Mesa" directive applied properly.

**B. Hand-encode shaders blind on-device.** Not recommended: slow, no oracle, and a dead-end for
   GLQuake (doesn't scale past a flat triangle). Only value is a one-off demo.

**C. Software Quakespasm on `/dev/fb0` (the roadmap's ship-first path, GPU-free).**
   We already have `/dev/fb0` (the framebuffer device landed, #148). Quakespasm's software
   renderer draws to the framebuffer with no GPU at all — likely puts **Quake on the HDMI screen
   far sooner** than any GPU path. Caveat: this is *software* Quake, not *GL*Quake — it doesn't use
   the V3D we just brought up. But it achieves "Quake running on the Pi," and was explicitly the
   de-risking ship line in the roadmap (Phase 3 Tier 2).

## Recommendation

- For the **GPU/GLQuake** goal specifically: **Path A** — build host Mesa, derive validated
  shaders/CLs from the simulator, land the triangle, then decide the on-device-compiler vs
  pre-compiled-shaders question for GLQuake. This is the only GPU path that actually reaches the
  goal and it reuses Mesa as directed.
- Consider **Path C in parallel** as the fast "Quake on screen" win (software), independent of the
  GPU work — it de-risks "is the rest of the Quake port (assets, input, sound, main loop) working"
  while the GPU/GL path matures.
- **Path B is not recommended.**

Autonomous loops are paused here for this decision — it changes the next several weeks of work and
shouldn't be made by grinding blind shader iterations.

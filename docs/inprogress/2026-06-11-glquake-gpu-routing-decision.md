# GLQuake GPU path — routing decision (2026-06-11)

## Honest status (what's proven vs unverified)

**Proven + durable (this session):**
- BCM2711 V3D 4.2 **powered on** from Phoenix userspace (PM + rpivid_asb).
- **Render pipeline works**: BO alloc + V3D MMU + control-list executor + binner +
  **render-target clear with verified pixel output** (4096/4096 px, 2/2 netboots).
- **Mesa v3d shader COMPILATION works on the host**: built Mesa's v3d compiler, drive
  `v3d_compile()` to emit V3D-4.2 QPU bytecode for FS + VS + coordinate shader.

**Unverified / the wall:**
- Shader **correctness**. The hand-built-NIR FS disassembles to `ldtlb` + `mov tlb, 0`
  (load-then-write-zero) — concrete evidence it's *incorrect*, not just uncolored; the
  VS is a near-empty `vpmwt`. Replicating partial driver NIR-finalization
  (gather_info/nir_lower_fragcolor/nir_lower_io) did not fix it.
- **No off-device validation.** The v3d *simulator* needs Broadcom's proprietary
  `libv3dv3`/`v3d_hw` (absent here); `qpu_disasm` checks encoding, not behaviour. So
  there is no oracle short of real HW.
- The **GL-state assembly** (the ~30-field `GL_SHADER_STATE_RECORD` + VPM/attribute
  config + draw packets) is what Mesa's driver computes from `prog_data`; hand-building
  it is effectively re-implementing the v3d gallium driver, unverified.

This is a **routing decision for the next leg**, not "the GPU work is stuck." The hard
hardware + compile milestones are banked.

## Why "just assemble the triangle on HW and iterate" is NOT the cheap oracle it was

The render-clear breakthrough worked because it had ~3 unknowns and good register
introspection. A triangle stacks **shader-I/O correctness × shader-state-record × VPM/
attribute config × draw packets** — all unverified simultaneously, debugged through
~6-minute fault/hang cycles with almost no introspection. And the FS is already known
wrong. That's a blind multi-unknown grind, and it does **not** reach GLQuake regardless
(Quake needs a shader *compiler* generating CLs at runtime — can't be hand-assembled).

## The fork (your call — none of these should be chosen by grinding autonomously)

- **A. Empirical HW hand-assembly.** Hand-build the triangle, iterate on HW. Now known
  low-yield: shaders are wrong, ~4 stacked unknowns, blind 6-min cycles, dead-ends for
  GLQuake. Yields at best a one-off demo triangle. Not recommended.
- **B. Get the v3d simulator working** (obtain/build `libv3dv3`/`v3d_hw`). Then Mesa's
  real driver generates *and validates* complete CLs + shaders off-device → we embed
  known-correct output. Blocked on a proprietary Broadcom lib (acquire it?).
- **C. Port Mesa's v3d gallium driver to run on Phoenix.** The genuine "full GPU based
  on Mesa" path and the real route to GLQuake: the actual driver generates correct CLs +
  shaders for GL calls at runtime, validated on real HW. Large (gallium state tracker +
  a GL/EGL frontend + a Phoenix winsys/DRM-shim + BO/submit on our kernel). Weeks of
  work, but it's THE path and reuses Mesa fully.
- **D. Software Quakespasm on `/dev/fb0`.** GPU-free; puts *Quake* on the HDMI screen
  fastest (we already have `/dev/fb0`, #148). Not GL-accelerated, so it doesn't use the
  V3D we brought up — but it achieves "Quake running on the Pi4" and de-risks the rest
  of the port (assets/input/sound/main loop). Was the roadmap's ship-first line.

## Recommendation

For the stated goal ("full GPU based on Mesa" → GLQuake), the honest path is **C**
(port the driver) — large but the only one that reaches GLQuake and validates on HW;
**B** (simulator) is the lighter enabler if `libv3dv3` can be obtained. **D** is the fast
tangible "Quake on screen" win in parallel. **A** is not recommended (dead-end demo).

Autonomous loop paused here for this decision.

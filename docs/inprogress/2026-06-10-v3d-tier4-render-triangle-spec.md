# V3D Tier-4: render pipeline → clear → triangle — implementation spec

**Status:** SPEC / not-yet-implemented. The analog of the V3D power-on draft that made
power-on first-try-correct. Foundation already DONE + HW-proven (devices `7281468`): BO
alloc, V3D MMU, and the CLE executing a control list from MMU-mapped memory. This spec
takes that to a visible triangle on HDMI. All packet/register facts verified against the
user's local clones `external/linux/drivers/gpu/drm/v3d/*` and
`external/mesa/src/{broadcom,gallium/drivers/v3d}/*` (file:line refs throughout).

## Decisive finding: a clear needs a BIN pass (resolved before any code)

The render control list (RCL) does **not** stand alone. mesa's `v3dx_rcl.c`:
- per-tile work is emitted via `TILE_COORDINATES_IMPLICIT` (code 124) +
  `BRANCH_TO_IMPLICIT_TILE_LIST` (code 21) — *implicit* lists produced by the binner
  (`v3dx_rcl.c:357,373`);
- the supertile walker uses `MULTICORE_RENDERING_TILE_LIST_SET_BASE` pointing at
  `job->tile_alloc` — **the binner's tile-allocation memory** (`v3dx_rcl.c:576-577`);
- `MULTICORE_RENDERING_SUPERTILE_CFG` needs the frame tile grid set up by the bin pass.

So even a no-geometry clear requires a minimal bin pass first: `TILE_BINNING_MODE_CFG` +
`START_TILE_BINNING` + `FLUSH`, with the binner's `tile_alloc`/`tile_state` BOs supplied
via the CT0 queue registers. **Decomposition follows from this.**

## Verifiable decomposition (each is a commit+manifest stop point)

### 4b-1 — minimal BIN pass → readback  [smallest next coded step]
Goal: the binner runs over an empty frame and initialises tile state in *our* memory.
- BOs (uncached `mmap(CONTIGUOUS)`+`va2pa`, as in `v3d_boAlloc`): `tile_alloc`, `tile_state`,
  and the bin CL. Sizes: port `v3d_tile_alloc_sizes()` (`mesa src/broadcom/common`, called
  from `v3d_job.c:558`); for a tiny frame a few KB each suffices — over-allocate generously
  first, shrink later.
- Bin CL packets (CT0 / `cl="B"`): `TILE_BINNING_MODE_CFG` (code 120, max_ver 42 variant) with
  `log2_tile_width/height`, `number_of_render_targets=1`, `tile_allocation_{initial,overflow}_block_size`
  enums (`v3dx_draw.c:71-100`) → `START_TILE_BINNING` (code 6) → `FLUSH` (code 4).
- Submit (CORE0, all GPU VAs through the MMU; offsets verified in `v3d_regs.h`):
  `CT0QMA`(0x170)=tile_alloc VA, `CT0QMS`(0x174)=tile_alloc size, `CT0QTS`(0x15c)=tile_state VA
  with `ENABLE`=BIT1, then `CT0QBA`(0x160)=bin CL start, `CT0QEA`(0x168)=end
  (`v3d_job.c:702-706`, `v3d_sched.c` bin_job_run).
- Oracle: `CT0CA` reaches end (bin CL consumed) + `tile_state`/`tile_alloc` BO no longer all-zero
  after the run (binner wrote tile state) + MMU_HIT incremented, no abort. Read the BO back.

### 4b-2 — render CL clear-to-color → readback  (needs 4b-1's tile_alloc/state)
Goal: GPU clears a small render-target BO to a known color and stores it; read it back.
- Render-target BO: a small RGBA8 image (e.g. 64×64, the V3D tile size). Map at a GPU VA.
- Render CL packets (CT1 / `cl="R"`), order per `v3dx_rcl.c:715-944`:
  `TILE_RENDERING_MODE_CFG_COMMON` (code 121, **must be first**; frame W/H, RT count) →
  `TILE_RENDERING_MODE_CFG_COLOR`/`_RENDER_TARGET_PART1` (RT format=RGBA8, base addr) →
  `TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1/2` (the clear color, code 121 variants
  `v3dx_rcl.c:834-855`) → `MULTICORE_RENDERING_TILE_LIST_SET_BASE`=tile_alloc →
  `MULTICORE_RENDERING_SUPERTILE_CFG` (frame tiles, `number_of_bin_tile_lists=1`) →
  the generic per-tile list = `TILE_COORDINATES_IMPLICIT` + `CLEAR_TILE_BUFFERS` (code 25,
  `Clear all Render Targets=1`) + `STORE_TILE_BUFFER_GENERAL` (code 29, RT→BO, set buffer-to-store
  + memory format raster) + `BRANCH_TO_IMPLICIT_TILE_LIST` + `END_OF_TILE_MARKER` (code 27) →
  loop `SUPERTILE_COORDINATES` (code 23) over the supertile grid → `END_OF_RENDERING` (code 13).
- Submit: `CT1QBA`(0x164)/`CT1QEA`(0x16c) = render CL start/end (CORE0). Flush V3D L2T cache (`L2TCACTL`,
  `v3d_gem.c:170-230`) between bin and render so the renderer sees fresh tile state.
- Oracle: read the RT BO back on the ARM side — all pixels == clear color. Fully verifiable
  without HDMI.

### 4b-3 — the triangle  (clear + geometry + shaders)
Add to the bin CL: a `GL_SHADER_STATE` (code 64) pointing at a shader-state record, a vertex
attribute array (one BO with 3 vertices: x,y,z,w + a color), and a draw
(`VERTEX_ARRAY_PRIMS` code 36, mode=triangles, count=3). Needs:
- **Shader-state record** (the GL shader state record struct — see `v3d_packet.xml` "GL Shader
  State Record" + `v3dx_draw.c`/`v3dx_emit.c`): pointers to the coordinate shader, vertex shader,
  fragment shader, their uniforms, and the attribute/VPM setup.
- **Hand-encoded V3D-4.x QPU shaders** (the hard part):
  - coordinate shader: pass clip coords to the VPM (minimal: read attribute → write VPM).
  - vertex shader: emit position + a varying (color).
  - fragment shader: output the (interpolated) color to the TLB (`tlbu`/`tlb` write).
  - Encode via `external/mesa/src/broadcom/qpu/` (`qpu_instr.[ch]`, `qpu_pack.c`) — either link a
    tiny host encoder to emit the u64 instruction words, or hand-assemble from the ISA in
    `qpu_instr.h` + the disassembler as an oracle. Start from the shaders the v3d compiler emits
    for `gl_FragColor = const` (dump via `V3D_DEBUG=qpu` in a host mesa build if available).
- Render to a 64×64 (or full-screen) RT BO, then either blit to the firmware framebuffer
  (`rpi4-fb` pa, `config.txt gpu_mem`) or render directly into the FB BO so HDMI shows it.
- Oracle: auto HDMI snapshot shows the triangle (or RT BO readback shows non-clear pixels in a
  triangular region).

## Cross-cutting

- **Coherency:** keep all BOs (CLs, tile_alloc/state, RT, shaders, attributes) uncached
  (`MAP_UNCACHED|MAP_CONTIGUOUS`); confirm writes landed before kicking. Flush the V3D L2T
  cache between bin and render (`V3D_CTL_L2TCACTL`, GFXH-1897 wait, `v3d_gem.c:179-230`).
- **Tile size:** V3D 4.2 utile from `v3d_utile_width/height(cpp)` (`v3d_tiling.c:38-56`); a tile
  is a grid of utiles. For RGBA8 (cpp=4) the supertile/tile maths come from `TILE_BINNING_MODE_CFG`
  `log2_tile_width/height`. Start with a single 64×64 RT = 1 tile to keep the supertile loop to 1.
- **MMU:** reuse the working flat-PT bring-up; map every BO (CLs, tile mem, RT, shaders) at distinct
  GPU VAs in the page table. Widen the PT loop to cover all pages used.
- **Submit registers:** bin on CT0 (`CT0QBA/QEA` + `QMA/QMS/QTS`), render on CT1 (`CT1QBA/QEA`).
  Poll `CTnCA` to end (no IRQs wired). Exact offsets: `v3d_regs.h` `V3D_CLE_CT*`.

## Risks / open unknowns (resolve empirically, smallest-step-first)
1. Does the binner write tile_state for an EMPTY frame, or does it need ≥1 primitive to init it?
   (4b-1's readback answers this. If empty-frame doesn't init, fold a trivial primitive in.)
2. `tile_alloc` overflow: if `QMS` is too small the binner faults; over-allocate first.
3. STORE_TILE_BUFFER_GENERAL memory-format/stride for a plain raster RGBA8 RT (get from the
   packet xml + `v3dx_rcl.c:136` store emit).
4. Shader-state record exact layout + VPM/attribute config — the densest part; lift verbatim
   from `v3dx_emit.c`/`v3dx_draw.c`.

## Reference map
- Render CL: `external/mesa/src/gallium/drivers/v3d/v3dx_rcl.c` (clear `:74-340`, frame setup
  `:573-944`).
- Bin CL: `v3dx_draw.c:71-100` (TILE_BINNING_MODE_CFG), draw emit; `v3d_job.c:553-706`
  (tile_alloc/state alloc + QMA/QMS/QTS submit).
- Packet codes/bitfields: `external/mesa/src/broadcom/cle/v3d_packet.xml`.
- Registers: `external/linux/drivers/gpu/drm/v3d/v3d_regs.h`; submit `v3d_sched.c` bin/render
  job_run; caches `v3d_gem.c`.
- QPU ISA/encoder: `external/mesa/src/broadcom/qpu/`.
- Working foundation: `sources/phoenix-rtos-devices/misc/rpi4-v3d-scout/rpi4-v3d-scout.c`
  (`v3d_boAlloc`, `v3d_mmuCleTest`, `v3d_powerOn`).

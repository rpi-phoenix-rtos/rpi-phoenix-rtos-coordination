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

## APPROACH (maintainer directive, 2026-06-10): PORT MESA, don't hand-encode

Reuse as much Mesa as possible — use it directly or port it — rather than rewriting
step-by-step. Concretely for the CLE: **port Mesa's generated packers** into the project
(`external/mesa/src/broadcom/cle/gen_pack_header.py v3d_packet.xml 42` →
`v3d_packet_v42_pack.h`, providing `V3D42_<PACKET>_pack()` + the `cl_emit()` macro) and emit
packets through them, instead of writing `cl[]` bytes by hand. The hand-encoded
`STORE_TILE_BUFFER_GENERAL` in the 4b-2 WIP **stalled the render thread** (CT1CA parked at the
store packet, RT unchanged) — exactly the bitfield-error class that Mesa's packers remove. The
CL *structure*/scaffold in `v3d_renderClearTest` (devices `8a90593`, gated/unused) is correct
and reusable; re-emit its packets via the ported packers. Apply the same principle to the QPU
shaders (4b-3): use Mesa's `src/broadcom/qpu` encoder rather than hand-assembling. See
[[reference_external_source_clones]] and the `reuse-mesa-not-rewrite` memory.

## 4b-2 DONE (2026-06-11): GPU clears a render target — full pipeline works

The V3D renders: bin→render→TLB-store, 4096/4096 px of the 64×64 RGBA8 RT == clear color,
FRDONE=1, 2/2 netboots (devices `22eb868`, manifest `2026-06-11-v3d-render-clear`). The three
fixes over the stalling version: bin CL needs `FLUSH_VCD_CACHE`+`OCCLUSION_QUERY_COUNTER`;
render CL needs `ZS_CLEAR_VALUES` as the last mode-cfg; and a **post-render V3D L2T flush**
(clean→writeback) before the ARM readback (the store was landing in V3D cache, not RAM — the
silent "no pixels" cause). All emitted via the ported Mesa packers. NEXT = 4b-3 triangle.

## (historical) 4b-2 earlier status: Mesa packers ported; render parked at END_OF_LOADS

Mesa's generated packers are now in the tree (`v3d_packet_v42_pack.h` + `v3d_gen.h` shim,
devices `c45883d`) and the render CL is emitted through them (correct by construction). bin
completes (FLDONE=1) and bin→render is synced on the HW done flags (FLDONE/FRDONE, INT_STS
CORE+0x50). Remaining: the **render thread parks at `END_OF_LOADS`** in the per-tile list
(`CT1CA=0x30801`, FRDONE never fires, RT unchanged) — a render-pipeline *semantics* issue, not
encoding/sync. Next leads: explicit `TILE_COORDINATES` vs implicit; read V3D error/CT1CS status;
match Mesa's clear RCL/TLB config more fully; verify supertile dims. The hand-encoded byte-layout
recipe below is now superseded by the packer structs but kept as the field reference.

## 4b-2 DECODED RECIPE (single 64×64 tile clear-to-color → RT BO readback)

4b-1 (bin pass) is DONE (devices `2117899`); reuse its tile_alloc/tile_state. All packet
encodings below are from `python3 external/mesa/.../cle/gen_pack_header.py v3d_packet.xml 42`
(regenerate to `/tmp/v3d42_pack.h` and read `*_pack` bodies for exact `cl[]` byte layouts).
This is near-mechanical to implement; it is an intricate **two-level** control list, which is
why it's a fresh-context task, not a context-bottom marathon.

**Enums (verified):** MEMORY_FORMAT_RASTER=0; INTERNAL_TYPE_8=2 (RGBA8 unorm); INTERNAL_BPP_32=0;
OUTPUT_IMAGE_FORMAT_RGBA8=27; buffer_to_store RENDER_TARGET_0=0, NONE=8. **sub_id** for the
code-121 variants: COMMON=0, COLOR=1, ZS_CLEAR_VALUES=2, CLEAR_COLORS_PART1=3, PART2=4.

**Opcodes / lengths:** TILE_RENDERING_MODE_CFG_* =121 (len 9 each); MULTICORE_..._SUPERTILE_CFG=122
(9); MULTICORE_..._TILE_LIST_SET_BASE=123 (5); TILE_COORDINATES=124 (4); TILE_COORDINATES_IMPLICIT=125
(1); CLEAR_TILE_BUFFERS=25 (2); END_OF_LOADS=26 (1); END_OF_TILE_MARKER=27 (1); STORE_TILE_BUFFER_GENERAL=29
(13); FLUSH_VCD_CACHE=19 (1); BRANCH_TO_IMPLICIT_TILE_LIST=21 (2); SUPERTILE_COORDINATES=23 (3);
END_OF_RENDERING=13 (1); PRIM_LIST_FORMAT and SET_INSTANCEID (look up codes/lens in the per-tile list).

**Key byte layouts (cl[] little-endian, from the v42 pack header):**
- COMMON(121,9): cl0=121; cl1=((numRT-1)<<4)|sub_id(0); cl2..3=width(64) u16; cl4..5=height(64) u16;
  cl6=(internal_depth_type<<7)|(early_z_disable<<6)|(dbuf<<3)|(ms<<2)|max_bpp(0); cl7..8≈0 (depth/ez clear).
- COLOR(121,9): cl0=121; cl1=(RT0_type(2)<<6)|(RT0_bpp(0)<<4)|sub_id(1); cl2..8 carry RT1-3 (0) + clamp(0).
- CLEAR_COLORS_PART1(121,9): cl0=121; cl1=(rt_number(0)<<4)|sub_id(3); cl2..5=clear_color_low_32 (memcpy,
  the 32bpp clear value e.g. 0xAABBGGRR); cl6..8=clear_color_next_24 (0 for 32bpp).
- TILE_LIST_SET_BASE(123,5): cl0=123; cl1=(tile_alloc_VA & 0xff)|set_number(0); cl2..4=tile_alloc_VA>>8.. .
- SUPERTILE_CFG(122,9): cl0=122; cl1=stw-1(0); cl2=sth-1(0); cl3=frameW_in_supertiles(1); cl4=frameH_in_supertiles(1);
  cl5..6=frameW_in_tiles(1, 12b)+frameH_in_tiles(1, bits4-15 of cl6..7); cl7=frameH hi; cl8=(numBinTileLists-1=0<<5)|(rasterOrder<<4)|multicore_en(0).
- STORE_TILE_BUFFER_GENERAL(29,13): cl0=29; cl1=(flipY<<7)|(mem_format RASTER(0)<<4)|buffer_to_store(RT0=0 or NONE=8);
  cl2=(output_format RGBA8(27)<<4)|(decimate<<2)|dither; cl3=(rbswap<<4)|(chrev<<3)|(clear_being_stored<<2)|(outfmt>>8);
  cl4..6=height_in_ub_or_stride (RASTER: row stride bytes=64*4=256, in bits 4-23 → store 256<<4? NO: field starts at bit4
  so value occupies cl4..6; set field=stride 256); cl7..8=height(64); cl9..12=RT BO GPU VA (address).
- TILE_COORDINATES(124,4): cl0=124; cl1=col(0); cl2=(row(0)<<4)|colhi; cl3=rowhi. CLEAR_TILE_BUFFERS(25,2):
  cl0=25; cl1=(clear_z<<1)|clear_all_RT(1). BRANCH_TO_IMPLICIT_TILE_LIST(21,2): cl0=21; cl1=set_number(0).
  SUPERTILE_COORDINATES(23,3): cl0=23; cl1=col(0); cl2=row(0).

**Emit order (main RCL on CT1, from v3dx_rcl.c emit_rcl + emit_render_layer):**
1. COMMON → COLOR → CLEAR_COLORS_PART1 (and PART2 if needed). 2. TILE_LIST_SET_BASE=tile_alloc.
3. SUPERTILE_CFG (1×1 tile, 1 supertile). 4. GFXH-1742 initial-clear dance: TILE_COORDINATES(0,0),
then 2× { [TILE_COORDINATES(0,0) if i>0] ; END_OF_LOADS ; STORE(buffer=NONE) ; [CLEAR_TILE_BUFFERS if i==0] ;
END_OF_TILE_MARKER }. 5. FLUSH_VCD_CACHE. 6. **generic per-tile list (in a SEPARATE indirect BO)** =
TILE_COORDINATES_IMPLICIT + (loads: none for clear) + PRIM_LIST_FORMAT(triangles) + SET_INSTANCEID(0) +
BRANCH_TO_IMPLICIT_TILE_LIST + STORE_TILE_BUFFER_GENERAL(RT0→RT BO) + END_OF_TILE_MARKER. 7. supertile loop:
SUPERTILE_COORDINATES(0,0). 8. END_OF_RENDERING.
Submit: CT1QBA=main-RCL VA, CT1QEA=end. Invalidate caches (L2T flush + slices) before the kick.

**ONE thing still to trace (the only gap):** how the main RCL is linked to the indirect per-tile list
— `v3d_rcl_emit_generic_per_tile_list` (v3dx_rcl.c:345-378) writes the list into `job->indirect` and
captures `tile_list_start = cl_get_address(cl)`; find where that address is handed to the main RCL /
supertile walker (likely a "Start Address of Generic Tile List" packet, or it's implicit). Resolve from
v3dx_rcl.c before coding. Everything else above is pinned.

**Then 4b-3 (triangle):** add to the BIN CL a vertex attribute BO (3 verts) + GL_SHADER_STATE(64) +
VERTEX_ARRAY_PRIMS(36, triangles, 3) + the shader-state record + hand-encoded coord/vertex/frag QPU
shaders (external/mesa src/broadcom/qpu). Render to the RT BO (or the firmware FB) → HDMI.

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

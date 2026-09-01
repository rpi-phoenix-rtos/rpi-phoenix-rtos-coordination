# vkQuake on Pi4/V3DV — striping root-cause + 3D-world wiring plan (2026-07-29)

Continues `2026-06-23-vkquake-port-scaffold-status.md`. Session goal: working vkQuake
rendering over GPU via Vulkan. Build chain reconstructed; ELF boots; 2D presents to fb0.

## Current HW state (post-EEE-network-fix, network-independent)
- Vulkan brings up clean: `vkCreateInstance/Device -> 0`, VID_Init done (1920x1080).
- Per-frame loop runs, **presents 2D to fb0 / HDMI** (`present 1/2/3...`, `2D draws=N nullLayoutPC=0`).
- **Untextured** 2D (magenta color quad) renders PERFECT.
- **Textured** 2D (conchars/whitetex) renders **striped**: regular every-other-row (2:1) horizontal
  interleave + a colour gradient (a grayscale font should not produce colour).
- 3D world NOT rendered: `con_forcedup=1` (no map) — a deliberate "2D-first" block in
  `tools/vkquake-port/platform/pl_phoenix_main.c` force-disconnects to avoid the unwired 3D path.

## STRIPING — root-caused to the READ side (descriptor is provably correct). Evidence:
Deployed instrumentation (v3dvx_image.c `v3dv-tex:` dump) + winsys CPU-tile write-log, per texture:
- conchars 128x128: descriptor tiling=UIF_NO_XOR xor=0 ub_pad=0 stride=512 padded_h=128 planeH=128
  identity-swizzle(R,G,B,A) ttype=4 — **EXACTLY matches** the winsys write `128x128 xor=0 phgt=128`.
- 256x256: descriptor UIF_XOR xor=1 phgt=256 — matches write `256 xor=1`. Same for 64/640x512.
RULED OUT (all measured on HW, not assumed):
- Descriptor fields wrong — NO, provably correct + identical to the working gallium GLQuake path.
- CPU-tiler wrong — NO, winsys `uif_pixel_off` matches Mesa `v3d_get_uif_pixel_offset` for cpp=4
  (utile 4x4, mb 8x8, mb_h, offsets all identical).
- Tiling upload the culprit — NO: TFU-write and CPU-tile-write are DIFFERENT code paths yet stripe
  IDENTICALLY => the common factor (the TMU read) is wrong.
- MSAA — NO (sample_count=1 everywhere). Version — NO (v3dvx built V3D_VERSION=42 = HW). cpp — cpp=4
  confirmed (SRC_INDEXED conchars uploaded as R8G8B8A8_UNORM).
=> The bug is in the ONE thing vkQuake does NOT share with the clean GLQuake path: its SPIR-V→v3dv-
compiled fragment shader's TMU access and/or the descriptor-set binding (VkSampler/descriptor set),
not tiling/descriptor fields. This is deferred as a tracked artifact — it lives in the shared
texture-sample path, so a later fix benefits both 2D and 3D at once. Instrumentation to remove
before ship: the `v3dv-tex:` fprintf in external/mesa .../v3dvx_image.c.

## 3D WORLD — the bulk of "working vkQuake". Wiring plan (Option C: single color+depth pass)
Gate (done): flip the `pl_phoenix_main.c` 2D-first block from `disconnect;menu_main` to `map start`
so V_RenderView runs. Upstream uses a secondary-cb TWO-pass (MAIN color+depth, then UI) architecture
(gl_vidsdl.c) that the shim collapsed to a single inline primary-cb UI pass. Simplest bridge:
1. create_render_resources (pl_phoenix_vk_vid.c): add a D32_SFLOAT depth image+view; make ONE render
   pass with color(scanout)+depth, loadOp=CLEAR both; framebuffer = color+depth.
2. Set `vulkan_globals.main_render_pass[MAIN_RENDER_PASS_STANDARD][both stencil modes]` = that pass;
   keep `ui_render_pass` = same pass (2D pipelines don't depth-test, so compatible). The engine's
   pipeline-build skip-guard (gl_rmisc.c:2545 `render_pass==NULL -> continue`) then AUTO-builds the
   MAIN pipelines once main_render_pass is non-NULL.
3. Call `R_CreatePipelines()` (full) instead of just `R_CreateBasicPipelines()` — builds world/alias/
   brush/sky/particle pipelines against main_render_pass.
4. Allocate + wire ALL scene secondary_cb_contexts (SCBX_WORLD..SCBX_PARTICLES; multiplicities
   NUM_WORLD_CBX=6, NUM_ENTITIES_CBX=6, rest=1 — SECONDARY_CB_MULTIPLICITY[]) each: cb=frame_cb,
   render_pass=the pass, render_pass_index=RENDER_PASS_INDEX_MAIN, subpass=0. GUI/POST_PROCESS keep UI.
   Single-threaded (r_tasks=0) records all contexts inline into frame_cb sequentially.
5. GL_BeginRendering: begin the color+depth pass (clear both) so V_RenderView records the world inline;
   SCR_DrawGUI records 2D after (no depth test). GL_EndRendering ends+submits (unchanged).
RISKS to watch on first boots: R_CreatePipelines building a V3D-unsupported pipeline (feature-scope
off OIT/WBOIT/compute-lightmap/RT — already partly done in VID_Init); render-pass compatibility of
the 2D pipelines with the depth attachment; any place the engine begins a context's cb as a real
secondary (would conflict with frame_cb already-begun). Keep a known-good clean-2D build deployable.

## 3D WIRING — IMPLEMENTED + BUILDS/INITS CLEAN ON HW (2026-07-29). Remaining blocker: init speed.
Implemented Option C (coord commit 9759e63): D32 depth buffer + single color+depth render pass
published as main_render_pass[STANDARD][*]; FULL R_CreatePipelines (world/alias/brush/sky/particle);
all 12 secondary_cb_contexts wired to the primary frame_cb; GL_BeginRendering clears color+depth;
pl_phoenix_main loads `map start`. **HW: every stage builds + inits with ZERO fault** — `rr: depth
buffer ok`, `renderpass ok (depth=1)`, `12 scbx contexts wired`, **`FULL pipelines ok (world+2D)`**
(R_CreatePipelines survives on V3D — the biggest risk cleared). BUT the world frame is NOT reached:
vkQuake stops after ~present 3 (2D console, con_forcedup=1, during Host_Init) — only ~3 frames +
18 texture uploads in a 340s window, HDMI frozen on the 2D bisector for ~5 min. Two candidate causes
(undistinguished — NEXT step is a print right after Host_Init() returns to tell them apart):
  (a) SYNCHRONOUS-WINSYS SLOWNESS: every GPU submit waits device-idle, so each texture precache
      upload is very slow; vkQuake precaches 100+ textures in Host_Init => 15-30 min just to finish
      Host_Init before `map start` even runs. Real fix = batch/async GPU submits in the winsys (a
      significant rework) or drastically cut precache.
  (b) A post-present-3 STALL in Host_Init/first-world-frame (present 1/2/3 completed cleanly, no
      wedge/fault; then no present 4). If (b), it's a logic/deadlock bug, not speed.
  NOTE (important): the ORIGINAL 2D-first build ALSO stops at the same present-3 2D-bisector, so
  this present-3 wall is PRE-EXISTING (prior scaffold state), NOT introduced by the 3D wiring — the
  3D changes are additive and build/init cleanly on top. Resolving the present-3 wall (slow-vs-hung
  distinguishing test = a print right after Host_Init() returns, over a long clean-boot window)
  unblocks BOTH finishing 2D and reaching the 3D world.
Compounding: an INTERMITTENT USB xHCI UART debug spam (`SLOT OUT`/`EP0 CTX`+hex, ~6 MB) floods the
UART on some boots after ~present 3, drowning vkQuake output (source token not found in
sources/phoenix-rtos-usb — likely a different build's stack or a formatting artifact). HDMI snapshots
bypass it (used them to confirm the frozen 2D screen). Deployed rpi4-vkquake md5 2ac63d52 = the
3D-wired build (boots + presents 2D; does not reach the world).

## Build/run quick ref
- Build: `python3 tools/v3d-driver-port/build-v3dv-phoenix.py` then
  `python3 tools/vkquake-port/build-vkquake-phoenix.py --link` -> /tmp/vkquake-phoenix; deploy to
  /srv/phoenix-rpi4-nfs/usr/bin/rpi4-vkquake (NFS binary, launched via psh like rpi4-quake — NO
  loader.disk swap).
- Boot: `./scripts/test-cycle-psh-interact.sh --label vkq-X --inter-cmd-secs 8 --idle-secs 200 -- \
  "ls -l /usr/bin/rpi4-vkquake" "/usr/bin/rpi4-vkquake"` (EEE off => netboot reliable).

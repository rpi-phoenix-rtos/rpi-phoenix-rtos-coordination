# vkQuake texture striping — standalone-repro campaign (2026-07-30)

Continues `2026-07-29-vkquake-striping-and-3d-plan.md`. Goal: root-cause + fix the
texture striping (2D textured draws — conchars/whitetex — render with a regular
every-other-row 2:1 horizontal interleave + colour on a grayscale font; untextured
2D renders perfectly). vkQuake reaches 2D present frames, so the bug is observable
**without** the 3D world.

## Method
Built a controlled standalone Vulkan texture probe in the existing V3DV harness
`tools/v3d-driver-port/v3dv_harness.c` (the `if (texprobe) …` blocks: OFFSCREEN blit
readback + the MIPCOL/CHECKPROBE textured-draw + defect/quad-uniform counters, all
reading back a LINEAR RT via `v3dv_MapMemory` — deterministic, no scanout/#67/HDMI
race). Each probe changes ONE variable vs the last and prints a gross machine verdict.
Shaders in `tools/v3d-driver-port/texprobe/` (glslangValidator), embedded in
`texprobe_spirv.h` (arrays: vert/frag/vbvert/mvvert/mvfrag/mvpvert/mvvbvert).

## Result: the read/sample/data path is EXONERATED; 8 controlled probes all CLEAN
| # | probe | result |
|---|---|---|
| 1 | 256→512 gradient blit (magnification, blit_shader) | CLEAN* (non-discriminating: upscale + tolerance) |
| 2 | mip-chain, solid-per-level, NEAREST, minification | UNIFORM (no LOD/mip jitter) |
| 3 | smooth ramp, LINEAR+trilinear+**aniso**, 8:1 anisotropic footprint | SMOOTH (0 spikes) |
| 4 | **hi-freq 1-texel checkerboard, 1:1 NEAREST LOD0** | CLEAN (0 defects, 0 uniform-quads) |
| 5 | UV from a single vertex-buffer **attribute** | CLEAN |
| 6 | **3 computed varyings** (texcoord+color+fog, like basic) | CLEAN |
| 7 | 3 varyings + runtime **identity-mat4 push-const** (computed gl_Position.w) | CLEAN |
| 8 | 3 varyings from a **multi-attribute VB** (pos+uv+color, stride 32) | CLEAN |

Also eliminated earlier (in-engine / winsys): the sampler machinery (forcing ALL 8
vkQuake samplers to NEAREST/mip-NEAREST/maxLod0/no-aniso did **not** fix the striping);
tiling/descriptor bytes (proven byte-identical to the clean gallium GLQuake path);
the TFU/CPU-tile upload (byte-exact canonical UIF, vcheck 0 mismatch).

The strongest single clue that started the varying trail: in vkQuake, forcing
`basic.vert` to emit **constant** varyings makes the striping VANISH (uniform); restoring
**texcoord from the VB** brings it back — so the corruption enters via the per-vertex
data→varying path. But every standalone reconstruction of that path is clean.

## Conclusion
The striping is a **specific interaction inside vkQuake's real `basic` pipeline** that
does NOT reproduce from textbook Vulkan primitives matching its individual features
(multi-varying, VB-multi-attr, computed-w, aniso, trilinear, hi-freq 1:1 — all clean in
isolation and in the tested intersections). Remaining untested deltas vs the probes:
the FS **reading** push-constants (fog); the exact **ortho** matrix; vkQuake's
**descriptor-set update** path; **render-to-scanout** vs offscreen-linear RT; the actual
conchars texture object. A second, independent bug (colour on a grayscale font) is the
return-size/**out32** issue (separate axis from the row-interleave).

## Decisive next step (NOT more standalone probes — that campaign is exhausted)
Dump vkQuake's **real** `basic` VS+FS compiled **QPU/VIR** (via a V3D_DEBUG=qpu-equivalent
or an in-winsys dump at pipeline-create) and **diff** against a clean 1-varying shader's
QPU: the `ldvary` count/order, the VPM read (FS) / write (VS) layout, the vertex
attribute-unpack, and the perspective/`payload_w` block. The first structural difference
localizes the fix (likely in v3dv's VS→FS varying/VPM linking or the shader-record).
Then fix + validate: re-run the probe that matches (should stay clean) and boot vkQuake
(striping should clear in the 2D conchars at present-3).

## Gotchas recorded
- `vk_common_CmdPushConstants` trampoline crashes when called directly (wild jump to a
  string-valued code ptr); use `v3dv_CmdPushConstants`.
- NFS `exec … -34` (ERANGE) after many reboots → `sudo systemctl restart nfs-server`,
  wait ≥90 s grace before booting.
- The fullscreen-tri VS makes the VISIBLE UV range 0..2 over the viewport (it extends to
  NDC 3), so a 1:1 texel:pixel map needs src = dst/2 (128² src for a 256² dst).

## UPDATE (2026-07-30, later) — DECISIVE: it's the READ of the REAL texture object; standalone repro is impossible
Two in-vkQuake tests settled the direction:
1. **Forced all 8 samplers** to NEAREST/mip-NEAREST/maxLod0/no-aniso → **still striped** (sampler exonerated).
2. **`basic.frag` outputs `in_texcoord` as colour** (no texture sample) → **smooth gradient, zero striping** on
   the real scanout → the **varying interpolation, rasterization, render-target, and shader compile are ALL
   CLEAN in the real runtime**. The striping enters *only* at `texture(tex, uv)`.

Then the standalone campaign was pushed to **replicate vkQuake's `basic` draw exactly** and STILL came back
clean on every axis: exact `basic.vert`+`basic.frag` (cross-stage push-const + FS fog-uniform read),
multi-attribute VB (pos+uv+color), computed `gl_Position.w` (identity-mat4 push-const), 8 draws + per-draw
rebinds in one command buffer, mid-grayscale (0x55/0xAA) intermediate values (R==G==B preserved, no colour),
non-zero image bind offset for **both** 128² NO_XOR **and** 256² UIF_XOR at a 4096-not-16KB-aligned offset,
and a non-64-aligned staging source offset. **Every one CLEAN.**

**Conclusion:** the striping is NOT reproducible with any controlled parameter — it is specific to vkQuake's
**real texture object as created/placed by TexMgr in the running engine process**: the `GL_HeapAllocate`
shared heap, the real decoded texture DATA, and/or the accumulated V3D MMU/VA/cache state of the large process
that a minimal one-shot harness never reaches.

**Sole remaining avenue (on-device instrumentation of the REAL texture — NOT more standalone probes):**
in vkQuake `TexMgr`, right after a striped texture's upload, `vkCmdCopyImageToBuffer(tex → mappable buffer)`
and compare the tiled readback to canonical `uif_pixel_off`; also dump the descriptor's
`texture_base_pointer`/stride/`padded_height` vs the real image's slice layout and the BO's GPU VA.
Data-wrong ⇒ upload/tiling fix; data-right-but-sample-wrong ⇒ descriptor/MMU/VA for the real object.

## UPDATE 2 (2026-07-30, latest) — CONFOUND-FREE PROOF: the GPU is clean; the bug is vkQuake's CPU-side texture DATA
Injected KNOWN data into the staging **after** the real fill (the correct point — the copy is *recorded*
before the fill, and the pre-fill overwrite the summary warned about is clobbered by the real memcpy).
- A mid-gray checker → clean grayscale checker (no colour, no bands).
- A **per-row grayscale gradient** → an unmistakable **clean smooth grayscale vertical ramp** per texture
  (definitely my data, not real Quake content) — **zero colour, zero row-interleave**.

So known data through vkQuake's **real** path (GL_HeapAllocate image, real descriptor, real 1920×1080
scanout, real process state) renders CLEAN. The GPU/upload/read/tiling is **exonerated** (confirming all
12+ standalone probes + the UV-visualize). The real render's striping is therefore in the **real texture
DATA** as prepared CPU-side by the vkQuake port, upstream of the `memcpy data→staging` (the fill's mip0
size is `glt->w*glt->h*4`, correct — so the corruption is in `data` itself). The palette build +
`TexMgr_8to32` are correct (grayscale index → grayscale RGBA), so the culprit is a **non-indexed image
loader** (e.g. the console background `conback`, a coloured 320×200 image whose *colour* is expected — the
real defect is the **every-other-row striping**) or a row-stride/pitch/flip bug producing `data`.

**FIX DIRECTION (CPU-side, tractable — no GPU/v3dv work): trace where `data` is produced for the striped
2D pics** (`Draw_CachePic`/`Draw_PicFromWad`/the image-file loader for conback etc.), dump `data`'s
dimensions + a few rows, and find the row-stride/pitch/top-down bug. Then fix + validate (with the injection
OFF, real textures should render clean). The injection diagnostic in gl_texmgr.c has been REVERTED.

## UPDATE 3 (2026-07-30) — texdatadump + clean-build characterization: GPU + mip0 data both clean
`texdatadump` (dump of the RGBA `data` memcpy'd to staging, 12 textures): ALL coherent — `conchars`
128² grayscale (transparent bg), `conback` 640×512 a real coherent image (its colour is expected),
`scrap0/1` 256² coloured atlases, sane dims, row stride == glt->width. So **mip0 source data is correct.**
Clean-build boot: untextured quad perfect; the two "striped" rects are the **256² scrap0/scrap1 UI
atlases** (mostly near-black/sparse) with only **faint** horizontal lines. The **gradient injection
rendered those exact textures perfectly clean** (smooth grayscale, no lines), and the standalone hi-freq
1-texel checkerboard was clean (0 defects) — so the sampling path handles high-frequency + smooth data
correctly. Net: the GPU/upload/read/tiling AND the mip0 data are exonerated; the dramatic
every-other-row striping characterized on 2026-07-29 does **not** reproduce under controlled data, and the
residual faint lines on the dark sparse atlases are subtle (possibly real sparse content, or a minor
mip-downsample/minification effect). Only remaining CPU-side suspect for any real residual = the mip
downsample (`stbir_resize_uint8` in `TexMgr_Downsample`), which affects minified content only.
**Separately, the 3D world is still not reached** (pre-existing present-3/Host_Init wall — likely
synchronous-winsys precache slowness or a hang, per the 2026-07-29 doc; unrelated to textures).

## UPDATE 4 (2026-07-30) — 3D-world blocker analysis (the primary "working vkQuake" gap)
Boot-log analysis of the clean build: the markers `vkquake: 3D bring-up: loading 'map start'` and
`loop 0 enter` (both printed right AFTER `Host_Init()` returns, in pl_phoenix_main.c) NEVER appear within
a ~320 s capture. The presents 0–12 are `SCR_UpdateScreen` calls from INSIDE Host_Init (con_forcedup=1,
sv.active=0, worldmodel=0; the main menu comes up at present 11, m_state=1). So **Host_Init has not
returned** — it reaches the menu but the `map start` (line 125, the 3D-world gate) hasn't run yet.
Per the 2026-07-29 estimate, Host_Init (~5 min) + the `map start` world precache (100+ textures, ~15–30
min on the SYNCHRONOUS winsys — every GPU submit spin-waits completion + L2T flushes) ≈ 20–35 min total.
**The Bash tool caps at 600 s and the test-cycle at ~320 s of useful capture, so the 3D world cannot be
observed in a single cycle** — it is a throughput problem, not a hang (present count climbs; no fault).

**The gating fix is the winsys async/batch rework** (the "significant rework" flagged 2026-07-29): make
texture-upload + frame submits asynchronous / batched instead of one synchronous spin-wait each, OR
drastically cut Host_Init/precache work, so Host_Init + one map load complete in an observable (< a few
min) window. Only then can the 3D world (V_RenderView, already wired: depth pass + FULL R_CreatePipelines
+ 12 scene contexts build clean per 2026-07-29) be reached and validated. Textures are a non-blocker now
(GPU proven clean; 2D renders correctly). This + "fast" are one and the same winsys-throughput task.

## UPDATE 5 (2026-07-30) — 🎉 3D WORLD RENDERS + FAST (mutex fix); striping is the last gap
The watchdog backtrace (task #43 in-process debugger) caught the "map start" hang: `phMutexLock` <-
`S_ClearBuffer` <- `S_StopAllSounds` (holds snd_mutex, then calls S_ClearBuffer which RE-locks it) <-
`SCR_BeginLoadingPlaque` <- `Host_Map_f`. Root cause: **SDL mutexes are recursive; the Phoenix
`SDL_LockMutex`→`mutexLock` shim was NOT** → self-deadlock. FIX (pl_phoenix_sdlcompat.c): recursive
mutex (owner pthread_self()+count; SDL_CondWait clears/restores across the single Phoenix unlock/relock).
RESULT on HW: `sv.active=1 worldmodel=1 signon=4/4`, start.bsp renders with CORRECT geometry/perspective/
corridors + viewmodel, present climbs to 7000+ (~29 fps). The GPU submits were fast all along (perf-diag:
8600 CL in 77 s) — so the earlier "20–35 min synchronous winsys" theory was WRONG; the whole
"slow/unreachable 3D world" was this ONE deadlock. **"working" (reaches 3D world) + "fast" are now MET.**

Remaining: **texture striping**, now visible on the 3D surfaces (green/purple per-texel SPECKLE on
minified walls/floor; diagonal RAINBOW on the viewmodel alias skin). Eliminated this session, each on HW:
diffuse-only world FS (walls still speckle → NOT the lightmap); `noperspective` texcoord (still speckle →
NOT perspective-correct interp); the mip downsample (box-filter AND stbir both speckle identically → NOT
mip generation); + all prior (sampler/tiling/descriptor/mip0-data/generic-sample-path/12 controlled probes
clean). The residue: it manifests ONLY on **minified, HIGH-FREQUENCY, real-content textures at world
scale** — the one regime the controlled probes never hit (mip-color=solid, aniso=smooth, checker=1:1).
Gallium GLQuake samples the same content CLEAN on the same silicon → it is **vkQuake-shader-specific**.
CONCLUSION: a read-side TMU issue for high-freq minified sampling in vkQuake's compiled FS — the
QPU-disassembly-vs-gallium diff (deferred across sessions) is now the right tool, and the 3D world is live
to iterate against. KEEP: the recursive-mutex fix (the breakthrough). Reverted: all striping diagnostics.

## State
- `external/mesa`, `external/vkquake` (Shaders/, gl_rmisc.c) reverted clean of all
  diagnostics (vkquake's gl_screen.c/host_cmd.c mods are pre-existing port baseline).
- Deployed `rpi4-vkquake` restored to a clean non-diag build (md5 `9ca1da8b`).
- `rpi4-texprobe` = the diagnostic harness (repro scaffold, gated by `texprobe`).
- Memory: `project_vkquake_bringup_mechanics` (full trail, newest entries first).

# vkQuake → Phoenix-RTOS Pi4 (aarch64-phoenix, V3DV) — build scaffold + gap inventory

> **STATUS (2026-06-27): TEXTURE-UPLOAD GAP CLOSED on the write side; striping localized to a
> single TFU→TMU CACHE-FENCE bug.** The TFU buffer→image copy is now implemented and executes
> in `v3d_phoenix_winsys.c` (present-hang gone; textures upload via TFU; extent + TMUWCF
> write-combiner fixes landed). Quake content reaches the GPU — but textures sample **striped**
> (horizontal banding). Two diagnostic probe boots pinned the cause precisely:
> - `TILING` probe → block-0 UIF correct (but block-0 is XOR/stride-invariant, too weak).
> - `TFU vcheck` vertical/inter-block probe → **`UIF-VERIFIED match=6/6`** on 64²/128²/256²
>   (incl. the `xor=1` variant): the TFU's FULL UIF output — vertical layout (y=8/16/32,
>   (8,8)/(16,16)) and the XOR page-interleave — is **correct in memory** (CPU-mapped read via
>   mesa `uif_pixel_off`). So **write-side is REFUTED**. And since GLQuake renders textured
>   through the IDENTICAL TMU/descriptor/MMU/L2T path with CPU-tiled UIF textures, the
>   **read-side descriptor is also proven correct**.
> - ∴ The bug is a **TFU-write → TMU-read coherency fence gap**: data is correct in memory but
>   samples striped ⇒ the L2T texture cache (and/or the TFU's TMUWCF write-combiner) is **not
>   flushed/invalidated between the TFU copy (`ioc_submit_tfu`) and the render CL that samples
>   the texture**. The TMU fetches stale/partial lines → banding.
>
> **NEXT (the fix, a focused GPU step):** between the TFU copy and the sampling render submit,
> add an **L2T flush + TMU cache invalidate** (`SLCACTL` with the TMU/L2T bits) and/or fence on
> **TFU completion** before the render issues TMU fetches. Reference `external/mesa`
> `src/broadcom/vulkan` (v3dv TFU path: how it fences TFU→sample) and `external/linux` v3d
> (`v3d_sched`/TFU IRQ + L2T flush sequencing). Validate by a vkQuake autostart boot — striping
> should resolve to clean textures on HDMI. Probes committed: coord `7e3d1f6`/`c5c1baf`/`123f9bf`
> (winsys `TFU vcheck`). This is the LAST known blocker to textured vkQuake.
>
> ---
> **STATUS (2026-06-26): 2D GPU RASTER PROVEN ON HW; PAUSED at the texture-upload gap.**
> Since this scaffold doc, vkQuake advanced well past init: Vulkan fully initializes on
> the V3D, the per-frame loop / present / projection / back-face-cull are all fixed, and
> a GPU 2D quad renders via Vulkan on the V3D (HW-proven). The **sole remaining blocker is
> the no-WSI winsys texture-copy gap**: `DRM_V3D_SUBMIT_TFU` is a no-op on the Phoenix
> winsys and the CL meta-copy fallback also fails to land textures, so no texture data
> reaches the GPU images → no recognizable Quake content yet. The fix is a proper
> buffer→image copy in `v3d_phoenix_winsys.c` (a focused session). vkQuake is task #29,
> PAUSED; the GL Quake flagship is unaffected. See `docs/done/2026-06-26-risky-items-results.md`
> and `docs/done/2026-06-25-hw-validation-results.md`.

**Date:** 2026-06-23
**Type:** IMPLEMENTATION (host-side build scaffolding) + gap scoping
**Scope:** ADDITIVE. Does NOT touch the GLQuake flagship (`tools/quakespasm-port`, the GL
path) or the main image build. Host-side only — no Pi boot (UART owned by the main agent).
Builds on the HW-validated V3DV ICD (Tier-4b, 2026-06-23): `libv3dv-phoenix.a` +
`libv3d-phoenix.a` back-end.

> **UPDATE 2026-06-25 — REAL SPIR-V SHADERS + FULL CHAIN REBUILT 0-UNDEFINED FROM SCRATCH.**
> The night-audit "no glslang" premise was stale: `glslangValidator` 11:16.2.0 + `spirv-opt`
> (SPIRV-Tools v2026.1) ARE on this host. Re-ran `gen-vkquake-shaders.py` → **all 41 shaders
> compiled to REAL SPIR-V** (magic `0x07230203`, no 20-byte placeholders), matching vkQuake's
> **authoritative `external/vkquake/meson.build` recipe exactly** (NOT the legacy
> `Shaders/compile.sh`, which hardcodes a 2017 `VULKAN_SDK=~/VulkanSDK/1.0.26.0` path and is
> dead). The meson recipe per shader is: `glslangValidator -V --quiet [--target-env vulkan1.1
> for *sops*] -o X.spv INPUT` **then `spirv-opt -Os --canonicalize-ids --strip-debug X.spv -o
> X.spv` on EVERY shader** (the `--canonicalize-ids --strip-debug` variant is taken because
> spirv-opt advertises `--canonicalize-ids`, i.e. glslang ≥ 16.0 — true here). The earlier §D
> note that *only* the sops variants need spirv-opt was wrong on two counts: the pass runs on
> all 41, and the load-bearing sops flag is `--target-env vulkan1.1`, not a spirv-opt option.
> Updated `gen-vkquake-shaders.py` to emit that exact glslang+spirv-opt pipeline (mode now
> prints `REAL (glslangValidator +spirv-opt -Os --canonicalize-ids --strip-debug)`); the
> optimize/strip pass is non-functional (shaders render identically) but makes the embedded
> bytes byte-faithful to the upstream build and ~40% smaller (e.g. `alias_frag` 3304→1832 B).
> `vkquake_shaders.c` (real optimized bytes) is committed for reproducibility.
>
> The three `/tmp` libs had been cleared, so the WHOLE chain was rebuilt host-side and
> re-verified — the deliverable's "0 undefined" is now a fact about *today*, not 2026-06-24:
> - Reconstructed the one-time host Mesa builds (`uv venv /tmp/mesa-pyenv` + mako/meson/ninja;
>   `meson setup /tmp/mesa-v3d-build` [gallium v3d] + `/tmp/mesa-v3dv-build` [+vulkan broadcom]).
>   The GL host build aborts on `v3d_resource.c`'s aarch64 `dc civac` cache-flush asm (can't
>   assemble on x86) — **expected**; the Phoenix cross-build re-emits it with the aarch64 gcc.
> - **New durability fix (`build-v3d-phoenix.py`):** that early abort left the meson
>   *custom-target* generated sources unmaterialized — `builtin_types.c` (defines the
>   `glsl_type_builtin_*` globals) + the `nir_*`/`v3d_nir_lower_algebraic.c` tables — so the
>   aux build silently skipped them and `libv3d`/`libv3dv`/`vkquake` linked with 40+
>   undefined `glsl_type_builtin_*`. Added `ensure_generated_sources()`: ninja-generates the
>   7 needed custom-target outputs (no-op if present) before the aux compile. Verified by
>   deleting the generated sources and re-running: `[gen] materialized 3 … aux] OK=43 FAIL=0`.
> - **Final link status (all host-verified today):** `libv3d-phoenix.a` 408 objs / aux 43/0;
>   `libv3dv-phoenix.a` frontend 97/0, **harness LINK PASS rc=0, 0 undefined**;
>   `build-vkquake-phoenix.py --link` → **compile 82/82, LINK OK → `/tmp/vkquake-phoenix`**
>   (22.6 MB aarch64 statically-linked ELF, `main` present, `world_vert_spv`/`alias_frag_spv`
>   real-SPIR-V `.rodata`). **No libphoenix gap:** `copysign` + `pthread_mutex_timedlock` both
>   resolve as `T` from `pl_phoenix_sdlcompat.c` (no libphoenix edit made).
> - **Task A (noop-job blocker) was already DONE + HW-validated** (2026-06-18, mesa
>   `3995663795a`): `vkCreateDevice` SUCCEEDS on real Pi4, no `binning_prolog` abort. The only
>   gap was *durability*: the v3dv fix lived as a local mesa commit absent from
>   `mesa-phoenix-port.patch` (it diffed working-tree-vs-HEAD, which skips committed changes).
>   **Regenerated the patch as `git diff <upstream-base 489aa1808f2>`** so it now captures all
>   13 Phoenix files incl. `v3dv_bo.c`/`v3dv_device.c`/`vk_image.{c,h}`. Verified it equals the
>   live diff and reverse-applies clean against the working tree.
>
> **EXACT next HW step (orchestrator, serial GPU-binary boot):** the bootable
> `sources/phoenix-rtos-devices/misc/rpi4-vkquake/` component is already wired (Makefile links
> `/tmp/libvkquake.a` + `/tmp/libv3dv-phoenix.a` + `/tmp/libv3d-phoenix.a`, 32 MB main stack,
> `--build-id`, `--allow-multiple-definition`, `-lstdc++ -lm`).
>
> **PRECONDITION — the three libs live in ephemeral `/tmp` and were observed to clear once
> mid-session.** Before the swap, if `/tmp/lib{v3d,v3dv,vkquake}*.a` are absent (check first),
> reconstruct the whole chain — this is the exact sequence run on 2026-06-25, ~10 min total:
> 1. `uv venv /tmp/mesa-pyenv --python 3.14` then `uv pip install --python
>    /tmp/mesa-pyenv/bin/python mako pyyaml packaging meson ninja`
> 2. `cd external/mesa && PATH=/tmp/mesa-pyenv/bin:$PATH meson setup /tmp/mesa-v3d-build
>    -Dgallium-drivers=v3d -Dvulkan-drivers= -Dplatforms= -Dglx=disabled -Degl=disabled
>    -Dgbm=disabled -Dvideo-codecs= -Dbuildtype=release` and the same for `/tmp/mesa-v3dv-build`
>    with `-Dvulkan-drivers=broadcom`.
> 3. `cd /tmp/mesa-v3dv-build && ninja src/broadcom/vulkan/libvulkan_broadcom.so` (succeeds).
>    For the GL dir run `cd /tmp/mesa-v3d-build && ninja -k 0` (it ABORTS on `v3d_resource.c`'s
>    aarch64 `dc civac` asm — **expected, ignore**; `-k 0` still materializes the other
>    generated sources, and `build-v3d-phoenix.py`'s `ensure_generated_sources()` ninja-makes
>    the 7 custom-target outputs it needs anyway).
> 4. `python3 tools/v3d-driver-port/build-v3d-phoenix.py` → `python3
>    tools/v3d-driver-port/build-v3dv-phoenix.py` → `python3
>    tools/vkquake-port/build-vkquake-phoenix.py --link`. Expect `libv3d` aux 43/0,
>    `libv3dv` harness LINK PASS rc=0, vkquake `82/82 compile + LINK OK` (0 undefined).
>    (`build-v3d-phoenix.py` `json.load`s `/tmp/mesa-v3d-build/compile_commands.json` at import,
>    so step 2's meson dir MUST exist before step 4.)
>
> Then to validate on HW: swap `rpi4-vkquake`
> IN / `rpi4-quake` OUT in `_targets/Makefile.aarch64a72-generic` `DEFAULT_COMPONENTS` +
> `user.plo.yaml` (only one large GL/VK binary fits `loader.disk`), `rebuild --scope core`,
> netboot, expect: vkQuake `VID_Init` brings Vulkan up (instance/device/queue — all already
> HW-proven via `v3dv_harness`), then the renderer draws with the real shaders to `/dev/fb0`
> via the winsys scanout. **Restore `rpi4-quake` after** (Friday flagship). First-frame /
> swapchain-coupling risk per §C is the on-HW Tier-1 unknown.
>
> **UPDATE 2026-06-24 — FULL LINK: 0 undefined symbols.** The whole-archive link of
> `libvkquake.a` against the V3DV ICD (`libv3dv-phoenix.a` + `libv3d-phoenix.a`) + the
> trampolines + the Phoenix platform shims now closes completely to a statically-linked
> aarch64 ELF (`/tmp/vkquake-phoenix`, 22 MB). Progression 174 → 109 → **26 → 0**:
> - **Build-infra bucket (83 syms) resolved.** `tools/vkquake-port/gen-vkquake-shaders.py`
>   emits the 41 embedded-SPIR-V arrays (`*_spv`/`*_spv_size`). The host has **no
>   glslang/glslc and no network**, so the arrays are **PLACEHOLDER minimal-valid-SPIR-V
>   headers** (link-green, NOT runnable). The generator auto-switches to REAL compiled
>   SPIR-V the moment glslang is on PATH — re-run, no other change. The 3 `vkquake_pak*`
>   are **REAL**: `Misc/vq_pak` `mkpak` + `Shaders/bintoc -c` built `embedded_pak.c`
>   (667 KB pak: gfx/maps/default.cfg). Both generated files wired into
>   `build-vkquake-phoenix.py`.
> - **Vulkan vid shim (26 syms) resolved.** `tools/vkquake-port/platform/pl_phoenix_vk_vid.c`
>   replaces `gl_vidsdl.c`: `VID_Init` brings Vulkan up the HW-proven `v3dv_harness.c` way
>   (vkCreateInstance → publish `g_vk_instance`; vkCreateDevice → publish `g_vk_device` +
>   `vulkan_globals.device`; vkGetDeviceQueue), presents to **`/dev/fb0` via the v3d winsys
>   scanout (NOT VK_KHR_swapchain — the ICD has no WSI)**, populates the `vulkan_globals`
>   fields the renderer reads (formats, sample_count, memory/feature props,
>   gfx_queue_family_index, the `vk_cmd_*` dispatch pointers), defines the genuine globals
>   (`vid modestate r_usesops prev_end_rendering_task`) + the 7 `vid_*` cvars, and stubs the
>   video menu + screenshot. Features V3D lacks (sops/RT/push-descriptor/BDA) are forced off.
> - **No libphoenix/libc gaps** surfaced this session (the prior `copysign` /
>   `pthread_mutex_timedlock` gaps were already filled by the platform shims).
> - **Runtime is NOT proven** (host-side link only, no Pi boot). The placeholder shaders
>   render nothing — REAL SPIR-V (glslang) is the gating build-infra item for any pixel.
>   The vid shim's per-frame `GL_BeginRendering`/`GL_EndRendering` are minimal-but-real
>   (submit + scanout hook); the swapchain-image / double-buffer / render-resource coupling
>   the engine's `gl_rmisc.c` expects needs on-HW Tier-1 bring-up. See §C below.
>
> **TL;DR (original 2026-06-23).** vkQuake source is now in `external/vkquake/` (cloned from
> github.com/Novum/vkQuake, master `9be3a5a`). A new build scaffold under
> `tools/vkquake-port/` compiles **all 72 portable engine TUs** (including the full
> Vulkan renderer + `tasks.c` threading + `mem.c`/mimalloc-style alloc + `hash_map` +
> `lodepng`) for aarch64-phoenix, and a generated public-`vk*` trampoline layer resolves
> the engine's direct Vulkan calls onto the ICD dispatch. A whole-archive link-drive
> leaves **174 undefined symbols** — the real gap inventory below. None are V3DV/back-end
> symbols (the ICD link closure is clean); they are the **platform-shim surface** (which
> is largely portable from `quakespasm-port`), the **SDL2 threading bodies**, the
> **embedded SPIR-V shaders** (80 of the 174 — a build-infra task needing glslang), and a
> tiny **libc gap** (`copysign`, `pthread_mutex_timedlock`).

---

## 1. Where the source is

- **Fetched** (internet was available this session): `external/vkquake/` —
  `git clone --depth 1 github.com/Novum/vkQuake`, master `9be3a5a`. Sibling of
  `external/quakespasm`, `external/mesa`, `external/linux`.
- Engine sources: `external/vkquake/Quake/*.c` (97 `.c`). Authoritative build set is the
  meson `srcs` list in `external/vkquake/meson.build`.
- Shaders: `external/vkquake/Shaders/*.{vert,frag,comp}` (41 GLSL), compiled to SPIR-V
  then `bintoc`-embedded into generated `.c`. `Shaders/Compiled/{Debug,Release}/` are
  **empty** in the checkout (only `.gitkeep`) — SPIR-V must be built (see §5, build-infra).

**Note on the plan's "no compute" assumption (`docs/inprogress/2026-06-16-...plan.md`
§1.4):** FALSE for modern master. vkQuake uses compute (`vkCmdDispatch`, the
`*_comp` shaders: lightmap updates, `cs_tex_warp`, `indirect`/`indirect_clear`),
indirect draws (`vkCmdDrawIndexedIndirect`), buffer-device-address, push descriptors,
and even RT-accel-structure debug paths. These are loaded via `fp*` pointers and gated
behind feature checks at runtime, so they are **not link symbols** and do **not** block
the host-side build. Whether V3DV supports them is a *runtime* question (out of scope —
no Pi boot). Flag for Tier-5 feature-scoping: the renderer must be steered onto a path
V3DV supports (classic raster, no compute lightmaps / no RT), likely via vkQuake cvars
(`r_rtshadows 0`, software/compute-lightmap fallbacks) or by stubbing the compute path.

## 2. The build scaffold (all new, under `tools/vkquake-port/`)

| File | Purpose |
|---|---|
| `build-vkquake-phoenix.py` | Mirrors `build-quakespasm-phoenix.py`. Probe-compiles the 72 portable engine TUs; `--link` does the whole-archive link-drive against the V3DV ICD + back-end and dumps the undefined closure. |
| `gen-vk-trampolines.py` | Parses `vulkan_core.h` prototypes; emits public `vk*` trampolines for the 75 core commands vkQuake calls directly (it does NOT use `VK_NO_PROTOTYPES`). |
| `vk_trampolines.c` | GENERATED (75 commands). Each forwards through the ICD dispatch (aliased to `v3dv_GetInstanceProcAddr` by the existing `tools/v3d-driver-port/vk_icd_link.c`). A Mesa ICD does NOT export public `vk*` symbols, so without this layer every direct core call is undefined at link. **Resolver split is load-bearing and HW-proven by `v3dv_harness.c`:** global cmds → `vkGetInstanceProcAddr(NULL,…)`; instance/phys-dev cmds → `vkGetInstanceProcAddr(g_vk_instance,…)`; **device cmds (VkDevice/VkQueue/VkCommandBuffer first arg, e.g. `vkQueueSubmit`/`vkCmd*`) → `vkGetDeviceProcAddr(g_vk_device,…)`** — instance-proc-addr returns NULL for device commands in a loader-less ICD (the harness proved calling that NULL = pc=0 abort). `g_vk_instance`/`g_vk_device` are published by the vid shim right after `vkCreateInstance`/`vkCreateDevice`. |
| `sdl-shim/SDL.h` | Grown SDL2-compat shim (superset of the quakespasm one): SDL2 threading TYPES (`SDL_mutex/cond/sem/Thread` — quakedef.h aliases SDL3→SDL2 names onto these), `SDL_MUTEX_MAXWAIT`, `SDL_GetPrefPath`, `SDL_RWsize` + RWops, `SDL_PRIs64/u64/u32`, `SDL_GetMouseState` (SDL2 `int*` form). |
| `vkq_phoenix_compat.h` | Force-included into every TU. Pulls `<arm_neon.h>` (mathlib.h auto-#defines USE_SIMD/USE_NEON on aarch64 but never includes the intrinsics header — relies on a PCH upstream); declares the libphoenix `<math.h>` gaps and the `struct ipv6_mreq` netinet gap. |
| `undefined-symbols.txt` | Saved snapshot of the 174-symbol whole-archive undefined closure (the gap inventory). |

## 3. How far compilation/link got

- **Compile: 72/72 portable engine TUs OK** (+ the trampoline TU). Net-new TUs vs
  quakespasm-port that now compile: `gl_heap hash_map lodepng mdfour mem palette pr_ext
  quakedef r_part_fte snd_umx snd_wave tasks`. The entire Vulkan renderer
  (`gl_rmain/gl_draw/gl_screen/gl_sky/gl_warp/gl_texmgr/gl_mesh/r_world/r_brush/r_alias/
  r_part/r_part_fte/...`) compiles against the Vulkan headers + shim.
- **Link (whole-archive): 174 undefined symbols** — NONE are V3DV or v3d-back-end symbols
  (the ICD closure links clean). All 174 are the platform-shim + shader + libc surface in §4.

> **Method note (important, was a trap):** a *bare* archive link (`gcc -o elf lib.a ...`)
> only pulls members crt0's `main` reference reaches — i.e. almost nothing — and
> falsely reports "1 undefined: `main`". The real closure requires
> `-Wl,--whole-archive libvkquake.a -Wl,--no-whole-archive` so every engine TU enters the
> link. `build-vkquake-phoenix.py --link` does this.

## 4. Gap inventory (the 174 undefined symbols), bucketed

### A. Platform shims — LARGELY PORTABLE from `tools/quakespasm-port/platform/` (reuse)
- **`Sys_*` (23):** `Sys_DoubleTime Sys_Printf Sys_Error Sys_Quit Sys_Sleep
  Sys_FileOpenRead/Write Sys_FileRead/Write/Seek/Close Sys_filelength Sys_fseek/ftell
  Sys_FileType Sys_mkdir Sys_ConsoleInput Sys_SendKeyEvents Sys_MemFileOpenRead
  Sys_PinCurrentThread Sys_StackTrace Sys_DebugBreak Sys_IsInDebugger`. Quakespasm-port's
  `pl_phoenix_sys.c` covers most; the file-I/O + `Sys_PinCurrentThread` (thread affinity,
  can be a no-op) + debugger hooks (no-op) are deltas.
- **`IN_*` (8):** `IN_Init/Shutdown/Activate/Deactivate/Commands/Move/ClearStates/
  UpdateInputMode`. Reuse `pl_phoenix_in.c` (`/dev/kbd0` decoder); `IN_UpdateInputMode`
  is a vkQuake addition (small).
- **`SNDDMA_*` (7):** `SNDDMA_Init/Shutdown/GetDMAPos/Submit/LockBuffer/Block/Unblock`.
  Reuse/adapt `pl_phoenix_snd.c` (the audio driver `/dev/audio0` exists — see MEMORY
  rpi4-audio). `BlockSound/UnblockSound` are small additions.

### B. SDL2 threading + path bodies — NET-NEW (I declared them in SDL.h; must implement)
- `SDL_CreateMutex SDL_LockMutex SDL_UnlockMutex SDL_CreateCond SDL_CondWait
  SDL_CondWaitTimeout SDL_CondBroadcast SDL_CreateSemaphore SDL_SemPost SDL_SemWait
  SDL_SemTryWait SDL_Delay SDL_GetCPUCount SDL_CreateThread SDL_DetachThread
  SDL_GetMouseState SDL_GetPrefPath SDL_free` (18).
- These map cleanly onto Phoenix `sys/threads.h` (`mutexCreate/Lock`, `condCreate/Wait`,
  `semaphoreCreate/Up/Down`, `beginthread`). Write `pl_phoenix_sdlcompat.c`. `tasks.c`
  (vkQuake's job system) is the main consumer — it is the one genuinely new threading
  dependency vs. quakespasm. `SDL_GetCPUCount` → return 1 (or the real core count;
  scheduler is cpu0-only per SMP findings). `SDL_GetMouseState`/`SDL_GetPrefPath` belong
  in the input/sys shims.

### C. Vulkan vid shim — NET-NEW, THE LONG POLE (replaces `gl_vidsdl.c`)
- `VID_Init VID_Shutdown VID_Restart VID_Toggle VID_Lock`; `GL_BeginRendering
  GL_EndRendering GL_WaitForDeviceIdle GL_UpdateDescriptorSets GL_SetObjectName
  GL_SynchronizeEndRenderingTask`; globals — **two distinct classes (verified against
  `undefined-symbols.txt`):** (a) GENUINELY UNDEFINED, the shim must **define** them:
  `vid modestate isDedicated r_usesops prev_end_rendering_task`; (b) TENTATIVE/COMMON
  (resolve at link, absent from the undefined list), the shim need only **initialize**:
  `vulkan_globals` + the `num_vulkan_*_allocations` / `total_device_vulkan_allocation_size`
  counters; `M_Menu_Video_f M_Video_Draw M_Video_Key`
  (video menu — can stub); `PL_GetClipboardData SCR_ScreenShot_f` (small); `net_drivers
  net_landrivers net_numdrivers net_numlandrivers` (net registry — comes from the net
  platform glue `pl_linux`/`net_bsd`, replace with the loopback-only table from
  quakespasm-port's `pl_phoenix_stubs.c`).
- This `pl_phoenix_vk_vid.c` is the bulk of the new work: it owns `vkCreateInstance`
  (then publishes `g_vk_instance`), device/queue/swapchain bring-up, the fb0 swapchain
  (plan §3), `GL_BeginRendering/EndRendering` (per-frame command-buffer + present), and
  the `vulkan_globals` table the whole renderer reads. Mirror upstream `gl_vidsdl.c`'s
  structure, swap SDL_Vulkan_* for the fb0 surface. Plan-§3 swapchain coupling applies.
- **SDL_Vulkan_* surface:** `SDL_Vulkan_CreateSurface/GetInstanceExtensions/
  GetDrawableSize/GetVkGetInstanceProcAddr/LoadLibrary` are referenced only in the
  excluded `gl_vidsdl.c`; the replacement vid shim calls our fb0-surface code directly
  and never needs them. (They are NOT in the undefined list precisely because gl_vidsdl
  is excluded.)

### D. Embedded SPIR-V shaders + pak — BUILD-INFRA (80 + 3 of the 174)
- 80 `*_spv` / `*_spv_size` extern arrays (`world_vert_spv`, `alias_frag_spv`,
  `*_comp_spv` lightmap/compute, `postprocess_*`, `sky_*`, `showtris_*`, `wboit_*`, ...)
  and 3 `vkquake_pak*` (the embedded base pak via `mkpak`).
- Upstream builds these by: GLSL → SPIR-V via **glslang** (+ `spirv-opt
  --canonicalize-ids` for the `sops` variants), then `bintoc` (host tool,
  `Shaders/bintoc.c`) emits a `<name>.spv.c` with `const unsigned char name_spv[]` +
  `int name_spv_size`. `embedded_pak.c` similarly from `mkpak` (`Misc/vq_pak/mkpak.c`).
- **glslang / spirv-opt are NOT on this host's PATH** → this is a build-infra prerequisite:
  install/build glslang + spirv-tools (host tools), compile the 41 shaders, run bintoc,
  add the generated `.spv.c` to the build. `bintoc`/`mkpak` themselves compile with the
  host cc (native). Until then the renderer links only with stubbed/zero shader arrays
  (which would render nothing — so this gates any on-HW pixel, but NOT the link scaffold).

### E. libc gaps — SMALL (upstreamable to libphoenix)
- `copysign` — libphoenix `<math.h>` lacks it (has `round/roundf/log2/trunc`; lacks
  `copysign`, also `rint/remainder/log2f/fmin/fmax` were missing as *declarations* but
  DO resolve from libm at link; only `copysign` is truly unresolved at link). Add to
  libphoenix math (the upstreamable fix), or provide in a shim `.c`.
- `pthread_mutex_timedlock` — libphoenix lacks it; reached via `SDL_SemWaitTimeout`/
  tasks.c timeouts. Either implement in `pl_phoenix_sdlcompat.c` over Phoenix cond-timed-
  wait, or add to libphoenix pthread. (Matches the libc-gap-filling pattern the
  quakespasm/X11 ports used.)
- `main` — entry point lives in the excluded `main_sdl.c`; provided by the
  `pl_phoenix_main.c` shim (reuse quakespasm-port's, adapt the init order for the Vulkan
  vid path).

## 5. Concrete next steps (in dependency order)

1. **Write `pl_phoenix_sdlcompat.c`** — the 18 SDL2 threading/path bodies over
   `sys/threads.h` + `pthread_mutex_timedlock`. Smallest, unblocks `tasks.c` at link.
   (Bucket B + the pthread gap in E.)
2. **Port the platform shims** from `tools/quakespasm-port/platform/` — `pl_phoenix_sys.c`
   (+ file-I/O + no-op thread-pin/debugger), `pl_phoenix_in.c` (+ `IN_UpdateInputMode`,
   `SDL_GetMouseState`), `pl_phoenix_snd.c` → `SNDDMA_*`, `pl_phoenix_main.c`,
   `pl_phoenix_stubs.c` (loopback `net_drivers` table). (Buckets A + parts of C/E.)
3. **Add `copysign` to libphoenix math** (or a shim) — one symbol. (Bucket E.)
4. **Build-infra: SPIR-V shaders** — get glslang + spirv-opt on the host, compile the 41
   shaders, run `bintoc`, generate `embedded_pak.c` via `mkpak`, add the `.spv.c` to the
   build. (Bucket D — needed before any real frame, parallelizable with 1–3.)
5. **Write the Vulkan vid shim `pl_phoenix_vk_vid.c`** — the long pole (Bucket C):
   instance/device/queue/swapchain bring-up, the fb0 swapchain (plan §3),
   `GL_BeginRendering/EndRendering`, the `vulkan_globals` table, publish `g_vk_instance`.
   Steer the renderer onto a V3DV-supported feature path (no compute lightmaps / no RT —
   see §1) via cvars/stubs. This is where the plan's Tier-4/5 risk concentrates.
6. **Link to a complete ELF**, then hand to the main agent for on-HW Tier-1 bring-up
   (`vkCreateInstance`/`vkCreateDevice` already HW-proven via `v3dv_harness`; vkQuake adds
   the renderer + swapchain on top).

## 6. Reuse vs. net-new summary (vs `quakespasm-port`)

- **~Free reuse:** engine core, net (loopback), the `Sys_*`/`IN_*` shim skeletons,
  `pl_phoenix_main/sys/in/snd/stubs.c`, the SDL RWops mapping.
- **Net-new:** the SDL2 *threading* bodies (`tasks.c` is vkQuake-only), the Vulkan vid
  shim + fb0 swapchain (replaces the GL `pl_phoenix_vid.c`/`glctx`), the trampoline layer
  (done — generated), the SPIR-V shader build pipeline, the V3DV feature-scoping for
  compute/RT paths.

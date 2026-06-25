# vkQuake on-HW Tier-1 — Vulkan initializes on the V3D; crash localized after VID_Init

Date: 2026-06-25 (unattended night run)
Variant: netboot; GPU-binary swap (rpi4-vkquake IN, rpi4-quake OUT) — swap since REVERTED, flagship restored.
Log: `artifacts/rpi4b-uart/…-netboot-vkquake-hw-tier1.log`

## Result: significant progress — Vulkan comes up on real hardware

The host-built `vkquake-phoenix` (22.5 MB aarch64, real SPIR-V shaders) was bundled and
boot-launched. On HW it reached **further than the Tier-1 goal**:

```
vkquake: main() entered (argc=1)
Initializing vkQuake (Phoenix/V3DV)
vkquake: found /nfstest/id1/pak0.pak after 18 tries (basedir=/nfstest)   <- gamedata located (NFS)
vkvid: VID_Init (Phoenix/V3DV fb0 scanout, no WSI)
vkvid: vkCreateInstance -> 0
vkvid: vkEnumeratePhysicalDevices -> 0 count=1
vkvid: vkCreateDevice -> 0
vkvid: fb0 1920x1080 pitch=7680 pa=0x3d3b2000 size=8294400
vkvid: VID_Init done (1920x1080, device=…036c5d58 queue=…03606cb0)        <- Vulkan UP on HW
Exception #36: Data Abort (EL0)                                          <- crash AFTER VID_Init
in thread 28, process "rpi4-vkquake" (PID: 18)
```

**What works on HW:** vkCreateInstance / EnumeratePhysicalDevices / CreateDevice all rc=0,
the V3DV queue is created, the fb0 1920×1080 scanout surface is mapped, and gamedata
(`pak0.pak`) is found over NFS. The whole Vulkan-init path is proven on the real V3D.

## The crash — a specific, debuggable next step

```
pc=0x00000000004c5d20  lr=0x00000000004c5d18
esr=0x92000007  far=0x0000000000000070   psr=0x20000100
```

- `esr=0x92000007` → Data Abort from EL0, **translation fault level 3** (DFSC=0b000111).
- `far=0x70` → a **NULL-pointer struct dereference** (NULL + 0x70).
- **DEFINITIVELY LOCALIZED (2026-06-25, symbol-table range lookup, not just addr2line):** the fault
  PC `0x4c5d20` lies inside **`SV_LocalSound`** (symbol `0x4c5c20` + size `0x10c`). The faulting
  line is `sv_main.c:1403` `MSG_WriteByte(&client->message, svc_localsound)` — **`client` is NULL**
  and `client->message` (a `sizebuf_t` at offset **0x70** in `client_t`) is dereferenced → `far=0x70`.
- **This OVERTURNS the build-time "first-frame / swapchain-surface" prediction.** It is NOT a render
  or WSI bug — Vulkan init fully succeeded. It is a **sound / init-ordering bug**: vkQuake's startup
  calls the SERVER-side local-sound (`SV_LocalSound`) with no connected client (`host_client` NULL)
  right after VID_Init.

## Next session (corrected — chase the sound/init path, NOT the swapchain)
1. Find why `SV_LocalSound` runs with a NULL `client` during vkQuake init — likely a UI/intro sound
   routed through the server path before `sv`/`host_client` exist, or an init-order difference vs
   the working GL quake (which doesn't hit this). Compare the two engines' Host_Init → S_Init →
   first-sound ordering.
2. Fix = guard the NULL `client` in `SV_LocalSound` (early-return if `!client`/`!sv.active`), or
   correct the call site / init order. A debug (`-g`) build will name the exact caller.
3. Then re-run the GPU-binary swap (rpi4-vkquake) and netboot — expect to get past 0x4c5d20.

(Superseded: an earlier draft of this doc pointed the next session at the no-WSI swapchain shim —
that was the build-time prediction, now disproven by the symbol-table localization above. The
authoritative next step is the SV_LocalSound NULL-client sound/init path described above.)

## FIX APPLIED (2026-06-25, host-side) — NULL-client guard in SV_LocalSound

- **Caller analysis:** the ONLY path into `SV_LocalSound` is `PF_sv_localsound` (a QuakeC builtin,
  `pr_cmds.c:1798`), which calls `SV_LocalSound (&svs.clients[entnum - 1], sample)`. Its guard
  `entnum < 1 || entnum > svs.maxclients` was *passed* (so `svs.maxclients >= 1` and `entnum == 1`),
  yet `client == NULL` → therefore `svs.clients == NULL`: the server local-sound builtin ran during
  startup before the client array was allocated (`Host_InitLocal`, `host.c:317`). The menu/intro UI
  correctly uses the CLIENT-side `S_LocalSound` (`snd_dma.c`), which is unaffected; only the
  server-path builtin faults. The GL flagship (`external/quakespasm`) has byte-identical
  `SV_LocalSound`/`PF_sv_localsound` and simply never triggers the builtin at this init point.
- **Fix:** `external/vkquake/Quake/sv_main.c` — early `if (client == NULL) return;` at the top of
  `SV_LocalSound` (writing a server datagram to a non-existent client is invalid regardless). This is
  the guard directly proven by `far=0x70` (client is *exactly* NULL). Durable as the tracked patch
  `tools/vkquake-port/vkquake-phoenix-port.patch` (external/vkquake is a gitignored clone).
- **Build:** `python3 tools/vkquake-port/build-vkquake-phoenix.py --link` → 82/82 TUs OK, **LINK OK
  (0 undefined)** → `/tmp/vkquake-phoenix`.
- **Honest status:** this is a **get-past-it guard, not a root-caused fix.** It provably stops the
  observed Data Abort, but it does NOT explain *why* the server-side localsound builtin runs at
  startup with no spawned server / unallocated client array (a Phoenix init-ordering difference vs.
  the GL engine that static reading can't reconcile — `svs.maxclients` set while `svs.clients` NULL).
  The HW re-run past 0x4c5d20 is what reveals whether a deeper init-order issue remains.

This is the vkQuake capstone's inflection point: Vulkan init is DONE on real HW; the remaining
blocker is a NULL-client crash in SV_LocalSound during startup (a sound/init-ordering issue, not
the render/present path). The GL flagship (rpi4-quake) is unaffected and restored.

## HW RE-TEST with the guard (2026-06-25) — GUARD DID NOT TAKE (build-cache issue)

Bundled the guarded rpi4-vkquake (force-relinked against /tmp/libvkquake.a, 03:33) + netbooted.
**Result: IDENTICAL crash — `pc=0x4c5d20`, `far=0x70`, same SV_LocalSound NULL deref.** An early
`if (client==NULL) return;` would shift the function's code, so an identical fault PC proves the
running binary is STILL PRE-GUARD. Vulkan init again fully succeeded (VID_Init done 1920×1080);
the crash is byte-identical.

**Diagnosis: `tools/vkquake-port/build-vkquake-phoenix.py` reused a CACHED `sv_main.o`** — the guard
is in the source (clone + tracked patch `vkquake-phoenix-port.patch`) but was never recompiled into
`/tmp/libvkquake.a` (the relink pulled the stale object). Confirmed mechanism: the build script does
not invalidate object files when the patched source changes.

**Next session (precise):** force a clean recompile of `sv_main.c` in the vkQuake build (delete the
cached `sv_main.o` / clean the vkquake build dir, or add a source-mtime check to the build script),
rebuild `libvkquake.a`, verify the guard is in the object (`objdump` the new `sv_main.o` shows the
early-return + the function size grew), THEN swap rpi4-vkquake in + netboot. Only then is the guard
actually under test. The flagship was restored after this test (loader.disk back to the psh/no-GPU
config; swap files match committed state).

## Progress log — 2026-06-25 morning (six blockers cleared; now at shader #10)

vkQuake advanced enormously today; each HW cycle cleared one blocker:
1. SV_LocalSound NULL-client guard (sv_main.c).
2. VID_Init's dropped renderer-init block restored (R_InitMeshHeap etc.) — mesh heap created.
3. `--whole-archive` libvkquake+V3DV (standalone build-vkquake-phoenix.py + the rpi4-vkquake
   component Makefile) — 13 NULL `vk_common_*` dispatch slots fixed → **VID_Init now COMPLETES**.
4. No-WSI render resources implemented (pl_phoenix_vk_vid.c): fb0 scanout image + UI render pass +
   framebuffer + command pool/buffer + SCBX_GUI cb context + R_CreateBasicPipelines; GL_BeginRendering/
   GL_EndRendering (submit + scanout-as-present). `vkvid: scanout image 1920x1080 bound`.
5. Render-resource steps all pass (`rr: imageview/renderpass/framebuffer/cmdpool/cmdbuf/gui-cbx ok`).

**Current blocker (shader #10):** `R_CreateShaderModules` calls `vkCreateShaderModule` per embedded
SPIR-V. The per-shader probe shows **9 modules create fine** (world_vert/frag/oit_frag, alias_vert/
frag/alphatest_frag/oit_frag/alphatest_oit_frag) then **crashes on `md5_vert`** (size=5228, valid
code ptr) — Exception #32 pc=0/lr=0 (blr to NULL fptr), NO `vktramp: UNRESOLVED`. Static trace
(commit 06c4b26) proved the whole vkCreateShaderModule→vk_common_CreateShaderModule→vk_alloc2→
vk_shader_module_init→blake3 chain resolves to real symbols + device->alloc is populated (9
successes confirm). So it is a **runtime NULL fptr specific to the md5_vert path** — candidates:
(a) `md5_vert`'s SPIR-V trips a v3dv/Mesa path with a NULL callback; (b) an earlier alloc corrupted
heap metadata (a fn-ptr-bearing struct) that the 10th alloc dereferences; (c) the md5* shader array
wiring (size/code) in vkquake_shaders.c.

**Next step (focused session):** md5* shaders are 2021-rerelease MD5-model shaders, **UNUSED by
shareware id1 content**. Quickest path to a first on-screen 2D/console frame = SKIP the md5*/
rerelease shader-module creation in R_CreateShaderModules (guard them out) and retest — if it then
reaches the 2D frame, the issue was md5-specific (defer rerelease support); if it crashes on the
next shader, it's heap corruption from an earlier alloc (root-cause the allocator). Either way the
2D frame is close. Beyond shaders: the world/3D render pass + pipelines are still stubbed (2D-first),
per the render-resource notes above.

**Status:** vkQuake iteration PAUSED here to advance the broader clean-repo goals; flagship restored.
All fixes committed (vkquake-phoenix-port.patch + the rpi4-vkquake Makefile + pl_phoenix_vk_vid.c).

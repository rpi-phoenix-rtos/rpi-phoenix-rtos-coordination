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

This is the vkQuake capstone's inflection point: Vulkan init is DONE on real HW; the remaining
blocker is a NULL-client crash in SV_LocalSound during startup (a sound/init-ordering issue, not
the render/present path). The GL flagship (rpi4-quake) is unaffected and restored.

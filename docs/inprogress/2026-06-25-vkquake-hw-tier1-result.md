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
- `far=0x70` → a **NULL-pointer struct dereference** (NULL + 0x70 offset).
- Fires **immediately after `VID_Init done`** — i.e. the renderer's first post-init step,
  exactly the "first-frame / swapchain-surface coupling" risk flagged at build time. The
  no-WSI shim likely returns NULL for a surface/swapchain object that the renderer then
  dereferences at field +0x70 (or an early `S_Init`/resource struct).
- `addr2line` on the (non-debug) ELF pointed near `SV_LocalSound` (sv_main.c:1403) — treat as
  a weak hint only; confirm with a debug build or a careful objdump of `0x4c5d20` next session.

## Next session (focused vkQuake bring-up)
1. Build vkquake-phoenix with frame-pointer/debug line info (or objdump `0x4c5d20`) to name the
   exact function + the NULL object dereferenced at +0x70.
2. Inspect the no-WSI present/surface shim (`tools/vkquake-port/platform/pl_phoenix_vk_vid.c`)
   and vkQuake's `R_Init`/`GL_*`/swapchain-equivalent path for an unset pointer the engine
   assumes valid after VID_Init.
3. Re-run the GPU-binary swap (rpi4-vkquake IN, see Makefile.aarch64a72-generic + user.plo.yaml
   comments) and netboot; expect to get past 0x4c5d20 toward a first rendered frame on HDMI.

This is the vkQuake capstone's inflection point: init is done, the remaining work is the
engine→V3DV render/present path. The GL flagship (rpi4-quake) is unaffected and restored.

# Porting Mesa's v3d gallium driver to Phoenix (GLQuake Path C)

Chosen route (2026-06-11): run Mesa's real v3d driver on Phoenix so it generates
correct control-lists + shaders for GL calls at runtime, validated on the actual
Pi4 V3D 4.2. This is the genuine "full GPU based on Mesa" path and the only one that
reaches GLQuake. The hardware (render pipeline) and the host shader compiler are
already proven; this is about running the driver itself on-device.

## Why this is tractable: the driver already abstracts its kernel seam

ALL kernel interaction goes through one call, `v3d_ioctl(fd, DRM_IOCTL_V3D_*, &arg)`
(gallium/drivers/v3d/{v3d_bufmgr.c,v3d_job.c,v3d_screen.c}), which already dispatches
between **DRM** and the **v3d simulator**. Adding a **Phoenix backend** is therefore
architecturally supported, not a hack. The full ioctl surface for a triangle is small:

| ioctl | Phoenix backend = proven scout primitive |
|---|---|
| `DRM_IOCTL_V3D_CREATE_BO` | `mmap(MAP_CONTIGUOUS\|MAP_UNCACHED)`; track handle→{va,pa,size} |
| `DRM_IOCTL_V3D_GET_BO_OFFSET` | assign a GPU VA + map it in the V3D MMU flat PT; return VA |
| `DRM_IOCTL_V3D_MMAP_BO` | return the BO's CPU va (already mapped) |
| `DRM_IOCTL_V3D_SUBMIT_CL` | parse submit{bcl_start,end, rcl_start,end, qma/qms/qts}; drive CT0/CT1 QBA/QEA + FLDONE/FRDONE sync + L2T flush |
| `DRM_IOCTL_V3D_WAIT_BO` | wait FRDONE (submit is synchronous in our backend) |
| `DRM_IOCTL_V3D_GET_PARAM` | return V3D-4.2 devinfo (ver=42, vpm/qpu from the real IDENTs) |
| `GEM_CLOSE/OPEN/FLINK` | not needed (no BO sharing) |

`rpi4-v3d-scout` already implements every one of these primitives (BO alloc+va2pa,
MMU bring-up, CT0/CT1 submit + sync, L2T flush, devinfo from the real IDENTs). The
scout is the prototype winsys; this port wraps it behind `v3d_ioctl`.

## Phases

1. **Cross-compile feasibility** (the big unknown). Build Mesa's v3d driver + its deps
   (gallium auxiliary, NIR, the v3d compiler, util) for **aarch64a72-phoenix**. Mesa
   assumes Linux/POSIX/DRM in places; find what Phoenix is missing (headers, syscalls,
   pthread, mmap flags) and decide vendored-subset vs shim. Start: a meson cross-file
   for the Phoenix toolchain (sysroot, aarch64-phoenix-gcc), `gallium-drivers=v3d`,
   no GL/EGL/DRM platforms, and see how far it gets.
2. **Phoenix `v3d_ioctl` backend** (the winsys). Implement the ~6 ioctls above against
   the scout primitives. This is the de-risked part. Likely a `v3d_phoenix.c` next to
   `v3d_simulator.c`, selected when not-DRM-not-sim.
3. **Minimal gallium harness on HW.** A small Phoenix program that creates a `pipe_screen`
   (v3d) + `pipe_context`, allocates an RT, sets a trivial VS/FS, and `draw_vbo`s a
   triangle → the real driver builds the CL+shaders → our backend submits to the V3D →
   triangle on HDMI. This is the milestone that proves the whole port (and gives a
   CORRECT triangle, since Mesa generates the shaders/CLs — fixing the hand-built-NIR
   `mov tlb,0` problem by construction).
4. **GL/EGL frontend → GLQuake.** Add the GL state tracker (st/mesa) or a lighter GL
   veneer, an EGL/surfaceless context bound to /dev/fb0, then build Quakespasm's GL
   renderer against it. Stage `pak0.pak`, wire USB-HID input.

## Risks / open questions
- **Cross-compiling Mesa for Phoenix** is the dominant risk (Linux/DRM assumptions,
  Phoenix libc gaps, the build system). Phase 1 is a feasibility spike; if a full Mesa
  cross-build is intractable, fall back to vendoring just the v3d driver + gallium-aux +
  NIR + compiler into a Phoenix-built static lib.
- Memory model: the driver assumes a unified GPU VA space the kernel manages; our MMU
  flat-PT + GPU-VA allocator must match what `GET_BO_OFFSET` promises and what the CLs
  reference. The scout's MMU is the basis; may need a real allocator (not fixed VAs).
- Threading/fences: the driver uses DRM syncobjs; our synchronous SUBMIT_CL + WAIT_BO
  can stub these initially.
- BO mmap: driver expects `MMAP_BO` to give a CPU pointer; our BOs are already CPU-mapped
  (uncached) — coherent, no flush needed (the scout pattern).

## Proven foundation (reused wholesale)
- HW render pipeline: `manifests/2026-06-11-v3d-render-clear.md` (devices 22eb868).
- Power-on + MMU + CLE: `manifests/2026-06-11-v3d-mmu-cle-foundation.md`, scout `dedb44c`.
- Host Mesa build + shader compile: `tools/v3d-shader-tool/` + this doc's host build recipe.

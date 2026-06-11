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

## Phase 1 findings (2026-06-11, cross-compile spike)

- **Toolchain:** `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc-14.2.0` (GCC 14) +
  sysroot `.toolchain/aarch64-phoenix/aarch64-phoenix/`.
- **Spike method:** reuse the host Mesa build's per-file flags
  (`/tmp/mesa-v3d-build/compile_commands.json`) and recompile representative files with
  `aarch64-phoenix-gcc -fsyntax-only` to surface libc/header gaps (the real cross risk;
  the `-DHAVE_*` are host-detected so some are wrong for Phoenix, but missing-symbol
  errors are the signal).
- **First gap (confirmed):** `ralloc.c`→`u_math.h` fails on `lrintf`/`fmax`/`fmin` —
  **Phoenix's libm lacks C99 math functions** Mesa relies on (also hit `llroundf` in the
  host shader harness). Bounded + shimmable: provide a small `phoenix-mesa-libm` shim
  (`lrintf`=`(long)rintf`, `fmax`/`fmin`/`llround*`, etc.). NOT a structural blocker —
  Mesa core (`ralloc`, `u_math`) otherwise compiles.
- **FEASIBILITY CONFIRMED (2026-06-11):** with the compat shim
  (`tools/v3d-driver-port/phoenix_mesa_compat.h`), **all representative files cross-compile
  clean (`-fsyntax-only`, rc=0) for aarch64-phoenix** — spanning Mesa util core (`ralloc`,
  `hash_table`, `u_math`, `os_file`, `u_debug`), central NIR (`nir.c`), AND the v3d compiler
  (`vir.c`). The full gap profile is bounded + shimmed: C99 math (lrintf/fmax/fmin/llround*),
  `posix_memalign`, GNU `qsort_r`, C11 `static_assert`, `pthread_barrier_t`. **No structural
  blockers** (mmap/pthread-core/etc. are fine). The dominant Path-C risk (cross-compiling Mesa
  for a non-Linux target) is therefore tractable via vendor-subset + this shim. `qsort_r` /
  pthread-barrier real implementations are port tasks (declared/stubbed now).
- **Verdict so far:** Mesa code is largely portable; Phoenix libc/libm has bounded gaps
  needing a compat shim. The **vendor-subset + compat-shim** approach (build the v3d
  driver + gallium-aux + NIR + compiler + util as a Phoenix static lib via the project
  toolchain, not Mesa's meson) is the working plan. The v3d gallium driver's hard
  `dep_libdrm` build-coupling also argues against a full meson cross-build.
- **Next:** build the libm/compat shim; push the cross-compile across the v3d-subset file
  set to enumerate the full gap profile (pthread, mmap flags, `getrandom`, `os_*`, etc.);
  then assemble the Phoenix-built static lib + the `v3d_ioctl` Phoenix backend (scout
  primitives) + the gallium triangle harness.

## Proven foundation (reused wholesale)
- HW render pipeline: `manifests/2026-06-11-v3d-render-clear.md` (devices 22eb868).
- Power-on + MMU + CLE: `manifests/2026-06-11-v3d-mmu-cle-foundation.md`, scout `dedb44c`.
- Host Mesa build + shader compile: `tools/v3d-shader-tool/` + this doc's host build recipe.

## Phase 1 full gap scan (2026-06-11): core is portable, failures are peripheral

Cross-compiled ALL 364 util + NIR + broadcom .c files (`aarch64-phoenix-gcc
-fsyntax-only` + shim, host flags). **331 compile clean; 33 fail — none in the core
v3d compiler / NIR / CLE / packing logic.** The failures categorize as:
- **Exclude from the vendored subset (not needed for CL+shader generation):**
  DRM (`xf86drm.h`, ~5), disk cache + `fossilize_db` (dlfcn, memfd, inotify, ftw,
  build_id), compression (`zlib.h`, `compress.c`/`crc32.c` for the disk cache),
  XML (`expat.h`), x86 SIMD (`cache_ops_x86*.c`, `streaming-load-memcpy.c`,
  `smmintrin.h`, `__builtin_ia32_*` — aarch64 won't build these anyway), locale
  (`*_l`, `newlocale`), process affinity (`cpu_set_t`, `pthread_*affinity_np`),
  `secure_getenv`, `_SC_PHYS_PAGES`, `dladdr`, `open_memstream`.
- **Trivial shim:** `rintf`/`rint` (3 files) — added to the compat shim.

So the vendored Phoenix subset = the clean core files + a few stubbed peripherals
(disk_cache→no-op, os_misc/os_time→Phoenix equivalents, the DRM winsys→our
`v3d_ioctl` Phoenix backend). **Phase-1 cross-compile feasibility is fully
characterized and positive.** NEXT: assemble the subset file list + build a real
`libv3d-phoenix.a` (link surfaces undefined symbols `-fsyntax-only` misses), then
the `v3d_ioctl` backend + gallium triangle harness.

## Phase 1 driver+aux scan (2026-06-11): DRM coupling is ONE header

Cross-compiled gallium/drivers/v3d + gallium/auxiliary (182 files): 45 fail, and the
v3d DRIVER files (v3dx_draw/emit/rcl/state/tfu, v3d_context/screen/program/job/
bufmgr/resource/...) fail almost entirely on a SINGLE cause: `#include "xf86drm.h"`
(libdrm, 30 hits) — they need the `DRM_IOCTL_V3D_*` numbers + `drm_v3d_*` ioctl
structs. The DRM coupling is therefore CONCENTRATED in one header dep, not scattered.
Remaining: more math (`llrint`/`lround` → added to shim), and excludable/stubbable
headers (`tr_util.h`, `u_tracepoints.h` generated traces → stub; `libsync.h` sync-file
→ stub; `dlfcn.h`/disk_cache → exclude).

**=> The DRM seam = (a) vendor the kernel UAPI `v3d_drm.h` (defines the ioctl structs +
numbers) + (b) a tiny `xf86drm.h`/libdrm shim whose `drmIoctl()` dispatches into our
Phoenix `v3d_ioctl` backend = the proven scout primitives.** The winsys backend and the
DRM-UAPI/libdrm-shim are the SAME Phase-2 work. No structural blockers.

### Phase 1 COMPLETE — characterization summary
- Core (util/nir/broadcom, 331/364): portable with `phoenix_mesa_compat.h` (math + posix_memalign + qsort_r + static_assert + pthread_barrier).
- Driver (gallium v3d + aux): portable once the DRM-UAPI header + libdrm/xf86drm shim (= the v3d_ioctl Phoenix backend) are provided; + math (llrint/lround) + stub tr_util/u_tracepoints/libsync; exclude disk_cache.
- No structural incompatibilities. Cross-compile is tractable end-to-end.
- **NEXT = Phase 2:** vendor `v3d_drm.h` + write `xf86drm.h`/`libdrm` shim + the `v3d_ioctl` Phoenix backend (CREATE_BO/GET_BO_OFFSET/MMAP_BO/SUBMIT_CL/WAIT_BO/GET_PARAM = scout primitives); then assemble `libv3d-phoenix.a` from the subset file list and link a gallium triangle harness.

## Phase 2 started (2026-06-11): UAPI vendored + winsys backend written

- Vendored kernel UAPI `tools/v3d-driver-port/v3d_drm.h` (from external/linux). Confirmed
  `drm_v3d_submit_cl` maps EXACTLY onto the scout submit: `bcl_start/end`→CT0 QBA/QEA,
  `rcl_start/end`→CT1 QBA/QEA, `qma/qms/qts`→CT0 tile-alloc/state. So SUBMIT_CL = the
  scout's bin+render path, parameterized.
- Wrote `tools/v3d-driver-port/v3d_phoenix_winsys.c` — the Phoenix backend the libdrm
  shim's `drmIoctl()` dispatches into: BO table, GPU-VA bump-allocator + V3D MMU flat-PT
  map (CREATE_BO/GET_BO_OFFSET/MMAP_BO), the real SUBMIT_CL (CT0/CT1 + FLDONE/FRDONE +
  L2T flush — scout primitives), GET_PARAM (real V3D-4.2 IDENT values), WAIT_BO=noop
  (synchronous). All param/ioctl/struct names verified against the vendored UAPI.
- Libdrm surface to shim is small: `drmIoctl`→`phoenix_v3d_ioctl`; `drmSyncobj*` stubbed
  (synchronous, no fences); `drmPrime*` not needed.
- STATUS: winsys design crystallized + grounded, but **pending integration** — needs the
  cross-built `libv3d-phoenix.a` + a gallium harness + the `xf86drm.h`/libdrm shim before
  it compiles/runs. Also needs the V3D power-on done first (scout `v3d_powerOn`) and the
  MMU PT may need to grow beyond one page for many BOs.
- NEXT: assemble the v3d-subset file list + build `libv3d-phoenix.a` (Phoenix toolchain +
  compat shim, exclude peripherals); write the minimal `xf86drm.h`/libdrm shim; then a
  gallium `pipe_context` triangle harness → CORRECT Mesa-generated triangle on HW.

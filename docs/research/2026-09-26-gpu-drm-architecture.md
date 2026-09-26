# High-performance, general GPU support for Phoenix-RTOS on the Raspberry Pi 4

*Research and design, 2026-09-26. No code changed, no Pi cycle run, nothing committed.*
*Evidence tags used throughout: **[read]** = read in source or a document at the cited place;
**[measured]** = a number this project measured on hardware (with its source); **[cited]** = a
published external number (URL in §2/§3); **[inferred]** = my reasoning, not verified.*

---

## 0. Executive summary and recommendation

**Where we are.** The port already runs unmodified-in-spirit Mesa (v3d gallium GL/GLES 3.1 and
v3dv Vulkan) on V3D 4.2, and it already has, *inside each application*, a userspace
re-implementation of the Linux `v3d` DRM uapi (`phoenix_v3d_ioctl`, 3620 lines,
`sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c`). It also has a
HW-proven multi-client GPU server (`/sbin/rpi4-v3d`, `/dev/v3d-srv`, M0–M3c on 2026-08-22) that
nothing launches except the glamor X desktop. Five games render correctly full-screen. What limits
performance and generality is not Mesa and not the shader compiler — it is the **plumbing around
them**:

1. **Every submit is synchronous and busy-waited** (`ioc_submit_cl`, `v3d_phoenix_winsys.c:2399`
   onward: TLB flush, three waited L2T flushes, spin on FLDONE, then spin on FRDONE). CPU and GPU
   never overlap, and the binner of job N+1 never overlaps the renderer of job N. Linux gets both
   overlaps for free from its separate bin/render queues. **[read]**
2. **There is no display driver.** The screen is the firmware's framebuffer (fixed mode, one plane);
   "page flip" is `SET_VIRTUAL_OFFSET` panning inside a 3×-tall firmware buffer, with no vblank
   event, so there is no frame pacing and triple buffering is a *workaround* for not knowing when a
   flip latched. **[read]** (`v3d_phoenix_power.c:625`, `PHOENIX-RTOS-RPI4-CHANGES.md:200`)
3. **There is no buffer-sharing primitive.** Buffers cross processes as a raw physical address
   mapped with `MAP_PHYSMEM` (no owner check, no refcount, no revocation). So the X server copies
   every frame through the CPU (glamor → `glReadPixels` → shadow → `write()` to `/dev/fb0`,
   ~77 ms for a full 1080p present), and a windowed GL client copies GPU → CPU → socket → CPU → GPU
   (14.2 fps at 640×480). **[measured]** (KNOWN-ISSUES G1; `project_x11_glamor_whole_screen_present`)

Raspberry Pi OS is fast for exactly the three opposite reasons: an IRQ-driven job scheduler with
concurrent bin/render queues, a real KMS driver whose planes scan out GPU buffers directly with
vblank-timed flips, and dma-buf zero-copy between GPU, display, X server and compositor (§2).
Against it, Quake III is **in the same range** (Phoenix quake3e ~54 fps gameplay at 1080p [measured]
vs Pi OS 36 fps ioq3 timedemo @1080p in 2019 / 62.5 fps quake3e in 2024 [cited] — different
engines, modes and eras, so not a real comparison; §5.3 generates one), but we are **~3.5–4× behind on
SuperTuxKart** (8.5 fps [measured] vs ≥30 fps [cited]) and **an order of magnitude behind on any
windowed or composited GPU use**.

**Recommendation — build a Phoenix-native, DRM-*compatible* stack, not a DRM port:**

```
 apps: Mesa v3d/v3dv (unmodified winsys-free), GBM, EGL, Xorg modesetting+glamor, a Wayland
       compositor, SDL2 KMSDRM, Vulkan WSI (display / xcb / wayland)
        |  libdrm API (drmIoctl, drmMode*, drmPrime*, drmSyncobj*, drmHandleEvent)
 libdrm-phoenix  (MIT; the Genode pattern: marshal DRM ioctls into Phoenix messages)
        |  msgSend (small descriptors)       |  mmap(fd) of a shared BO object (no copies)
 +--------------------------+     +-------------------------------------+
 | rpi4-v3d  "render node"  |     | rpi4-kms  "card node"                |
 | V3D IRQ, bin/render/TFU/ |<--->| firmware-KMS planes (SET_PLANE),     |
 | CSD queues, GPU MMU, BOs,|seqno| SMI vblank IRQ, atomic commit, flips,|
 | fences/syncobjs          |page | dumb/contiguous scanout BOs, events  |
 +--------------------------+     +-------------------------------------+
        |  kernel: one new primitive — an exportable memory object named by an oid
```

* **API boundary: the DRM uapi *as a C library API*, not as a kernel ABI.** Keep Mesa's
  `drmIoctl()` entry points (the port already does this in-process); move their implementation
  into servers behind a libdrm replacement. This is what Genode does and what this port's own
  `libv3d-client` already proved (M1: bit-exact GL through the daemon). It lets us track upstream
  Mesa with *less* patching than today and unlocks GBM/EGL/X modesetting/Wayland/SDL KMSDRM/Vulkan
  WSI unmodified. The wire format stays Phoenix-native (flat descriptors in `msg_t`), because a
  Phoenix server cannot chase client pointers the way a kernel ioctl handler does.
* **Render server = the existing `rpi4-v3d` daemon, turned asynchronous.** V3D interrupt (GIC SPI 74)
  via Phoenix `interrupt()` (proven by `bcm-genet.c:1989`), separate bin/render/TFU/CSD queues,
  per-queue seqnos published in a read-only shared page, syncobjs, BO lifetime tied to fences. This
  is the single largest performance lever and needs no display work.
* **Display server = new `rpi4-kms`, built first on the firmware's own plane interface.** The
  firmware exposes `RPI_FIRMWARE_SET_PLANE` (tag `0x00048015`): up to 8 planes per display, each with
  an arbitrary DMA address, source/destination rectangles (scaling), layer, alpha and transform, plus
  a **vblank interrupt delivered through the SMI block** (`0xfe600000`, GIC SPI 112). **[read]**
  (`external/linux/drivers/gpu/drm/vc4/vc4_firmware_kms.c:62-107,262-268,1225`;
  `arch/arm/boot/dts/broadcom/bcm270x.dtsi:67-74`, `bcm2711-rpi-ds.dtsi:175-178`). That gives
  zero-copy scanout of GPU buffers, overlay planes and vsynced flips with completion events
  **without programming the HVS, pixelvalves or HDMI** — which overturns the project's standing
  "Tier 2 needs GPL HVS code, defer indefinitely" verdict. A native HVS driver stays a later,
  optional step.
* **Buffer sharing = one small kernel addition:** a memory object whose pages are already resident
  and which is named by an oid `{server port, buffer id}`, so `mmap(fd)` maps the *same* pages
  instead of demand-copying them through `proc_read` (today's behaviour, `vm/object.c:405-420`).
  With that, a Phoenix fd on such an oid **is** a dma-buf: it passes over AF_UNIX with the existing
  `SCM_RIGHTS` support (`posix/fdpass.c`), maps zero-copy, and can be imported by the other server.
* **Windowing:** full-screen apps flip their buffers straight onto the primary plane; X11 moves to
  the Xorg modesetting DDX + glamor + DRI3/Present (the model Pi OS used until 2023); a Wayland
  compositor (Pi OS's current model) is the last milestone.

**Plan (§5):** M0 kernel shared-object probe + GPU timing split (1–2 wk) → M1 async render server,
games on it (3–5 wk) → M2 `rpi4-kms` on firmware planes + kmscube (3–4 wk) → M3 libdrm-phoenix +
GBM/EGL, glmark2-es2-drm, SDL2 KMSDRM (4–6 wk) → M4 Xorg modesetting+glamor+DRI3/Present (4–6 wk) →
M5 Vulkan WSI display/xcb (2–3 wk) → M6 Wayland compositor (4–8 wk) → M7 optional native HVS.
Total M0–M6 ≈ 21–34 engineer-weeks (≈ 5–8 months) to "all kinds of GPU apps", with each milestone
demonstrable on HDMI and measured against Raspberry Pi OS on the same board with the same benchmarks.

**Biggest risks:** (1) firmware-KMS is undocumented outside a GPL Linux driver and RPi has
deprecated it for the Pi 5 — pin the firmware (already done) and keep native HVS as the escape
hatch; one forum report shows fkms 3× slower than full KMS for a full-screen X app, which M2 must
measure first; (2) C1 — an unidentified device write of `0x8000000x` at page+4 of recycled
physical pages — argues for making "pages never return to the kernel while a device can still
touch them" structural in the BO manager, and for not multiplying per-frame mailbox traffic
blindly; (3) the kernel primitive is a change to `vm/object.c`, i.e. core code.

---

## 1. Where we are — current architecture, measured, and the old assumptions re-judged

### 1.1 What exists (source map)

| Piece | Where | What it does | Status |
|---|---|---|---|
| In-process winsys | `phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c` (3620 l.) + `v3d_phoenix_power.c` (628) + `v3d_libdrm_shim.c` (81) + `v3dv_libdrm_shim.c` (177) | Implements `DRM_IOCTL_V3D_*` in userspace for Mesa: power-on through `/dev/vcmbox`, maps V3D HUB/CORE0 MMIO (`0xfec00000`), owns the MMU page table, VA allocator, BO table, binner-overflow pool; synchronous submit; scanout aliasing and flip | **What every game uses** [read] |
| GPU server | `gpu/rpi4-v3d/rpi4-v3d.c` (304) + `v3d_gpu.c` (1447) + `v3d_rpc.h` (170) | Same logic in a daemon; `/dev/v3d-srv`; clients link `libv3d-client.c` (448) which re-implements `phoenix_v3d_ioctl` as RPC; BO by PA + `MAP_PHYSMEM`; one message at a time ⇒ serialised | HW-proven M0–M3c (2026-08-22); launched only by `startx_gpu`; not in `DEFAULT_COMPONENTS` [read] |
| Framebuffer device | `video/rpi4-fb/rpi4-fb.c` | `/dev/fb0` over the firmware framebuffer plo allocated (1080p, triple-height virtual); `read`/`write` at an offset; no live mmap (issue #149) | shipped [read] |
| GL present (SDL2) | `phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c` (357) | Surfaceless `st_create_context` (no EGL); three scanout-backed FBOs (one per firmware buffer), `glFinish()`, then `v3d_phoenix_flip()` = mailbox `SET_VIRTUAL_OFFSET`; client's FBO 0 redirected to a user FBO | shipped [read] |
| Vulkan present (vkQuake) | `phoenix-rtos-ports/vkquake/glue/pl_phoenix_vk_vid.c` (1319) | No WSI; VkImages whose memory the winsys backs with the firmware fb pages; flip after device-idle | shipped [read] |
| X server | `phoenix-rtos-ports/xorg_server/files/ddx/fbdev.c` + glamor shim | kdrive Xphoenix; glamor renders the root pixmap as a GL texture; damage bands are `glReadPixels`'d into a shadow and `write()`n to `/dev/fb0` | desktop of record [read] |
| Mailbox server | `misc/rpi4-vcmbox/` | Serialises the single VideoCore property FIFO for all clients | project rule: all mailbox traffic goes through it [read] |

### 1.2 How a frame gets to the screen today

```
 game ── Mesa v3d ── drmIoctl ─(in-process)─ phoenix_v3d_ioctl
          │ BOs: mmap(MAP_CONTIGUOUS|MAP_UNCACHED) + va2pa, one PTE per page      (winsys:1487-1519)
          │ SUBMIT_CL: dsb; SLCACTL inv; TLB flush; L2T flush x3 (waited);
          │            kick CT0; spin FLDONE (servicing OUTOMEM); kick CT1; spin FRDONE (winsys:2550-2820)
          │ colour RT of the "window" = pages of firmware fb buffer k (RASTER)    (winsys:1426-1460)
          └ glFinish(); v3d_phoenix_flip(k) → mailbox SET_VIRTUAL_OFFSET(0, k*1080) (power.c:625)
 X ── glamor draws into a GL texture root ── damage timer (50 ms) ──
          glReadPixels(GL_BGRA) → shadow → CPU swizzle → write(/dev/fb0) per dirty band
          (1.74 ms + 0.07 ms/row; 77 ms for 1080 rows)                          [measured]
 GL-in-X client ── FBO ── glReadPixels ── XPutImage 1.2 MB over AF_UNIX ── server upload ── present
          (70.6 ms/frame, 14.2 fps at 640x480)                                   [measured, 2026-09-25]
```

### 1.3 Measured performance and known bottlenecks

| Workload | Phoenix today | Bottleneck (as far as measured) | Source |
|---|---|---|---|
| QuakeSpasm (GLQuake) 1080p | ~38–50 fps | CPU/GPU serial; flip via mailbox (~0.15 ms) | support matrix; W39 gate |
| Quake II (yquake2 GL) | 38.2–38.8 fps | — | W39 gate T3; `project_v3d_clock_race_mmio_hang` |
| Quake III (quake3e) | 53.8–54.2 fps | — | W39 gate T4 |
| vkQuake | ~31 fps in-game (42 fps on static console) | — | W39 §5031; STK memory note on console windows |
| SuperTuxKart 1.4 (SP renderer) | **7.4–8.7 fps** | **not fill-bound** (720p = 1080p, 8.4 fps both); ~8 CL submits/frame ≈ 88% of frame time spent inside submit | `docs/misc/2026-09-16-stk-fps-not-fill-bound.md`; STK memory |
| Glamor X desktop | 25.6 presents/s, full-screen present 77 ms | CPU readback + swizzle + `write()` IPC | `project_x11_glamor_whole_screen_present` |
| GL window in X (640×480) | 14.2 fps (70.6 ms: put 33.9, read 12.5, pack 6.4, draw 1.6) | copies through the socket | KNOWN-ISSUES G1 |
| GPU compute via daemon | 0.02 ms dispatch floor; CSD 12.62 vs 11.96 ms in-process | IPC round trip ≈ 20 µs | `2026-08-22-concurrent-gpu…md` M1-2b; CNN memory |
| H.265 1080p playback on fb0 | 21.7 fps | ~41.5 of 46 ms is the framebuffer blit | CHANGES §1419 |

Two numbers matter most for the design: the **IPC round trip is ~20 µs** (so a server hop per
submit costs ~0.2 ms per STK frame, i.e. nothing), and **STK is submit-bound, not fill-bound**. What
"inside submit" consists of — GPU execution vs the waited flushes vs the CPU spin that prevents
overlap — has **not** been split. That is experiment **E2** (§6) and it decides how much of the
STK gap M1 closes. **I explicitly do not predict a speedup.**

Checked and ruled out as the STK gap: the "16-bit vertex format" fallback that cost STK 8–20 fps
on Pi OS until Mesa MR 21361 [cited, Igalia 2023] — our Mesa 26.2 lists
`R16G16B16A16_FLOAT`/`R16_FLOAT` as native vertex formats (`external/mesa/src/gallium/drivers/v3d/v3d_screen.c:430-449`) [read].
Also not a gap: scanout layout. The HVS scans LINEAR, VC4 T-tiled or SAND only — never UIF
(`external/linux/drivers/gpu/drm/vc4/vc4_plane.c:2785-2790`) [read], so Pi OS also renders
full-screen scanout targets linear; our RASTER scanout RT (`v3d_resource.c:933-936`) costs nothing
relative to Pi OS. Clocks match too: V3D at 500 MHz both on Pi OS (default) [cited] and here
(`v3d-coldstate: clk_v3d cfg=500000000`) [measured].

### 1.4 What worked, what failed, and why

**Worked (keep):**
* The *idea* of implementing the DRM uapi below an unmodified `drmIoctl()` — Mesa v3d/v3dv needed
  almost no driver changes to run. This is exactly the Genode shape.
* Power/clock via serialised `/dev/vcmbox` with read-back (`9f9ec65`), after two separate outages
  from racing the unarbitrated FIFO (quake flicker `3cc684c`; the clock race).
* The daemon split (M0–M3c): single owner of power, page table and VA space; bit-exact through IPC;
  global BO handles already let a second process import a buffer (`tools/boshare-probe`,
  `gl_bo_import.c`, `resource_from_handle(TYPE_SHARED)` landed in Mesa `3b339c93a07`).
* Linux-parity GPU disciplines learnt the hard way: zero every BO (Phoenix `MAP_CONTIGUOUS` pages
  are *not* zeroed), unbounded-ish binner overflow (32 MiB pool), never write `MISCCFG`, clear the
  full `CTL_INT` status, reset-and-drop-frame on a wedge, `dsb` before kick, clean+invalidate on
  every new uncached mapping (now in the kernel, `_pmap_cacheOpAfterChange`).
* The shader disk cache (with the manual-invalidation footgun).

**Failed or became a trap (replace):**
* **Render-to-scanout** (GPU writing the buffer the display reads): ~40% of boots wedged; only
  triple buffering in the firmware buffer cured it. A real display pipeline never lets the GPU
  write the scanned-out buffer, by construction.
* **The FBO-0 redirect** that makes surfaceless contexts look windowed turns GLES no-op hints into
  destructive operations (`glInvalidateFramebuffer` ⇒ 100% black Quake II;
  `project_gles_fb0_redirect_traps`). This whole class disappears with a real EGL window surface.
* **Mesa's `>=1024x768 ⇒ Y_0_TOP` scanout heuristic** and the `set_next_scanout()` one-shot flag:
  Phoenix-only hacks that mirrored the glamor desktop (`phx_scanout_flip_gate`). With GBM/EGL the
  scanout property is explicit (`PIPE_BIND_SCANOUT`/modifiers) and these go away.
* **Two copies of the winsys** (libv3d and libv3dv both embed it; the second silently wins the
  link, `project_vkquake_stale_winsys_in_libv3dv`). A server removes the duplication.
* **Whole-process GPU state per app**: two GPU apps at once silently corrupt each other (M0).
* **All BOs physically contiguous.** `vm_objectContiguous` rounds every BO up to a power of two
  (`size = 1UL << p->idx`, `vm/object.c:491`) [read] — a 9 MiB render target costs 16 MiB — and
  contiguity is only needed by the (MMU-less) display engine, not by the V3D, which has an MMU.

### 1.5 The old assumptions, re-judged

| Assumption (where) | Verdict |
|---|---|
| "V3D 4.2 is single-context ⇒ multi-app = serialised time slicing" (`done/2026-08-13-dri-drm-design.md`) | **Right about arbitration, wrong if read as "synchronous".** One core and one PT, yes; but bin, render, TFU and CSD are separate queues that Linux runs *concurrently* (one job per queue, `credit_limit=1`). Serialise per queue, pipeline across queues. |
| "Per-process GPU address spaces are part of what makes Linux fast" (task framing) | **False for V3D 4.2.** Linux uses one page table for all clients on this GPU (`v3d_mmu.c`, see §2); isolation is absent upstream too. Not a performance feature; don't budget for it. |
| "Copy-first compositing; zero-copy needs an unverified physmem-share primitive" (08-13 design) | **Superseded.** Cross-process BO import is HW-proven (boshare-probe, `gl_bo_import.c`); what is missing is (a) a *safe, refcounted* handle instead of raw PA, and (b) an export path. |
| "Tier 2 (direct display) has no non-GPL reference — defer indefinitely; stay on the property mailbox" (`knowledge/gpu-vc6-non-linux.md` §Synthesis) | **Overturned in part.** The mailbox itself offers planes + vblank (`SET_PLANE` + SMI IRQ). We can have KMS semantics while *staying* on the firmware ABI. Native HVS is still a large, GPL-referenced job — but no longer a prerequisite. |
| "3D acceleration needs a DRM-shaped uapi shim — months of research" (same doc §10) | **Obsolete** — the port built that shim and ships five games on it. |
| "Windowed-GL ceiling is ~1.7× because the DDX present survives buffer sharing" (`misc/2026-09-09-gl-window-buffer-sharing-work-order.md`) | **True only for the shadow→`write(/dev/fb0)` DDX.** With the X screen pixmap in a scanout buffer flipped by a display server, the present term (1.74 ms + 0.07 ms/row) becomes a GPU blit or a flip. The ceiling must be recomputed against the new architecture. |
| "Triple buffering is required" (CHANGES §200) | **A workaround for not having vblank feedback.** With flip-completion events double buffering (or mailbox/FIFO present modes) is normal. |
| "`MAP_SHARED` is required for the fb mapping" (winsys:1158) | **Wrong** — `MAP_SHARED == 0` (`kernel/include/mman.h:28`). The effect comes from `MAP_PHYSMEM`. |
| "Per-submit IPC would cost frame rate" (implicit in keeping games in-process) | **Refuted by measurement**: 20 µs per round trip. |
| "Synchronous submit is fine because a submit is frame-sized" (08-22 feasibility §4) | **The biggest remaining architectural cost** (see 1.3, E2). |
| "SET_PIXEL_ORDER is broken on Pi 4, trust nothing" (non-linux doc, NetBSD note) | Still good hygiene; the project measured its own fb as RGB with `tools/fbprobe`. |

---

## 2. The target — what makes Raspberry Pi OS fast

### 2.1 The stack

```
 app ── Mesa (v3d gallium / v3dv / vc4-kmsro) ── libdrm ──┬── /dev/dri/renderD128 (v3d.ko)
        GBM, EGL (x11/wayland/gbm/surfaceless), WSI       └── /dev/dri/card0 (vc4.ko, KMS only)
 v3d.ko: shmem BOs + one shared GPU page table; drm_sched with bin/render/TFU/CSD(/cache-clean)
         queues; dma-fences + syncobjs; IRQ-driven (FLDONE/FRDONE/OUTOMEM/CSDDONE); per-job
         timeout + reset; OUTOMEM serviced by allocating a fresh 256 KiB BO per event
 vc4.ko: HVS display lists (planes with scaling/format conversion), pixelvalves, HDMI0/1, atomic
         modesetting, vblank-timed page flips + events, async flips; CMA (contiguous) scanout
         buffers; imports v3d buffers via dma-buf
 compositor: Wayland (labwc since Oct 2024, Wayfire before) or Xorg modesetting+glamor+DRI3/Present
```

With **renderonly** (Mesa `kmsro`), a scanout resource is allocated on the *display* device as a
dumb (contiguous) buffer and imported into v3d through a dma-buf fd
(`external/mesa/src/gallium/drivers/v3d/v3d_resource.c:956-988`) [read] — because the HVS has no
MMU and V3D does. This is precisely the split our design adopts.

### 2.2 Which pieces carry the performance (and which don't)

| Mechanism | Effect | Evidence |
|---|---|---|
| Direct scanout / page flip onto a plane (no copy) | the biggest single fullscreen factor: same app **44 fps under fkms-era X → 141 fps under full KMS** at 1080p | [cited] forum t=304534 (row 16 of the survey) |
| Async queue: CPU builds frame N+1 while GPU runs N; bin(N+1) overlaps render(N) | not separately published; follows from `v3d_sched.c` design | [inferred] + [read] Linux source |
| Zero-copy dma-buf between GPU, display and compositor | removes 8 MB copies per 1080p frame on a bus that delivers ~4–5 GB/s in practice (~2.8 GB/s memcpy) | [cited] bandwidth rows 23; RPi Bookworm post ("primary advantage of Wayland is performance") |
| Compiler work | +20–30% (UE4 demo) | [cited] Igalia 2021 — **we already have this**, same Mesa |
| Vulkan vs GL | +35–60% on the same game | [cited] Igalia 2020/2023 — **we have v3dv** |
| Super pages in the GPU MMU | +1.4% average, +8% best | [cited] Igalia 2024 — small |
| Shader cache | load time, not fps | we have it |
| Per-process GPU address spaces | **none on V3D 4.2** (one shared PT) | [read] Linux `v3d_mmu.c` (see §3.1) |

### 2.3 Reference numbers (Pi 4, stock clocks) to measure against

Full table with URLs is kept from the survey; the ones we will reproduce:

| Benchmark | Pi OS | Phoenix today |
|---|---|---|
| glmark2-es2 off-screen 800×600, performance governor | ~256–450 (2020, Mesa 19/20); 425–697 (2023, Bookworm Mesa) [cited] | not portable yet (needs EGL) |
| glmark2-es2-drm | vsync-capped 50–79 [cited] | — |
| Full-screen 1080p texture/clear app, vsync off | 141–206 fps under KMS [cited] | — |
| kmscube | 60 (vsync) [cited] | — |
| Quake III timedemo four, 720p / 1080p | 86 / 36 fps (ioq3 GL1, 2019, fkms) [cited]; quake3e 62.5 (2024, res. not stated) [cited] | quake3e ~54 fps @1080p gameplay [measured] (not a timedemo — not directly comparable) |
| SuperTuxKart | 29 fps @720p, 40 @1024×768 (2019); 30–110 fps GL, Vulkan +35–50% (2023) [cited] | 8.4 fps (720p and 1080p) [measured] |
| vkmark | 311 (2020) [cited] | — |
| Quakespasm / yquake2 / vkQuake absolute | **not published** [cited: none found] | 38–50 / 38 / 31 fps |

Rule for comparison (from the survey): same board, performance governor, record vsync state and
display stack (Bookworm Wayland/labwc vs X11), and use the *same* benchmark binaries built from the
same sources on both systems. §5.3 gives the protocol.

---

## 3. How other systems do it

Clones used (all under `external/`): `linux` (raspberrypi/linux **rpi-6.18.y**, 6.18.35 — the
downstream tree, which is why it contains `vc4_firmware_kms.c`), `mesa` (our fork, 51c5ee977ba0),
`freebsd-src`, `drm-kmod` (HEAD 2026-09-17), `drm-subtree` (evadot, last commit 2022-05-03),
`openbsd-src`, `netbsd-src` (sparse checkouts widened for this study), `genode`, `genode-world`,
`genode-imx`, `genode-allwinner`, `genode-rpi`, `haiku-libdrm2`, `haiku-radeongfx`, `haiku-nvidia`.
Fuchsia sources were fetched into the job scratch dir. Line counts are `wc -l`; licences are file
headers. All **[read]** unless marked.

### 3.1 Linux — the reference hardware model (read for understanding, nothing copied)

**v3d render driver** (6,705 lines, GPL-2.0+; `v3d_sched.c` 919, `v3d_submit.c` 1438,
`v3d_irq.c` 351, `v3d_mmu.c` 161, `v3d_bo.c` 306, `v3d_gem.c` 350):

* Queues BIN, RENDER, TFU, CSD, CACHE_CLEAN, CPU — each its own `drm_sched` with `credit_limit=1`
  and a 500 ms timeout. Render depends on its bin job's fence. **Different queues run
  concurrently.**
* **One page table for all clients**: 4 MiB contiguous (`dma_alloc_wc`), 4 GiB GPU VA, PTE bits
  Valid(28)/Write(29)/64K bigpage(30)/1M superpage(31); GMP isolation "not yet implemented";
  faulting accesses redirected to a scratch page; full MMUC flush + TLB clear on every insert/remove.
* BOs = shmem pages (non-contiguous) → sg table → `drm_mm` VA → PTEs.
* IRQs: OUTOMEM ⇒ a work item allocates a fresh 256 KiB BO and writes `PTB_BPOA/BPOS`; FLDONE,
  FRDONE, CSDDONE, TFUC signal fences; MMU errors decoded.
* Hang handling: on timeout, if `CTnCA/CTnRA` moved since the last check the job is still
  progressing and the timer re-arms; otherwise stop schedulers, bridge reset, re-init, restart.

**vc4 display on BCM2711** (30,164 lines incl. Pi 5 code; **every Pi 4 display file is GPL**:
`vc4_hvs.c` 2308, `vc4_plane.c` 2929, `vc4_crtc.c` 1566, `vc4_hdmi.c` 3487, `vc4_hdmi_phy.c` 1207,
`vc4_kms.c` 1205, `vc4_regs.h` 1444). HVS: 3 channels; display lists in on-chip memory at HVS+0x2000
(4096 words); a "plane" is a display-list entry (16 per CRTC exposed); commit copies dlist words and
writes `SCALER_DISPLISTx`, latched next frame; async flip rewrites only the pointer word. Scanout =
format + position + size + pitch + 32-bit bus address; LINEAR, VC4 T-tiled, SAND; **no UIF**; buffers
contiguous (no IOMMU). Five pixelvalves (PV2→HDMI0, PV4→HDMI1); vblank = `PV_INT_VFP_START`.
HDMI (clocks, PHY, SCDC scrambling for 4K, audio, CEC) is the bulk of the work.

**Firmware-KMS** (`vc4_firmware_kms.c`, 2079 lines, GPL-2.0-only, *protocol only is relevant*;
permissive primary sources exist for much of the vocabulary: `raspberrypi/userland` is BSD-3-Clause
(LICENCE: Broadcom 2012 / Raspberry Pi 2015), and its `interface/vctypes/vc_image_types.h` is BSD-3 and
defines the `vc_image_type` enum incl. `VC_IMAGE_XRGB8888`/`RGBA32`/`RGBX8888`/`BGRX8888` and the YUV
formats [read via web]; the property tag numbers are published on the raspberrypi/firmware wiki. The
`set_plane` struct layout and the SMI vblank convention are, as far as found, documented only by the
GPL driver — we take the wire format, not code):
the firmware keeps HVS/PV/HDMI; the ARM sends `SET_PLANE` (struct at `:64-96`: display, plane_id,
vc_image_type, layer, width/height, pitch/vpitch, 16.16 src rect, dst rect, alpha, num_planes (YUV),
is_vu, color_encoding, `planes[4]` DMA addresses, transform), `SET_TIMING`, `SET_DISPLAY_POWER`,
`GET_EDID_BLOCK_DISPLAY`, `GET_DISPLAY_TIMING`, `GET_DISPLAY_CFG`, `FRAMEBUFFER_{GET_NUM_DISPLAYS,
SET_DISPLAY_NUM, GET_DISPLAY_ID, BLANK}`. Vblank arrives as a firmware-raised interrupt on the SMI
peripheral (`SMICS` bits 9–11, `SMIDSW0/1` per-display flags; `reg 0x7e600000`, GIC SPI 112 on
2711). 8 planes per CRTC.

**Licensing of "DRM"** (the distinction the owner asked for):

| Part | Licence | Usable in Phoenix core? |
|---|---|---|
| uapi headers `drm.h` (1476), `drm_mode.h` (1451), `drm_fourcc.h` (1762), `v3d_drm.h` (793), `vc4_drm.h` (442) | MIT (no SPDX line); also vendored in Mesa `include/drm-uapi/`; our copies in `gpu/rpi4-v3d/uapi/` confirmed MIT | **yes** — interface definitions |
| DRM core: ioctl layer, atomic (`drm_atomic*.c` ~8.4k), planes/crtc/connector/fourcc/modes/EDID, `drm_vblank.c` 2332, `drm_gem.c`, `drm_prime.c`, `drm_syncobj.c` 1744, `scheduler/` ~2.4k, `dma-resv.c` | MIT / X11-style | legally yes; **but it is Linux kernel code written against Linux internals** — the owner's rule excludes it in practice; useful as a behavioural spec |
| GEM memory helpers `drm_gem_dma_helper.c`, `drm_gem_shmem_helper.c`, fb helpers, `drm_managed`, `drm_of`, 17 files | GPL | no |
| `dma-fence.c`, `dma-buf.c`, `sync_file.c` | GPL-2.0-only | no (semantics can be reimplemented) |
| vc4 (Pi 4 display), v3d (render) drivers | GPL | no — hardware facts only |
| libdrm (userspace) | MIT | **yes** (port) |
| Mesa (v3d, v3dv, GBM, EGL, WSI, drm-shim, simulator) | MIT | **yes** (already a port) |
| X.org server, modesetting DDX, glamor | MIT/X11 | yes (port) |
| Weston / wlroots | MIT | yes (port) |

Most useful MIT reference we did not know we had: **Mesa's own V3D simulator backend**
(`external/mesa/src/broadcom/simulator/v3dx_simulator.c` 594 + `v3d_simulator.c` 1269, MIT © Broadcom)
implements the v3d kernel ioctls in userspace — CL submit, OUTOMEM servicing via `PTB_BPOA`, TFU, CSD,
perfmon. It is a permissively licensed description of exactly the server's job (its register header
comes from Broadcom's simulator library and is not in the tree).

### 3.2 FreeBSD — the counter-example

* **drm-kmod is Linux code on LinuxKPI.** 6.45 M lines, of which amdgpu 5.75 M, i915 348 k, radeon
  197 k; the DRM core itself is 79 files / 69,379 lines with only 22 touched for FreeBSD; LinuxKPI
  base is 73,725 lines (BSD-2) in `sys/compat/linuxkpi`. Buildable modules: `dmabuf ttm drm dummygfx
  i915 amd radeon linuxkpi_video` — **no vc4, v3d or any SoC display**; ships a GPL file
  (`drm_writeback.c`).
* This is precisely the "use Linux code directly" route. It gets a monolithic kernel a huge driver
  catalogue at the cost of chasing Linux internals forever — and it still has **no Pi 4 support**.
  For a microkernel with userspace drivers it is doubly wrong: the Linux drivers assume in-kernel
  `copy_from_user`, kernel memory management and kernel IRQ context.
* Native FreeBSD on the Pi: `sys/arm/broadcom/bcm2835/bcm2835_fbd.c` (283, BSD-2) — firmware
  framebuffer only.
* **evadot/drm-subtree** (dormant since 2022) is the interesting part: the MIT DRM core plus a 21 k-line
  BSD shim, and on top **native BSD-2 drivers** — Allwinner DE2 (3,264), Rockchip VOP (1,896), and
  **Panfrost, a from-scratch BSD-2 GPU render driver (4,734 lines, Ruslan Bukin) using `drm_sched`,
  GEM, dma_fence, dma_resv**. The closest precedent to our render server: a clean-room render driver
  whose uapi is the Linux one so Mesa runs unmodified.

### 3.3 OpenBSD

* `sys/dev/pci/drm`: 6.7 M lines of ported Linux DRM with a 4,164-line ISC compat layer
  (`drm_linux.c`) and 274 compat headers. **Licence discipline worth copying:** the tree has no
  GPL-only driver files — Linux's GPL helpers were rewritten (public-domain
  `drm_gem_framebuffer_helper.c` 69 lines, `drm_gem_atomic_helper.c` 33, `drm_fb_dma_helper.c` 17,
  `drm_managed.c` 215) or replaced by NetBSD BSD-2 code (`drm_gem_dma_helper.c` 267), or dropped.
* arm64 display drivers are native and small: `rkdrm.c` 516 + `rkvop.c` 609 (BSD-2, from NetBSD),
  `qcdrm.c` 170 (ISC), `simplefb.c` 396. **`rkvop` has no vblank interrupt at all** — the atomic
  helper completes flip events at commit (`drm_atomic_helper.c:704,2682`). A useful lesson: a KMS
  device *without* vblank is legal and works, just without pacing.
* Pi 4: `simplefb` only (firmware framebuffer); no vc4/v3d/HVS.

### 3.4 NetBSD

* `sys/external/bsd/drm2`: Linux import in `dist/` (3.17 M) + compat `linux/` (11 k) + `include/`
  (18.6 k) + native glue `drm/` (4.4 k). GPL files are **left out** of `dist/` and the needed ones
  rewritten BSD-2 (McNeill): `drm_gem_cma_helper.c` 276 (bus_dma + a vmem contiguous pool),
  `drm_gem_framebuffer_helper.c` 161, `drm_lease.c` 189, `drm_vma_manager.c` 332, `drmfb.c` 295.
* Native BSD-2 arm KMS drivers: sunxi (`sunxi_drm.c` 557, `sunxi_lcdc.c` 552, `sunxi_mixer.c` 685,
  `sunxi_debe.c` 1006, `sunxi_tcon.c` 925, `sunxi_hdmi.c` 1223), rockchip (`rk_drm.c` 507,
  `rk_vop.c` 846), tegra (`tegra_drm*.c` ~1.8 k), ti (`ti_lcdc.c` 700). Registration =
  `drm_dev_alloc` → private contiguous pool (`bus_dmamem_alloc` + `vmem`) → `drm_gem_cma_*` hooks →
  driver fb → atomic helpers → `drm_vblank_init`. **sunxi has a real IRQ-driven flip**: the TCON
  interrupt calls `drm_crtc_handle_vblank` and sends the pending event under `event_lock`
  (`sunxi_lcdc.c:435-461`). A KMS display driver on this model is ~2–4 k lines of hardware code —
  when the hardware is documented.
* **No GPU render driver on any arm board.** Pi: `bcm2835_genfb.c` (217) only. The wiki lists a
  "VC4 DRM driver" project as completed, but NetBSD HEAD contains no vc4 code (unresolved; it would
  have been an import of the GPL driver anyway).

**BSD verdict:** no BSD has any Pi 4 GPU or native display support; every one uses the firmware
framebuffer. What the BSDs *do* offer is (a) the pattern — MIT DRM *semantics* as the contract,
native hardware code underneath, GPL helpers rewritten; and (b) two proofs that the pattern works for
render drivers (drm-subtree Panfrost) and for IRQ-driven KMS (NetBSD sunxi). There is no BSD code
to "wrap our GPU code into" for this SoC; what we take is the architecture and the discipline.

### 3.5 Genode — the closest architectural analogue

* **Gpu session** (`repos/os/include/gpu_session/gpu_session.h`, 306 lines, AGPLv3): 13 RPCs —
  `info_dataspace`, `execute(vram, off) → seqno`, `complete(seqno)`, `completion_sigh`,
  `alloc_vram → dataspace`, `free_vram`, `export_vram → capability`, `import_vram`, `map_cpu`,
  `unmap_cpu`, `map_gpu(va)`, `unmap_gpu`, `set_tiling_gpu`. Buffers are dataspaces; sharing is by
  capability.
* **libdrm back-end** (`repos/libports/src/lib/libdrm/`): `ioctl_iris.cc` 1674, `ioctl_lima.cc` 1229,
  `ioctl_etnaviv.cc` 1042, `ioctl_dispatch.cc` 130 — it translates DRM ioctls into session RPCs
  *below an unmodified Mesa*. **No v3d/vc4**; the only Pi display driver is a 105-line 1024×768
  mailbox framebuffer that copies every 10 ms.
* Drivers: Intel is native (9.5 k lines); etnaviv and lima are **Linux drivers run via DDE Linux**
  (GPL) — again "Linux code".
* Weaknesses they document themselves: iris waits for every EXECBUFFER to finish
  (`ioctl_iris.cc:721-725`), `SYNCOBJ_WAIT` returns 0, PRIME is a single global slot (no real
  dma-buf), no Vulkan, no timelines; the present path is `mapImage` → `genode_blit` into a GUI buffer
  → compositor → display driver copy = **two or three CPU copies, no vsync** ("vsync timing is not yet
  provided", 24.11). Regrets: an RPC per BO (fixed in 23.02 by 16 MiB chunks sub-allocated in libdrm),
  Intel-specific nomenclature in a "generic" session (23.02). Quantitative: +50% glmark2 from lazy
  mapping and fewer RPCs (22.02).
* Good ideas to take: completion counter in a shared page (Intel `info_intel.h:43`), capability-shared
  memory, libdrm as the adaptation layer. Ideas to avoid: synchronous submit, CPU-copy present, fake
  syncobjs.

### 3.6 Fuchsia Magma, Haiku, Redox, QNX (brief)

* **Magma** (BSD-style): ICD in the app, MSD a privileged userspace driver; 46-call client API
  (`magma.h`, 607 lines); `ExecuteCommand(resources, command_buffers, wait_semaphores,
  signal_semaphores)` is **one-way**, fences travel inside the message, `Flush()` is the only round
  trip, flow control by events. Display coordinator: sysmem buffer collections, `ImportImage`,
  `SetLayerImage2(layer, image, wait_event)`, `CommitConfig(stamp)`, `OnVsync(stamp)` — **fence-gated
  zero-copy layer flips**. No v3d MSD. Fuchsia's Mesa fork retargeted turnip's kernel layer in ~760
  lines (`tu_knl_magma.cc`) — evidence that the kernel-interface layer of a Vulkan driver is small.
* **Haiku**: upstream Mesa is software-only there, but X547's **RadeonGfx** is a userland server that
  receives DRM ioctls over IPC and reimplements the amdgpu uapi *including the full SYNCOBJ set and
  PRIME*, with a display consumer that programs the CRTC scanout address and retires on the page-flip
  IRQ (zero-copy flip). No licence file — reference only. The strongest existence proof that
  "DRM-ioctl-over-IPC to one server" carries an unmodified Mesa driver.
* **Redox**: userspace daemons behind schemes; March 2026 added "more Linux DRM APIs, removing the
  need for Redox-specific ioctls" — converging on DRM-compatible APIs, no 3D yet.
* **QNX Screen**: a compositing server; display drivers are OpenWF Display (WFD) plug-ins; windows
  are assigned to hardware pipelines (overlay layers); commits apply at vsync. Design reference for
  plane assignment in a message-passing OS.

### 3.7 Mesa's actual DRM surface (what we must serve)

Counted over our Mesa fork, real-device paths only [read]:

* **13 of 14 `DRM_V3D_*` ioctls**: GET_PARAM, SUBMIT_CL, SUBMIT_TFU, SUBMIT_CSD, **SUBMIT_CPU**,
  CREATE_BO, MMAP_BO, WAIT_BO, GET_BO_OFFSET, PERFMON_CREATE/DESTROY/GET_VALUES/GET_COUNTER
  (only PERFMON_SET_GLOBAL unused). Plus GEM_CLOSE, GEM_FLINK/OPEN (gallium), PRIME both ways,
  and syncobjs: create, destroy, wait (7 sites in gallium), export/import sync file, signal; v3dv
  additionally uses the Vulkan runtime's `vk_drm_syncobj.c` (create, signal, reset, wait, fd↔handle,
  transfer, query) with timelines disabled (`v3dv_device.c:1574-1580`).
* **v3dv refuses the device** unless GET_PARAM reports `SUPPORTS_TFU`, `SUPPORTS_CSD`,
  `SUPPORTS_CACHE_FLUSH`, `SUPPORTS_MULTISYNC_EXT` and `SUPPORTS_CPU_QUEUE`
  (`v3dv_device.c:861-868`). The submit extensions used are MULTI_SYNC plus CPU-job extensions
  0x02–0x07 (indirect CSD, timestamp and performance queries) — CPU work Linux does kernel-side.
  Today the winsys only *advertises* them: GET_PARAM returns 1 for all five
(`v3d_phoenix_winsys.c:3396-3411`, comment: "CPU_QUEUE is a kernel-side convenience we don't
implement"), the MULTI_SYNC extension chain is ignored (submit is synchronous), and
`DRM_V3D_SUBMIT_CPU` falls into the ioctl `default:` which returns 0 — a silent no-op
(`:3617-3618`) [read]. So Vulkan timestamp/occlusion/performance queries and indirect dispatch are
**not implemented at all** today; for the server this is net-new work, not a refinement.
* **WSI allocates on the display device**: v3dv's `device_alloc_for_wsi()`
  (`v3dv_device.c:2346-2398`) creates every swapchain image as `MODE_CREATE_DUMB` on the vc4 node and
  imports it into v3d by PRIME; gallium does the same through kmsro. So the display server owns
  scanout memory and the render server imports it — which matches the HVS-has-no-MMU fact.
* `src/drm-shim/` (MIT) + `src/broadcom/drm-shim/v3d_noop.c` (221) is an LD_PRELOAD fake render node —
  a skeleton for client-side interception, glibc-specific as is.

---

## 4. Proposed architecture

### 4.1 Decision: the API boundary

| Option | Performance | Porting effort | Upstream Mesa tracking | Ecosystem |
|---|---|---|---|---|
| **A. Custom winsys (today)** — Mesa talks to Phoenix-specific present/scanout calls | good full-screen only; no CPU/GPU overlap unless rebuilt; no zero-copy windows | lowest *per step*, highest in total: every app needs Phoenix glue (SDL glue, vkQuake glue, glamor shim, FBO-0 redirect…) | Phoenix hooks in `v3d_resource.c`, `st_atom_framebuffer.c`, `v3d_bufmgr.c`, `v3dv_queue.c` | none of X DRI3/Present, Wayland, SDL KMSDRM, EGL GBM, Vulkan WSI |
| **B. DRM uapi as a literal device ABI** — `/dev/dri/*` answering raw `ioctl()` | same as C | high: a Phoenix server cannot dereference the pointers inside `drm_v3d_submit_cl`, `drm_mode_atomic`, `drm_syncobj_wait` etc.; every nested-pointer ioctl needs a kernel copy-in facility Phoenix does not have | best | best |
| **C. DRM-compatible *library* API over Phoenix messages** (libdrm-phoenix; Genode/RadeonGfx pattern) | same as Linux's structure: async submit, zero-copy, flips | moderate: marshal ~50 ioctls once, in one library | near-unmodified Mesa (MIT), libdrm (MIT) | all of the above via standard code paths |

**Choose C.** It is what the port half-did already (the in-process `phoenix_v3d_ioctl` *is* a
userspace v3d uapi) and what `libv3d-client` proved over IPC. Apps and Mesa see libdrm; libdrm
flattens each ioctl (struct + referenced arrays) into `msg_t` (`i.raw[64]` + `i.data`, outputs in
`o.raw`/`o.data`) and sends it to the right server. Where an application calls raw `ioctl()` on a DRM
fd with a *flat* struct, libphoenix's `ioctl()` passes the `IOCPARM_LEN(request)`-sized argument to
`sys_ioctl` (`libphoenix/unistd/file.c:797-826`) [read], so a flat DRM struct reaches the server
unchanged [inferred: the server side of `sys_ioctl` → `mtDevCtl` not traced]. libphoenix even has a
per-request serializer for ioctls with embedded pointers (`IOC_NESTED`, `unistd/ioctl-helper.c:294`,
used today only for `SIOCGIFCONF`/`SIOCADDRT`) [read] — a possible second home for the DRM
flatteners, but DRM request codes do not carry the `IOC_NESTED` bit, so libdrm remains the natural
place. Nested-pointer ioctls must go through libdrm, which all Mesa and known compositors do.

### 4.2 Process structure

```
                         clients (each links Mesa + libdrm-phoenix)
      game (EGL/GBM or VK_KHR_display)   Xorg (modesetting+glamor)   compositor   GL/VK clients
            |            |                       |        |               |             |
   render node ops   KMS ops            render + KMS ops  |           render ops    DRI3/Wayland:
            |            |                       |        |               |       dmabuf fds over
            v            v                       v        v               v        AF_UNIX (SCM_RIGHTS)
   +-----------------------------+        +-----------------------------------+
   | rpi4-v3d  (/dev/dri/renderD128)       | rpi4-kms  (/dev/dri/card0)         |
   |  dispatch thread(s) (msgRecv)|        |  dispatch thread (msgRecv)         |
   |  IRQ thread: V3D SPI 74      |        |  IRQ thread: SMI SPI 112 (vblank)  |
   |  queues: BIN RENDER TFU CSD  |        |  planes/CRTC/connector state       |
   |          CACHE_CLEAN CPU     |        |  atomic commit, flip queue, events |
   |  BO manager + GPU MMU (1 PT) |        |  contiguous scanout pool (dumb BOs)|
   |  syncobjs, per-BO resv       |        |  fbdev emulation (/dev/fb0 compat) |
   |  power/clock via /dev/vcmbox |        |  mailbox via /dev/vcmbox           |
   +--------------+---------------+        +----------------+------------------+
                  |  fence page (RO shared): completed seqno per queue          |
                  +--------------------------->  read at vblank, no IPC  <------+
                  |          imports scanout BOs by oid (PRIME)                 |
                  v                                                             v
      kernel: vm_object "exported" under oid {server port, buffer id}  (new, §4.4)
              interrupt(), msgSend/msgRecv/msgRespond, fdpass (existing)
```

**Separate or combined?** Separate, for four reasons: (1) fault isolation — a GPU hang and reset must
not blank the display or kill the console; (2) Mesa's own model is two fds (kmsro/v3dv WSI), so two
servers map one-to-one onto unmodified code; (3) scanout memory has different constraints
(contiguous, 32-bit bus-addressable) and belongs to the display side, exactly as in Linux; (4) the
display server is useful without the GPU (console, the HEVC decoder's frames as a YUV plane — which
removes the 41.5 ms-of-46 ms framebuffer blit that caps H.265 playback at 21.7 fps [measured; the fix
is inferred]). The usual cost of separation — a server-to-server round trip per frame for
"flip when the render finishes" — is avoided by the **fence page**: `rpi4-kms` reads the render
server's completed seqnos from a shared read-only page at vblank time.

### 4.3 Render server (`rpi4-v3d`, evolved from the existing daemon)

* **Ownership (unchanged from M1–M3c):** power/clock/reset through `/dev/vcmbox` with read-back
  (`9f9ec65`/`1292480` lessons), MMIO, the one page table, VA allocator, BOs.
* **Async submit.** `SUBMIT_CL` validates and enqueues a bin job and a render job and responds
  immediately with the job's seqno (~20 µs round trip [measured]). The IRQ thread (Phoenix
  `interrupt(106, handler, …, cond)`; GIC SPI 74 = IRQ 32+74 [read DT `bcm2711.dtsi:613`];
  `interrupt()` is already used by `phoenix-rtos-lwip/drivers/bcm-genet.c:1989` on this board)
  handles FLDONE (bin done → start next bin; release render), FRDONE, OUTOMEM (hand the next
  overflow chunk — no spinning), CSDDONE, TFUC and MMU faults, then publishes seqnos. **Bin of job
  N+1 runs while render of job N runs**; the client's CPU builds the next frame meanwhile. Per-queue
  one-job-in-flight, like Linux.
* **Cache maintenance per job** follows Linux ordering and keeps the empirically needed extras
  (`dsb sy` before kick; SLCACTL early; L2T flush). The "fix-A" extra waited flush is timing margin
  for a render wedge (`winsys:2595-2606`) — re-validate under async before dropping anything.
* **Fences.** A 4 KiB **fence page** (server-writable, client/kms read-only) holds a monotonically
  increasing completed seqno per queue plus a reset generation. Syncobjs are server objects holding
  `(queue, seqno)` or a signalled flag, with sync-file export as a Phoenix fd. Waiting: fast path =
  read the fence page (no IPC); slow path = a `WAIT` message the server answers from the IRQ thread
  when the seqno passes (deferred `msgRespond` — verify in E5). `WAIT_BO` = wait for the BO's last
  writer/reader seqno (a per-BO reservation, the `dma_resv` equivalent), which is also what gives
  **implicit sync** for buffers shared with X/compositor/KMS.
* **BO manager.**
  - GPU-only BOs: ordinary pages (**not** `MAP_CONTIGUOUS`) — the V3D MMU makes contiguity pointless
    and `vm_objectContiguous` rounds every BO to a power of two (§1.4). Allocate in 64 KiB groups so
    super/big pages (+1.4–8% on Pi OS [cited]) are possible later.
  - Scanout BOs are allocated by `rpi4-kms` and **imported** (Mesa already does this).
  - Every BO zeroed at allocation (Phoenix pages are not zeroed; this caused real wedges).
  - Lifetime: refcounts from handles, CPU mappings, exports, **and in-flight jobs** (a job pins
    every BO in its list until its fence signals — Linux semantics). On last release a BO's pages
    go to a server-internal quarantine: PTEs cleared → TLB flushed → wait for every queue to pass the
    release seqno → only then reused or returned to the kernel. This makes "a device writes a page
    after it was given back" structurally impossible *for V3D writes* — the C1 class of bug (§4.9).
  - Sub-allocation of small BOs from larger chunks is optional: Genode needed it to cut per-BO RPCs
    and capabilities; our round trip is 20 µs and STK creates ~300 BOs per race, so it is not needed
    for speed [measured + inferred].
* **Scheduling across clients:** per-client FIFO entities, round-robin per queue (drm_sched's shape),
  per-job timeout with Linux's "progress check" (CTnCA moved ⇒ extend), reset + fail the job's fence
  with an error + resume. This replaces today's "drop frame after ~0.8 s frozen" heuristic.
* **Isolation:** one shared GPU VA space (Linux parity on V3D 4.2). The server validates that every
  handle in a submit belongs to the caller, but a command list can still address any VA — document,
  don't pretend. Per-client page tables switched only at whole-GPU-idle points are possible (E10) if
  multi-tenant isolation is ever required; it costs cross-client overlap.
* **CPU-queue jobs** (v3dv timestamp/perf queries, indirect CSD): executed by the server, which maps
  every BO it allocated.
* **Shader cache** stays client-side (Mesa `disk_cache`), but key it by a real driver build-id
  (e.g. a hash of `libv3d` embedded at build time) to remove the manual `V3D_PHX_CACHE_VERSION`
  footgun that already produced GPU speckle once (`project_v3d_shader_disk_cache`).

### 4.4 Buffer sharing: a Phoenix dma-buf

**Today:** PA by value + `MAP_PHYSMEM` (any process can map any physical address — `vm_objectPage`
returns `page_get(offs)` for `VM_OBJ_PHYSMEM` with no owner check, `vm/object.c:382-390`), global BO
handles in the daemon, no refcount across processes, no revocation.

**Proposal — one kernel addition** (the only core change the design needs):

* `vm_objectExport(oid, vaddr, len, memtype)`: the caller (a server) turns pages it already has mapped
  into a `vm_object` that holds references to those pages, records the memory type (cached /
  write-combine / device), and inserts it into the existing object tree under `oid` =
  `{caller's own port, buffer id}` (the kernel checks the port belongs to the caller). Unexport drops
  the server's reference.
* `mmap(fd)` of that oid then hits `vm_objectGet()`'s tree lookup (`vm/object.c:68`) and maps the
  **same pages**; `object_fetchCluster()`/`proc_read` is never called because every page is present.
  Pages are freed only when the last mapping *and* the server's reference are gone.
* The kernel enforces the object's memory type on every mapping (no cached/uncached aliasing of one
  page — the lesson of the stale-dirty-line bugs, `done/2026-09-04-uncached-page-stale-cache-rootcause.md`).
* A PRIME fd is simply a Phoenix fd whose oid is the buffer's oid. It passes over AF_UNIX with the
  existing `SCM_RIGHTS` code (`posix/fdpass.c`), maps zero-copy, and its oid tells the other server
  which buffer it is (import = look up `{port, id}`, check the port is a known buffer provider, add
  the pages to its own PT / plane).
* How a client obtains an fd for an oid without a devfs path is an open detail: either the server
  resolves a tiny namespace (`/dev/dri/buf/<id>` via its own lookup) or libphoenix gains a call that
  wraps an oid the caller received from a server into an fd. Decide in E1.
* Contiguous memory for scanout: `rpi4-kms` reserves one pool at start (e.g. 64–96 MiB, below the
  32-bit bus limit — E6) and sub-allocates exact-size dumb BOs from it, instead of power-of-two
  `MAP_CONTIGUOUS` per buffer.

This also gives a path to retire `MAP_PHYSMEM` for non-driver processes later (a security win), and it
generalises to other producers — the HEVC decoder can export decoded SAND frames to `rpi4-kms` the
same way.

### 4.5 Display server (`rpi4-kms`)

**Stage A — firmware planes (M2).** The server takes the display from the firmware framebuffer
and drives it through the property mailbox (via `/dev/vcmbox`, project rule):

* Modes: `GET_NUM_DISPLAYS`, `GET_DISPLAY_CFG`, `GET_DISPLAY_TIMING`, `GET_EDID_BLOCK_DISPLAY`;
  mode change via `SET_TIMING`/firmware config.
* Planes: up to 8 per display via `SET_PLANE` (one mailbox transaction per plane update; our measured
  cost of a flip mailbox call is ~0.15 ms [measured, STK patch-0012 budget]). Primary, cursor and
  overlay planes; RGB and YUV (`num_planes`, `is_vu`, `color_encoding`) with hardware scaling.
* Vblank: map the SMI block (`0xfe600000`), `interrupt()` on GIC SPI 112 (Phoenix absolute IRQ 144,
  using the SPI+32 convention of `bcm-genet.c:237`), count vblanks per display
  (`SMIDSW0/1`), timestamp them, deliver `DRM_EVENT_VBLANK`/`FLIP_COMPLETE` into each client's event
  queue.
* Atomic commit: validate → for each plane with an in-fence, wait until the fence page shows it
  signalled (no IPC; if not yet, defer the commit to a later vblank) → issue `SET_PLANE`s → the event
  completes on the next vblank interrupt. Page flip = a one-plane atomic commit; async flips = no
  vblank wait.
* **Implicit sync** (Xorg modesetting/glamor flips pass no `IN_FENCE_FD`; Linux waits on the
  framebuffer BO's reservation): the per-BO last-writer seqno lives in `rpi4-v3d`. To keep flips
  IPC-free, `rpi4-v3d` also writes each *exported* BO's last-write `(queue, seqno)` into a slot of a
  shared read-only "resv page" that `rpi4-kms` indexes by buffer id; the fallback is one
  `rpi4-kms → rpi4-v3d` "fence for BO" query per flip (~20 µs). Decide in E5.
* Legacy: keep `/dev/fb0` semantics (read/write, `RPI4FB_GETMODE`) as an fbdev emulation on the
  primary plane so current `/dev/fb0` clients keep working during migration. **This does not cover
  the console**: fbcon (pl011-tty + teken) writes the plo-provided graphmode PA directly, not through
  `/dev/fb0` (`project_pi4_fb0_groundwork` deferred exactly this arbitration). With firmware planes
  the firmware framebuffer plausibly stays as the bottom layer under our planes — console visible
  when no plane covers it, which would be a feature — but that is an E3 question, not a given; a
  console handover (fbcon told to stop drawing, or redirected into a kms-owned buffer) is part of M2.
* Events are read from the card fd; `poll()` on it works through `atPollStatus`, and the server can
  honour the existing `block_ms` extension so the caller blocks in the server instead of spin-polling
  (the pattern lwip already uses, `posix.c:3136-3142`).

Caveats that make E3/E4 the first thing to run: the `SET_PLANE` layout and SMI vblank convention are
documented only by a GPL Linux driver (we use the wire format, not its code; the image-type enum has a
BSD-3 source in `raspberrypi/userland`); RPi deprecated fkms for the Pi 5
(not relevant to a Pi 4 with pinned firmware, but it caps the investment); whether the firmware needs
`dtoverlay=vc4-fkms-v3d` in `config.txt` to raise the SMI vblank interrupt is unknown; and one forum
report shows a full-screen X11 GLES app at **44 fps under fkms vs 141 fps under full KMS** [cited] —
the cause (firmware composition, X copies, or flip latency) is unknown and must be measured before
committing X11 to this path.

**Stage B — native HVS (M7, optional).** Keep the firmware's mode set and HDMI, but write our own
display list into HVS dlist memory and point `SCALER_DISPLIST` at it (no firmware round trip per
flip, async flips by rewriting one pointer word). Feasibility unknown (does the firmware rewrite the
dlist, e.g. on hotplug?) and the only complete reference is GPL — facts from the BCM2711 peripherals
document + register-level observation only. Full native modesetting (pixelvalves, HDMI PHY, SCDC) is
the largest item and is **not** needed for any milestone here.

### 4.6 libdrm-phoenix and Mesa

* **libdrm**: port upstream libdrm (MIT). Add a Phoenix backend under `drmIoctl()`: a table mapping
  each supported ioctl to a flattening function (v3d: 13 ioctls + MULTI_SYNC/CPU extensions; KMS:
  GETRESOURCES, GETCONNECTOR, GETENCODER, GETCRTC, GETPLANERESOURCES, GETPLANE, OBJ_GETPROPERTIES,
  GETPROPERTY, GETPROPBLOB, CREATEPROPBLOB/DESTROYPROPBLOB, ATOMIC, PAGE_FLIP, ADDFB2, RMFB,
  CREATE_DUMB/MAP_DUMB/DESTROY_DUMB, SET_CLIENT_CAP, GET_CAP, VERSION, WAIT_VBLANK,
  CRTC_GET/QUEUE_SEQUENCE; generic: GEM_CLOSE, PRIME ×2, SYNCOBJ ×~10). `drmGetDevices2()` gets a
  Phoenix implementation (a static two-node list; there is no sysfs). Estimate 3–5 k lines [inferred;
  Genode's per-driver files are 1.0–1.7 k each].
* **Mesa**: build v3d gallium + v3dv + kmsro + GBM + EGL (platforms `surfaceless, drm/gbm, x11,
  wayland`) + Vulkan WSI (display, xcb, wayland). Delete the in-process winsys and the Phoenix hooks
  that only exist because there was no window system (`should_tile` scanout gate,
  `phx_scanout_flip_gate`, `set_next_scanout`, the `__phoenix__` bufmgr hooks). Keep genuine fixes
  (they are driver bugs regardless). Dynamic loading: Phoenix has `dlopen` (T-DYNLINK Phase A), but
  a static "megadriver" link is simpler for the first milestones (E7).

### 4.7 Presentation paths

| Path | Mechanism | What it removes vs today |
|---|---|---|
| **Full-screen GL/GLES** | EGL on GBM: scanout BOs from `rpi4-kms` imported into v3d; `eglSwapBuffers` → atomic commit with the render's out-fence; flip at vblank; buffer released on the flip event. SDL2's upstream **KMSDRM** backend does exactly this. | `glFinish()` before flip (CPU waits for GPU), the FBO-0 redirect class of bugs, triple-buffer requirement; adds pacing |
| **Full-screen Vulkan** | v3dv `VK_KHR_display` (uses KMS through libdrm; swapchain images are dumb BOs, `device_alloc_for_wsi`) or SDL2 KMSDRM + Vulkan | vkQuake's hand-rolled scanout-alias WSI |
| **X11** | **Xorg server (hw/xfree86) + modesetting DDX + glamor (EGL/GBM) + DRI3 + Present** — Pi OS's stack until 2023. Root pixmap in a scanout BO; full-screen windows page-flipped by Present; otherwise glamor composites GPU→GPU. GL/VK clients render into their own BOs and hand dma-buf fds to the server (DRI3, over the X socket with `SCM_RIGHTS`). Needs a small xf86 input driver for `/dev/kbd0`/`/dev/mouse0` (port from our kdrive DDX). | 77 ms `glReadPixels`+`write()` desktop present; 1.2 MB-per-frame `XPutImage` for GL windows |
| **Wayland** | Weston (DRM backend) first — smaller dependency set — then labwc/wlroots if Pi OS parity of the desktop matters; `linux-dmabuf` for clients, Xwayland for X apps. Both need an input backend (libinput/udev shim or a native backend) — the largest non-GPU item. | X server + compositor both touching every frame (Pi OS's stated reason for switching) |
| **Video** | HEVC decoder exports SAND/NV12 frames; `rpi4-kms` shows them on a YUV overlay plane with hardware scaling | the 41.5 ms-per-frame blit (21.7 fps → decode-bound) [inferred] |

Expected performance *targets* per path, all [inferred] and to be measured: full-screen GL at
GPU-bound rate with CPU overlap (the STK share of that depends on E2); windowed GL in X at the client's
render rate minus one GPU blit per frame (vs 14.2 fps now); the desktop present at vblank rate
instead of 25.6/s with a 77 ms full-screen copy.

### 4.8 What is reused and what is replaced

| Existing | Fate |
|---|---|
| `v3d_gpu.c` / `rpi4-v3d.c` daemon (power, MMIO, PT, VA, BO table, CL/TFU/CSD submit, wedge reset) | **reused** as the render server core; submit path rewritten async + IRQ |
| `v3d_phoenix_power.c` (vcmbox power/clock with read-back) | reused (server only) |
| All Linux-parity GPU fixes (BO zeroing, binner overflow, MISCCFG, INT clear, `dsb`, L2T ordering, QPU-int ack, VA window) | reused |
| `libv3d-client.c` + `v3d_rpc.h` | grows into the v3d half of libdrm-phoenix |
| In-process winsys `v3d_phoenix_winsys.c` (3620 lines) and its copy inside libv3dv | **retired** after M1 |
| Scanout aliasing, `set_next_scanout`, firmware-pan flip, FBO-0 redirect, Y-flip gate, RASTER size gate | **retired** after M3 (replaced by GBM/EGL/KMS) |
| `rpi4-fb` `/dev/fb0` | becomes fbdev emulation inside `rpi4-kms` |
| plo's 3×-tall firmware framebuffer + `max_framebuffer_height=4096` | kept only for boot splash/fbcon until M2, then reduced |
| kdrive Xphoenix + fbdev DDX + glamor shim | kept as fallback until M4 lands, then retired |
| Mesa `resource_from_handle(TYPE_SHARED)` Phoenix route (`3b339c93a07`) | superseded by standard FD/PRIME import |
| SDL2 Phoenix video driver | input/events kept; video/GL moves to KMSDRM (or the driver calls GBM/EGL) |
| Shader disk cache | kept; keyed by build-id |

### 4.9 Risks designed for

* **C1** (`docs/c1-heap-corruption.md`): a 32-bit `0x8000000x` store at page+4 of recycled physical
  pages, root cause unknown; `0x8000000x` is also the property-mailbox response code and a DMA
  descriptor OWN bit. The design does not speculate on the writer; it makes two things structural:
  (1) V3D-visible pages are never returned to the kernel before PTE removal, TLB flush and fence
  completion (4.3); (2) scanout pages are never freed while a plane may still scan them (kms holds a
  reference until the flip *away* completes). Caution: Stage-A KMS adds one mailbox transaction per
  plane update per frame — if C1 turns out to be a late mailbox response, that multiplies the
  exposure. All mailbox traffic stays on `/dev/vcmbox` with its fixed bounce buffer; watch the C1
  rate when M2 lands.
* **The V3D clock race**: power/clock/reset are centralised in one server through `/dev/vcmbox` with
  read-back and checked returns; no client ever touches power again (removes the class that hung
  processes forever with SError masked, TD-10).
* **Cache coherency**: one memory type per exported object, enforced by the kernel; `dsb sy` before
  every kick; kernel clean+invalidate on new uncached mappings (already in `pmap.c`).
* **Firmware dependence** (Stage A): firmware pinned by SHA (`project_quake_flicker_firmware_pin`);
  any bump re-runs the M2 display gate.
* **Wedge recovery goes global** in a multi-client server: fail the fence with an error, resubmit is
  the client's decision (Mesa handles `-EIO` on submit poorly — keep drop-frame semantics for GL).
* **Contiguous memory** for scanout: fixed pool at boot; fragmentation stays inside `rpi4-kms`.

---

## 5. Phased plan

Each milestone ends in an HDMI-visible demo, a manifest, and a measurement against the previous
milestone and against Raspberry Pi OS (§5.3). Estimates are engineer-weeks for this project's
working mode (agent-driven, one Pi, serialized hardware cycles) and are [inferred].

| # | Milestone | Demo on the Pi | Effort | Expected result |
|---|---|---|---|---|
| **M0** | Linchpins: E1 kernel `vm_objectExport` prototype + two-process probe; E2 split STK submit time into flushes / bin / render / CPU gap; E3 firmware-plane probe (set a plane to a CPU buffer, SMI vblank IRQ at 60 Hz, flip latency, two planes); E5 deferred `msgRespond` | probe logs + a moving square on a plane, vsync-counted | 1–2 wk | decides M1's payoff and M2's viability |
| **M1** | Async render server: V3D IRQ, bin/render/TFU/CSD/CPU queues, fence page, syncobjs, MULTI_SYNC, per-BO reservations, quarantine; games linked against the client library (in-process winsys removed); firmware-pan present kept temporarily | all five games + glamor X at once through one server; flipstat vs today | 3–5 wk | Quake family: small gain; STK: bounded by E2's CPU/serial share [no prediction]; any number of GPU clients |
| **M2** | `rpi4-kms` Stage A: modes, planes, SMI vblank, atomic commit, page flip + events, contiguous dumb-BO pool, fbdev emulation, console handover | kmscube-equivalent (CPU-drawn then GPU-rendered via the M1 server) at vsync; flip-latency histogram | 3–4 wk | vsync-paced flips with events; throughput vs Pi OS KMS clear test (141–206 fps vsync off) |
| **M3** | Kernel export productised; libdrm-phoenix (v3d + KMS + PRIME + syncobj); Mesa GBM + EGL (gbm, surfaceless); SDL2 KMSDRM; games moved to EGL/GBM; Phoenix present hacks deleted | kmscube, glmark2-es2-drm, glmark2-es2 off-screen, all games via SDL2 KMSDRM | 4–6 wk | glmark2-es2 off-screen within ~20% of Pi OS (256–697 by era) [target] |
| **M4** | Xorg + modesetting + glamor + DRI3/Present + xf86 input driver; GLX/EGL-x11 via DRI3 | Window Maker desktop; glxgears/glmark2-es2 windowed; `gl-x11-window` at render rate | 4–6 wk | GL window from 14.2 fps to render-bound; desktop updates at vblank |
| **M5** | Vulkan WSI: `VK_KHR_display`, xcb (DRI3/Present) | vkcube, vkmark, vkQuake via WSI (vkQuake glue retired) | 2–3 wk | vkmark vs Pi OS 311 [target] |
| **M6** | Wayland: Weston DRM backend + input backend + linux-dmabuf; Xwayland; (labwc as stretch) | Weston desktop with a GL and a Vulkan client, glmark2-es2-wayland | 4–8 wk | Pi OS-like composited desktop |
| **M7** | Optional: native HVS display lists (firmware mode kept); later full native modesetting | async flips without mailbox | 8–16 wk | removes firmware round trip per flip |

**Total M0–M6 ≈ 21–34 weeks (≈ 5–8 months); with M7 ≈ 7–12 months.** M1 alone is worth doing even if
nothing else is: it is the one change that attacks the STK gap and it retires the duplicated
in-process winsys. M2+M3 together are the point where "any GBM/EGL/KMS app runs unmodified".

### 5.1 Ordering rationale

Render first (M1) because it needs no new kernel facility and its payoff is measurable immediately on
existing games. Display next (M2) because it is independent of libdrm and de-risks the biggest
unknown (firmware planes). The kernel object and libdrm (M3) are needed by everything after. X11
(M4) before Wayland (M6) because the project already has an X desktop and X clients, and because
DRI3/Present exercise every buffer-sharing mechanism a compositor needs.

### 5.2 Gates per milestone (this project's rules)

`--scope core` rebuild + `strings` verification for any core change; 0 faults; the five-game
showcase gate plus the X desktop gate (`scripts/grade-x-desktop-video.py`); flipstat measured over
gameplay windows only; multi-trial for anything intermittent; manifest recorded.

### 5.3 Measuring against Raspberry Pi OS

Same Pi 4, same HDMI monitor and mode (1080p60), an SD card with **Raspberry Pi OS Bookworm 64-bit**
(labwc Wayland default; also run the X11 session) and the Phoenix netboot/SD image, `performance`
governor on Linux (the governor alone moved glmark2 from 155 to 256 [cited]):

1. **Micro:** glmark2-es2 off-screen 800×600 (score + per-scene), glmark2-es2-drm, kmscube (vsync
   on/off), a full-screen 1080p clear/texture flip test (vsync off), vkmark, vkcube.
2. **Games, built from the same sources and data on both:** quakespasm `timedemo demo1`, yquake2
   `timedemo`, quake3e `timedemo four.dm_68` at 720p and 1080p, vkQuake timedemo, STK
   `--profile-laps` on a fixed track and graphics level (record the "scene complexity" line) — Pi OS
   figures for these games are mostly unpublished, so we generate our own baseline.
3. **Desktop:** GL-in-a-window test (`gl-x11-window` on both), desktop present latency (camera or
   HDMI capture of a timestamped client), X vs Wayland.
4. Record for every number: vsync on/off, display stack, resolution, governor, Mesa version, firmware.
   On Phoenix, fps from the winsys/kms flip counter; on Linux, the benchmark's own counter.

---

## 6. Open questions and experiments

| ID | Question | Experiment | Decides |
|---|---|---|---|
| **E1** | Can the kernel expose server-owned pages under an oid so `mmap(fd)` maps them zero-copy, refcounted, with an enforced memory type? How does a client get an fd for an oid? | prototype `vm_objectExport`; two-process probe: same PA both sides, coherent both directions, survives server unmap until client unmaps; fd via server-resolved path vs new libphoenix call | M3 (and the security story) |
| **E2** | What is STK's ~88% "in submit" made of? | timestamp each `ioc_submit_cl`: pre-kick flushes, bin spin, render spin, and the CPU gap between submits; sum per frame | how much M1 closes the STK gap |
| **E3** | Does the pinned firmware honour `SET_PLANE` for arbitrary contiguous buffers, several planes, and raise SMI vblank IRQs without a vc4-fkms DT overlay? What does fbcon/the firmware fb do meanwhile? | small probe driving a CPU-drawn plane + counting SMI IRQs over 10 s | M2 Stage A viability |
| **E4** | Why 44 fps (fkms) vs 141 fps (KMS) in the one published comparison? | M2 probe: flip loop with a trivial GPU clear, vsync off, measure flips/s and SET_PLANE latency | whether X/Wayland can sit on Stage A or need Stage B |
| **E5** | Can a Phoenix server answer a request (`msgRespond`) from a thread other than the one that received it, much later? Does `atPollStatus` + `block_ms` work for a device server? Resv page vs per-flip query for implicit sync? | two-thread server probe; flip-path timing | fence-wait, event and implicit-sync design |
| **E6** | Which physical range can the HVS/firmware scan (32-bit bus, alias of low 1 GiB?) and how big must the scanout pool be? | allocate in low/high RAM, set plane, observe | contiguous pool placement |
| **E7** | Mesa EGL/GBM + loader on Phoenix: static megadriver vs `dlopen`; `drmGetDevices2` without sysfs | build only | M3 build approach |
| **E8** | Xorg hw/xfree86 + modesetting on Phoenix: libpciaccess/udev dependencies; input driver | build only | M4 size |
| **E9** | Does per-client isolation (switch PT only when the GPU is idle) cost acceptable overlap? | only if multi-tenant security becomes a requirement | optional |
| **E10** | Does the "fix-A" extra waited L2T flush remain necessary once submits are async? | A/B over ≥10 boots with the wedge counters | M1 cache sequence |
| **E11** | Native HVS: does the firmware rewrite HVS display lists after boot (hotplug, mode change)? | read `SCALER_DISPLIST0` and dlist memory over time with HDMI replug | Stage B feasibility |

**Unresolved facts carried from the survey:** NetBSD's wiki claims a completed VC4 DRM port but HEAD
has none; the firmware's behaviour for fkms without the DT overlay; exact v3dv paths that still issue
SYNCOBJ TRANSFER/QUERY with timelines off.

---

*Sources: project docs cited inline (`docs/done/2026-08-13-dri-drm-design.md`,
`docs/misc/2026-08-22-concurrent-gpu-v3d-server-feasibility.md`,
`docs/misc/2026-09-09-gl-window-buffer-sharing-work-order.md`, `docs/knowledge/gpu-vc6-non-linux.md`,
`docs/done/2026-06-16-drm-multiclient-gpu-model-plan.md`, `docs/misc/2026-09-16-stk-fps-not-fill-bound.md`,
`docs/KNOWN-ISSUES.md`, `docs/c1-heap-corruption.md`, `docs/PHOENIX-RTOS-RPI4-CHANGES.md`); external
performance sources: forums.raspberrypi.com t=270951, t=304534, t=247677, t=281183, t=279683;
github.com/glmark2/glmark2/issues/186; github.com/geerlingguy/sbc-reviews/issues/4;
blogs.igalia.com/itoral (2020-07-23, 2021-03-16, 2023-02-20); mairacanal.github.io (super pages,
2024-10); raspberrypi.com/news (Bookworm, Oct 2023; labwc, Oct 2024); jeffgeerling.com (2022-02);
elektormagazine.com (Pi 4 review, 2019); raspberrypi/documentation overclocking.adoc;
raspberrypi/firmware#1818. Genode: repos/os/include/gpu_session, repos/libports/src/lib/libdrm,
doc/release_notes (22.02, 23.02, 24.05, 24.11, 25.11). Fuchsia: fuchsia.dev Magma design,
magma.h/magma.fidl/coordinator.fidl. Haiku: github.com/X547/RadeonGfx, X547/nvidia-haiku. Redox:
redox-os.org/news/this-month-260331. QNX: qnx.com Screen/wfd-server docs.*

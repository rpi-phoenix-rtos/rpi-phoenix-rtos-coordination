# GLQuake (or equivalent oldschool 3D) Bring-Up Plan for Phoenix-RTOS on Pi 4

> **Related**:
> - `docs/knowledge/gpu-vc6.md` — Linux-flavoured VC6 GPU research brief
>   (HVS / PV / V3D / mailbox / Mesa).
> - `docs/knowledge/gpu-vc6-non-linux.md` — non-Linux community work on
>   the same hardware (FreeBSD / NetBSD / Circle / Ultibo / LdB-ECM).
> - `docs/inprogress/wifi-bringup-plan.md` — template for tier shape.
> - `docs/todo/bluetooth-bringup-plan.md` — template for "we may stop at
>   tier N and ship that" framing.

**Status**: research / planning. Not a scheduled task. This is the
user's dream-scenario PoC, scoped explicitly as best-effort but with
strong evidence the hardware-accelerated path is achievable.
**Owner**: open.
**Estimated effort**:
- Tier 0–2 (framebuffer + software-rendered Quake) — 6–10 focused
  iterations.
- Tier 3–6 (V3D scout, BO allocator, first hardware-rendered
  triangle, Quake-subset GL shim) — realistic **3–5 person-months**
  once Tier 2 is shipped, on the back of (a) the Random06457 /
  macoy.me bare-metal V3D reference code and (b) macoy.me's
  single-header V3D assembler + Lambda-V's Keller VC6 ISA reference.
  See §"The game-changer references" below.
- Tier 7+ (GLQuake itself + optional input/sound) — additional 1–3
  person-months for the Quake host-layer port + USB-HID / PWM-audio
  stretch.

**Total Tier 0 → Tier 7 honest budget**: 5–8 person-months — not
the 9–18 the first draft of this document estimated. The single
biggest driver of the reduction is the existing public MIT-licensed
V3D-on-Pi4 prior art.

## Scope summary

Dream scenario: run a hacked-together port of original GLQuake
(id Software 1996) full-screen on the Pi 4 HDMI output under
Phoenix-RTOS, using the BCM2711 VideoCore VI V3D 4.2 core for
hardware-accelerated 3D rendering. The first deliverable is an
attract-mode demo (e1m1 walkthrough) on HDMI; audio and input are
explicitly **stretch goals**.

Acceptable shortcuts: subsetting Quake's OpenGL surface, baking the
shareware PAK file into the image, single hard-coded resolution, no
save/load, no menu, no console, no mod loading, no sound.

This document is the same shape as `docs/inprogress/wifi-bringup-plan.md` —
tier-by-tier with a validation gate per tier, an honest verdict on
where the realistic stop line is, and references for every claim.

## The game-changer references

The most important discoveries during this research, in order of
impact:

**Random06457/rpi4-gpu-bare-metal-examples** —
<https://github.com/Random06457/rpi4-gpu-bare-metal-examples>,
MIT-licensed, ~80% C++ / 16% C / minor asm. The repo contains
`src/kernel/examples/gl_rainbow_triangle.cpp` — a complete,
working, **bare-metal V3D 4.2 triangle-on-Pi-4** that:

- Emits binning + render command lists (`bcl` / `rcl`) with all the
  packet types we'd need (`TileBinningModeCfg`, `StartTileBinning`,
  `GlShaderState`, `VertexArrayPrims`, `TileRenderingModeCfg*`,
  `MulticoreRenderingSupertileCfg`, `StoreTileBufferGeneral`,
  `OP_END_OF_RENDERING`, etc.).
- Ships **hand-encoded QPU shader binaries** for a vertex, coordinate,
  and fragment shader (about a dozen 64-bit instructions each) — the
  same task my draft previously called out as "1–2 months of solo
  work."
- Sets up `V3D_CLE_CT0QMA/QMS/QTS/QBA/QEA` (binning) and
  `V3D_CLE_CT1QBA/QEA` (rendering) registers directly via MMIO.
- Polls completion via `V3D_CLE_BFC` (binning fence count) and
  `V3D_CLE_RFC` (render fence count).
- Allocates BOs (`V3DBuffer` class) with physical-address awareness
  and tile-alloc / tile-state memory.
- Invalidates the L2T and slice caches between binning and
  rendering passes.

**Follow-on (Macoy Madson):** converting Random06457's work into a
"single-header C interface" V3D library
(<https://macoy.me/blog/programming/PiV3D>), with **100,000
randomly colored triangles at 177 fps 1080p** demonstrated on
bare-metal Pi 4. Two source trees:
- <https://macoy.me/code/macoy/rpi-system> — Circle fork being
  converted to C (84% C / 14% C++, ~16 MiB code).
- <https://macoy.me/code/macoy/rpi-bare-metal> — the bare-metal
  Pi 4 B coding project that hosts the `tools/` directory below.

Inside `rpi-bare-metal/tools/`:
- **`v3dAssembler.h`** — a V3D-shader assembler **in a single
  dependency-free header**, built by bundling Mesa's disassembler
  + validator. Makes hand-authoring QPU shader binaries tractable
  instead of byte-by-byte heroic.
- **`v3dSimulator.h`** — a bare-bones simulator for the V3D
  binning phase, runnable on any host platform — debug shader
  binaries on Linux x86-64 before submitting to the Pi.
- **`Shaders.org`** — Macoy's notes on the V3D shading language as
  used in bare-metal.

All MIT-licensed.

**Companion reference — Wolfgang Keller / Lambda-V** ("GPU
Programming: Raspberry Pi (VC4/VC6/VC7)") —
<https://www.lambda-v.com/texts/programming/gpu/gpu_raspi.html>.
Comprehensive instruction-set documentation for the VC4 and VC6
QPUs (field-by-field encoding of `sig`/`op_mul`/`cond`/`msfign`,
ALU instruction forms, branch instructions, load-immediate
instructions, register file `ra0-ra31`/`rb0-rb31`). The single best
human-readable reference for V3D 4.2 ISA — Broadcom has not
publicly released V3D 4.x specs (per
<https://docs.mesa3d.org/drivers/v3d.html>), so this article + the
macoy v3dAssembler.h together *are* the public specification.
Drafted 2021-11-01, last modified 2026-03-04.

Implications for this plan, in plain English:

1. Hand-authoring V3D shader binaries is a **solved problem in
   public, MIT-licensed code** — not a months-long unknown. The
   assembler is a single header file; the ISA is documented at
   Lambda-V; the simulator runs on the build host.
2. The full draw-one-triangle CL packet sequence already exists in
   readable C++; porting to Phoenix is mostly translation, not
   research.
3. Throughput on real hardware is ~100k triangles at 1080p, well in
   excess of GLQuake's ~5–20k triangles per frame at 640×480. The
   silicon is more than fast enough.
4. The Linux v3d kernel driver itself is **only ~209 KB / 18 source
   files** (per
   <https://api.github.com/repos/torvalds/linux/contents/drivers/gpu/drm/v3d>:
   `v3d_drv.c` 13 KB, `v3d_gem.c` 9.5 KB, `v3d_mmu.c` 4 KB,
   `v3d_submit.c` 34 KB, `v3d_regs.h` 28 KB, etc.) — a Phoenix
   reimplementation is firmly in the realm of "reasonable port" not
   "rewrite Linux." A Mesa port (option A in §"GL implementation
   choice") remains feasible long-term because Mesa supports
   `surfaceless` EGL platforms and "other flavors of Unix and
   Haiku" officially (per <https://docs.mesa3d.org/systems.html>,
   <https://docs.mesa3d.org/egl.html>) — the work is libdrm-shim
   + libc-shim + meson scaffolding, not "make Mesa run on
   non-Linux."

This is the central reason this plan does not bottom out at
"software Quake is the realistic ship target." The hardware path is
genuinely reachable on a multi-month rather than multi-year horizon.

## What's already in tree

Pi 4 already lights up the HDMI framebuffer end-to-end. The pieces
useful for graphics work that exist today:

- **`plo` mailbox framebuffer bring-up** — `sources/plo/hal/aarch64/
  generic/video.c` allocates a linear ARGB framebuffer at boot via
  the channel-8 property mailbox (`SET_PHYS_WH` / `SET_VIRT_WH` /
  `SET_DEPTH` / `SET_PIXEL_ORDER` / `ALLOCATE_BUFFER` / `GET_PITCH`,
  same recipe as Circle / rpi4-osdev / bztsrc), masks the returned
  base with `& 0x3fffffff` to convert the GPU-bus alias to ARM PA,
  and paints a blue "boot progress" panel. The chosen mode (width /
  height / bpp) is compile-time from `PLO_RPI_FB_*` in plo's config.
- **Graphmode hand-off to the kernel** — `video_publish()` calls
  `syspage_graphmodeSet(&graphmode)` with `{framebuffer, width,
  height, bpp, pitch}`. Kernel exposes this back to userspace via
  the `pctl_graphmode` arch-pctl call (see
  `sources/phoenix-rtos-kernel/include/arch/aarch64/generic/
  generic.h`). Tested path: `pl011-tty` reads `graphmode.framebuffer`
  and `mmap()`s it `MAP_PHYSMEM | MAP_UNCACHED | MAP_SHARED` to draw
  the post-`fbcon: ok` HDMI console.
- **Mailbox property channel from userspace** — `bcm-genet.c`
  already calls `GET_BOARD_MAC` (tag `0x10003`) and the WiFi/BT plan
  reuses the same transport for `SET_GPIO_STATE`. The transport
  itself is `physmmap` of `0xfe00b880` + the standard 28-byte
  property header + 16-byte alignment. Reusable verbatim for any
  future mailbox tag (clock state, V3D power domain, etc.).
- **`dmammap()` / `physmmap()`** — `sources/phoenix-rtos-lwip/
  drivers/physmmap.c`. `dmammap(size)` returns a page-aligned
  physically-contiguous uncached anonymous mapping (`MAP_PRIVATE |
  MAP_ANONYMOUS | MAP_UNCACHED | MAP_CONTIGUOUS`). This is exactly
  the allocator GENET uses for descriptor rings and would be the
  primitive for V3D BO allocation — physically contiguous chunks of
  ARM-visible DRAM that the GPU can DMA against. Today's call sites
  allocate at most ~256 KiB (GENET TX/RX rings); we have no proof
  the kernel can satisfy a 32–64 MiB `MAP_CONTIGUOUS` request, so
  that's a Tier-1 measurement.
- **`libgraph` 2D graphics library** —
  `sources/phoenix-rtos-corelibs/libgraph/`. Provides `graph_open`,
  `graph_line`, `graph_rect`, `graph_fill`, `graph_print` (with
  framebuffer font), `graph_move`, `graph_copy`, plus an adapter
  abstraction (`graph_adapter_t`: VGA, Cirrus, virtio-GPU, software).
  All current adapters are x86. A new `vc6` or `bcm2711-fb` adapter
  that wraps the syspage graphmode + mmap'd framebuffer would slot
  in cleanly. There is also a `_user/rotrectangle` demo that opens
  a `graph_t` and animates a rectangle — proven smoke test for any
  new adapter.
- **`phoenix-rtos-ports` pattern** — fetch-upstream + patches +
  `port.def.sh`. This is where a Quake / WinQuake / vkQuake port
  would live (alongside `lua`, `busybox`, `openssl111`, etc.).
- **`fbcon` minimal framebuffer console** in `pl011-tty` — proves
  the post-handoff framebuffer is writable from a userspace
  msg-port driver, that scrolling works, and that the firmware
  doesn't reclaim the page. Bytes-per-pixel and pitch handling all
  match what a userspace renderer would need.
- **No interrupt support for HDMI / V3D** today. The framebuffer is
  pure linear scanout; nothing arms vblank or V3D interrupts. Tier
  4+ adds a V3D IRQ handler (GIC SPI 74/75 per BCM2711 DT).

## What's NOT in tree

Grouped by tier of need:

1. **`vc6-fb` libgraph adapter** — currently `graph.h` enumerates
   only x86 / virtio adapters. A simple BCM2711 backend that pulls
   the framebuffer base/pitch from the syspage graphmode, mmaps it,
   and implements the 2D primitives in software (memcpy / memset /
   per-pixel) is the minimum to call this a "graphics-capable"
   Phoenix port.
2. **An audio driver.** Pi 4 has both PWM-audio over the headphone
   jack and I2S/HDMI audio via VideoCore. None in tree. **Decision:
   skip audio entirely for the PoC.** Quake without sound is still
   recognizably Quake.
3. **A keyboard input path.** USB HID is parked (USB-HCD is task
   #26, dependent on USB stack bring-up which is statistically
   flaky today — see `docs/inprogress/status.md`). **Decision: skip input for
   the v1 PoC** (attract-mode demo only). If USB-HID lands later, a
   second pass adds WASD movement.
4. **A filesystem capable of holding `pak0.pak`.** The shareware
   `id1/pak0.pak` is ~18 MiB; the registered set is ~30 MiB. Phoenix
   on Pi 4 currently runs out of dummyfs (rootfs embedded in the
   image). Two options: (a) bake PAK into the image via the same
   dummyfs path used for binaries, (b) TFTP-pull at boot like the
   WiFi-blob staging plan considers. Embedding is simplest and the
   image size is acceptable for a PoC.
5. **A C stdlib `malloc` budget large enough for Quake.** GLQuake
   allocates a single ~16 MiB hunk via `Hunk_Alloc` at startup
   (`-mem 16` is the canonical command line). Phoenix's userspace
   `malloc` (`libphoenix`) is heap-backed; the heap size for the
   psh process tree on Pi 4 is whatever the kernel grants on first
   `sbrk`/`brk` — needs measurement before we know if 16 MiB is
   easy or hard.
6. **A V3D 4.2 driver.** Nothing in Phoenix. References we will
   port from / read alongside:
   - **Random06457/rpi4-gpu-bare-metal-examples** — primary
     reference (see §"The game-changer reference"). MIT.
   - **macoy.me PiV3D / rpi-system** — the single-header C version
     and the 100k-triangles demo. MIT.
   - **Linux `drivers/gpu/drm/v3d/`** —
     <https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/v3d>,
     ~209 KB / 18 files, GPL-2.0. Useful as reference but cannot
     directly land in Phoenix.
   - **Mesa `src/broadcom/` and `src/gallium/drivers/v3d/`** —
     <https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/broadcom>
     and <https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gallium/drivers/v3d>.
     MIT. Hundreds of kLoC; useful as packet-encoder reference (the
     `cle/` subdir) and as a shader-compiler reference (the
     `compiler/` subdir), not as a wholesale port target.
7. **A V3D BO (Buffer Object) allocator + MMU page-table manager.**
   V3D 4.2 has a single-level page-table MMU mapping a 4 GiB virtual
   range; the page table itself must be physically contiguous (~4
   MiB worst case). Linux uses `shmem`-backed pages plus a per-BO
   IOMMU mapping; Phoenix would do a much simpler "carved-out
   contiguous pool + bitmap allocator" — `dmammap()` for the pool,
   a bitmap allocator on top. **For the first few Tier-4 iterations
   we can skip the V3D MMU entirely and run with identity-mapped
   physical addresses, the way Random06457's example does.**
8. **A GL implementation that talks to V3D.** Three honest options:
   - **(A) Mesa v3d Gallium driver port.** Mesa is MIT-licensed and
     officially supports headless (`-Dplatforms=surfaceless`) and
     direct-DRM (`-Dplatforms=drm`) EGL modes
     (<https://docs.mesa3d.org/egl.html>: "egl_dri2 supports android,
     device, drm, surfaceless, wayland and x11"). The build does
     **not** strictly require X11/Wayland. The hard dependencies are
     **glibc-ish libc**, **pthread**, **libdrm**, **meson + python**,
     and a Linux DRM uAPI for the kernel side. Phoenix has none of
     those today. A Mesa port would need a tiny `libdrm`-shim that
     turns `DRM_IOCTL_V3D_*` ioctls into Phoenix msg-port calls
     against our own V3D driver, plus a libc-compatibility shim. Not
     trivial, but **also no longer the multi-year mountain my
     original draft suggested** — Mesa's stated platforms include
     "other flavors of Unix and Haiku"
     (<https://docs.mesa3d.org/systems.html>), so the cross-platform
     scaffolding exists.
   - **(B) Hand-rolled "GL-shaped" shim** that intercepts the ~60
     OpenGL entry points GLQuake actually uses and emits V3D Control
     Lists directly, modeled on Random06457's `cl_example.cpp`. ~5–10
     kLoC of new code. Owns the world; no external dependencies.
   - **(C) `llvmpipe` software rasterizer** via OSMesa. Mesa's
     software fallback. Useful only if (A) and (B) both fail; brings
     in LLVM (huge dependency for Phoenix). At GLQuake's resolution
     and triangle count, OK perf is plausible but the dependency
     cost is brutal.
9. **GLQuake source itself.** id Software released the full Quake
   source under GPL-2.0 in 1999
   (<https://github.com/id-Software/Quake>). The cleanest fork for
   porting is **Quakespasm**
   (<https://github.com/sezero/quakespasm>) — actively maintained,
   SDL2-based, OpenGL 1.x renderer; the SDL2 dependency would have
   to be either stubbed out or replaced with a tiny Phoenix-side
   "fake SDL" shim (libgraph for video, /dev/null for sound, no
   input). **Quakespasm has both a software and a GL renderer**, so
   Tier 2 can use the software path and Tier 7 can flip to the GL
   path without changing source repos.
10. **The PAK files** — shareware `pak0.pak` (~18 MiB) is freely
    redistributable per id's shareware EULA. Registered content
    requires a purchased Quake key; out of scope.

## V3D in detail (so you don't have to re-research)

The 3D engine on BCM2711 is **V3D 4.2** (Broadcom internal ID
`V3D-7268`-class). Kernel reports `revision 4.2.14.0, MMU: yes`. It
sits at ARM-side MMIO `0xfe004000` (per upstream Linux DTB
`arch/arm64/boot/dts/broadcom/bcm2711.dtsi`'s `v3d@7ec04000` node —
VPU bus `0x7ec04000` → low-peri `0xfe004000`). Interrupts: **GIC SPI
74 (`v3d hub`) and 75 (`v3d core 0`)** per the same DTB.

Three submission queues:

- **CL (Control List)** — bin/render tile-based graphics pipeline.
  The only queue GLQuake-style fixed-function rendering needs.
- **TFU (Texture Formatting Unit)** — async texture format
  conversion (RGBA8 ↔ T-format-tiled, mipmap chain). Optional for a
  Quake-class workload; we pre-tile textures on the CPU at load
  time.
- **CSD (Compute Shader Dispatch)** — async compute. Unused;
  ignore.

Key V3D architectural facts that govern Phoenix integration cost:

- **Single-level page-table MMU.** 4 GiB virtual range. Page table
  is physically contiguous (~4 MiB worst case). Random06457's
  example **does not enable the MMU** — it submits CLs using
  physical addresses directly, which works on Pi 4 V3D 4.2 because
  the MMU is software-configurable (Linux always enables it for
  isolation between DRM clients, but bare-metal can run without).
  Phoenix Tier 4 inherits that simplification.
- **Tile-based deferred renderer (TBDR).** Geometry is binned
  per-screen-tile in a first pass, then each tile is rasterized
  independently in a second pass with tile memory acting as an
  on-chip framebuffer. Quake's frame is small (320×240 to 640×480)
  so the binning cost is negligible.
- **Shader cores**: 4 QPUs per slice, 2 slices on BCM2711's V3D
  4.2. Shader ISA is Broadcom-proprietary; the **only** open-source
  shader compiler for V3D is `src/broadcom/compiler/` in Mesa. But
  **we don't need a compiler.** GLQuake's fragment work is "fetch
  one texture, multiply by lightmap, emit" — ~10 QPU instructions.
  Random06457 already ships a working vertex + coordinate +
  fragment shader as hand-encoded 64-bit instructions. For Quake
  we author two more variants (with-texture, with-lightmap) the
  same way.
- **CL submission.** The control list is a sequence of typed
  packets. Mesa's `src/broadcom/cle/v3d_packet_v42.xml` enumerates
  ~200 packet types. The **subset Quake needs** is 15–25 packets,
  all already present in Random06457's `cl_example.cpp`'s emitter
  classes.

V3D driver `drivers/gpu/drm/v3d/` file inventory (size from
GitHub API, verbatim):

| File | Size |
| --- | --- |
| `v3d_drv.c` | 13.5 KB |
| `v3d_drv.h` | 17.4 KB |
| `v3d_gem.c` | 9.6 KB |
| `v3d_submit.c` | 34.0 KB |
| `v3d_sched.c` | 23.3 KB |
| `v3d_bo.c` | 7.3 KB |
| `v3d_mmu.c` | 4.0 KB |
| `v3d_irq.c` | 9.1 KB |
| `v3d_regs.h` | 28.3 KB |
| `v3d_perfmon.c` | 21.6 KB |
| `v3d_fence.c` | 1.1 KB |
| `v3d_debugfs.c` | 9.5 KB |
| others | ~30 KB |
| **total** | **~209 KB / 18 files** |

A Phoenix port that omits perfmon, debugfs, sysfs, scheduler
(Phoenix has its own), and most of `v3d_submit.c`'s job-queueing
infrastructure (we don't need multi-client isolation) is realistically
~5–10 kLoC.

Public Linux V3D uAPI (from
`include/uapi/drm/v3d_drm.h`): **13 ioctls total** —
`SUBMIT_CL`, `WAIT_BO`, `CREATE_BO`, `MMAP_BO`, `GET_PARAM`,
`GET_BO_OFFSET`, `SUBMIT_TFU`, `SUBMIT_CSD`, `PERFMON_*` (4
variants), `SUBMIT_CPU`. For Quake-on-Phoenix, the minimum useful
set is **`SUBMIT_CL`, `CREATE_BO`, `WAIT_BO`, `GET_BO_OFFSET`** — 4
ioctls, easily wrapped behind a Phoenix msg-port driver.

## Bring-up tier plan

### Tier 0 — confirm userspace framebuffer access (mostly done)

Goal: a Phoenix userspace process can render arbitrary pixels to
the HDMI screen.

State today: `pl011-tty`'s `pl011_fbcon_init` already does this for
text. We've never written a non-tty userspace process that opens the
syspage graphmode, mmaps the framebuffer, and draws.

- Sub-step 0a: write a 10-line `fbtest` that calls the `pctl_graphmode`
  arch-pctl to read `{framebuffer, width, height, pitch, bpp}`, mmaps
  the framebuffer with `MAP_PHYSMEM | MAP_UNCACHED | MAP_SHARED`, and
  fills it with a solid color, then sleeps. Validate visually on the
  HDMI panel and via `artifacts/hdmi/*tick*.png` snapshots from the
  test cycle.
- Sub-step 0b: implement a `vc6-fb` (or `bcm2711-fb`) backend for
  `libgraph` so `_user/rotrectangle` builds for `aarch64a72-generic-rpi4b`
  and runs. ~150–250 LoC modeled on the software backend
  (`soft.c`) plus the syspage-graphmode hookup.
- **Validation gate**: the existing rotrectangle demo runs on the Pi
  4 HDMI panel. HDMI screenshot in `artifacts/hdmi/` shows a
  rotating rectangle.

Estimated effort: 1–2 iterations. Almost zero risk.

### Tier 1 — `fbtest2`: arbitrary pixel-level demo + perf floor

Goal: prove we can do real-time animation from userspace and measure
the achievable framerate of a plain memcpy/memset scanout path. This
also forces the kernel to demonstrate the upper end of `MAP_CONTIGUOUS`
sizing — we ask for a 32–64 MiB chunk via `dmammap` (will be needed
in Tier 5) and confirm the request succeeds before we sink time into
V3D code that depends on it.

- A 200×200 sprite bounces full-screen at 60 Hz; measure actual fps
  via the diag-udp `t` probe (per-thread cpuTime). The Cortex-A72 at
  1.5 GHz running uncached writes to the framebuffer is the perf
  floor; if this is below ~30 fps at 320×240, software-rendered
  Quake is already in trouble.
- Decide: leave the framebuffer mapped uncached (current pl011-tty
  setup) vs. switch to write-combining via a new MAP flag. The
  current `MAP_DEVICE | MAP_UNCACHED` mapping is right for fbcon
  text (one byte at a time, no need for fences) but is brutal for
  full-frame redraws. Linux uses `pgprot_writecombine` for scanout
  surfaces; Phoenix would need an equivalent.
- Validate `dmammap(32 << 20)` and `dmammap(64 << 20)` both succeed
  on the Pi 4 image. Document the answer in the manifest.
- **Validation gate**: 320×240 full-frame redraw at ≥60 fps on
  HDMI; `dmammap(64 << 20)` returns non-NULL.

Estimated effort: 2–3 iterations.

### Tier 2 — Quakespasm software renderer port

Goal: actually run Quake on Phoenix-RTOS, with the original software
renderer, no GPU involvement. The realistic "ship if everything else
fails" deliverable of this plan.

- Port **Quakespasm** in software-renderer mode
  (<https://github.com/sezero/quakespasm>). Place under
  `phoenix-rtos-ports/quakespasm/` following the existing port pattern
  (`config`, `patches`, `port.def.sh`).
- The software renderer writes to a back-buffer in plain malloc'd
  memory; then `memcpy` to the framebuffer at vsync (we don't have
  vsync; just memcpy continuously). 320×240×8bpp = 76800 bytes per
  frame; 60 fps = 4.6 MB/s — trivial.
- Phoenix-side glue:
  - Stub `Sys_*` (Quake's host-abstraction layer): `Sys_FileOpen`
    → `open()`, `Sys_FloatTime` → `clock_gettime(CLOCK_MONOTONIC)`,
    `Sys_Error` → `fprintf + exit`.
  - Stub `IN_*` (input) to a no-op until USB HID lands.
  - Stub `S_*` (sound) to a no-op.
  - Replace the `VID_*` (video) backend with a libgraph-or-direct-
    framebuffer one. Quake expects 8-bit indexed video with a
    256-entry palette; we run libgraph in 8bpp mode (the firmware
    framebuffer supports 8bpp per Circle's `bcmframebuffer.cpp`)
    or do an in-process palette lookup to 32bpp every frame.
  - Bake `id1/pak0.pak` into the rootfs image via dummyfs.
  - Command line: `quake -basedir /quake -nosound -nomouse +map
    e1m1` (attract mode).
- **Validation gate**: HDMI panel shows the Quake start screen, the
  title sequence runs, and e1m1's first room renders correctly
  (compare to a Linux software-Quake reference screenshot).

Estimated effort: 4–6 iterations. The bulk of the work is
porting/stubbing Quake's host layer, *not* graphics.

**This is the realistic safety-net ship line.** Everything below
is the actual dream-scenario hardware-accelerated path.

### Tier 3 — V3D MMIO scout

Goal: confirm the V3D core is alive and we can talk to it from
Phoenix userspace, before committing to the larger Tier 4+ work.
Mirrors the SDHCI / GENET scouts that landed for WiFi and Ethernet
bring-up.

- Add a diag-udp probe (`v` for V3D) in the lwip-port responder.
  `physmmap(0xfe004000, 0x10000)` for the V3D HUB + CORE region.
  Read identity registers — `V3D_HUB_IDENT0/1/2/3` at offsets
  `0x0`/`0x4`/`0x8`/`0xc` from HUB base, per Linux's `v3d_regs.h`.
  Expected: `IDENT0` reads ASCII "V3D" + generation byte; check
  `V3D_HUB_IDENT0_ID_SHIFT`.
- Random06457's example assumes the V3D is powered on at boot. On
  our image it may be power-gated (Phoenix has never asked the
  firmware to power it up). Mitigation: call mailbox
  `SET_POWER_STATE` for the V3D domain (`RPI_FIRMWARE_POWER_DOMAIN_V3D`
  = ID `10` per `include/dt-bindings/soc/bcm2835-pm.h`) and re-read.
- Also read `V3D_CLE_BFC` / `V3D_CLE_RFC` to confirm the fence
  counters increment to 0 at power-on as expected.
- **Validation gate**: read a non-zero `V3D_HUB_IDENT0` from
  Phoenix userspace, confirming the core is alive and accessible.
  Capture the IDENT register snapshot the same way the SDHCI Tier
  1a scout captured the controller state.

Estimated effort: 1 iteration. Pure scouting.

### Tier 4 — port Random06457's triangle to Phoenix

Goal: replicate the rainbow triangle on Phoenix-RTOS userspace.

This is the **single highest-leverage step** in the plan. We are
not authoring novel V3D code; we are translating an existing
working MIT example to Phoenix's syscall surface.

- Stage Random06457's `cl_example.cpp` and supporting files
  (`device/v3d/v3d_cl.hpp`, `device/v3d/v3d_qpu.cpp/hpp`,
  `device/v3d/v3d_memory.hpp`, `kernel/cl_emitter.hpp`) into
  `sources/phoenix-rtos-utils/v3dtest/` (or wherever the project
  prefers; this is a research utility, not a long-lived component).
- Replace Random06457's bare-metal MMIO helpers with `physmmap()` of
  `0xfe004000` + `V3DBuffer` allocations backed by `dmammap()`.
- Replace bare-metal logging with Phoenix's `printf` (drains via
  pl011-tty / klog).
- Compile under Phoenix's aarch64 toolchain; resolve any C++
  toolchain gaps (rtti / exceptions off, no STL — Random06457 uses
  `std::fill_n` and `<cstring>`, both replaceable).
- Run: `v3dtest` from psh; verify the binner fence count
  (`V3D_CLE_BFC`) and render fence count (`V3D_CLE_RFC`) both tick.
- Read back the V3D-rendered 500×500 RGBA8 buffer from ARM, blit it
  into the HDMI framebuffer at coordinate `(600, 200)` (same as
  Random06457's `drawTex` call), see a rainbow triangle on screen.
- **Validation gate**: HDMI screenshot shows the rainbow triangle.

Estimated effort: **2–3 iterations** if Random06457's MMIO + CL
encoder code translates cleanly; up to **6–8 iterations** if we hit
unforeseen Phoenix-side issues (cache flushing semantics across the
ARM↔V3D boundary, MAP_CONTIGUOUS limits, mailbox power-domain
behavior, etc.).

This tier *is the inflection point*. If a rainbow triangle renders
through Phoenix in <10 iterations, Tier 5–7 are extrapolation. If
Tier 4 stalls past 15 iterations, the plan should fall back to
Tier 2 as the ship line.

### Tier 5 — BO allocator + (optional) V3D MMU

Goal: production-quality BO allocation, sized for Quake's full
working set (vertex buffers + textures + framebuffer + tile-alloc +
tile-state ≈ ~32 MiB).

- Wrap `dmammap()` in a `v3d_bo_alloc(size, align)` /
  `v3d_bo_free(bo)` API.
- Decide MMU on/off:
  - **MMU off (Random06457's model)**: simpler, faster to land,
    works for a single-process renderer; everything lives in
    physical address space and is dmammap-backed.
  - **MMU on (Linux's model)**: requires implementing
    `v3d_mmu.c`-equivalent — a ~4 MiB contiguous page table, a PTE
    walker, TLB flush on BO unmap. Adds ~500 LoC. **Required for
    safety only if multiple processes use V3D**, which is not on
    the table for the Phoenix PoC. **Recommend: skip the MMU**
    until a real need surfaces.
- IRQ side: add a V3D IRQ handler on GIC SPI 74/75; replace
  Random06457's `while (getRfCount() <= last_rfc)` busy-loop with
  an interrupt-driven condvar wait. Same Phoenix pattern as
  GENET's RX path.
- **Validation gate**: Tier-4 triangle still renders, now with
  IRQ-driven completion, with the BO allocator handling 32+ MiB of
  combined allocations.

Estimated effort: 1–2 person-months.

### Tier 6 — hand-rolled "GL-shaped" subset for Quake

Goal: implement just enough OpenGL 1.x entry points to satisfy
GLQuake. Not real Mesa — just function stubs that emit V3D CLs
based on the Tier-4 reference patterns.

GLQuake's actual OpenGL surface (per `gl_*.c` files in
id-Software/Quake):

- Immediate mode: `glBegin`, `glEnd`, `glVertex2f/3f/3fv`,
  `glColor3f/3fv/4f/4fv/3ub/4ub`, `glTexCoord2f/2fv`.
- Matrix stack: `glMatrixMode`, `glLoadMatrixf`, `glLoadIdentity`,
  `glOrtho`, `glFrustum`, `glViewport`, `glScalef`, `glTranslatef`,
  `glRotatef`, `glPushMatrix`, `glPopMatrix`.
- State: `glEnable`/`glDisable` for `GL_TEXTURE_2D`, `GL_BLEND`,
  `GL_DEPTH_TEST`, `GL_ALPHA_TEST`, `GL_CULL_FACE`. `glBlendFunc`,
  `glAlphaFunc`, `glDepthFunc`, `glDepthMask`, `glDepthRange`,
  `glShadeModel`, `glPolygonMode`, `glCullFace`, `glColorMask`.
- Textures: `glGenTextures`, `glBindTexture`, `glTexImage2D`,
  `glTexSubImage2D`, `glTexParameteri`, `glTexEnvi`. Plus the
  `GL_EXT_paletted_texture` family if we go that route, or just
  re-encode palette → RGBA at load time (the workaround the user
  noted; simpler).
- Framebuffer: `glClear`, `glClearColor`, `glClearDepth`,
  `glFinish`, `glFlush`, `glReadPixels` (for screenshots; we can
  stub).
- Lookup / query: `glGetString` (GL_VENDOR/RENDERER/VERSION —
  return canned strings; Quake checks them but doesn't gate on
  values).

That's ~60 distinct entry points; the immediate-mode primitives are
the bulk because the impl has to accumulate vertices into a buffer
until `glEnd` and then emit a V3D `VERTEX_ARRAY_PRIMS`. Pattern is
already demonstrated in Random06457's example.

Implementation tactic:

- One `.c` file per group (`gl_immediate.c`, `gl_matrix.c`,
  `gl_state.c`, `gl_texture.c`, `gl_framebuffer.c`).
- Maintain CPU-side shadow state for everything (current matrix
  stack, current texture binding, current blend func, …).
- At `glEnd` / `glDrawArrays`, emit the CL fragment: state setup
  packets for any dirty state since last draw, then the actual
  primitive packet.
- Frame end (`SwapBuffers`-equivalent — Quake calls `glFinish`):
  submit the accumulated CL, wait for fence, blit V3D's output BO
  to the HDMI scanout framebuffer.

The fragment shader for Quake is one of two cases:
**(a)** textured surface with lightmap → texture-sample × lightmap-
sample → output. ~12 QPU instructions. **(b)** unlit / alpha-test
surfaces → texture-sample → output with discard. ~8 QPU
instructions. Author both binaries using macoy.me's
`tools/v3dAssembler.h` (single-header MIT V3D assembler, built on
Mesa's disassembler/validator) — *not* by hand-encoding 64-bit
instruction words like Random06457's first-cut example. Cross-check
encodings against Lambda-V's Keller ISA reference
(<https://www.lambda-v.com/texts/programming/gpu/gpu_raspi.html>)
which documents the VC6 instruction format field by field. Debug
binning-phase issues with macoy.me's `tools/v3dSimulator.h` on the
Linux host, not on the Pi.

Estimated effort: **2–3 person-months** of focused work (down from
the 3–5 estimate the previous draft carried, on the back of the
v3dAssembler.h + Keller ISA reference combination). The two
fragment shaders remain the most novel individual items; the rest
is straightforward state-machine code modeled on Random06457.

### Tier 7 — Quakespasm GL renderer + Phoenix

Goal: link our `libGL_phoenix.a` against Quakespasm's GL renderer
and watch frames render.

- Flip the Quakespasm port (from Tier 2) from software to GL
  renderer mode.
- Link against our hand-rolled GL stubs (`libGL_phoenix.a`).
- Boot the image, run `quake -basedir /quake +map e1m1`.
- **Validation gate**: HDMI shows the Quake start screen rendered
  through V3D, then loads e1m1, then renders the first frame
  matching a reference Linux GLQuake render to within visual
  inspection tolerance.

Estimated effort: 2–4 iterations once Tier 6 is solid. Most of the
work is matching the state Quake expects vs. what our subset sets
up — debugging "why does the sky look pink" type issues.

### Tier 8 — gameplay (input + sound). Optional / deferred.

- USB-HID for keyboard. Depends on USB-HCD (task #26) which is
  currently parked pending JTAG. May never land.
- PWM-audio for the headphone-jack output. Brand-new driver; no
  precedent in tree. Roughly the same effort as a basic SDHCI
  bring-up but smaller scope.

## Critical-path decisions

These are the calls that gate everything else.

1. **Mesa port vs. hand-rolled GL subset?**
   - Question: bigger-but-real vs. smaller-but-bespoke?
   - Candidates: (A) Mesa v3d Gallium driver port — Mesa supports
     surfaceless EGL and "other flavors of Unix and Haiku"
     officially, so the cross-platform scaffolding exists. (B)
     Hand-rolled ~5–10 kLoC GL shim emitting V3D CLs, modeled on
     Random06457.
   - Recommendation: **(B), hand-rolled, for the first PoC.**
     Reasons: (i) Random06457's working triangle is a 1:1 template
     for everything Quake needs; (ii) the ~60-entry GL surface
     Quake exercises is a small target; (iii) avoiding Mesa avoids
     the libdrm + libc-shim + meson-build infrastructure cost; (iv)
     hand-rolled code is owned and publishable without licensing
     friction. Mesa stays the long-term answer for any future
     "real OpenGL ES 3.1 on Phoenix" effort. The user is correct
     that Mesa is portable — but it's also a one-way door:
     committing to it means building libdrm, libc, and pthread
     shims first, before any pixel renders.

2. **V3D MMU on or off for Tier 4–6?**
   - Question: enable the on-chip MMU now or run V3D in
     physical-address mode?
   - Candidates: MMU on (Linux's choice, ~500 LoC overhead, isolation
     between clients); MMU off (Random06457's choice, single-client
     direct-physical, simpler).
   - Recommendation: **off for the PoC.** Phoenix Quake is a single
     userspace process; isolation isn't needed. Add MMU later if a
     second V3D client materializes.

3. **Pre-tiled textures vs. TFU?**
   - Question: do we need to bring up V3D's Texture Formatting Unit
     for texture tiling, or can we pre-tile on the CPU at load time?
   - Recommendation: **pre-tile on CPU.** Quake loads a few hundred
     KB of texture data per level; one-time conversion cost is
     trivial. Skipping TFU saves a whole queue's worth of driver.

4. **Quake source base: original 1996 `id-Software/Quake` vs.
   Quakespasm vs. yquake2?**
   - Recommendation: **Quakespasm.** Has both software and GL
     renderers, compiles cleanly with modern toolchains, has the
     largest community for troubleshooting. The dual-renderer
     property is especially valuable here — Tier 2 ships software,
     Tier 7 flips to GL, same source repo.

5. **Framebuffer mapping: uncached vs. write-combining?**
   - Measure in Tier 1 first. If `MAP_UNCACHED` gives <30 fps at
     320×240, escalate to a `MAP_WC` flag in the kernel HAL.

6. **Cache hygiene at the ARM↔V3D boundary.**
   - V3D reads BOs over the AXI bus directly from DRAM. If ARM
     populated those BOs through the L1/L2 cache, dirty lines must
     be cleaned before V3D reads. Random06457's example calls
     `v3dInvalidateL2T` + `v3dInvalidateSlices` (V3D-side caches)
     but assumes ARM-side data is already coherent — bare-metal it
     was, because there were no caches. **On Phoenix with caches
     enabled, every BO write needs a dcache-clean before V3D
     submission and every BO read needs a dcache-invalidate after
     V3D completion.** Phoenix already has `hal_dcacheClean` /
     `hal_dcacheInval` (used in `plo/hal/aarch64/generic/video.c`)
     but no userspace wrapper today. Add `dmammap_clean(va, sz)` /
     `dmammap_inval(va, sz)` to physmmap.[ch], analogous to
     Linux's `dma_sync_single_for_device` / `_for_cpu`.

7. **Audio?**
   - **Skip entirely for the PoC.** Audio is its own multi-iteration
     project; Quake-without-sound is still visibly Quake.

8. **Input?**
   - **Skip for v1.** Attract-mode demo. Add later if USB-HID
     (task #26) lands.

## Honest feasibility verdict (revised)

The original draft of this plan estimated 9–18 person-months for
Tier 0→7. The discovery of Random06457 + macoy.me's bare-metal V3D
work materially shortens the estimate.

- **Tier 0–2 (software-rendered Quake on HDMI) is highly achievable**
  in a 6–10 iteration window. No unknown hardware involved. Risks
  are in Quake's portability layer, which has been solved 50 times
  over on other RTOSes. This remains the realistic safety-net ship
  target.

- **Tier 3 (V3D MMIO scout) is trivially achievable** — 1 iteration,
  low risk. Worth doing as a cheap reality check even if no further
  3D work follows.

- **Tier 4 (V3D rainbow triangle ported to Phoenix) is plausibly
  achievable in 2–6 iterations** thanks to Random06457's MIT example.
  This is the linchpin: if the port works, Tier 5–7 follows by
  extrapolation; if it doesn't, the safety net is Tier 2.

- **Tier 5 (BO allocator + IRQ + perhaps MMU) is 1–2 person-months**
  on top of Tier 4. Phoenix's `dmammap` and the GENET IRQ pattern
  give us most of the infrastructure for free.

- **Tier 6 (hand-rolled GL subset) is 2–3 person-months.** The two
  fragment shaders (textured + textured+lightmap) are the dominant
  individual risks but the macoy.me `v3dAssembler.h` plus Keller's
  Lambda-V ISA reference take "hand-encode 64-bit QPU instructions"
  off the critical path entirely. The remaining effort is
  state-machine code that maps GL state changes to V3D CL packets.

- **Tier 7 (Quakespasm GL on Phoenix) is 2–4 iterations** once Tier
  6 lands. Debugging-heavy but not novel research.

- **Total Tier 0 → Tier 7 honest budget**: **5–8 person-months of
  focused work**, *not* 9–18. The two reductions vs. the original
  draft are: (a) Random06457 + macoy.me bare-metal V3D code gives
  us a working triangle template; (b) macoy.me's
  `tools/v3dAssembler.h` plus Keller's Lambda-V ISA reference make
  shader authoring tractable.

The deepest realistic risk is no longer shader authoring; it is
**cache/coherence correctness across the ARM↔V3D boundary** under
Phoenix's caches-enabled regime. Random06457's example runs caches-
off (bare-metal style) and Linux's drm/v3d handles coherence via
the standard DMA APIs; Phoenix has neither. The `dmammap` allocator
is uncached, which is correct for V3D-shared buffers, but the
implicit assumption that "userspace populates the BO with normal
stores and V3D reads it directly" needs an explicit
`dcacheClean`/`dcacheInval` wrapper around every submission — see
decision §6 in §"Critical-path decisions." This is the load-bearing
hidden cost that turns "port the triangle" into a multi-iteration
exercise rather than a single afternoon.

Mesa-port-as-a-long-term-bet remains viable: Mesa is MIT, supports
surfaceless EGL, and is officially portable to non-Linux Unixes.
If after Tier 4 ships we decide we'd rather have OpenGL ES 3.1 than
a hand-rolled GL subset, the Mesa work becomes a parallel track —
not a prerequisite — because the V3D kernel/driver layer we built
for Tier 4–5 *is* substantially the libdrm-shim Mesa expects (4
ioctls: `SUBMIT_CL` / `CREATE_BO` / `WAIT_BO` / `GET_BO_OFFSET`).

## Risks / unknowns

- **`dmammap` at 32–64 MiB scale.** Today's call sites max out at
  ~256 KiB. We have no evidence the kernel can satisfy a 32+ MiB
  `MAP_CONTIGUOUS` request. Mitigation: Tier 1 measures this
  explicitly.
- **TD-15 still open.** The VC4/V3D + Phoenix DRAM hygiene work
  (`docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` §TD-15) is pending.
  Touching V3D from userspace will need `/reserved-memory` and
  `dma-ranges` parsing in the DTB code; TD-15 should close before
  Tier 4.
- **Firmware ownership of HDMI scanout.** The VPU keeps the HDMI
  pipeline alive after handoff (per `docs/knowledge/rpi4-os-development-guide.md`
  §"VideoCore remains live after handoff"). If we change the
  framebuffer mode at runtime (e.g. switching depth from 32bpp to
  8bpp for Quake software renderer), the firmware reallocates the
  scanout buffer — the syspage `graphmode` becomes stale. Either
  redo the mailbox call from userspace and update graphmode (needs a
  new `pctl` write side) or pick the mode at `plo` time and never
  change it.
- **Cache coherence at the ARM↔V3D boundary** — see decision §6
  above. Required infrastructure must land before Tier 4 can pass.
- **License posture.** Quake source is GPL-2.0. Phoenix-RTOS is
  three-clause BSD. Linking-style restrictions matter. If we ship
  a Quake binary as part of the Phoenix image, the image's "Quake"
  component is GPL-2.0 — fine for a PoC, but document it. The
  shareware PAK is redistributable; the registered PAK is not.
- **Mesa license is MIT, not the blocker.** Mesa is fine to ship in
  Phoenix; the blocker for a Mesa port is the libdrm/libc/build
  scaffolding, not the licence.
- **No JTAG today**, so any V3D crash that hangs the SoC requires a
  power-cycle. The watchdog reboot path already shipped (per
  `docs/inprogress/status.md`) makes this less painful but iteration is slow.
- **Pi 5 lurks.** BCM2712 has V3D 7.1 (Mesa supports it; see
  <https://docs.mesa3d.org/drivers/v3d.html>: "V3D 7.1 (Raspberry Pi
  5): Currently supported"). V3D 4.2 work on Pi 4 is throwaway for
  Pi 5 hardware, but the *infrastructure* (BO allocator, libdrm-shim,
  IRQ handler, msg-port driver) survives — V3D 7.1 differs in CL
  packet layout, not in driver shape. Random06457's macoy fork
  explicitly mentions Pi 5 transferability.
- **V3D 4.2 specs are partially documentation-locked.** Per
  <https://docs.mesa3d.org/drivers/v3d.html>: "Broadcom has not
  publicly released specifications for V3D 3.x or 4.x." Mesa's
  source under MIT *is* the de-facto public specification. This
  doesn't block us — Random06457 + Mesa source + Linux v3d source
  are sufficient — but means there is no Broadcom errata document
  to consult when something behaves oddly.

## Validation tooling already in place

- **HDMI snapshots in `artifacts/hdmi/`** — `<ts>-<label>-tick.png`
  captured every 25 s during the test cycle. The exact mechanism
  Tier 0–7 needs to validate rendering output.
- **diag-udp responder** in lwip-port. A `v` (V3D) sub-command can
  expose V3D register snapshots, BO pool usage, fence completion
  counters — same pattern as the SDHCI / GENET scouts.
- **`scripts/uart-summary.sh`** — adding a `quake started` /
  `quake e1m1 loaded` / `v3d triangle ok` stage check to the
  boot-progress table is trivial once Tier 2 lands.

## Sequencing relative to other work

This plan is **explicitly not on the active task list.** WiFi (#36)
and BT are the current next pieces. GPU/Quake is the fun-after-utility
project.

- **Do Tier 0 + Tier 1 + Tier 2 (software Quake) opportunistically**
  — they don't block anything, are entertaining, demonstrate the
  port to non-systems audiences, and validate the heap / dummyfs
  / framebuffer subsystems under a real-world workload.
- **Tier 3 (V3D scout) is also cheap.** Worth running alongside
  Tier 0 as a parallel research thread.
- **Tier 4 (port Random06457's triangle) is the real commitment
  point.** Once it lands, Tier 5–7 become an extrapolation effort
  rather than a research bet. Tier 4 itself is a 2–6 iteration
  exercise based on translating MIT example code, not a months-long
  unknown.

## References

- **Random06457/rpi4-gpu-bare-metal-examples** —
  <https://github.com/Random06457/rpi4-gpu-bare-metal-examples>.
  MIT-licensed bare-metal V3D 4.2 triangle on Pi 4. **The primary
  reference for Tier 4.**
- **macoy.me PiV3D blog post** —
  <https://macoy.me/blog/programming/PiV3D>. 100k triangles at
  177 fps 1080p on bare-metal Pi 4 using a single-header C V3D
  library. Discusses the Random06457 starting point and missing
  pieces (texture sampling, shader compilation).
- **macoy.me rpi-system (Circle fork in C)** —
  <https://macoy.me/code/macoy/rpi-system>. ~16 MiB code,
  84% C / 14% C++, includes V3D inspector + render control list
  work. MIT.
- **macoy.me rpi-bare-metal V3D tooling** —
  <https://macoy.me/code/macoy/rpi-bare-metal/src/branch/master/tools>.
  Two artifacts of major value:
  - `tools/Shaders.org` — notes on the V3D shading language as
    actually used in bare-metal Pi 4.
  - `tools/v3dAssembler.h` — **a V3D-shader assembler in a single
    dependency-free header**, built by bundling Mesa's disassembler
    + validator. Eliminates "hand-encode QPU instructions byte by
    byte" as a Tier-6 cost. Same MIT lineage.
  - `tools/v3dSimulator.h` — bare-bones simulator for debugging
    binning-phase shaders on any host platform.
- **Lambda-V "GPU Programming: Raspberry Pi (VC4/VC6/VC7)"** by
  Wolfgang Keller —
  <https://www.lambda-v.com/texts/programming/gpu/gpu_raspi.html>.
  Comprehensive instruction-set reference for both VC4 and VC6
  QPUs, including the encoding of `sig` / `op_mul` / `cond` /
  `msfign` fields, ALU instructions, branch instructions, load-
  immediate instructions, and `raddr_a/b` semantics. Cross-links
  Mesa's `libbroadcom_qpu.a` (the disassembler), `vcqpudisasm`
  (independent disassembler at
  <https://bitbucket.org/nubok/vcqpudisasm>), `Terminus-IMRC/vc6qpudisas`,
  and the macoy.me V3D tooling above. **This is the de-facto
  human-readable V3D 4.2 ISA reference** — the answer to "Broadcom
  has not publicly released V3D 4.x specifications" per
  docs.mesa3d.org. Drafted 2021-11-01, last modified 2026-03-04.
- **Mesa v3d driver page** —
  <https://docs.mesa3d.org/drivers/v3d.html>. Confirms V3D 4.2 (Pi 4)
  and V3D 7.1 (Pi 5) are currently supported; GLES 3.1 conformant;
  Broadcom V3D specs not publicly released; Mesa source is the
  de-facto specification.
- **Mesa systems page** —
  <https://docs.mesa3d.org/systems.html>. "Mesa is primarily
  developed and used on Linux systems. But there's also support for
  Windows, other flavors of Unix and other systems such as Haiku."
- **Mesa EGL page** —
  <https://docs.mesa3d.org/egl.html>. Confirms egl_dri2 supports
  "android, device, drm, surfaceless, wayland and x11" — surfaceless
  is the Phoenix-relevant mode.
- **Mesa install page** —
  <https://docs.mesa3d.org/install.html>. Build dependencies:
  meson, python 3.10+, gcc/clang, lex/yacc. Specific platform
  requirements not enumerated.
- **Mesa v3d Gallium driver source** —
  <https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gallium/drivers/v3d>.
- **Mesa Broadcom shared code (CLE / compiler / common / qpu)** —
  <https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/broadcom>.
  `cle/v3d_packet_v42.xml` is the canonical CL packet definition;
  `compiler/` is the shader compiler (NIR → V3D ISA); `qpu/` is
  the QPU instruction encoder/decoder.
- **Linux drm/v3d driver page** —
  <https://docs.kernel.org/gpu/v3d.html> /
  <https://dri.freedesktop.org/docs/drm/gpu/v3d.html>. Describes
  the bin/render queues, TFU, CSD; the per-fd FIFO scheduler; the
  4 GiB single-level page-table MMU; the "no GMP-based memory
  protection yet" caveat.
- **Linux drm/v3d source** —
  <https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/v3d>.
  Total ~209 KB across 18 files. Most relevant individual files:
  `v3d_drv.c`, `v3d_submit.c`, `v3d_mmu.c`, `v3d_irq.c`,
  `v3d_regs.h`. GPL-2.0 — reference-only for Phoenix.
- **Linux v3d uAPI header** —
  <https://github.com/torvalds/linux/blob/master/include/uapi/drm/v3d_drm.h>.
  13 ioctls; Phoenix-minimum subset is 4 (`SUBMIT_CL` /
  `CREATE_BO` / `WAIT_BO` / `GET_BO_OFFSET`).
- **Linux drm/vc4 driver (display pipeline reference)** —
  <https://docs.kernel.org/gpu/vc4.html>,
  <https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/vc4>.
- **drm/v3d "add bcm2711" series** —
  <https://patchwork.kernel.org/project/linux-arm-kernel/patch/20220603092610.1909675-4-pbrobinson@gmail.com/>.
- **BCM2711 ARM Peripherals datasheet** —
  <https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf>.
- **Raspberry Pi firmware wiki — mailboxes / property interface** —
  <https://github.com/raspberrypi/firmware/wiki/Mailboxes>,
  <https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface>.
- **bakhi.github.io V3D summary** —
  <https://bakhi.github.io/mobileGPU/v3d/>. Cleanest third-party
  overview of the V3D MMU + CL layout.
- **Eric Anholt's vc4/v3d authoring posts** —
  <https://anholt.livejournal.com/>.
- **LdB-ECM bare-metal Pi (V3D experiments)** —
  <https://github.com/LdB-ECM/Raspberry-Pi>. Earlier prior-art
  reference; superseded for Pi 4 V3D 4.2 work by Random06457.
- **Circle bare-metal framework (mature framebuffer reference)** —
  <https://github.com/rsta2/circle>.
- **id-Software/Quake (GPL-2.0 source release)** —
  <https://github.com/id-Software/Quake>.
- **Quakespasm (actively-maintained Quake fork; both software + GL
  renderers)** —
  <https://github.com/sezero/quakespasm>.
- **vkQuake (Quake-on-Vulkan; reference for a modern Quake source
  port doing GPU work)** —
  <https://github.com/Novum/vkQuake>.
- **yquake2 (Quake II port; alternative reference for a clean port
  layer)** —
  <https://github.com/yquake2/yquake2>.
- **FreeBSD on Pi — "What about 2D/3D hardware acceleration"** —
  <https://forums.freebsd.org/threads/what-about-2d-3d-hardware-acceleration-and-audio-support-on-raspberry-pi.86341/>.
- **Phoenix-RTOS in-tree references**:
  - `sources/plo/hal/aarch64/generic/video.c` (mailbox FB bring-up)
  - `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
    (`pl011_fbcon_*` userspace FB consumer)
  - `sources/phoenix-rtos-corelibs/libgraph/` (graphics library)
  - `sources/phoenix-rtos-lwip/drivers/physmmap.c` (`dmammap`)
  - `sources/phoenix-rtos-kernel/include/mman.h` (MAP_* flags)
  - `docs/knowledge/gpu-vc6.md`, `docs/knowledge/gpu-vc6-non-linux.md`
  - `docs/knowledge/rpi4-os-development-guide.md` (mailbox, framebuffer,
    GPU-reserve memory model, TD-15)

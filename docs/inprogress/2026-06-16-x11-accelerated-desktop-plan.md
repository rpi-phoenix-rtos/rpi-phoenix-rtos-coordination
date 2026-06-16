# From TinyX to a full accelerated X11 desktop on Raspberry Pi 4

**Status:** planning / feasibility study (no code). 2026-06-16.
**Companion:** [`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md) (the software
TinyX/kdrive bring-up plan, just reconciled to current reality). This doc
studies the step *beyond* it: how hard is a full **X.org / X11 experience
with GPU acceleration** plus a lightweight desktop environment, given what
Phoenix-RTOS on the Pi 4 actually has today.

## TL;DR verdict (read this first)

- **Software X on kdrive → `/dev/fb0` is the realistic milestone and, for any
  reasonable effort, the practical ceiling.** It is mostly unblocked on the
  Phoenix side now (caches on, fb0 exists, USB HID kbd+mouse work); the cost
  is porting the X11 *library* stack + writing a kdrive fbdev/input backend.
  Estimate **~6–10 weeks** to a usable `WM + xterm` software desktop.
- **Full Xorg (not kdrive) is not realistic.** Xorg's DDX + input driver model
  is built on `dlopen` loadable modules; Phoenix has **no `dlfcn.h`/`dlopen`**.
  kdrive (statically linked single binary) is the correct and only viable target.
- **GPU-accelerated X is a research stretch, not a near-term deliverable.** The
  V3D GPU works and renders accelerated GL — but through a **per-process,
  no-DRM, synchronous, single-client** userspace winsys that **cannot be shared
  between an X server and its clients** without first building a GPU-arbiter
  daemon (a userspace DRM equivalent) *and* porting **EGL**. Both are large.
  The only tractable accelerated model is **X as the sole GPU client** (Glamor
  composites; clients render in software), and even that needs the EGL port.
- **Top risks (ranked):** (1) GPU single-client arbitration / no DRM — *the*
  blocker for accel; (2) EGL is net-new (our GL bring-up bypasses EGL); (3) the
  whole X11 library stack is unported (PR #82 never landed); (4) `/dev/fb0` has
  no fbdev `mmap`/`FBIOGET_*` ABI — workable via shadow-blit but needs a kdrive
  backend edit; (5) AF_UNIX runtime on aarch64-rpi4b unverified.

---

## 0. What the Pi 4 port actually has today (grounding)

| Capability | Where | State |
|---|---|---|
| Firmware framebuffer device `/dev/fb0` | `sources/phoenix-rtos-devices/video/rpi4-fb/rpi4-fb.c` | `read()`/`write()` + `RPI4FB_GETMODE` devctl; geometry runtime-queried via `platformctl(pctl_graphmode)` (HW: `1024x768x32`). **No** `FBIOGET_*` veneer, **no** `mmap(fd,0)` FB backing. |
| GPU: accelerated OpenGL on real V3D 4.2 | `tools/v3d-driver-port/` (Mesa gallium `v3d` + `st/mesa` GL frontend) | Renders on HW; **GL Quake** (`tools/quakespasm-port/`) runs, render-to-scanout into `/dev/fb0`. |
| The "kernel driver": userspace winsys | `tools/v3d-driver-port/v3d_phoenix_winsys.c` | **In-process, no DRM, synchronous, single-client.** Crux of part 3. |
| USB keyboard | `sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c` → `/dev/kbd0` | Cooked ASCII **and raw 8-byte HID mode** (write `1`). |
| USB mouse | `sources/phoenix-rtos-devices/usb/usbmouse/` → `/dev/mouse0` | Raw 4-byte boot-mouse packets. |
| Proven HID→app-events reference | `tools/quakespasm-port/platform/pl_phoenix_in.c` | Raw HID → key down/up + char + mouse motion/buttons, in-process. |
| System | aarch64, SMP 4-core (cpu0-scheduled), GENET+lwip, NFS + ext2 roots, posixsrv, psh, libphoenix (partial POSIX) | Caches **on** (TD-16). |
| libphoenix gaps relevant to X | `sources/libphoenix/include/` | `sys/un.h` + `sys/socket.h` present (AF_UNIX runtime **unverified**); **no `dlfcn.h`**; **no `shm_open`**. |
| X11 library stack | `sources/phoenix-rtos-ports/` | **Absent.** No `x11/`, libX11, libxcb, pixman, freetype, fontconfig, Xau, Xdmcp. PR #82 never landed. Present neighbours we can lean on: zlib, openssl, libevent, lua, mbedtls, curl, dropbear. **No GTK/Qt/glib/pango/cairo.** |

---

## 1. TinyX (kdrive) vs full X.org — be honest

**What kdrive provides.** kdrive (`xserver/hw/kdrive`, the "tinyx" lineage) is
a stripped X server: the dix/os core + a tiny hardware abstraction (`KdScreen`,
`KdCardFuncs`, `KdKeyboardDriver`/`KdPointerDriver`, the shadow-framebuffer
layer). A backend (e.g. `Xfbdev`) statically links into **one binary** — no
runtime module loading. Software rendering is pixman/fb into a framebuffer. It
was built for exactly the embedded/no-accel case we are in.

**What full Xorg needs that Phoenix lacks.** The full `Xorg` server is a thin
core that `dlopen`s everything at runtime: the DDX video driver
(`modesetting`/`fbdev`/vendor), input drivers (`xf86-input-evdev`/`libinput`),
GLX/DRI modules, and extension modules — all `.so` plugins discovered via
config + `LoadModule`. This rests on:

- **`dlopen`/`dlfcn.h`** — **absent in Phoenix** (verified). This alone makes
  the Xorg loadable-DDX model a non-starter without statically pre-linking a
  custom Xorg, which defeats the point and is more work than kdrive.
- **udev / input hotplug** — Xorg discovers `/dev/input/event*` via udev. We
  have neither udev nor evdev nodes; we have `/dev/kbd0`+`/dev/mouse0` char
  devices with a Phoenix-native raw-HID contract.
- **DRM/KMS `/dev` backends** — `modesetting` DDX needs a kernel DRM driver and
  KMS. We have **no DRM, no KMS** (the GPU "driver" is a per-process userspace
  winsys). Only the `fbdev` DDX (or kdrive's fbdev) is even conceivable.

**Verdict.** **kdrive/Xephyr-style is the pragmatic and only realistic target.**
Full Xorg is not realistic on Phoenix today, and `dlopen`-absence is the concrete,
decisive reason. Everything below assumes kdrive.

---

## 2. The framebuffer / display path

X needs a framebuffer to drive; we have `/dev/fb0` (firmware fb,
render-to-scanout proven). There is **no DRM/KMS modesetting** — so this is
strictly **fbdev-style**, not modesetting. Implications:

- **No mode set, no page-flip, no vsync from the kernel.** The mode is whatever
  plo/firmware allocated; the X server lives with it (query via `RPI4FB_GETMODE`).
  Tearing is possible (no flip); acceptable for a software desktop.
- **The fbdev ABI mismatch is the real work.** kdrive's fbdev backend
  (`hw/kdrive/fbdev`) and Xorg's `fbdevhw` both speak Linux fbdev ioctls
  (`FBIOGET_VSCREENINFO`, `FBIOGET_FSCREENINFO`) and `mmap` the device to get a
  pointer they draw into directly. `/dev/fb0` here provides **neither**: it has
  `RPI4FB_GETMODE` instead of the Linux ioctls, and `mmap(fd,0)` demand-pages a
  *private copy*, not the live scanout (see the device's own header comment).
- **Pragmatic presentation = shadow framebuffer + `write()`-blit.** kdrive's
  *shadow* layer (`KdShadowFbAlloc` / `shadowUpdate`) keeps the X drawing surface
  in ordinary process memory and periodically flushes damaged regions to the
  real framebuffer. Point that flush at `pwrite(fb0, ...)` (the device's
  `write()` does the `memcpy` into uncached scanout DRAM). **This avoids the
  `mmap(fd,0)` kernel-VM one-way door entirely** — it is the recommended M1 path.
  The cost is one extra full/partial-frame copy per update; with caches on and a
  damage-tracked shadow, fine for a desktop (xterm, a clock, window drags).
- **Pixel format / pitch.** `RPI4FB_GETMODE` returns `{bpp,pitch}`; pitch ≥
  width*4 (validated `pitch=4096` at 1024 wide). The shadow→blit copies row by
  row honoring pitch. Confirm RGB vs BGR channel order against firmware once on HW.

A small kdrive backend (a `KdCardFuncs`/`KdScreenFuncs` that calls
`RPI4FB_GETMODE` for geometry and `write()` for flush) is the concrete deliverable.

---

## 3. GPU acceleration for X — the crux, analysed deeply

This is where the GPU "works" but does **not** translate into accelerated X for
free. The argument is mechanical, not hand-wavy.

### 3.1 What the winsys actually does (the single-client mechanism)

`tools/v3d-driver-port/v3d_phoenix_winsys.c` is the entire "kernel driver":
Mesa's gallium v3d talks to it via `drmIoctl(fd, DRM_IOCTL_V3D_*, …)`, and a
libdrm shim dispatches into `phoenix_v3d_ioctl`. Look at `winsys_init()`:

```
v3d_phoenix_powerOn();                                  // per-process V3D power-on
W.hub  = map_dev(V3D_HUB_BASE, …);                      // maps the ONE V3D HUB MMIO
W.pt   = mmap(... MAP_CONTIGUOUS ...);                  // this process's private page table
W.hub[MMU_PT_PA_BASE/4] = (W.pt_pa >> PAGE_SHIFT);      // points the ONE MMU base reg at it
W.hub[MMU_CTL/4]        = MMU_CTL_ENABLE | …;
```

and `ioc_submit_cl()` pokes the **one** CORE0 submit queue (`CLE_CT0QBA`,
`CLE_CT1QBA`, `CTL_SLCACTL`, …) directly and **spins** to completion
(`DRM_V3D_WAIT_BO` is a no-op: "submit is synchronous"). State lives in a single
global `W`; `W.scanout_claimed` is process-local and guards the **single** scanout
surface.

**Therefore two processes cannot both use this winsys.** The V3D has exactly one
MMU page-table base register (`MMU_PT_PA_BASE`) and one CORE0 submit queue.
Whichever process runs `winsys_init()` second **reprograms the MMU base register
to its own page table** — the first process's GPU virtual addresses now resolve
through the wrong PT → MMU fault / garbage rendering. Submits race on the same
CORE0 with **no arbiter and no locking** across processes. Both also fight for
the one scanout surface (`scanout_claimed` is not shared).

This is precisely the gap that **DRM/KMS** fills in Linux: the kernel DRM driver
owns the single MMU + submit queue and multiplexes them across clients (per-client
GPU address spaces / context switches, command-buffer validation, scheduling,
fences). **We have none of that.** Our "DRM" is faked per-process.

### 3.2 What X acceleration normally requires, mapped to our gaps

| Normal X accel ingredient | What it assumes | Our reality |
|---|---|---|
| **GLX** (indirect or direct) | A GL implementation reachable from the X server + clients | We have GL via the gallium `st`, but only **in-process per app**; no shared server-side GL context model. |
| **DRI2/DRI3** | A **DRM** kernel driver + buffer sharing (GEM handles / dma-buf) across X + clients | **No DRM.** No cross-process buffer sharing primitive. dma-buf has no analogue. |
| **Glamor** (Xorg's GL-based 2D accel) | **EGL** + a GL/GLES context owned by the X server; renders X drawing ops as GL into pixmaps | We have GL but **no EGL** (our bring-up uses surfaceless `st_create_context` + `_mesa_make_current` directly — it bypasses EGL/GBM, and GBM needs DRM). |
| **GPU arbitration between X and clients** | DRM kernel arbitration of the single GPU | The winsys is single-client; concurrent users corrupt each other (§3.1). |

### 3.3 Can Glamor work here?

Glamor is the most plausible accel path *because it confines GPU use to the X
server* — X clients submit ordinary 2D protocol (no client GPU access), and the
server turns drawing ops into GL into pixmaps via EGL. That neatly sidesteps the
multi-client problem (§3.1) **if and only if X is the sole GPU process**. But it
needs **EGL**, which we do not have, and Glamor specifically wants an EGL context
(`eglMakeCurrent`) plus pixmap↔GL-texture binding (normally via dma-buf/`EGLImage`
or `glTexImage`). Concretely, to get Glamor:

1. **Port EGL on top of our gallium st.** Mesa ships an EGL implementation; the
   Phoenix-viable path is the **surfaceless EGL platform** (`EGL_PLATFORM_SURFACELESS`)
   backed by our existing winsys, *not* GBM (GBM requires DRM). This is net-new:
   our current GL bring-up deliberately skips EGL. Medium-large.
2. **Make X the only GPU client.** Glamor's model already does this for *drawing*;
   we must additionally ensure **no** other process runs the winsys concurrently
   (no GL clients), or the MMU-reprogram corruption of §3.1 returns. So
   "accelerated X" here means *accelerated 2D compositing of software-rendered
   clients* — not GPU-accelerated client apps. Acceptable, but a real limit.
3. **Present.** Glamor renders into a GL pixmap = the screen; flush to `/dev/fb0`
   either by render-to-scanout (the winsys already supports a scanout-backed BO)
   or by readback + `write()`-blit. Render-to-scanout is the existing proven path.

Even if all three land, the result is **"X server uses the GPU for 2D; clients
are software."** True client-side GLX/DRI3 (an X client running its own GL on the
GPU) is blocked by §3.1 until a GPU-arbiter daemon exists.

### 3.4 The GPU-arbiter alternative (the "real" fix, large)

To let X *and* GL clients use the GPU concurrently, build a **userspace
DRM-equivalent daemon**: one process owns the V3D (single MMU + submit queue),
exposes a Phoenix-IPC protocol for BO alloc / map / submit / fence, and
multiplexes clients by switching the MMU base register per submit (or by giving
each client a sub-range of one PT) and serializing the submit queue. This is, in
effect, writing the part of a DRM driver that matters for arbitration, in
userspace. It is a multi-month effort and a research project in its own right;
out of scope for a desktop-bring-up milestone but the only path to fully
accelerated X-with-accelerated-clients.

### 3.5 Software X as the honest first milestone

Given the above, the realistic first (and likely only, for sane effort)
milestone is **unaccelerated software X**: pixman/fb rendering into a shadow
framebuffer, blitted to `/dev/fb0`. With caches on this is perfectly usable for a
WM + terminal + a clock. **Accelerated X (Glamor + EGL, X-as-sole-GPU-client) is
the stretch; accelerated-clients X (arbiter daemon) is research.**

---

## 4. Dependencies — the porting surface

### 4.1 X11 library stack (all net-new — PR #82 did not land)

Required, in rough dependency order (none currently in `phoenix-rtos-ports/`):

- **xorgproto** (headers only — easy), **xtrans** (transport; the AF_UNIX path is
  the Phoenix-relevant lift), **Xau**, **Xdmcp** (small).
- **libxcb** (+ `xcb-proto`, needs Python at build) — pthread-TLS usage to check.
- **libX11** (large but mostly portable C; locale/`Xlib` thread support).
- **pixman** (software raster — has an aarch64 NEON path to enable/disable),
  **libXext**, **libXrender**, **libXfont/libXfont2**, **libICE/libSM**.
- **freetype** + **fontconfig** (+ a small set of fonts) for scalable text; or
  bitmap PCF fonts only for a minimal first pass.
- The kdrive server itself (`xserver` configured `--enable-kdrive --enable-xfbdev`,
  `--disable-glamor` for M1, `--disable-dri*`, `--disable-xkb`-layout-optional,
  `--disable-mitshm` because `shm_open` is missing).

Build leverage: `zlib`, `openssl`, `libevent`, `pcre` already ported; `lua`
present. **Missing libc bits to confirm/patch:** `shm_open` (→ `--disable-mitshm`),
`dlopen` (→ static kdrive, no modules), full `pthread` TLS, `mkstemp`/`/tmp`
(needs a writable fs — NFS or ext2 root, both available).

### 4.2 IPC / sockets

X clients connect over AF_UNIX at `/tmp/.X11-unix/X0`. `sys/un.h` + `sys/socket.h`
are present in libphoenix, but **AF_UNIX runtime on aarch64-rpi4b is unverified** —
gate the whole effort on a 50-line client/server round-trip first (the posixsrv
path implements the kernel side; INET sockets work via lwip, AF_UNIX is the open
question). Fallback if AF_UNIX is weak: TCP-localhost display (`X :0` listening on
`127.0.0.1:6000`) — works over lwip, at the cost of `-nolisten tcp` being off.

### 4.3 Input — map our devices to a kdrive input backend

We do **not** need evdev/udev. Write a **kdrive input driver** (a `KdKeyboardDriver`
+ `KdPointerDriver`) that:

- opens `/dev/kbd0`, writes `1` to enable raw 8-byte HID mode, and on `read()`
  diffs successive reports into `KdEnqueueKeyboardEvent(down/up)` — reusing the
  exact HID-usage→key map already proven in `pl_phoenix_in.c`;
- opens `/dev/mouse0` and turns 4-byte packets into `KdEnqueuePointerEvent`
  (relative motion + buttons).

xkbcommon (not ported) is **optional** — only for layout flexibility; a hardcoded
US-QWERTY map suffices for the demo. Porting surface here is **small** (days),
because the driver-side raw-HID work and a reference consumer already exist.

---

## 5. Desktop environment — recommendation

The dependency surface dictates the answer. There is **no GTK/Qt/glib/pango/cairo**
in `phoenix-rtos-ports/`, and **no `dlopen`**. That rules out **LXDE** (GTK-based,
huge dep tree) and makes **IceWM** marginal (it wants `libXft`/fontconfig and
several X helper libs; buildable but heavier and a bigger first bite).

**Recommendation: a minimal window manager + a terminal, not a full DE.**

- **WM: `twm`** (classic MIT, in PR #82's app set, no exotic deps — just core
  Xlib + Xmu/Xt) — or **`dwm`** (single C file, suckless; GPL-2.0, so a public-demo
  license note applies). `twm` is the conservative, license-clean pick.
- **Terminal: `xterm`** (pulls libXaw/Xt — the X-default polish) or **`st`**
  (suckless, minimal, needs fontconfig+Xft). For a first light, `st` is the
  smaller bite; `xterm` if we want the canonical look.
- Optionally `xclock`/`xeyes` as motion/render proof.

Defer any real DE (panel/file-manager/settings) until the GTK/Qt stack is a
deliberate, separate project. **One pick: `twm` + `xterm` (or `st`) + `xclock`.**

---

## 6. Phased decomposition (commit-stoppable milestones)

Each milestone is independently demonstrable and stoppable.

| M | Goal | Key obstacle | What proves it | Difficulty / risk |
|---|---|---|---|---|
| **M0** | kdrive X server **links + runs headless** on aarch64-rpi4b | Porting the X11 lib stack (xorgproto→xtrans→Xau/Xdmcp→xcb→libX11→pixman→Xfont) + `--disable-mitshm/dlopen/glamor`; AF_UNIX round-trip | `Xfbdev :0` reaches its event loop against a **stub fb** (anon mmap) + a test client connects over AF_UNIX (or TCP-localhost) and exits clean | **High** (volume of unported libs; AF_UNIX unknown) |
| **M1** | X **draws to `/dev/fb0`** — an xterm/xeyes visible on HDMI (software) | kdrive fbdev backend that uses `RPI4FB_GETMODE` for geometry + **shadow-FB → `write()`-blit** (no fbdev mmap/ioctls) | xeyes/xclock window visible on HDMI, pixels correct (no shift/tear-rows), fbcon banner replaced | **Medium** (backend is small; pixel-format + damage-flush tuning) |
| **M2** | **WM + input working** | kdrive input driver over `/dev/kbd0`(raw)+`/dev/mouse0` → `KdEnqueue*Event`; `twm` running | Move/click the mouse → cursor tracks; type in xterm; `twm` moves/raises windows | **Low–Medium** (raw HID + reference already exist) |
| **M3** | **GPU-accelerated** X (Glamor) — *stretch* | Port **EGL** (surfaceless, no GBM) on the gallium st; enable Glamor; **enforce X-as-sole-GPU-client**; present via render-to-scanout | Glamor enabled, a GL-accelerated 2D op (large blit/compositing) measured faster than pixman; HDMI correct; **no** concurrent GL client | **Very High / research** (EGL net-new; single-client constraint per §3.1) |
| **M3′** | Accelerated *clients* too (GLX/DRI) — *research* | Build a **GPU-arbiter daemon** (userspace DRM-equivalent: owns V3D, multiplexes MMU+submit, fences) | An X GL client (e.g. glxgears) renders on the GPU concurrently with the X server, no corruption | **Research / multi-month** (effectively writing DRM arbitration) |
| **M4** | A DE | GTK/Qt/glib stack net-new; no dlopen | A panel + file manager run | **Out of scope** for now; recommend stopping at M2 (+ M3 if pursued) |

**Recommended stopping point: M2** for a credible, demoable software X11 desktop.
M3 only if accelerated 2D is specifically wanted and the EGL + sole-client cost is
accepted. M3′/M4 are explicitly deferred.

---

## 7. Honest verdict — effort + accelerated-X feasibility

- **Software X (M0–M2): realistic, ~6–10 weeks at 1 FTE.** Dominated by porting
  the X11 library stack (M0) and AF_UNIX risk; M1/M2 are comparatively small
  because fb0, caches, and raw-HID input are done. This is the recommended target
  and the *practical ceiling* for sane effort. Output: `twm` + `xterm` + `xclock`
  on HDMI, keyboard + mouse working — a genuine "X11 desktop on Phoenix/Pi 4."

- **Accelerated X (M3): a research stretch, not a near-term deliverable.** It is
  achievable *only* in the narrow form "**X server uses the GPU for 2D
  (Glamor) while being the sole GPU process; clients are software-rendered**,"
  and even that requires a **net-new EGL port** (surfaceless, since GBM needs
  DRM). Estimate **+1–2 months** on top of M2, with real risk that EGL/Glamor
  bring-up on a faked-DRM winsys hits a wall.

- **Accelerated X with accelerated clients (M3′): blocked without a GPU-arbiter
  daemon.** The winsys reprograms the V3D's single MMU base register to its own
  page table and serializes one CORE0 submit queue with no cross-process lock
  (§3.1), so two GPU users corrupt each other. There is no DRM to arbitrate.
  Closing this is effectively writing the arbitration core of a DRM driver in
  userspace — **multi-month research**, out of scope for a desktop milestone.

**Bottom line:** the GPU working for GL Quake does **not** make accelerated X
easy, because Quake is a *single* in-process GPU client and X is inherently
multi-process. The faked-per-process DRM is exactly the assumption that breaks
when a second GPU consumer appears. **Ship software X; treat accelerated X as a
funded research follow-on contingent on EGL + single-client Glamor (and, for the
full dream, a userspace GPU arbiter).**

---

## 8. Cross-references

- Software TinyX bring-up plan (reconciled): [`tinyx-x11-demo.md`](../todo/tinyx-x11-demo.md)
- fb0 device: `sources/phoenix-rtos-devices/video/rpi4-fb/rpi4-fb.c` (+ `.h`),
  attended decisions `docs/inprogress/2026-06-05-fb0-attended-decisions.md`
- GPU winsys (the no-DRM/single-client crux):
  `tools/v3d-driver-port/v3d_phoenix_winsys.c`;
  build `tools/v3d-driver-port/build-v3d-phoenix.py` / `build-gl-phoenix.py`;
  Mesa port patch `tools/v3d-driver-port/mesa-phoenix-port.patch`
- GL Quake port + input reference: `tools/quakespasm-port/` (esp.
  `platform/pl_phoenix_in.c`), plan `docs/inprogress/2026-06-12-quakespasm-port-plan.md`
- Input drivers: `sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c` (raw HID mode),
  `sources/phoenix-rtos-devices/usb/usbmouse/`
- libphoenix surface: `sources/libphoenix/include/sys/{un,socket,mman}.h`;
  no `dlfcn.h`; no `shm_open`
- Ports tree (X11 stack absent): `sources/phoenix-rtos-ports/`
- Original X11 PR (not landed): `phoenix-rtos/phoenix-rtos-ports#82`

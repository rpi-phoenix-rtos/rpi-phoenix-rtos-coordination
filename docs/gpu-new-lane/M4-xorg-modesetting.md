# M4 preparation — `Xorg-drm`: X.Org + modesetting + glamor on the new lane

Milestone M4 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §4.7 and §5
(M4: "Xorg + modesetting + glamor + DRI3/Present + xf86 input driver"). Builds on
[M3](M3-libdrm-phoenix.md) (libdrm-phoenix, the `/dev/dri` nodes, Mesa-DRM GBM/EGL/GLES),
[M2](M2-kms-server.md) (`rpi4-kms`), [M1](M1-async-render-server.md) (`rpi4-v3d-async`) and
[E5](E5-deferred-reply.md) (IPC and `poll()` costs).

**Update (2026-09-27, part 2):** m4c = first light (xclock, glamor on V3D), m4d = Window Maker (Result
sections at the end). **DRI3/Present GL clients** — Mesa EGL-X11 build, the Phoenix xshmfence backend
(G16), `eglx11-demo`, `Xorg-drm-m4p2` — built and host-tested, cycle `m4p2a-eglx11` pre-registered:
[M4 part 2](#m4-part-2--dri3present-gl-clients-2026-09-27).

**Status (2026-09-27):** two Pi cycles, two build defects, both fixed, cycle **`m4c-xorg-drm`
re-registered** (§10). `m4a`: exit in bus configuration (`Cannot run in framebuffer mode`) — an
upstream test that is always fatal without libpciaccess → xorg-server patch 0008. `m4b`: past bus
configuration, then `Given depth (2) is not supported by the driver` — **libphoenix's `<ctype.h>`
macros evaluate their argument more than once**, so Xorg's config scanner read `DefaultDepth 24` as
2 → compat `ctype.h` (§6). See the Result sections at the end. Proven on hardware so far: bash job
control, config parsing, the builtin-module table, libdrm-phoenix inside Xorg (resources, caps,
dumb BO + ADDFB/RMFB), the fb-slot probe. `Xorg-drm` — the unmodified hw/xfree86 X
server of xorg-server **21.1.24** (the old lane's version and tarball) with the **modesetting** DDX,
**glamor** on GBM/EGL, **DRI2, DRI3, Present, Xv**, and a new xf86 input driver **`phxhid`** for
`/dev/kbd0` + `/dev/mouse0` — is one static aarch64-phoenix program: **0 undefined symbols**, text
20.3 MB, **20.8 MB stripped**. Every module Xorg would `dlopen()` is linked in and found through a
builtin-module table. Nothing committed, nothing staged; no server, no old-lane file, no sibling repo
touched. Code:
[`tools/gpu-lane/xorg-drm/`](../../tools/gpu-lane/xorg-drm/).

Evidence tags: **[read]** = read in source at the cited place; **[built]** = the cross build / link
shows it; **[inferred]** = reasoning, not verified.

---

## 0. Decisions at a glance

| Question | Decision |
|---|---|
| Server | xorg-server **21.1.24** hw/xfree86 (`Xorg`), meson, the same `xorg-server-21.1.24.tar.xz` the old lane's recipe pins (sha256 `1a4eb36c…`, read from `sources/phoenix-rtos-ports/xorg_server/`, never modified). Binary name **`Xorg-drm`** — the old lane's kdrive `Xphoenix`, its fbdev DDX and glamor shim are neither compiled nor linked (`build.sh` fails if any of their strings appears). |
| Modules without `dlopen` | **Builtin-module table** (xorg-server patch 0002): the program supplies `xf86BuiltinModules[]` = {name, `<name>ModuleData`, the symbols its users look up}; `LoadModule()` consults it before the module path and then runs the ordinary version-check/SetupProc path, `LoaderSymbolFromModule()` resolves from the module's list. Modules become archives on Phoenix (patch 0001). Alternatives in §4. |
| Linked modules | `modesetting`, `glamoregl` (with glamor whole), `shadow` (the no-glamor ShadowFB fallback), `phxhid`. `fb`, `dri2`, `dri3`, `present`, `dbe`, `record`, `extmod` are compiled into the server already (upstream's `compiled_in_modules`). |
| GL dispatch for glamor | **Real libepoxy 1.5.10** (MIT), static, EGL only, with a 1-patch **static-EGL dispatch**: every EGL/GL symbol is resolved through the linked Mesa's `eglGetProcAddress()` (Mesa answers every EGL and GL entry point) — no `dlopen("libEGL.so.1")`. Not the old lane's 3-function epoxy shim: glamor-on-EGL needs the full dispatch (EGL extensions, GLES vs desktop GL provider selection). |
| EGL/GL | The mesa-drm static Mesa 26.2.0 (GBM + EGL `drm` + GLES 2/3, v3d + vc4 via kmsro) **built privately into `build-out/mesa`** with the `DRM_CAP_PRIME` fix (mesa-drm patch 0008 + 0009), linked against **libdrm-phoenix `build-out-m3p3`** (`DRMPHX_TRACE`). `build.sh` refuses a Mesa without the fix (it checks the `drmGetCap` call in `u_init_pipe_screen_caps`). GLES-only (`-Dopengl=false`), so glamor runs on **OpenGL ES 3.1**; `--mesa-out` takes an `--opengl` Mesa to give glamor desktop GL instead. |
| Device discovery | No udev, no libpciaccess, no platform bus: modesetting's legacy `Probe()` opens `Option "kmsdev" "/dev/dri/card0"` and claims a framebuffer slot (patch 0006 makes the driver build without libpciaccess; patch 0008 stops xf86PostProbe from treating that claim as always fatal — the m4a failure; the platform-bus alternative is weighed in §4a). Configuration from `-config /etc/X11/xorg-drm.conf`. |
| Input | **`phxhid`**, a 400-line xf86 input driver (new file): usbkbd raw 8-byte boot reports diffed into evdev keycodes + 8, usbmouse 4-byte reports into relative motion/buttons/wheel; drained by a 10 ms server timer (not `poll()` readiness — G12). Keymap: a precompiled evdev/pc105/us `.xkm` compiled into the server (patch 0003; Phoenix has no xkbcomp). |
| Threads | `-Dinput_thread=false`: everything on the main thread (phxhid's timer, DRM events, clients). |
| DRI3 / Present | **Built and initialised.** DRI3 open works today; client buffers need G4 (and G16, new: process-shared fences for xshmfence); Present flips of client buffers need G7 + cross-process implicit sync; Present timing rides G12 (§8). |
| GLX | Off (`-Dglx=false`; glamor_glx.c dropped by patch 0007). |
| libphoenix gaps | libphoenix's multi-evaluating `<ctype.h>` macros dropped by `compat/include/ctype.h` (the m4b failure), three constants in `compat/include/` (`SI_USER`, `O_NOFOLLOW`, `RTLD_DEFAULT`), one os-support header block (patch 0005), `timingsafe_memcmp`/`reallocarray` from the server's own `libxlibc` fallbacks; **no link-time stand-in needed** (§6). |

## 1. Design: where Xorg-drm sits

```
 X clients (xclock, xterm, wmaker…)          ─ AF_UNIX /tmp/.X11-unix/X1 (+SCM_RIGHTS for DRI3)
          │
   Xorg-drm  (one static process)
   ├─ dix/os/xkb/randr/render/composite/present/dri3 (upstream)
   ├─ modesetting DDX ── libdrm-phoenix ── msgSend ──► rpi4-kms        /dev/dri/card0  (display)
   │     modes, CRTC, planes, flips, vblank           (planes via firmware SET_PLANE, vblank IRQ,
   │     events (read on the card fd)                   dumb-BO pool < 1 GiB, /kmsbuf exports)
   ├─ glamoregl + glamor ── epoxy ── Mesa EGL/GBM ── gallium v3d (kmsro) ── libdrm-phoenix
   │     2D/RENDER on the GPU                         ──► rpi4-v3d-async  /dev/dri/renderD128 (GPU)
   │     root pixmap = a GBM scan-out BO:                 BO_IMPORT of the /kmsbuf front buffer (G1),
   │     dumb BO in the kms pool, imported on             CL submits, fences
   │     the render node (G1), flipped/set by kms
   └─ phxhid ── read() ──► usbkbd /dev/kbd0, usbmouse /dev/mouse0
```

Start-up path [read: `hw/xfree86/drivers/modesetting/driver.c`, `glamor/glamor_egl.c`, M3 part 3]:

1. `Probe()` → `open("/dev/dri/card0", O_RDWR|O_CLOEXEC)` → `drmModeGetResources` (one connector)
   → `xf86ClaimNoSlot`. `PreInit()` → `drmGetCap`s, `drmmode_pre_init` (connector `HDMI-1`, the
   synthesized 1920×1080@60 mode) → `xf86LoadSubModule("glamoregl")` = builtin table →
   `glamor_egl_init(card0 fd)` → `gbm_create_device(card0)` → kmsro: vc4 display + v3d render
   (M3p3 steps 2–4) → `eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA)` → `eglInitialize` → needs
   `EGL_KHR_surfaceless_context` → desktop-GL attempt fails (not built) → **GLES 3.1 context** →
   needs `GL_OES_EGL_image`, `GL_EXT_texture_format_BGRA8888`, `GL_OES_texture_border_clamp`,
   `GL_OES_vertex_array_object` (all `dummy_true` on ES2+ in Mesa's extension table [read]).
2. `ScreenInit()` → front buffer `gbm_bo_create(1920×1080 XRGB8888, SCANOUT|RENDERING)` →
   renderonly `CREATE_DUMB` in the kms pool (~7.9 MiB) → `PRIME_HANDLE_TO_FD` (`/kmsbuf/<id>`) →
   render `BO_IMPORT` (G1) → glamor textures it through an EGLImage → `drmModeAddFB` →
   `drmModeSetCrtc` (EnterVT). glamor then renders the root window directly into the scanned-out
   buffer ("front-buffer rendering", as on Linux without TearFree): no copy, no flip, no present path.
3. `InitInput` → phxhid opens `/dev/kbd0` (raw mode) and `/dev/mouse0`, arms its timer.
4. Dispatch: client requests → glamor GL → `glFlush` in the block handler → `SUBMIT_CL` to
   `rpi4-v3d-async`, writing the kms-pool pages the HVS scans out.

**Checked for an Xorg-first:** Xorg is the first client that builds its own `drmModeModeInfo`
(`drmmode_ConvertToKMode`: fresh struct, `type = 0`, `vrefresh = 0`, xf86's mode name) instead of
passing the connector's struct back. That is safe: libdrm-phoenix's `SETCRTC` forwards only
`hdisplay`/`vdisplay` (`drm_phoenix_kms.c:562-565`) and rpi4-kms rejects only another size or a
non-zero x/y (`kms_main.c:1041-1043`; the atomic `MODE_ID` blob likewise compares only the size,
`:1066`) [read]; a disable (`fb_id 0`) turns every plane off and shows the console (`:1030`).

What changes versus the old lane's desktop: no `glReadPixels` + shadow + `write(/dev/fb0)` (the
77 ms full-screen present of the research doc's §2): the GPU writes the scan-out buffer.

## 2. What was built

| Path (under `tools/gpu-lane/xorg-drm/`) | What |
|---|---|
| `build.sh` | fetch (sha256-pinned) → private Mesa (first run) → libxcvt, libxshmfence, libepoxy → xorg-server meson (static archives only) → own objects → hand link → verification |
| `patches/xorg-server/0001…0008` | `git format-patch` series over 21.1.24 (§3) |
| `patches/libepoxy/0001` | static-EGL dispatch (§3) |
| `src/xorg_drm_builtin.c` | the builtin-module table (4 modules, 22 symbols) |
| `src/phxhid.c`, `src/phxhid_evdev_map.h` | the input driver; the HID→evdev table is the old kdrive server's (FreeBSD `evdev_usb_scancodes[]`, BSD-2 notice kept) |
| `compat/include/{ctype,signal,fcntl,dlfcn}.h`, `compat/xorg_drm_compat.c` | libphoenix-gap shim (§6) |
| `conf/xorg-drm.conf` | the server configuration (stage as `/etc/X11/xorg-drm.conf`) |
| `pi/xorg-drm-m4a.sh` | the Pi-side cycle script: bash does the job control psh lacks (§10) |

Outputs (`build-out/`, gitignored): `Xorg-drm` (unstripped, `addr2line`), **`Xorg-drm-stripped`
(stage this)**, `Xorg-drm.map`, `xorg-drm-full.patch` (the xorg-server series as one diff), the
private Mesa in `mesa/` (≈1.5 GB) with its `kmscube`, logs.

New files carry the Phoenix header with `%LICENSE%` (as the M1–M3 files); the patches touch MIT/X11
files and stay MIT (new files inside patches carry the MIT text). No GPL source was read or copied;
libepoxy, libxcvt and libxshmfence are MIT.

## 3. Patches

### xorg-server 21.1.24 (`patches/xorg-server/`)

| # | Patch | Lines | Rationale |
|---|---|---|---|
| 0001 | `xfree86: build the modules as archives on static-only systems` | +78/−8, 4 meson files | On `host_machine.system() == 'phoenix'` (`xorg_static_modules`): modesetting, glamoregl (glamor linked whole, `epoxy_dep` added — upstream relies on epoxy headers in `/usr/include`), shadow become `static_library`; everything the `Xorg` executable links becomes one `libxorgserver_static.a` (`link_whole: xorg_link`); the shared-only targets (exa, fbdevhw, shadowfb, wfb, `libxorgserver.so` for the symbol tests) are not defined — non-PIC archives cannot go into shared objects; `-DXORG_BUILTIN_MODULES`. Other systems unchanged. |
| 0002 | `loader: builtin-module table for servers without a dynamic linker` | +138 (`loader_builtin.h` new, MIT) | §4. A name missing from a module's list is logged (`Builtin module <m>: no symbol <s> in its table`) instead of a silent NULL; an entry may list a symbol as absent. |
| 0003 | `xkb: builtin precompiled keymap for systems without xkbcomp` | +852 (of which 780 = `.xkm` bytes) | `XkbCompileKeymapForDevice()` writes the embedded evdev/pc105/us keymap to `<xkm dir>/server-<display>.xkm` and `LoadXKM()`s it before trying xkbcomp. The bytes are the old lane's `builtin_keymap.h` (XkmFileVersion 15 = this tree's `XKM.h`); the mechanism is the kdrive server's, rebased onto 21.1's `ddxLoad.c`. `-Dxkb_output_dir=/tmp`. |
| 0004 | `os: enlarge accepted clients' receive buffer on Phoenix-RTOS` | +21 (`__phoenix__`) | Phoenix AF_UNIX sockets start with a 4 KiB ring: a 1.2 MB PutImage = ~300 blocking round trips (measured 402 ms/frame with the kdrive server). `SO_RCVBUF` 256 KiB→64 KiB right after `accept()`. Same change as the old lane's `os-client-rcvbuf` patch. |
| 0005 | `os-support: treat Phoenix-RTOS like the other POSIX systems` | +4/−3 | `xf86_OSlib.h` has no Phoenix block, so posix_tty.c/sigio.c/xf86Config.c lacked termios/`sys/stat.h`/`POSIX_TTY`; take the Linux/glibc block (its VT parts are `__linux__`-only). |
| 0006 | `modesetting: build without libpciaccess` | +11 | Upstream modesetting does not compile with `-Dpciaccess=false` (includes `xf86Pci.h` → `<pciaccess.h>`; references `ms_device_match`/`ms_pci_probe`/`probe_hw_pci` defined only under `XSERVER_LIBPCIACCESS`). Guarded; DriverRec PCI members NULL → xf86 probes through `Probe()` (kmsdev path). |
| 0007 | `glamor: build glamor_glx.c only when epoxy has GLX` | +15/−1 | An EGL-only libepoxy installs no `<epoxy/glx.h>`; `glamor_glx.c` serves only Xephyr's GLX screens. Built when `epoxy.pc` says `epoxy_has_glx=1` (default when absent); otherwise `GLAMOR_NO_GLX` and `glamor_init()` fails cleanly for a GLX-screen request. |
| 0008 | `xfree86: allow a framebuffer-slot probe in builds without libpciaccess` | +7/−1 | **The m4a failure.** `xf86PostProbe()` (`common/xf86Bus.c:556`) aborts when a framebuffer slot *and* a real-bus slot were claimed; its PCI term was `pciSlotClaimed` with libpciaccess but the constant **`TRUE`** without it, so every framebuffer-slot claim — the only probe path with neither libpciaccess nor udev — was fatal, whatever the config said. `xf86ClaimFbSlot()` (`xf86fbBus.c:57-67`) already refuses an fb slot after any bus slot, and without PCI support no PCI slot exists: the term is `FALSE`. [built: `xf86PostProbe` compiles to a bare `ret`; the FatalError string is gone.] |

### libepoxy 1.5.10 (`patches/libepoxy/0001`, +85/−1)

`dispatch: resolve symbols through a statically linked EGL on Phoenix-RTOS` — with `EPOXY_STATIC_EGL`
(meson, host `phoenix`; requires `-Degl=yes -Dglx=no`) `get_dlopen_handle()` reports every library
present (and skips the dynamic-linker guard, which relies on a constructor) and `do_dlsym()` calls
`epoxy_static_proc_address()` (new file `dispatch_static.c`, which includes the *implementation's*
`<EGL/egl.h>` so `eglGetProcAddress` is the linked function, not epoxy's macro).
[read: Mesa `eglapi.c:2765` answers `egl*` names from `eglentrypoint.h` (all EGL entry points) and
everything else from `_mesa_glapi_get_proc_address`.] libepoxy's provider logic (GLES vs desktop,
version/extension checks) is untouched.

### Build options that matter

`-Dxorg=true -Dglamor=true -Dglx=false -Ddri1=false -Ddri2=true -Ddri3=true -Dxv=true -Dudev=false
-Dudev_kms=false -Dhal=false -Dsystemd_logind=false -Dpciaccess=false -Dint10=false -Dvgahw=false
-Ddga=false -Dmitshm=false -Dxdmcp=false -Dsecure-rpc=false -Dinput_thread=false -Dsha1=libmd
-Dlisten_tcp=false -Ddefault_font_path=/usr/share/fonts/X11/misc,/usr/share/fonts/X11/75dpi
-Dxkb_output_dir=/tmp -Dlog_dir=/tmp -Dfallback_input_driver=phxhid`, `debugoptimized`, asserts on,
`-Db_staticpic=false`. **Xv must stay on:** modesetting's `ScreenInit` calls `xf86XVScreenInit`
unconditionally for glamor's adaptor [built: undefined with `-Dxv=false`].

## 4. Why a builtin-module table

| Option | Verdict |
|---|---|
| `dlopen()` the `.so` modules (libphoenix has Phase-A dlopen, SYMTAB resolution) | no: non-PIC libphoenix, no TLS relocations in `dl.c`, Mesa must be one static megadriver anyway (E7 §3.4); modules would need the server's symbols exported from a static binary |
| Resolve `LoaderSymbol*()` with Phoenix `dlsym()` over the executable's own SYMTAB | fragile: stripping (the staged binary is stripped) removes it; silent NULL on any mismatch |
| Patch modesetting to call glamor/shadow directly | touches the driver in ~25 places and diverges from upstream's module ABI |
| **Builtin table in the loader + modules as archives** | **chosen**: two small, generic, upstreamable patches (a meson switch + a loader table), zero driver changes, modules keep their ModuleData/version checks/options, and the table is the one place that lists what is linked. The Xorg log still says `Module "modesetting": linked into the server`. |

The table (`src/xorg_drm_builtin.c`) mirrors modesetting's `LoaderSymbolFromModule()` calls (17 glamor
names, 5 shadow names) plus `DRI2Version` (modesetting's `xf86LoaderCheckSymbol`). A rebase that adds a
lookup shows up as a loader error line, not as a crash.

### 4a. Device discovery without udev: fb-slot probe (chosen) vs a static platform bus

m4a showed that modesetting's legacy `Probe()` (claims a framebuffer slot, `driver.c:498`) was killed by
xf86PostProbe's always-TRUE test (patch 0008). Three ways out were weighed:

| Option | Verdict |
|---|---|
| (a) **Platform bus without udev** — `XSERVER_PLATFORM_BUS` plus a static `xf86_platform_devices` entry for `/dev/dri/card0`, so `ms_platform_probe` runs | upstream-shaped but **not small** in 21.1: meson enables the platform bus only with `udev_kms` (`include/meson.build:327,377`) and compiles `xf86platformBus.c` only with `udev` (`common/meson.build:68`); that file includes `<pciaccess.h>` unconditionally and dereferences `struct pci_device` in `xf86platformProbe`/`probeSingleDevice` (`pci_device_probe`, `pci_device_is_boot_vga`, `xf86scanpci`, `device_id`) without guards; the device probe (`xf86PlatformDeviceProbe`, `…CheckBusID`, `…ReprobeDevice`, `NewGPUDeviceRequest`) exists only in `os-support/linux/lnx_platform.c` (with a PCI branch); `config_odev_probe()` has only a udev backend (`config/config.c:74-78`). ≈ 150 lines over 6–7 files, on a path upstream never runs (platform bus without udev) [read]. It buys hot-plug, GPU-screen/PRIME-offload plumbing and systemd-logind fds — none of which the Pi's single fixed display uses. Worth doing only if a second GPU/display device ever appears. |
| (b) **Accept the framebuffer-slot probe when no other bus exists** | **chosen**: one generic, clearly-correct line (patch 0008); modesetting's legacy path is complete in 21.1 — `ms_get_drm_master_fd` opens `kmsdev` itself (`driver.c:1100`), everything else keys on `BUS_PCI`/`BUS_PLATFORM` only for fd ownership and PRIME/offload paths we do not use. The log keeps upstream's `(WW) Falling back to old probe method for modesetting`. |
| (c) a `BusID` in `xorg-drm.conf` | **cannot help**: without libpciaccess the abort in `xf86PostProbe` does not look at any BusID; a `BusID` would at most change which slot is claimed, and `PCI:`/`platform:` BusIDs have no bus to match here |

## 5. Input: `phxhid`

| | |
|---|---|
| Keyboard | no-op bell and keyboard-control procs (dix/XKB call them unconditionally on `ChangeKeyboardControl` and indicator updates); `open("/dev/kbd0", O_RDWR|O_NONBLOCK)` (40 × 25 ms retries: pl011-tty releases the single-opener device asynchronously once the display leaves text mode), write `0x01` → usbkbd raw 8-byte boot reports; modifier-bit and usage diffs → `xf86PostKeyboardEvent(evdev + 8)`; `InitKeyboardDeviceStruct(dev, NULL…)` → XKB → the builtin keymap; autorepeat by XKB |
| Mouse | `/dev/mouse0` 4-byte boot reports → `xf86PostMotionEvent(Relative)`, buttons HID bit0/1/2 → X 1/3/2, wheel → 4/5 press+release; 7 buttons, 2 relative axes with the standard labels |
| Event source | a 10 ms `TimerSet` timer drains every enabled device (≤ 64 reads each); timers run on the main thread and `poll()` honours sub-20 ms timeouts exactly (kernel `posix.c:3254-3257` [read]), so latency ≤ 10 ms; readiness-driven `xf86AddEnabledDevice()` would ride G12's 20 ms re-poll for device fds (§8) |
| Failure | an unopenable device is logged (`PHXHID dev=… open=<error>`) and produces no events; never fatal (a failed core keyboard would abort the server) |
| Config | `Driver "phxhid"`, `Option "Device"`, optional `Option "Type" "keyboard"|"mouse"`; `AutoAddDevices false` (no hotplug source) |

The keyboard is held by the console until the HDMI console leaves text mode: on the new lane only
`rpi4-kms -C` does that (never exercised yet, M2 §13). The first cycle (§10) runs without `-C` and
grades both keyboard outcomes; a follow-up arm adds `-C`.

## 6. libphoenix gaps (shim, never edits to `sources/libphoenix`)

| Gap (tree sysroot of 2026-09-27) | Used by | Shim | Effect |
|---|---|---|---|
| **`<ctype.h>` macros evaluate their argument more than once** (`#define __isdigit(c) (((c) >= '0' && (c) <= '9') ? 1 : 0)`, same for all 15 `is*`/`to*`; C17 7.1.4 requires single evaluation) | `hw/xfree86/parser/scan.c:373` `while (isdigit(c = configBuf[configPos++]) …)` advanced two characters per test: **`DefaultDepth 24` → 2** (the m4b failure; any number in xorg.conf is affected). The only such call site in the xorg-server tree [grep] | `compat/include/ctype.h`: `#include_next`, then `#undef` all 15 macros — libphoenix exports a real function for each, so calls become single-evaluation (gcc may still inline them as builtins, correctly). `build.sh` preprocesses a probe and fails if the shim is not in effect | **real fix belongs in libphoenix** (single-evaluation macros, e.g. `((unsigned)(c) - '0' < 10u)` or inline functions). Other victims outside this build: the ports' prebuilt **libXfont2** `fontxlfd.c:156` `isdigit((unsigned char)*p1--)` (double decrement while formatting scalable-font matrices, can step below the buffer) — linked into Xorg-drm *and* the old lane's X server; fixed only by a libphoenix fix + rebuild of the port. Mesa's GBM/EGL/v3d/util/GLSL sources have no side-effecting ctype arguments [grep] |
| no `si_code` constants (`SI_USER`) | `os/osinit.c` `OsSigHandler` (wording of a log line) | `compat/include/signal.h` → 0 | none |
| no `O_NOFOLLOW` | `os/utils.c` `LockServer` (refuse a symlinked lock file) | `compat/include/fcntl.h` → 0 | plain open of `/tmp/.X1-lock` |
| no `RTLD_DEFAULT` | `hw/xfree86/loader/loader.c` `LoaderSymbol` fallback after the builtin tables | `compat/include/dlfcn.h` → NULL | the fallback cannot succeed in a static program anyway |
| no POSIX block for Phoenix in `xf86_OSlib.h` | os-support, xf86 common | patch 0005 | — |
| no `timingsafe_memcmp`; `reallocarray` | `os/mitauth.c`; os | the server's own `libxlibc` fallbacks (meson detects the absence) | — |
| `pthread_condattr_setpshared(PROCESS_SHARED)` → `EINVAL` (`pthread.c:1549`), no `memfd_create`/`shm_open` | libxshmfence (DRI3 fences) | none — built (`--disable-futex`, `SHMDIR=/tmp`, mkostemp) | **G16 (new)**: `xshmfence_alloc_shm`/`map_shm` work, but the shared mutex/cond init fails → DRI3 `FenceFromFD`/client idle fences fail at run time |
| `sendmsg`/`recvmsg` carry `warning("not fully supported")` | xtrans fd passing (DRI3) | none | the attribute is stale for this use: multi-iov is copied through one buffer (`sys/socket.c:135-210` [read]) and SCM_RIGHTS descriptors cross AF_UNIX (E1 PASS) |
| inherited from Mesa-DRM | `static_assert`, `SCNxPTR`, `LOCK_*`, `_SC_PHYS_PAGES`, `open_memstream`, `posix_memalign` probe | mesa-drm's `compat/` + `libmesadrm-compat.a` (linked) | as M3p3 |

The link needed **no** stand-in (`compat/xorg_drm_compat.c` stays empty; `build.sh` compiles a
stand-in only while `libphoenix.a` lacks the symbol, list `XORG_DRM_COMPAT_FNS`).

## 7. Verification [built]

| Check | Result |
|---|---|
| static link | OK; link log only libphoenix's `sendmsg`/`recvmsg` attribute warnings |
| `aarch64-phoenix-nm -u Xorg-drm` | **0** symbols |
| `size` | text ≈ 20.29 MB, data ≈ 546 KB, bss ≈ 541 KB; file 108 071 952 B, **stripped 20 844 704 B** (old lane: `Xphoenix` 6.1 MB fbdev-only, `Xphoenix-glamor-daemon` 28.1 MB; kmscube 16.5 MB) |
| sha256 (first 16) | **`Xorg-drm` `002aca08c9a1f231`, `Xorg-drm-stripped` `4c13641af1e84ada`** (patch 0008 + ctype shim; m4a ran `9538077f3bdfc6db`, m4b ran `b67d33783531e854`; a `--relink` reproduces the same bytes) |
| ctype shim in effect | the preprocessed `scan.c:373` keeps `isdigit(c = configBuf[configPos++])` as a call (no macro expansion); `build.sh` probe passes |
| patch 0008 in the binary | `xf86PostProbe` disassembles to `ret`; `Cannot run in framebuffer mode` 0 occurrences (`build.sh` checks it) |
| modules | `modesettingModuleData`, `glamoreglModuleData`, `shadowModuleData`, `phxhidModuleData`, `xf86BuiltinModules`, `LoaderBuiltinFind`; `glamor_egl_init`, `glamor_init`, `ms_present_screen_init`, `dri3_screen_init`, `present_screen_init` |
| libdrm-phoenix / Mesa | `__wrap_mmap`, `drmPhoenixMmap`, `drm_phoenix_ioctl`, `gbmint_get_backend`, `kmsro_drm_screen_create`, `v3d_drm_screen_create_renderonly`, `epoxy_static_proc_address`, `xshmfence_map_shm`, `libxcvt_gen_mode_info`; strings `/dev/dri/card0`, `/dev/dri/renderD128`, `/kmsbuf`, `libdrm-phoenix:`, `DRMPHX_TRACE`, `kmsro`, `V3D 4.2`, `EGL_KHR_platform_gbm`, `EGL_MESA_platform_gbm` |
| X server | strings `modesetting`, `glamor` (96), `PHXHID dev=`, `linked into the server`, `builtin keymap`, `DRI3`, `Present`, `X.Org X Server`, builder `Xorg-drm` |
| old lane absent | `Xphoenix`, `[fbdev]`, `fbdevKeyboardDriver`, `fbdevMouseDriver`, `FBCONSETMODE(`, `glamor_phoenix`, `phxgl`, `v3d-winsys:`, `phoenix_v3d_ioctl`, `peek_next_scanout`, `v3d-srv`, `/dev/v3d-srv`: **0** each |
| Mesa fix present | `u_init_pipe_screen_caps` calls `drmGetCap` (mesa-drm 0008) — checked by `build.sh` on the private Mesa |
| `dlopen` | linked (the loader's fallback for a module outside the table); libepoxy never calls it |
| warnings | 23 upstream warning lines (unused variables in posix_tty.c and modesetting's non-platform-bus path, xtrans const, the attribute warnings); none in patched lines; own files built with `-Wextra -Werror` |

## 8. What works on today's servers, and what waits for which gap

| Feature | Today (m3p2/m3p3 servers + libdrm-phoenix m3p3) | Waits for |
|---|---|---|
| Server start, modeset, root window on HDMI, glamor 2D/RENDER/Xv on the GPU, core fonts, xkb | expected to work: every call is on paths drmprobe (36/36) and kmscube exercise — G1 import of the kms front buffer, G2 fstat, G3 dmabuf size, `/dev/dri` names | Pi cycle §10 |
| Front-buffer updates | immediate (GPU writes the scanned-out buffer; the block handler flushes); tearing possible, as Linux modesetting without TearFree | — |
| Cursor | software cursor (`SWcursor on`) | a cursor plane behind `MODE_CURSOR` (libdrm-phoenix follow-up, M3 §3.2) |
| Gamma / colour maps | `gamma_size 0`: RandR gamma absent | not needed for M4 |
| DRI2 | extension present; clients need GEM flink names (`GEM_FLINK` stub) | not planned (DRI3 is the path) |
| **DRI3 open** | works: glamor re-opens `drmGetDeviceNameFromFd2(card0)` = `/dev/dri/card0`, `drmGetMagic` (library-local) + `drmAuthMagic` (accepted) → the fd crosses the X socket by SCM_RIGHTS (E1) [read: `glamor_egl.c:841-876`] | — |
| **DRI3 client buffers** (`PixmapFromBuffers`) | a buffer allocated **on card0** (a kms dumb BO: Mesa's scan-out-capable back buffers) imports: `gbm_bo_import` → render `BO_IMPORT` ns=kmsbuf (G1 ✅); the renderonly scan-out import (`renderonly_create_gpu_import_for_resource`: export from the render node + import on card0) fails soft → pixmap usable for compositing, not for flips [read: `v3d_resource.c:1007`, `renderonly.c:131-170`]. A buffer allocated **on the render node** cannot even be exported by the client | **G4** (`V3DA_OP_BO_EXPORT` + `/v3dbuf`, and `BO_IMPORT ns=v3dbuf`) |
| DRI3 `BuffersFromPixmap` (server → client) | `glamor_make_pixmap_exportable` → GBM scan-out BO (kms dumb) → card0 PRIME export ✅ [inferred] | — |
| **DRI3 fences** (`FenceFromFD`, Mesa's loader_dri3 idle tracking) | xshmfence's pthread backend fails at `pthread_condattr_setpshared(PROCESS_SHARED)`; cross-process coherence of MAP_SHARED `/tmp` files is also unproven | **G16 (new)**: process-shared memory + pshared mutex/cond in libphoenix, or an xshmfence backend over a server (e.g. the render server's fence page / syncobjs = G6). ↪ **Part 2: closed with a polled-word backend in shmsrv memory (§P2.2), no libphoenix change** |
| DRI3 explicit sync (syncobj fds) | not in 21.1's DRI3 1.2 | G6 (for 24.x DRI3 1.4 / Present explicit sync) |
| **Present — window copies at vblank** | work; timing = G12 below | G12 |
| **Present — flips of client buffers** | refused: a client pixmap must become a KMS fb on card0; for a buffer another process allocated that is `KMS_OP_PRIME_IMPORT` (G7), and the flip must wait for the client's GPU work (cross-process implicit sync, `BO_LAST_FENCE`, M3p2 G13 note) → falls back to a glamor copy | **G7** + `BO_LAST_FENCE` |
| Page flips of Xorg's own buffers | in-process G13 covers them (imports recorded in this process) | — |
| Mesa GLX/EGL-x11 clients | no client-side Mesa X11 platform is built (`-Dplatforms=`) | M4 work: Mesa `-Dplatforms=x11` (+ libxcb-dri3/present/xfixes/sync, all in the ports prefix) → then G4, G16. ↪ **Part 2: built (`mesa-drm --x11`); G4 turned out not to be on the DRI3 path (§P2.1)** |

### G12 in Xorg's main loop, quantified

Xorg waits in `poll()` (ospoll, no epoll on Phoenix) on: the listening socket and every client socket
(AF_UNIX), the card0 descriptor (modesetting's `SetNotifyFd` for DRM events), nothing for input
(phxhid is timer-driven). The kernel (`posix.c:3238-3296` [read]) runs a poll set that contains an
AF_UNIX socket as: block on the unix wait queue for `min(timeout, 20 ms)`, then re-query **every** fd
(one `atPollStatus` message to rpi4-kms per iteration, ~31 µs, E5). Consequences:

| Traffic | Latency added by the poll model |
|---|---|
| X requests / replies (AF_UNIX) | **none**: a socket write wakes the unix queue at once |
| Server timers (phxhid 10 ms, Present/DPMS/screensaver timers) | none: timeouts < 20 ms are honoured exactly |
| DRM events on card0 (flip complete, vblank for Present, queue_sequence) | **0–20 ms** after the event when no client traffic arrives meanwhile (E5 measured p50 8.2 ms, max 16.7 ms for a phase-locked loop); earlier when any client request wakes the loop (the re-query then sees POLLIN) |

Effect on a vsync-paced client (Present flip or copy at vblank, swap interval 1): the next frame can
only be queued after the completion event. With event-notice delay *d* ~ U(0, 20 ms), render+submit
*r* and the kms latch guard (~2 ms, M2 §5), the next vblank is caught only if *d + r* ≲ 14.7 ms:
P = 73 % for *r* ≈ 0 → mean frame interval 0.73·16.7 + 0.27·33.3 = **21 ms ≈ 47 fps**; *r* = 5 ms →
P = 48 % → **≈ 40 fps**; *r* ≥ 15 ms → 30 fps regardless (GPU-bound anyway) [inferred model]. The
desktop itself (front-buffer glamor rendering) and input are **not** affected. Fixes, in order of
cost: (a) **E5 §5 item 2** — kernel readiness wake-up for server-backed fds (a server marks an oid
ready; pollers wake like the unix queue) — the real fix, also for Weston/SDL; (b) a user-space
bridge: a thread per card fd blocks in `read()` (E5: blocking reads wake in µs) and forwards the
event bytes into an AF_UNIX `socketpair` that modesetting registers instead of the card fd —
`drmHandleEvent(socketpair_fd)` parses the same bytes unchanged; ~60 lines, but it moves event
ownership out of the library; (c) E5 §5 item 1 (`block_ms` for a single non-unix fd) does **not**
help Xorg, whose poll set always contains AF_UNIX sockets.

## 9. Risks only the Pi can show

| # | Risk | Where it would show |
|---|---|---|
| R1 | glamor on GLES 3.1 on V3D 4.2 is new here (the old lane ran glamor on desktop GL): a shader glamor needs that the v3d compiler rejects, or a GLES-only glamor path (e.g. `glamor_transfer` BGRA formats, `GL_MESA_pack_invert` absent on ES) misbehaves | `glamor: …` errors / wrong colours / black windows; remedy: `--mesa-out` an `--opengl` Mesa (then desktop GL) |
| R2 | the static-EGL epoxy dispatch resolves a function Mesa's glapi exports only as a no-op for the current API | `epoxy: … not found` abort, or a GL call without effect — read the log line and the `DRMPHX` trace |
| R3 | kms pool: the front buffer takes ~7.9 MiB of 32 MiB; `glamor_make_pixmap_exportable` (DRI3 export, Present) takes more per full-screen window | `CREATE_DUMB failed` → `rpi4-kms -p 48` |
| R4 | memory: 20.8 MB static binary + Mesa's compiler state + glamor's program cache | the first-frame compile time and any `ENOMEM` |
| R5 | `/dev/kbd0` is held by the console without `rpi4-kms -C` | `PHXHID dev=/dev/kbd0 … open=<error>` — graded, not a failure of the first cycle |
| R6 | xclock/xterm are the old lane's static clients; they speak plain X11 (+RENDER/Xft) and need nothing from the new lane, but they were only ever run against the kdrive server | client errors on stderr (`Xlib:` / `Warning:` lines) |
| R7 | Xorg's first-generation paths never ran on Phoenix: lock file, `/tmp/.X11-unix` creation, `-logfile` on NFS, `-terminate` | the `(EE)` line and the log file |

## 10. Pre-registered Pi cycle `m4c-xorg-drm` (one netboot cycle; re-registered after m4a, m4b)

m4a (queue22) and m4b (queue24) used the same staging and commands and stopped at build defects
(Result sections). **m4c = m4b with the ctype-fixed binary** (`Xorg-drm-stripped` sha256
`4c13641af1e84ada…`); `xorg-drm-m4a.sh` and `xorg-drm.conf` are unchanged since m4b (re-stage only
the binary). Staging paths and commands are unchanged; only the label differs.

**Question:** does the unmodified X.Org modesetting DDX with glamor bring up an X screen on HDMI
through libdrm-phoenix, Mesa-DRM and the two new-lane servers — GBM/EGL context on V3D, a GPU-rendered
root window in a kms scan-out buffer — serve a stock X client (xclock, one repaint per second) and
exit cleanly; and do input, DRI3 and Present initialise as predicted?

**Preconditions:** netboot image ≥ build 9 (as M3 §7); no GPU app, no old-lane X, no SDL program, no
`rpi4-v3d` in the boot; the M3 part-2 servers staged as `/bin/rpi4-v3d-async-m3p2` and
`/bin/rpi4-kms-m3p2` (staged and proven by `m3p2-drmprobe`); `m3p3-kmscube` should have passed (its
GBM/EGL path is step 1 of this one; if it failed, fix that first — this cycle adds nothing).

**Build + stage (coordinator):**

```
tools/gpu-lane/xorg-drm/build.sh        # first run also builds the private Mesa (build-out/mesa)
EXPORT=/srv/phoenix-rpi4-nfs-gcc16      # the live fsid=0 export (/etc/exports currently lists the Linux rootfs)
sudo mkdir -p "$EXPORT/etc/X11"
```

| Source | Export path |
|---|---|
| `tools/gpu-lane/xorg-drm/build-out/Xorg-drm-stripped` | `$EXPORT/bin/Xorg-drm` |
| `tools/gpu-lane/xorg-drm/conf/xorg-drm.conf` | `$EXPORT/etc/X11/xorg-drm.conf` |
| `tools/gpu-lane/xorg-drm/pi/xorg-drm-m4a.sh` | `$EXPORT/bin/xorg-drm-m4a.sh` |
| already on the export (old lane, unchanged): `/bin/xclock`, `/bin/xterm`, `/bin/bash`, `/bin/sleep`, `/bin/kill`, `/bin/rm`, `/usr/share/fonts/X11/{misc,75dpi}`, `/usr/share/X11/{locale,app-defaults}`, `/var/log`, `/root` | — |
| already staged (M3p2): `/bin/rpi4-v3d-async-m3p2`, `/bin/rpi4-kms-m3p2`, `/bin/kmstest-m3p2`, `/bin/v3dasync-ping` | — |

(`sudo install -m 755 …` for the binaries/script, `-m 644` for the conf; `cmp` afterwards. Keep the
unstripped `build-out/Xorg-drm` on the host for `addr2line`.)

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m4c-xorg-drm --idle-secs 45 --max-cmd-secs 240 \
    --hdmi-dense-on 'XORGDRM client start' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-m3p2 -G" \
    "/bin/bash /bin/xorg-drm-m4a.sh" \
    "/bin/kmstest-m3p2 stats" \
    "/bin/kmstest-m3p2 quit" \
    "/bin/v3dasync-ping stats" \
    "/bin/v3dasync-ping quit"
```

psh has no `&` and no `;`: the servers detach themselves; **bash** (on the export, used by the old
lane's daemon scripts the same way) runs `Xorg-drm :1 -config /etc/X11/xorg-drm.conf -logfile
/var/log/Xorg-drm.1.log -verbose 3 -nolisten tcp -ac -terminate &` with `DRMPHX_TRACE=1`
(rate-limited: first 16 calls per request number, then 1 in 256), waits ≤ 90 s for
`/tmp/.X11-unix/X1`, starts `DISPLAY=:1 xclock -geometry 480x480+720+300 -update 1 &` with the
environment pl_phoenix_xlaunch gives the old clients (`HOME=/root PATH=/bin XFILESEARCHPATH
XLOCALEDIR`), prints a heartbeat every 10 s for 30 s (psh-interact's idle cut is 45 s), kills the
client, and waits ≤ 15 s for `-terminate` to end the server. Wall clock ≈ netboot 60–150 s + 2 × ~10 s
+ script 60–150 s + 4 × ~10 s ≈ 4–6 min. Grade:

```
grep -a -E '^(XORGDRM|PHXHID|KMS|V3DA|DRMPHX|KMSTEST|V3DAPING) |\((EE|WW|II)\)|glamor|modeset\(|XKB:|Fatal|Xlib|Warning' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m4c-xorg-drm.log
./scripts/uart-summary.sh m4c-xorg-drm
cat $EXPORT/var/log/Xorg-drm.1.log        # the full X log (verbosity 3), after the cycle
```

Allow ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice.

**Predictions** (in boot order) and what each alternative means:

| Line / observation | Predicted | If instead… |
|---|---|---|
| `V3DA srv …` / `KMS srv …` ready lines incl. `dri name=/dev/dri/… registered=1` | as in m3p2 | a server missing: staging/boot — stop. |
| `XORGDRM start …`, `XORGDRM server pid=…` | once | `bash: … not found`: bash not staged / psh quoting. |
| `X.Org X Server 1.21.1.24`, builder `Phoenix-RTOS new GPU lane (Xorg-drm)`; `Using config file: "/etc/X11/xorg-drm.conf"` | first lines of the server | `(EE) Unable to locate/open config file`: staging; a lock-file/`/tmp/.X11-unix` error: R7. |
| `(II) Module "modesetting": linked into the server`, later `"glamoregl"`, `"phxhid"` (and no `Failed to load module`) | builtin table works (**proven by m4a** for modesetting and phxhid) | `couldn't open module modesetting`: table not consulted (patch 0002 missing) — stale build. |
| `(WW) Falling back to old probe method for modesetting`, `(II) modeset(0): using /dev/dri/card0`, and **no** `Cannot run in framebuffer mode` | the legacy fb-slot probe passes xf86PostProbe (patch 0008; **proven by m4b**) | the fatal line again: an m4a binary was staged. |
| `DRM_IOCTL_GET_CAP … cap=0x3 value=0x18`, `CREATE_DUMB w=1 h=1 bpp=32`, `ADDFB … depth=24`, `RMFB`, `DESTROY_DUMB` (drmmode_get_default_bpp's probe), then `(**) modeset(0): Depth 24, (--) framebuffer bpp 32` and `(==) modeset(0): RGB weight 888` — **no** `Creating default Display subsection … 2/4` / `Given depth (2)` | as in m4b up to the ADDFB, then the configured depth 24 (ctype shim) | `Given depth (2)` again: the m4b binary was staged (check the sha256 `4c13641a…`). Any other depth error: read `(**) Depth` — the config value now reaches the server. |
| `DRMPHX conn fd=… path=/dev/dri/card0 node=card0 …`; `(II) modeset(0): … /dev/dri/card0`; `Output HDMI-1 connected`, mode `1920x1080` | Probe/PreInit through libdrm-phoenix | `(EE) No devices detected` / `no screens found`: Probe's `check_outputs` failed — read the `DRMPHX ioctl … name=DRM_IOCTL_MODE_GETRESOURCES rc=` line. |
| `KMS srv fstat answered …`, `V3DA srv fstat answered …` (if not already answered), `DRMPHX conn … node=render` | GBM/kmsro pairing (kmscube's step 2) | `couldn't get display device`: `gbm_create_device` NULL — as kmscube's failure table (M3p3). |
| `glamor: Using OpenGL ES 3.1 context` (3.0/2.0 possible: glamor asks for ES 2, Mesa returns its highest compatible ES; the desktop-GL attempt fails silently first) and **`glamor X acceleration enabled on V3D 4.2`** | glamor up on the GPU | `EGL_KHR_surfaceless_context required` / `GL_… required` / `Failed to create GL or GLES2 contexts`: R1 — then the server falls back to ShadowFB (dumb buffer, CPU `shadow` module) and the cycle still grades the display half: expect `glamor initialization failed` + `ShadowFB: …` and a CPU-drawn screen. |
| `V3DA srv import handle=… ns=kmsbuf id=… pages=2026… contiguous=1 …` once or twice, `KMS srv kmsbuf atSize …` | the front buffer (and GBM's) imported on the render node (G1/G3) | `Failed to get v3d handle for dmabuf` / `Couldn't get size of dmabuf fd`: G1/G3 as kmscube's rows; `CREATE_DUMB failed`: R3 (`-p 48`). |
| `(II) Initializing extension DRI3`, `… Present`, `… XVideo`, `… RANDR`, `… Composite`, `… RENDER`; **no** `Failed to initialize DRI3` | DRI3/Present initialised (§8) | `Failed to initialize DRI3`: `drmGetDeviceNameFromFd2(card0)` returned NULL (libdrm-phoenix identity) — M3 §2.9. |
| `XKB: using the builtin keymap (evdev/pc105/us): /tmp/server-1.xkm` | once per keyboard device (core + phxhid) | `builtin keymap unusable`: xkm version mismatch — core keyboard would then abort (`Failed to activate virtual core keyboard`). |
| `PHXHID dev=/dev/mouse0 type=mouse open=ok …` or `open=<error>` (no mouse attached); `PHXHID dev=/dev/kbd0 type=keyboard open=…` | **both outcomes are acceptable**: without `-C` the console may hold `/dev/kbd0` (R5) | a crash in phxhid: `addr2line` the PC. |
| `XORGDRM socket=up wait_s=<1–20>` | within ~20 s (static 20.8 MB binary from NFS ≈ 1 s; Mesa screen + glamor init a few s) | `socket=missing … server=exited` + `XORGDRM server exited rc=… before its socket appeared`: the server died — the `(EE)` lines above say where (the script then ends at once); `server=running` after 90 s: a hang in init — the last `DRMPHX`/`V3DA`/`KMS` line names the stuck request. |
| HDMI (dense snapshots from `XORGDRM client start`) | **black X root** covering the console, then **an xclock face (white, black hands) at ~(720,300) 480×480**, the seconds hand at a different angle in consecutive snapshots; software cursor (arrow/X) near the centre | console text still visible: SETCRTC never reached the display (`KMS apply …`); black screen and no clock with the client alive: glamor draws elsewhere (compare `V3DA srv import pa0` with `KMS pool pa`) or never flushes; a clock that never advances: the server stalled (look for a parked `V3DA` wait / `DRMPHX … rc=-110`); garbage: tiled buffer scanned out (impossible for SCANOUT BOs, M3p3). |
| `XORGDRM hold t=… held=10s/20s/30s`, then `XORGDRM client exited rc=…` | three heartbeats; xclock killed (`rc` 143 or similar) | heartbeats stop: the script (not the server) is stuck — bash on Phoenix, report. |
| `XORGDRM server exited rc=0 socket=gone`, `XORGDRM done`; KMS client-death restore line; HDMI back to the console | `-terminate` ends the server after its last client; teardown restores the display | `server still up after 15s: sending TERM`: `-terminate` path not taken (still a pass for M4a; note it); a fault during exit: `addr2line`. |
| `DRMPHX ioctl …` lines | rate-limited trace; every `rc=0` except the predicted gaps: `DRM_IOCTL_MODE_CURSOR*` not called (SWcursor), possible `DRM_IOCTL_MODE_GETGAMMA`/`SETGAMMA` `rc=-1 errno=38` (stubs), `DRM_IOCTL_PRIME_HANDLE_TO_FD` on `node=render` only if something exports (G4) | any other `rc<0`: the first one names the failing request. |
| `KMSTEST stats … apply_errors=0 … bos=0 exports=0`, `V3DAPING stats … parked=0 … pages_to_kernel=0`, both `quit rc=0` | no leaks after the server exited | `bos>0`: kms leak on client death; `pages_to_kernel>0`: an import went to the pool path. |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | any EL0 fault in Xorg-drm: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/xorg-drm/build-out/Xorg-drm <pc>`. |

**What the cycle decides:** a clock ticking on HDMI with `glamor X acceleration enabled on V3D 4.2` =
**the new lane has an X server** (front-buffer glamor rendering on the GPU into a kms scan-out
buffer, no CPU present). The same with ShadowFB instead of glamor = display path proven, glamor is
the next problem (R1/R2; try an `--opengl` Mesa). A failure before the socket = an integration bug in
Probe/PreInit/ScreenInit — the `DRMPHX` trace and the X log name it.

**Follow-up arms** (separate cycles, same staging): (b) `rpi4-kms-m3p2 -G -C` to free `/dev/kbd0`,
`CLIENT="/bin/xterm -geometry 80x24+100+100"` and `HOLD=60` with keys typed on the USB keyboard
(grades `PHXHID first keyboard event` and characters in xterm on HDMI); (c) `CLIENT=/bin/wmaker` for
the Window Maker desktop on the new lane; (d) a Present/DRI3 client once Mesa's X11 platform is built.

## 11. What remains for M4 (honest estimate)

| Step | Size | Notes |
|---|---|---|
| First Pi cycle (§10) + the fixes it finds | 1–3 cycles, 0.5–2 days | the integration surface is the same as kmscube's plus glamor's GLES paths (R1) |
| Input (arm b), Window Maker desktop (arm c) on Xorg-drm | 1–2 days | phxhid done; `-C` console handover untested |
| Mesa client side: `-Dplatforms=x11`, GLX off → EGL-x11 via DRI3 (libxcb-dri3/present/sync/xfixes from the ports prefix) + a static test client (`eglgears`-style / `gl-x11-window` port) | 2–3 days | build only; blocked at run time by G4 and G16 |
| **G4** render-node export (`V3DA_OP_BO_EXPORT`, `/v3dbuf`, `BO_IMPORT ns=v3dbuf`) | ~120 + ~40 lines server + library, 1–2 days | M3 §4 spec exists |
| **G16** DRI3 fences: process-shared mutex/cond + coherent MAP_SHARED in libphoenix/kernel, *or* an xshmfence backend over the render server's fence page | 2–5 days (libphoenix/kernel) / 1–2 days (backend) | new; decide with the owner (kernel risk) |
| **G7** kms import of a foreign buffer + cross-process implicit sync (`BO_LAST_FENCE`) for Present flips | ~150 lines + library, 2–3 days | M3 §4 G7 spec exists |
| **G12** poll readiness wake-up (E5 §5 item 2), or the user-space socketpair bridge (§8) | kernel: 2–4 days incl. audit; bridge: 0.5 day | decides 60 vs ~40–47 fps for Present clients |
| GL-in-a-window at render rate (`gl-x11-window` vs the old lane's 14.2 fps), desktop gate, ports-framework recipe (`xorg_server_drm` port next to the old one) | 2–3 days | the M4 gate of the research doc §5 |

Total: **≈ 3–4 weeks** of focused work to the research doc's M4 gate (Window Maker desktop +
windowed GL at render rate), of which ~1 week is server/kernel gap work (G4, G7, G16, G12) that
M5 (Vulkan xcb WSI) and M6 (Wayland) need as well. The X server itself is done up to its first cycle.

## Result — `m4a-xorg-drm` (queue22, 2026-09-27 05:37): FAIL, fixed by patch 0008

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-053749-m4a-xorg-drm.log` (read with `grep -a`). Binary
`Xorg-drm-stripped` `9538077f3bdfc6db` (patches 0001–0007). 0 exceptions, 0 faults.

Rows that applied, in order:

- `XORGDRM start …`, `XORGDRM server pid=31`: bash on Phoenix runs the script and backgrounds the server
  (the psh-workaround works).
- `X.Org X Server 1.21.1.24`; the config file parsed (layout, screen, device, both `InputDevice`s,
  every ServerFlags option echoed `(**)`).
- **Builtin-module table proven:** `LoadModule: "modesetting"` → `Module "modesetting": linked into the
  server`, version info checked (`X.Org Video Driver, version 25.2`); same for `"phxhid"`
  (`X.Org XInput driver, version 24.4`).
- `(WW) Falling back to old probe method for modesetting` (no platform bus, as designed) →
  **libdrm-phoenix on Xorg's first call:** `DRMPHX conn fd=5 path=/dev/dri/card0 node=card0 port=24
  client=1 rc=0`, two `DRM_IOCTL_MODE_GETRESOURCES rc=0 … crtcs=1 connectors=1 encoders=1` (Probe's
  `check_outputs`) → `(II) modeset(0): using /dev/dri/card0`.
- **Then** `(EE) Fatal server error: Cannot run in framebuffer mode. Please specify busIDs for all
  framebuffer devices` → `Server terminated with error (1)`. Not predicted: `xf86PostProbe()`'s test is
  constant-TRUE without libpciaccess (patch 0008 row, §3; options §4a).
- The script then waited its full 90 s (`XORGDRM socket=missing wait_s=90`); xclock: `Error: Can't open
  display: :1`; `XORGDRM server exited rc=1`.

Not reached: PreInit (GBM/EGL/glamor), modeset, input, DRI3/Present. Fix: patch 0008, rebuilt
(`b67d33783531e854`), script now stops waiting when the server has died; **`m4b-xorg-drm`
re-registered** in §10 with the same staging and commands.

## Result — `m4b-xorg-drm` (queue24, 2026-09-27 07:02): FAIL, fixed by the compat `ctype.h`

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-070215-m4b-xorg-drm.log` (`grep -a`), X log
`/srv/phoenix-rpi4-nfs-gcc16/var/log/Xorg-drm.1.log`. Binary `b67d33783531e854` (patches 0001–0008).
0 exceptions, 0 faults.

- **Patch 0008 proven:** `(WW) Falling back to old probe method` → `modeset(0): using /dev/dri/card0`,
  no `Cannot run in framebuffer mode`.
- **libdrm-phoenix answered every PreInit call correctly** (the m4b hypotheses about the cap mapping
  and `GETFB` are refuted by the trace): `GET_CAP cap=0x3 value=0x18` (`DRM_CAP_DUMB_PREFERRED_DEPTH`
  = 24, the rpi4-kms value), then drmmode_get_default_bpp's probe `CREATE_DUMB 1x1 bpp=32 handle=1
  pitch=64` → `ADDFB 1x1 bpp=32 depth=24 fb=4096` → `RMFB` → `DESTROY_DUMB`, all `rc=0` — so the
  driver computed depth 24 / bpp 32 (`drmmode_display.c:4260-4295`; `GETFB` is never called).
- Then `Creating default Display subsection in Screen section "screen0" for depth/fbbpp 2/4` →
  `(EE) Given depth (2) is not supported by the driver` → `no screens found`.
- **Cause:** `xf86SetDepthBpp()` (`xf86Helper.c`) takes the depth from the config Screen's
  `DefaultDepth` before the driver's default; fbbpp 4 is its derivation for a depth ≤ 4. So the
  parser stored 2. Its number scanner is `while (isdigit(c = configBuf[configPos++]) || …)`
  (`parser/scan.c:373`), and libphoenix's `isdigit` is a macro that evaluates `c = buf[pos++]` twice:
  for `24\n` the first test reads `4` then `\n` (10 ≤ '9' passes), storing `\n`, and the number
  string becomes `"2\n…"` → `strtoul` = 2. A libphoenix defect (C17 7.1.4), shimmed in
  `compat/include/ctype.h` (§6), full rebuild (the shim is on every compile line of xorg-server,
  libepoxy, libxshmfence).
- The script's early-exit detection worked (ended 2 s after the server died).

Not reached: glamor/GBM/EGL, modeset, input, DRI3/Present. Re-registered as **`m4c-xorg-drm`** (§10).

## Result — `m4c-xorg-drm`

*(to be filled: log path, snapshot paths, the tagged lines, the rows that applied)*

### Result — m4c (queue28, 2026-09-27 07:42–07:48): **PASS — first light: xclock on Xorg-drm with glamor on V3D**

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-074153-m4c-xorg-drm.log`. With the ctype shim, the config
parsed `Depth 24, framebuffer bpp 32`; `glamor: Using OpenGL ES 3.1 context` → **`glamor X acceleration
enabled on V3D 4.2.14.0`** → `glamor initialized`; EDID read from the monitor (HJW 2131, 60×34 cm);
xclock (old-lane client binary, `DISPLAY=:1`) ran for the full 30 s hold; server exited `rc=0`
("Server terminated successfully"), scanout BO import released, 0 exceptions. HDMI
(`artifacts/hdmi/20260927-074503-m4c-xorg-drm-tick.png`): **a black root window with the xclock face** —
exactly the pre-registered PASS picture. glamor renders into the kms scanout BO and presents with
`MODE_DIRTYFB` (front-buffer path; `PageFlip on` did not engage — no flip ioctls in the trace).
⚠ This cycle started early (a queue-guard bug, see the weekly log) while build 12 compiled; it ran on
build 11's image and finished before build 12's image stage — the result is valid for these static binaries.
The ctype shim is now redundant: libphoenix `156422a` (weekend sync) fixes the macros at the source.
Next: Window Maker + input, DRI3/Present clients (G4/G6/G16), page flips.

### Result — m4d (queue29, 2026-09-27 08:52): **Window Maker desktop on Xorg-drm**

`export CLIENT=/bin/wmaker HOLD=150`, same script, `rpi4-kms-gate -G`. glamor enabled; the old lane's
`wmaker` binary (unchanged) came up on `DISPLAY=:1` and held the screen for the whole window: HDMI
(`artifacts/hdmi/20260927-085717-m4d-wmaker-tick.png`, stable over 26 snapshots) shows the Window Maker
workspace clip, the dock with its icons and the software cursor on the default background. 0 exceptions.
Not yet exercised: input (phxhid), windowed clients, DRI3/Present GL clients, page flips.

## M4 part 2 — DRI3/Present GL clients (2026-09-27)

**Goal:** the "windowed GL at render rate" half of the M4 gate — an unmodified Mesa EGL client in an X
window on Xorg-drm, its frames rendered by V3D into DRI3 buffers and put on screen by Present, no CPU
copy. **Status: built, host-tested, one Pi cycle pre-registered (§P2.6); nothing committed or staged;
no server process (rpi4-kms, rpi4-v3d-async) and no libdrm-phoenix change was needed; no libphoenix
branch.** Code: [`tools/gpu-lane/x11-drm/`](../../tools/gpu-lane/x11-drm/) (new),
`tools/gpu-lane/mesa-drm/build.sh --x11` + Mesa patches 0013–0015,
`tools/gpu-lane/xorg-drm/build.sh --xshmfence-prefix`.

### P2.1 The chain, traced (what each step needs, and whether it exists)

Read in Mesa 26.2 (`loader_dri3_helper.c`, `platform_x11_dri3.c`, `x11_dri3.c`, `v3d_resource.c`,
`renderonly.c`) and xorg-server 21.1.24 (`glamor_egl.c`, `dri3/`, `present/`, `miext/sync/misyncshm.c`):

| # | Step | Path on our stack | Status |
|---|---|---|---|
| 1 | `eglInitialize(EGL_PLATFORM_X11)` → `dri3_x11_connect` → `xcb_dri3_open` | glamor's `glamor_dri3_open_client` opens `drmGetDeviceNameFromFd2(card0)` = **`/dev/dri/card0`**, `drmGetMagic` + `drmAuthMagic` (library-local / accepted no-op), the fd crosses the X socket by SCM_RIGHTS (the kernel's `fdpass_pack` takes a file reference in flight; E1). Server: `XTRANS_SEND_FDS 1` in `dix-config.h`, `ReadFdFromClient`/`WriteFdToClient`/`_XSERVTransSendFd`/`_XSERVTransRecvFd` linked in Xorg-drm-m4p2; libxcb in the ports prefix is built with fd passing (`xcb_send_fd`, `recvmsg`) [built: nm] | ✅ today |
| 2 | driver for that fd | `loader_get_user_preferred_fd` keeps it (no `DRI_PRIME`: display = render fd); `drmGetVersion` = `vc4` → `vc4_drm_screen_create` → `VC4_GET_PARAM` `-ENOTTY` → **kmsro** → renderonly v3d on `/dev/dri/renderD128` — exactly kmscube's GBM path. libdrm-phoenix identifies the received descriptor by `sys_fdpath` (kept on the open file, survives SCM_RIGHTS) + an idempotent `HELLO`; rpi4-kms logs once `request pid … != client pid …` (Stage A does not enforce the opener) [read: `kms_main.c:1231`] | ✅ today |
| 3 | back buffer allocation (`dri3_alloc_render_buffer`) | `dmabuf_capable` is **off** in glamor unless the server runs `-debug dmabuf_capable` (`glamor_egl.c:1181`), so `GetSupportedModifiers` answers none, and Mesa calls `dri_create_image_with_modifiers(NULL, 0, SHARE\|SCANOUT\|BACKBUFFER)` → v3d `SCANOUT` → **renderonly**: `CREATE_DUMB` in the kms pool, card0 export `/kmsbuf/<h>`, render `BO_IMPORT` (G1) — linear. (Mesa patch 0012's opt-in is not needed: SCANOUT is set.) | ✅ today |
| 4 | the buffer's fd for X (`__DRI_IMAGE_ATTRIB_FD`) | `v3d_bo_get_dmabuf` = render `PRIME_HANDLE_TO_FD` of an **imported** BO = **G4a** (library, M5 §4.1, proven on hardware by m5b). G4 proper (render-owned export) is **not** on this path | ✅ with libdrm-phoenix ≥ m5 (the client links m5b) |
| 5 | `PixmapFromBuffers` (modifier LINEAR, DRI3 1.2) | server: `dmabuf_capable` off → `glamor_back_pixmap_from_fd` → `gbm_bo_import(GBM_BO_IMPORT_FD)` → render `BO_IMPORT` of the client's `/kmsbuf/<h>` (G1; the render server opens the name itself, any process's export) → EGLImage → texture. Then `renderonly_create_gpu_import_for_resource` tries render export + card0 import: with Xorg's m3p3 library the export fails locally (`-ENOSYS`, stderr `Failed to export gem bo … to dmabuf`); with a G4a library it would reach card0 `PRIME_FD_TO_HANDLE` of a foreign buffer = **G7** `-ENOSYS`. **Both fail soft** (`rsc->scanout = NULL`, no error path) [read: `v3d_resource.c:1007`, `renderonly.c`]: the pixmap works for compositing, not for scan-out | ✅ today (copy path) |
| 6 | `FenceFromFD` (one xshmfence per buffer) | libxshmfence: the pthread backend cannot work across processes (below), and `xshmfence_alloc_shm` ignores the failing init | ❌ **G16** — the one real gap |
| 7 | `PresentPixmap` | windowed → not flippable (not screen-sized) → `present_execute_copy`: glamor copy client pixmap → window (v3d samples the linear buffer through its shadow-tiled copy, as for the root pixmap), `screen->flush` (glFlush → SUBMIT_CL) **before** `present_pixmap_idle` triggers the idle fence + sends IdleNotify [read: `present_execute.c:82-90`]. With `rpi4-v3d-async -m serial` jobs run in global submission order across clients, so the client's render (submitted before its PresentPixmap) finishes before the server's copy reads it, and the server's copy before the client's next render into that buffer. (Pipeline mode would need cross-process implicit sync = `BO_LAST_FENCE`.) | ✅ today (serial mode) |
| 8 | swap interval 1: vblank-timed copy | modesetting `ms_present_queue_vblank` → `CRTC_QUEUE_SEQUENCE` (libdrm-phoenix ✅, M3) → event read on card0 in Xorg's `poll()`. The pollwake kernel (build 11, merged) wakes mixed AF_UNIX + server-fd sets on a notify; rpi4-kms-gate notifies (kmscube 60.00 fps) — so the §8 G12 model (≈ 40–47 fps) should no longer apply | ✅ expected; first exercised here |
| 9 | Present flips of client buffers, DRI3 explicit sync | full-screen windows only; need G7 + `BO_LAST_FENCE`; explicit sync needs xcb ≥ 1.17 (ports: 1.16) and DRI3 1.4 (server 21.1: 1.2) | not needed for a windowed client |

**Minimal gap set for the first DRI3 GL client: G16 plus build glue.** Not G4 (step 3 keeps client
buffers on the display device), not G7 (step 5 fails soft, step 9 not taken), not G6.

### P2.2 G16 — xshmfence on Phoenix: design and the chosen answer

An xshmfence is a word in memory shared by the X server (trigger, reset, query) and one client
(await). The server never awaits [read: `misyncshm.c` uses only trigger/reset/query/map/alloc];
Mesa's client awaits a buffer's idle fence in `dri3_find_back`/`dri3_get_buffer`, normally after the
IdleNotify for that buffer has arrived — i.e. the fence is almost always triggered already.

Two independent problems: (a) **the wait primitive** — no futex in the kernel; libphoenix mutexes and
condition variables are per-process kernel handles, so `pthread_{mutex,cond}attr_setpshared(PROCESS_SHARED)`
can never mean anything (it returns `EINVAL`, `pthread.c:1549`); (b) **the shared memory** — no
`memfd_create`/`shm_open`, and a `/tmp` file is unsafe: the kernel keys a file's pages by the file id,
which dummyfs reuses, so an unlinked fence file can alias a later one while an old mapping lives
(shmsrv's analysis, `weston-drm/shmsrv/shmsrv.c` header).

| Option | Verdict |
|---|---|
| libphoenix process-shared pthread mutex/cond | needs a kernel wait-on-address or a user-space emulation inside libphoenix (spin/sleep) under the POSIX API, for every program; still needs (b). Large, touches every pthread user — rejected |
| kernel futex (wait on a physical address) | the real fix for (a), benefits pshared pthread too; kernel work + audit, owner-gated (ground rule 3) — later, if polling ever shows in a profile |
| message-based fence (a fence server; await = blocking read) | exact wake-up, but one more server and an IPC per trigger/await on every frame — rejected for the first client |
| **polled word in shmsrv memory (chosen)** | (a) trigger/reset = atomic store-release, query = load-acquire, await = spin 64 × `yield`, 16 × `sched_yield`, then `usleep` 50 µs doubling to 1 ms; correct because both mappings are the same cached physical page on one coherent SoC; cost = latency ≤ 1 ms only when a client really waits (rare, see above). (b) the fence object comes from **shmsrv** (`/shm`, the M6 lane's `memfd_create` backing: `memExport`ed MAP_CONTIGUOUS cached pages, ids never reused, pages live while any mapping does). **No libphoenix change** |

Implementation: [`tools/gpu-lane/x11-drm/patches/libxshmfence/0001`](../../tools/gpu-lane/x11-drm/patches/libxshmfence/)
(+ `xshmfence_phoenix.[ch]`, MIT like the library; `xshmfenceint.h` selects it with
`HAVE_PHOENIX_FENCE`; `xshmfence_alloc_shm` asks shmsrv first — devctl CREATE, `open("/shm/<id>")` —
and keeps upstream's `/tmp` file only as a fallback with a one-time stderr warning). `xshmfence_init` is
a no-op: `ftruncate` zero-fills = untriggered, and a shmsrv descriptor has no `write`. Built without
configure (two sources, `-DHAVE_PHOENIX_FENCE=1`), deterministic archive. **Both Xorg-drm and every
DRI3 client must link this archive** (struct layout); `xorg-drm/build.sh` checks the link map
(`xshmfence members: phoenix=21 pthread=0`). `__wrap_mmap` passes `/shm/<id>` descriptors through
unchanged (not a buffer namespace it knows), so the fence page stays cached as exported.

Lifetime, checked against shmsrv's protocol: client `alloc` + `ftruncate` (1 MiB object) + `mmap` →
`FenceFromFD` (xcb closes the client's fd after sending; the in-flight fd holds a file reference,
kernel `fdpass_pack`) → server `mmap` and **keeps** the descriptor for the fence's lifetime
(`miSyncShmCreateFenceFromFd` stores it in `pPriv->fd` [read: `misyncshm.c:124-137`]) → the object
stays live until the fence is destroyed (buffer freed, client gone), then the last close withdraws it
(`destroy` line); the mappings keep the page until unmapped. Cost: shmsrv gives every object **≥ 1 MiB contiguous** (its minimum capacity, chosen for
wl_shm pools) — ~2–4 MiB per window for 2–4 fences. Fine for first light; a small-object capacity
(one page) is a shmsrv follow-up.

**Host test** (`tools/gpu-lane/x11-drm/hosttest/run.sh`, native gcc + ASan/UBSan, the patched
upstream sources, a Linux memfd passed over an AF_UNIX socketpair to forked processes): **8/8 PASS** —
fresh = untriggered (`size=4`), local trigger/await/reset idempotence, cross-process trigger after
20 ms seen by `await` in 23.2 ms, cross-process reset visible, **2000 ping-pong round trips in 5.0 ms
(2.5 µs each)**, three waiter processes held while untriggered and all released within 1.8 ms of one
trigger, the mapping outlives the descriptor.

### P2.3 Builds (new outputs; nothing older replaced)

| What | Command | Output | sha256 (first 16) |
|---|---|---|---|
| Mesa EGL X11 (DRI3/Present), GLES only, libdrm-phoenix **m5b** | `tools/gpu-lane/mesa-drm/build.sh --x11 --libdrm-prefix tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix --x11-xshmfence tools/gpu-lane/x11-drm/build-out/xshmfence-prefix` | `mesa-drm/build-out-x11/` (+ `x11-link.txt`; patch set `7373c40f97bb3f64` = 0001–0015) | — |
| libxshmfence, Phoenix backend | `tools/gpu-lane/x11-drm/build.sh [--xshmfence-only]` | `x11-drm/build-out/xshmfence-prefix/lib/libxshmfence.a` | `4049c5b0435f7ad8` |
| shmsrv (weston-drm's source, unchanged, compiled here) | same | `x11-drm/build-out/shmsrv-stripped` (124 KB) | `6a89f2610a5ad80d` |
| **eglx11-demo** (static, `--wrap=mmap --wrap=ioctl`) | same | `x11-drm/build-out/eglx11-demo-stripped` (16.3 MB; unstripped `eglx11-demo` for addr2line) | `f7a5bb3825530032` / `7030cee17ed6392b` |
| **Xorg-drm-m4p2** = m4c/m4d's Xorg-drm with only libxshmfence changed | `tools/gpu-lane/xorg-drm/build.sh --out tools/gpu-lane/xorg-drm/build-out-m4p2 --mesa-out tools/gpu-lane/xorg-drm/build-out/mesa --xshmfence-prefix tools/gpu-lane/x11-drm/build-out/xshmfence-prefix` | `xorg-drm/build-out-m4p2/Xorg-drm-stripped` (20.8 MB) | `8cefbd033f4a8796` / unstripped `b3b55047e34cf0d1` |

**Mesa `--x11`** (`mesa-drm/build.sh`, additive next to `--wayland`, own dir `build-out-x11`, marker
`mesa-x11.txt`): `-Dplatforms=x11 -Dglx=disabled -Dxlib-lease=disabled`, GLES only. The X11/xcb
dependencies come through a **private prefix** (`<out>/x11-prefix`: only `X11/` and `xcb/` headers,
the ports' `.pc` files with includedir moved — the ports `include/` also holds the old lane's `GL/`).
Found: xcb 1.16, xcb-randr, xcb-keysyms, x11-xcb 1.8.7, xcb-dri3/present/shm/sync/xfixes 1.16 (so
**no** DRI3 explicit sync — `HAVE_DRI3_EXPLICIT_SYNC` needs 1.17), xshmfence. Summary: EGL platforms
`x11 surfaceless drm xcb`. Link list `x11-link.txt` (gallium whole-archive, EGL/GBM/GLES/v3d/…,
X11-xcb, X11, xcb-dri3/present/sync/xfixes/randr/shm/render/shape/keysyms, xcb, Xau, Xdmcp,
xshmfence, libdrm, compat, z).

**Mesa patches** (`mesa-drm/patches/mesa/`, all three only reachable with the X11 platform; they
change the patch-set stamp, so other mesa-drm build dirs re-apply and rebuild once, with no
behavioural change):

| # | Patch | Why |
|---|---|---|
| 0013 | `egl/x11: build without SysV shared memory (HAVE_SYS_SHM_H)` | `platform_x11.c` included `<sys/ipc.h>` unconditionally and called the xshm probe that `x11_display.c` already compiles only with `HAVE_SYS_SHM_H`; Phoenix has no SysV IPC. Only the swrast MIT-SHM path is affected (not built here) |
| 0014 | `gbm/dri: add the xcb include paths when the X11 platform is built` | `gbm_dri.c` → `loader_dri_helper_screen.h` includes `<xcb/*.h>` under `HAVE_X11_PLATFORM`, but `dri_gbm` has no xcb dependency; upstream builds only because xcb sits in `/usr/include`. Header-only `partial_dependency` |
| 0015 | `meson: no dril_dri shared stub on Phoenix-RTOS` | the X11 platform always builds `targets/dril` (the `<driver>_dri.so` AIGLX loads with `dlopen`) as a `shared_library`, which cannot link against non-PIC libphoenix; no dynamic loader exists to use it |

The build also still carries the M6 lane's **untracked** patch 0012 (opt-in, env-gated, off by default).

**eglx11-demo** (`x11-drm/src/eglx11_demo.c`): Xlib window 640×480+640+300 (the old lane's 14.2 fps
GL-in-X measurement size), `eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR)`, GLES2 context, a rotating
colour-cycling hexagon fan over a moving background, `eglSwapInterval`, fps every 2 s with swap and
frame maxima, the first frame's centre pixel read back before the first swap. Refuses to start without
shmsrv (`XDEMO shmsrv=missing`, rc 3; `-f` overrides); sets `EGL_LOG_LEVEL=debug` for itself only
(`XDEMO_EGL_DEBUG=0` silences); knobs from the environment because psh cannot quote a command line
into bash's `CLIENT=` (`XDEMO_SECS`, `XDEMO_FRAMES`, `XDEMO_INTERVAL`, `XDEMO_GEOM`). SIGTERM ends it
after the current frame; a stuck swap is ended by `alarm(5)`, so the script's `kill; wait` never hangs.

Verification [built]: eglx11-demo `nm -u` = 0; `dri2_initialize_x11`, `dri3_x11_connect`,
`x11_dri3_open`, `loader_dri3_swap_buffers_msc`, `xcb_dri3_open`, `xcb_dri3_pixmap_from_buffers`,
`xcb_present_pixmap`, `xshmfence_phoenix_alloc_shm`, `kmsro_drm_screen_create`,
`v3d_drm_screen_create_renderonly`, `__wrap_mmap`, `__wrap_ioctl`; strings `/dev/dri/card0`,
`/dev/dri/renderD128`, `/kmsbuf`, `/shm`, `EGL_KHR_platform_x11`, `EGL_EXT_platform_xcb`; no
old-lane strings; no pthread xshmfence member in the map. Xorg-drm-m4p2: `nm -u` = 0, every §7 check
as before, `xshmfence members: phoenix=21 pthread=0`. Host tests: `x11-drm/hosttest/run.sh` 8/8 PASS;
`DRMPHX_OUT=tools/gpu-lane/libdrm-phoenix/build-out-m5b tools/gpu-lane/libdrm-phoenix/hosttest/run.sh`
= logic **134/0 PASS**, e2e legacy + dri **PASS** (only the four fake-GPU pixel checks fail, as
designed; `prime_export_render` = the G4 gap) — the library the client links is unchanged.

### P2.4 What closed, what remains

| Gap | State after part 2 |
|---|---|
| **G16** DRI3 fences | ✅ closed for Phoenix (libxshmfence backend + shmsrv), host-tested; hardware = §P2.6 |
| Mesa client platform | ✅ `--x11` build + 3 patches + demo client |
| G4 render-node export (implemented 2026-09-27, pending Pi `m6g-g4`, [M6 §15](M6-wayland.md)) | not needed for DRI3 while `dmabuf_capable` is off; needed for UIF (tiled) client buffers = no shadow-tiling of each frame in the server (`-debug dmabuf_capable` + modifiers), for Wayland dmabuf of render-allocated buffers, and v3dv external memory |
| G7 kms import of a foreign buffer + `BO_LAST_FENCE` | needed for Present **flips** of full-screen client windows (today: copy fallback) and for pipeline-mode render servers (cross-process implicit sync) |
| G6 cross-process syncobj / sync files | DRI3 1.4 explicit sync (xcb ≥ 1.17, a newer server), Vulkan xcb WSI (M5 second half) |
| shmsrv | lives in `tools/gpu-lane/weston-drm/shmsrv/` (M6, untracked at the time of writing); x11-drm compiles the same source and includes `shm_proto.h` from there. **Recommend promoting it to a shared `tools/gpu-lane/shmsrv/`** and adding a one-page minimum capacity for small objects |
| Present timing | first measured by §P2.6 (pollwake should give 60 Hz; the §8 G12 model predates build 11) |

### P2.5 Risks only the Pi can show

| # | Risk | Where it shows |
|---|---|---|
| Q1 | the SCM_RIGHTS-received card0 fd: libdrm-phoenix identification or rpi4-kms per-client state behaves differently for a descriptor opened by another process | client `DRMPHX conn … node=card0 … rc<0`, or `CREATE_DUMB rc=-1` in the client |
| Q2 | shmsrv object lifetime vs the server's `mmap` (withdrawn before the server maps) | `xcb_dri3_fence_from_fd` error from the client's Mesa; `SHMSRV destroy id=N` while the demo still runs |
| Q3 | Present vblank path in modesetting (`CRTC_QUEUE_SEQUENCE` events through Xorg's poll) never exercised | interval-1 arm stalls at the first swap while the interval-0 arm runs |
| Q4 | glamor sampling a linear foreign buffer (shadow tiling each frame) costs more than expected | interval-0 fps low with high `swap_avg_ms` |
| Q5 | kms pool: Xorg's front (7.9 MiB) + 2–4 client buffers (≈1.2 MiB each at 640×480) | `CREATE_DUMB … rc=-1 errno=12/28` → rpi4-kms `-p 48` |
| Q6 | teardown: the client's kms dumb BOs die with its fd while Xorg's imports still map them (E1 keeps pages) | faults or `V3DA srv import released` errors after `XDEMO done` |

### P2.6 Pre-registered Pi cycle `m4p2a-eglx11` (one netboot cycle, two arms)

**Question:** does an unmodified Mesa EGL-X11 client render with V3D into DRI3 buffers inside Xorg-drm
and appear in its window through Present — with vsync (interval 1) and at render rate (interval 0) —
with the Phoenix xshmfence backend synchronising client and server?

**Preconditions:** the m4d setup (netboot image ≥ build 11 with the pollwake kernel; `rpi4-v3d-async-m3p2`,
`rpi4-kms-gate`, `kmstest-poll`, `v3dasync-ping`, `/bin/xorg-drm-m4a.sh`, `/etc/X11/xorg-drm.conf`, bash
already on the export); no other GPU client.

**Stage (coordinator; `EXPORT=/srv/phoenix-rpi4-nfs-gcc16`, `sudo install -m 755`, `cmp` afterwards):**

| Source | Export path |
|---|---|
| `tools/gpu-lane/xorg-drm/build-out-m4p2/Xorg-drm-stripped` (`8cefbd03…`) | `$EXPORT/bin/Xorg-drm-m4p2` (the m4c/m4d `/bin/Xorg-drm` stays) |
| `tools/gpu-lane/x11-drm/build-out/eglx11-demo-stripped` (`f7a5bb38…`) | `$EXPORT/bin/eglx11-demo` |
| `tools/gpu-lane/x11-drm/build-out/shmsrv-stripped` (`6a89f261…`) | `$EXPORT/bin/shmsrv-m4p2` (does not replace the M6 lane's `/bin/shmsrv`; only one may run per cycle — both serve `/shm`) |

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m4p2a-eglx11 --idle-secs 45 --max-cmd-secs 300 \
    --hdmi-dense-on 'XORGDRM client start' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" "/bin/rpi4-kms-gate -G" "/bin/shmsrv-m4p2 -v" \
    "export XSRV=/bin/Xorg-drm-m4p2" "export CLIENT=/bin/eglx11-demo" "export HOLD=60" \
    "export XDEMO_SECS=45" "export XDEMO_INTERVAL=1" \
    "/bin/bash /bin/xorg-drm-m4a.sh" \
    "export XDEMO_INTERVAL=0" "export DRMPHX_TRACE=0" \
    "/bin/bash /bin/xorg-drm-m4a.sh" \
    "/bin/shmsrv-m4p2 -s" "/bin/kmstest-poll stats" "/bin/v3dasync-ping stats"
```

`-m serial` is required (§P2.1 step 7). Arm a keeps `DRMPHX_TRACE` at the script's default 1 (server
and client, rate-limited); arm b turns it off for a clean rate. The demo ends itself after 45 s
(`XDEMO done … stop=limit`), inside the script's 60 s hold; `-terminate` then ends the server. Wall
clock ≈ netboot 60–150 s + 3 × ~5 s + 2 × ~90 s + 3 × ~5 s ≈ 5–6.5 min.

Grade:

```
grep -a -E '^(XDEMO|XORGDRM|SHMSRV|KMS|V3DA|KMSTEST|V3DAPING) |DRMPHX .*(CREATE_DUMB|PRIME_|conn )|libEGL|DRI3|xcb_dri3|xshmfence|Failed to export|\((EE|WW)\)|glamor X|Fatal|Exception #' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m4p2a-eglx11.log
./scripts/uart-summary.sh m4p2a-eglx11
```

**HDMI rule:** only snapshots taken after an arm's `XORGDRM client start` count (dense from there);
earlier snapshots show the console or the bare root.

**Predictions** (per arm, in order; ~1.3 % UART line corruption — re-read, don't count; EL0 dumps print twice):

| Line / observation | Predicted | If instead… |
|---|---|---|
| `SHMSRV srv ready ns=/shm port=… proto=1 …`, `SHMSRV srv detached pid=…` | once | `srv FAIL /shm is served already`: another shmsrv runs — fine if it is the same protocol, note it |
| m4c/m4d server rows: `glamor X acceleration enabled on V3D 4.2.14.0`, `Initializing extension DRI3` / `Present`, `XORGDRM socket=up wait_s=<1–20>` | as m4d | anything else: the m4c table (§10) applies — this binary differs from m4d's only in libxshmfence |
| `XDEMO start … geom=640x480+640+300 interval=1 …`, `XDEMO shmsrv=up`, `XDEMO x11 display=:1 screen=0 depth=24 vendor="The X.Org Foundation" …` | once | `shmsrv=missing` + `done rc=3`: shmsrv not staged/started; `reason=XOpenDisplay`: the script's DISPLAY/socket |
| client `DRMPHX conn … path=/dev/dri/card0 node=card0 … rc=0` (the DRI3-passed fd), one `KMS srv note: request pid <client> != client pid <Xorg>` (logged once), then `DRMPHX conn … node=render` (kmsro opens renderD128) | DRI3Open + kmsro, as kmscube | EGL debug `DRI3 error: Could not get DRI3 device` / `No driver found`: step 1–2 (Xorg `Failed to initialize DRI3`, the `conn` line's rc) — Q1 |
| `XDEMO egl version=1.5 vendor="Mesa Project" driver=vc4 apis="OpenGL_ES"`, `XDEMO window id=0x… visual=0x… depth=24`, `XDEMO gl renderer="V3D 4.2.14.0" version="OpenGL ES 3.1 Mesa 26.2.0"` | the DRI3 path on kmsro (the dri driver of the card0 fd is vc4; rendering is v3d) | `eglInitialize err=0x3001`: read the `libEGL` debug lines; any `swrast`/`kopper` line: DRI3 was refused |
| per back buffer (2–4; arm a traced): client `MODE_CREATE_DUMB rc=0` (≈1.2 MB) → `PRIME_HANDLE_TO_FD node=card0 rc=0 … fdpath=/kmsbuf/<h>` → `PRIME_FD_TO_HANDLE node=render rc=0` + `V3DA srv import … ns=kmsbuf id=<h>` → **`PRIME_HANDLE_TO_FD node=render rc=0 … fdpath=/kmsbuf/<h>`** (G4a); `SHMSRV create id=N pid=<client>`, `SHMSRV truncate id=N size=4 exported=4096 cap=1048576`; server: `PRIME_FD_TO_HANDLE node=render rc=0 … fdpath=/kmsbuf/<h>` + a second `V3DA srv import … id=<h>` (another client), `PRIME_HANDLE_TO_FD node=render rc=-1 errno=38` + stderr `Failed to export gem bo … to dmabuf` (m3p3 library, soft, §P2.1 step 5); no `SHMSRV destroy` yet: Xorg keeps each fence's descriptor for the fence's lifetime, so the `destroy id=N size=4 cap=1048576` lines come when the demo exits (all of an arm's ids) | steps 3–6 | client `CREATE_BO` on render instead of `CREATE_DUMB`: tiled/non-scanout allocation (modifiers offered: was `-debug dmabuf_capable` set?) — export then fails (G4); client `PRIME_HANDLE_TO_FD node=render rc=-1 errno=38`: a pre-M5 library (BUILD-INFO); Mesa `xcb_dri3_pixmap_from_buffer[s] failed`: the server import (its `PRIME_FD_TO_HANDLE` rc, `V3DA srv import FAIL`); `xcb_dri3_fence_from_fd failed`: the server's `mmap` of the `/shm` descriptor failed (Q2), or the server lacks the Phoenix backend (a pre-m4p2 Xorg-drm); a `SHMSRV destroy` **while the demo still runs** = a fence withdrawn early (Q2) |
| `XDEMO first_frame centre_rgba≈153,153,153,255 glerr=0x0` (white centre × 0.6 at t≈0, ±3) | the client renders | `0,0,0,…` or the background colour: nothing drawn into the back buffer |
| `XDEMO first_swap ok t_ms=<…>` within a few seconds | Present + idle fences work | no `first_swap`/`fps` lines in arm a but fine in arm b: Q3 (vblank event path); stuck in both: idle fence never seen triggered (xshmfence coherence) or IdleNotify missing — the client is killed by `alarm` 5 s after the script's TERM (`XORGDRM client exited rc=142`) |
| arm a `XDEMO fps=` lines every 2 s: **55–60** (vsync; pollwake wakes Xorg's mixed poll set) | 60 Hz Present | **40–47**: Xorg still sees vblank events on a 20 ms quantum (G12 model §8: pollwake not reaching the card fd in Xorg's set); **≤ 30**: per-frame cost > 16.7 ms (server copy incl. shadow tiling, Q4) |
| arm b `XDEMO fps=`: **> 60**, well above the old lane's 14.2 fps at this size | render rate through DRI3/Present (the M4 gate) | ≤ 60 with `swap_avg_ms` ≈ frame time: the per-frame X/Present round trip or the server copy dominates — report `swap_avg_ms`, `frame_max_ms` |
| `XDEMO done rc=0 frames=… secs=45.0 fps=… stop=limit` per arm; `XORGDRM client exited rc=0`; `XORGDRM server exited rc=0 socket=gone` | clean exits | `stop=signal` + rc 143: the demo did not end itself (hung swap) |
| HDMI after `XORGDRM client start`: black root, a 640×480 window at (640,300) with a rainbow hexagon (pale centre) turning over a dark blue/purple background — hexagon angle and colours differ between consecutive snapshots | frames reach the screen | window black while `fps` lines advance: the server copies something else (import of the wrong buffer / tiling: compare the client's `fdpath=/kmsbuf/<h>` with the server's import ids); garbage: a tiled buffer read as linear; torn/partial frames: cross-process ordering (was `-m serial` used?) |
| `SHMSRV stats rc=0 live=0 bytes=0 ids=<2–8>`, `KMSTEST stats … apply_errors=0`, `V3DAPING stats … parked=0` | no leaks after both servers' clients left | `live>0`: a fence descriptor leaked; `bos>0`: kms leak on client death (Q6) |
| faults (`uart-summary.sh`) | 0 kernel, 0 EL0 | EL0 in the demo: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/x11-drm/build-out/eglx11-demo <pc>`; in Xorg: `…/xorg-drm/build-out-m4p2/Xorg-drm` |

**What the cycle decides:** frames on HDMI with arm b above 60 fps = **GL in an X window at render
rate on the new lane** (the M4 gate's second half; the old lane: 14.2 fps). Arm a at 60 = Present
timing is right after pollwake. A failure names its step in §P2.1.

**Follow-ups** (separate cycles): (c) the same client full-screen (`XDEMO_GEOM=1920x1080+0+0`) to
grade the flip attempt and its copy fallback (G7); (d) under Window Maker (`CLIENT=/bin/wmaker`
started first, the demo from a second script — needs a two-client script); (e) Xorg-drm with
`-debug dmabuf_capable` once G4 exists (UIF client buffers, no shadow tiling).

### Result — m4p2a (queue33, 2026-09-27 10:37–10:49): **PASS — windowed GL in X at render rate**

`eglx11-demo` (EGL on X11 via DRI3/Present, Mesa-DRM `--x11`) in a 640×480 window on `Xorg-drm-m4p2`
(glamor), `rpi4-v3d-async -m serial`, `rpi4-kms-gate -G`, shmsrv for the xshmfence pages; 0 exceptions.
- **Run 1, swap interval 1: 60.00 fps** in every 2 s window after start-up (swap_avg 16.32 ms, max
  17.45 ms); 57.8 fps over the whole 45 s including start-up. Vsync-locked — the pollNotify kernel +
  rpi4-kms-gate deliver Present's vblank on time.
- **Run 2, swap interval 0: 484.7 fps** over 45 s (21 811 frames, 2 s windows 483–488, swap_avg 1.70 ms,
  frame_max ≤ 4.6 ms) — **the M4 "windowed GL at render rate" gate**. The old lane's GL-in-X path
  (KNOWN-ISSUES G1: pixels GPU → CPU → socket → CPU → GPU every frame) measured ~14 fps at this size.
- HDMI (`artifacts/hdmi/20260927-104552-m4p2a-eglx11-tick.png`): the rotating shaded hexagon in the
  window on the black root.
The Phoenix xshmfence backend (G16) worked on first contact; the pre-registered soft `Failed to export
gem bo` / G7 path is the copy path Present used.

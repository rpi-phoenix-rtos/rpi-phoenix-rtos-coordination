# M4 preparation — `Xorg-drm`: X.Org + modesetting + glamor on the new lane

Milestone M4 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §4.7 and §5
(M4: "Xorg + modesetting + glamor + DRI3/Present + xf86 input driver"). Builds on
[M3](M3-libdrm-phoenix.md) (libdrm-phoenix, the `/dev/dri` nodes, Mesa-DRM GBM/EGL/GLES),
[M2](M2-kms-server.md) (`rpi4-kms`), [M1](M1-async-render-server.md) (`rpi4-v3d-async`) and
[E5](E5-deferred-reply.md) (IPC and `poll()` costs).

**Status (2026-09-27): builds and links; no Pi cycle yet.** `Xorg-drm` — the unmodified hw/xfree86 X
server of xorg-server **21.1.24** (the old lane's version and tarball) with the **modesetting** DDX,
**glamor** on GBM/EGL, **DRI2, DRI3, Present, Xv**, and a new xf86 input driver **`phxhid`** for
`/dev/kbd0` + `/dev/mouse0` — is one static aarch64-phoenix program: **0 undefined symbols**, text
20.3 MB, **20.8 MB stripped**. Every module Xorg would `dlopen()` is linked in and found through a
builtin-module table. Nothing committed, nothing staged; no server, no old-lane file, no sibling repo
touched. The first Pi cycle is pre-registered in §10. Code:
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
| Device discovery | No udev, no libpciaccess: modesetting's legacy `Probe()` opens `Option "kmsdev" "/dev/dri/card0"` (patch 0006 makes the driver build without libpciaccess). Configuration from `-config /etc/X11/xorg-drm.conf`. |
| Input | **`phxhid`**, a 400-line xf86 input driver (new file): usbkbd raw 8-byte boot reports diffed into evdev keycodes + 8, usbmouse 4-byte reports into relative motion/buttons/wheel; drained by a 10 ms server timer (not `poll()` readiness — G12). Keymap: a precompiled evdev/pc105/us `.xkm` compiled into the server (patch 0003; Phoenix has no xkbcomp). |
| Threads | `-Dinput_thread=false`: everything on the main thread (phxhid's timer, DRM events, clients). |
| DRI3 / Present | **Built and initialised.** DRI3 open works today; client buffers need G4 (and G16, new: process-shared fences for xshmfence); Present flips of client buffers need G7 + cross-process implicit sync; Present timing rides G12 (§8). |
| GLX | Off (`-Dglx=false`; glamor_glx.c dropped by patch 0007). |
| libphoenix gaps | Three constants in `compat/include/` (`SI_USER`, `O_NOFOLLOW`, `RTLD_DEFAULT`), one os-support header block (patch 0005), `timingsafe_memcmp`/`reallocarray` from the server's own `libxlibc` fallbacks; **no link-time stand-in needed** (§6). |

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
| `patches/xorg-server/0001…0007` | `git format-patch` series over 21.1.24 (§3) |
| `patches/libepoxy/0001` | static-EGL dispatch (§3) |
| `src/xorg_drm_builtin.c` | the builtin-module table (4 modules, 22 symbols) |
| `src/phxhid.c`, `src/phxhid_evdev_map.h` | the input driver; the HID→evdev table is the old kdrive server's (FreeBSD `evdev_usb_scancodes[]`, BSD-2 notice kept) |
| `compat/include/{signal,fcntl,dlfcn}.h`, `compat/xorg_drm_compat.c` | libphoenix-gap shim (§6) |
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
| `size` | text ≈ 20.29 MB, data 545 748, bss 541 496; file 108 064 536 B, **stripped 20 844 624 B** (old lane: `Xphoenix` 6.1 MB fbdev-only, `Xphoenix-glamor-daemon` 28.1 MB; kmscube 16.5 MB) |
| sha256 (first 16) | `Xorg-drm` `8c814332ed565cf6`, `Xorg-drm-stripped` `9538077f3bdfc6db` (a `--relink` reproduces the same bytes) |
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
| **DRI3 fences** (`FenceFromFD`, Mesa's loader_dri3 idle tracking) | xshmfence's pthread backend fails at `pthread_condattr_setpshared(PROCESS_SHARED)`; cross-process coherence of MAP_SHARED `/tmp` files is also unproven | **G16 (new)**: process-shared memory + pshared mutex/cond in libphoenix, or an xshmfence backend over a server (e.g. the render server's fence page / syncobjs = G6) |
| DRI3 explicit sync (syncobj fds) | not in 21.1's DRI3 1.2 | G6 (for 24.x DRI3 1.4 / Present explicit sync) |
| **Present — window copies at vblank** | work; timing = G12 below | G12 |
| **Present — flips of client buffers** | refused: a client pixmap must become a KMS fb on card0; for a buffer another process allocated that is `KMS_OP_PRIME_IMPORT` (G7), and the flip must wait for the client's GPU work (cross-process implicit sync, `BO_LAST_FENCE`, M3p2 G13 note) → falls back to a glamor copy | **G7** + `BO_LAST_FENCE` |
| Page flips of Xorg's own buffers | in-process G13 covers them (imports recorded in this process) | — |
| Mesa GLX/EGL-x11 clients | no client-side Mesa X11 platform is built (`-Dplatforms=`) | M4 work: Mesa `-Dplatforms=x11` (+ libxcb-dri3/present/xfixes/sync, all in the ports prefix) → then G4, G16 |

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

## 10. Pre-registered Pi cycle `m4a-xorg-drm` (one netboot cycle)

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
./scripts/test-cycle-psh-interact.sh --label m4a-xorg-drm --idle-secs 45 --max-cmd-secs 240 \
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
    artifacts/rpi4b-uart/rpi4b-uart-*-m4a-xorg-drm.log
./scripts/uart-summary.sh m4a-xorg-drm
cat $EXPORT/var/log/Xorg-drm.1.log        # the full X log (verbosity 3), after the cycle
```

Allow ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice.

**Predictions** (in boot order) and what each alternative means:

| Line / observation | Predicted | If instead… |
|---|---|---|
| `V3DA srv …` / `KMS srv …` ready lines incl. `dri name=/dev/dri/… registered=1` | as in m3p2 | a server missing: staging/boot — stop. |
| `XORGDRM start …`, `XORGDRM server pid=…` | once | `bash: … not found`: bash not staged / psh quoting. |
| `X.Org X Server 1.21.1.24`, builder `Phoenix-RTOS new GPU lane (Xorg-drm)`; `Using config file: "/etc/X11/xorg-drm.conf"` | first lines of the server | `(EE) Unable to locate/open config file`: staging; a lock-file/`/tmp/.X11-unix` error: R7. |
| `(II) Module "modesetting": linked into the server`, later `"glamoregl"`, `"phxhid"` (and no `Failed to load module`) | builtin table works | `couldn't open module modesetting`: table not consulted (patch 0002 missing) — stale build. |
| `DRMPHX conn fd=… path=/dev/dri/card0 node=card0 …`; `(II) modeset(0): … /dev/dri/card0`; `Output HDMI-1 connected`, mode `1920x1080` | Probe/PreInit through libdrm-phoenix | `(EE) No devices detected` / `no screens found`: Probe's `check_outputs` failed — read the `DRMPHX ioctl … name=DRM_IOCTL_MODE_GETRESOURCES rc=` line. |
| `KMS srv fstat answered …`, `V3DA srv fstat answered …` (if not already answered), `DRMPHX conn … node=render` | GBM/kmsro pairing (kmscube's step 2) | `couldn't get display device`: `gbm_create_device` NULL — as kmscube's failure table (M3p3). |
| `glamor: Using OpenGL ES 3.1 context` (3.0/2.0 possible: glamor asks for ES 2, Mesa returns its highest compatible ES; the desktop-GL attempt fails silently first) and **`glamor X acceleration enabled on V3D 4.2`** | glamor up on the GPU | `EGL_KHR_surfaceless_context required` / `GL_… required` / `Failed to create GL or GLES2 contexts`: R1 — then the server falls back to ShadowFB (dumb buffer, CPU `shadow` module) and the cycle still grades the display half: expect `glamor initialization failed` + `ShadowFB: …` and a CPU-drawn screen. |
| `V3DA srv import handle=… ns=kmsbuf id=… pages=2026… contiguous=1 …` once or twice, `KMS srv kmsbuf atSize …` | the front buffer (and GBM's) imported on the render node (G1/G3) | `Failed to get v3d handle for dmabuf` / `Couldn't get size of dmabuf fd`: G1/G3 as kmscube's rows; `CREATE_DUMB failed`: R3 (`-p 48`). |
| `(II) Initializing extension DRI3`, `… Present`, `… XVideo`, `… RANDR`, `… Composite`, `… RENDER`; **no** `Failed to initialize DRI3` | DRI3/Present initialised (§8) | `Failed to initialize DRI3`: `drmGetDeviceNameFromFd2(card0)` returned NULL (libdrm-phoenix identity) — M3 §2.9. |
| `XKB: using the builtin keymap (evdev/pc105/us): /tmp/server-1.xkm` | once per keyboard device (core + phxhid) | `builtin keymap unusable`: xkm version mismatch — core keyboard would then abort (`Failed to activate virtual core keyboard`). |
| `PHXHID dev=/dev/mouse0 type=mouse open=ok …` or `open=<error>` (no mouse attached); `PHXHID dev=/dev/kbd0 type=keyboard open=…` | **both outcomes are acceptable**: without `-C` the console may hold `/dev/kbd0` (R5) | a crash in phxhid: `addr2line` the PC. |
| `XORGDRM socket=up wait_s=<1–20>` | within ~20 s (static 20.8 MB binary from NFS ≈ 1 s; Mesa screen + glamor init a few s) | `socket=missing`: the server died or hangs before `CreateWellKnownSockets` — the log above says where. |
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

## Result

*(to be filled after `m4a-xorg-drm`: log path, snapshot paths, the tagged lines, the rows that applied)*

# M6 preparation — `weston-drm`: Weston 14 (Wayland) on the new lane

Milestone M6 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §5 (M6:
"Wayland — Weston DRM backend"). Builds on [M3](M3-libdrm-phoenix.md) (libdrm-phoenix, `/dev/dri`
nodes, the static Mesa GBM/EGL/GLES, the `--wrap=mmap/ioctl` rules), [M4](M4-xorg-modesetting.md)
(builtin-module table, `phxhid` input, baked keymap, the libphoenix-gap shim pattern),
[M5](M5-vulkan.md) (the sync-file `ioctl` interposer, G4a/G17), [E1](E1-vm-object-export.md)
(`memExport`, fd per buffer across AF_UNIX) and [poll-wake](poll-wake.md) (`pollNotify`,
`rpi4-kms-gate`).

**Status (2026-09-27): code complete, builds, statically verified; no Pi cycle yet.** The whole
Wayland stack — libwayland 1.24.0 (server, client, egl, cursor), wayland-protocols 1.45,
libxkbcommon 1.7.0, libdisplay-info 0.2.0, libseat 0.9.1 (noop backend) — and **Weston 14.0.2 with
only the DRM backend, the GL (and pixman) renderer and the kiosk shell** cross-build static for
aarch64-phoenix; `weston`, `weston-simple-shm`, `weston-simple-egl` (GLES on the new Mesa
`--wayland` EGL platform) and a new `/shm` server `shmsrv` link with **0 undefined symbols**, carry
the libdrm-phoenix and weston tags, and contain **no old-lane string**. Two Pi cycles are
pre-registered in §9. Nothing committed, nothing staged, no server, old-lane file or sibling repo
touched. Code: [`tools/gpu-lane/weston-drm/`](../../tools/gpu-lane/weston-drm/); one opt-in patch
added to [`tools/gpu-lane/mesa-drm/`](../../tools/gpu-lane/mesa-drm/) (§4.4, read §12).

Evidence tags: **[read]** = read in source at the cited place; **[built]** = the cross build / link
shows it; **[inferred]** = reasoning, not verified.

---

## 0. Decisions at a glance

| Question | Decision |
|---|---|
| Versions | libwayland **1.24.0** (= the host `wayland-scanner`, which the build requires to match), wayland-protocols 1.45, libxkbcommon 1.7.0, libdisplay-info 0.2.0, seatd/libseat 0.9.1, **Weston 14.0.2**, libinput 1.26.2 (header only). All MIT. Release tarballs, sha256-pinned in `build.sh`. pixman 0.42.2, libffi, expat, zlib from the ports prefix through private per-library views (header poisoning, as mesa-drm does for zlib). |
| Weston configuration | `-Dbackend-drm=true`, every other backend off, `-Drenderer-gl=true` (the pixman renderer is always built in), `-Dshell-kiosk=true`, desktop/ivi/fullscreen shells off, no Xwayland, no systemd, no lcms, no remoting/pipewire, `-Dsimple-clients=shm,egl`, demo clients off. Kiosk shell because it needs **no helper client** (desktop shell needs `weston-desktop-shell`, a cairo toytoolkit client). |
| Event loop (epoll/timerfd/signalfd/eventfd — libphoenix has none) | A poll()-based emulation in `compat/` (`wlphx_epoll.c`, 756 lines), the role epoll-shim plays for libwayland on FreeBSD. libwayland stays unpatched here; programs link `-Wl,--wrap=close -Wl,--wrap=write`. §5.1 |
| `memfd_create` / wl_shm | **New `/shm` server `shmsrv`** (memExport-based) + a 60-line `memfd_create()` in compat. A file on the RAM `/tmp` does **not** work reliably (§6). Weston's and libwayland-cursor's `HAVE_MEMFD_CREATE` paths are taken (their configure probes see the compat archive). |
| Input without libinput/udev | **libinput-phoenix**: the libinput API (upstream `libinput.h`) implemented over `/dev/kbd0` + `/dev/mouse0` (phxhid's report handling, a reader thread, socketpair wake-up); **libudev shim** with a fixed device table (`/dev/dri/card0`); `libevdev_event_code_from_name` shim. Display-only is one argument away (`noinput`). §7.1 |
| Seat (no logind/seatd) | Real **libseat with only its noop backend** (opens devices with `open()`), `LIBSEAT_BACKEND=noop`. 4 small seatd patches. §7.2 |
| Modules without `dlopen` | **Builtin-module table** (weston patch 0001, the M4 pattern): `weston_load_module()` consults a weak `weston_builtin_modules[]` before `dlopen()`; the modules build as archives (`library()` + `-Ddefault_library=static`, patch 0002). §7.3 |
| Keymap without xkeyboard-config | evdev/pc105/us **compiled on the build host** (a native xkbcommon's `xkbcli-compile-keymap` against the host's xkeyboard-config) and embedded; weston patch 0003 falls back to it when the rule names fail. §7.4 |
| GL client platform | mesa-drm gains **`--wayland`** (own build dir `build-out-wayland/`, `-Dplatforms=wayland`, GLES only). Mesa needed **no** source patch to build it. |
| Client GPU buffers (render-node export = G4) | Opt-in **mesa-drm patch 0012** (`V3D_PHOENIX_SHARED_SCANOUT=1`): shared (dma-buf) resources allocated through renderonly on the display device, exported as `/kmsbuf`, imported by weston's Mesa via G1. Default off; drop it when G4 lands. §8 |

## 1. Design

```
 weston-simple-shm ───────────────┐             weston-simple-egl (Mesa --wayland, v3d via kmsro)
   memfd_create() → /shm/<id>     │ AF_UNIX       wl_egl_window, linux-dmabuf buffers
   ftruncate → shmsrv allocates   │ $XDG_RUNTIME_DIR/wayland-0 (+SCM_RIGHTS fds)
   mmap = the export window (E1)  │               (0012: buffers = kms dumb BOs → /kmsbuf fds)
                                  ▼
   weston (one static process, 18.8 MB stripped)
   ├─ frontend + kiosk-shell (builtin modules)          libseat noop ── open() ──► card0, kbd0, mouse0
   ├─ libwayland-server: event loop = compat epoll over poll()  (unix sockets, card0 fd with
   │    pollNotify, libinput socketpair, emulated timers/signals — no 20 ms quantum on this set)
   ├─ wl_shm: mmap(shm fd) → the same pages → glTexImage (GL) / pixman composite
   ├─ linux-dmabuf: EGL_EXT_image_dma_buf_import → Mesa → render BO_IMPORT ns=kmsbuf (G1)
   ├─ DRM backend ── libdrm-phoenix ── msgSend ──► rpi4-kms   /dev/dri/card0   atomic, planes, events
   │    udev shim: card0 only                                  (fence-gated flips: -G)
   ├─ GL renderer ── GBM/EGL/GLES (kmsro) ── libdrm-phoenix ──► rpi4-v3d-async /dev/dri/renderD128
   │    or pixman renderer: dumb buffers + CPU composition (no Mesa at run time)
   └─ libinput-phoenix: reader thread polls usbkbd/usbmouse every 8 ms → events → socketpair wake
```

Start-up path [read: `frontend/main.c`, `libweston/compositor.c`, `backend-drm/drm.c`, `kms.c`,
`launcher-libseat.c`, `libinput-seat.c`]:

1. `wet_main` → `verify_xdg_runtime_dir` (exits if `XDG_RUNTIME_DIR` is missing, only warns on mode)
   → config `--config` → `weston_compositor_create` (xkb context; no data files needed yet).
2. `weston_load_module("drm-backend.so", "weston_backend_init")` → builtin table.
   `drm_backend_create` → `weston_launcher_connect` → libseat noop (socketpair, immediate enable) →
   `udev_new` → `find_primary_gpu`: enumerate `drm`/`card[0-9]*` = card0 → `drm_device_is_kms`:
   `libseat_open_device` = `open("/dev/dri/card0", O_RDWR|O_NONBLOCK|…)`, `fstat` (G2),
   `drmModeGetResources` → `init_kms_caps`: `DRM_CAP_TIMESTAMP_MONOTONIC` must be 1 (it is, M3 §7),
   `UNIVERSAL_PLANES` must succeed, `ATOMIC` + `CRTC_IN_VBLANK_EVENT` → atomic (G17 closed).
3. `udev_input_init` → libinput-phoenix opens the input devices (or none) → the renderer:
   `weston_load_module("gl-renderer.so", "gl_renderer_interface")` → GBM on the card0 fd (kmscube's
   proven kmsro path) → `eglGetPlatformDisplay(GBM)` → GLES context; or `--renderer=pixman`.
4. Heads from connectors (`HDMI-A-1`, EDID via libdisplay-info), output enable → first atomic
   commit with `ALLOW_MODESET` (`MODE_ID` blob: rpi4-kms compares the size only, M4 §1).
5. `wet_load_shell("kiosk-shell.so")` → builtin table; `wl_display_add_socket(display, "wayland-0")`
   (`--socket`), lock file via `flock` = fcntl record locks (real on Phoenix) → main loop.

## 2. What was built

| Path (under `tools/gpu-lane/weston-drm/`) | Lines | What |
|---|---|---|
| `build.sh` | 604 | fetch (sha256) → extract + patch (each patch its own commit) → dep views + libdrm snapshot → compat → libwayland, protocols, xkbcommon, display-info, libseat (meson) → shims → baked keymap → mesa-drm `--wayland` → Weston (meson, archives only) → hand links → verification |
| `compat/include/`, `compat/src/wlphx_{epoll,memfd,misc}.c` | ≈880 (C) | libphoenix-gap shim (§5.1): epoll/timerfd/signalfd/eventfd/ppoll + `__wrap_close`/`__wrap_write`, `memfd_create` over shmsrv, `msync`, `pipe2`; headers for `itimerspec`, `AF_LOCAL`, `MFD_*`, seals, `O_NOFOLLOW`, coarse clocks, `RTLD_NOLOAD`, `BYTE_ORDER`, `program_invocation_short_name`, `<values.h>` |
| `shims/include/{libudev.h, libevdev/libevdev.h, linux/{input,types,limits,vt,ioctl}.h}` | | own declarations (not systemd's header); `<linux/input.h>` over FreeBSD's BSD-2 `input-event-codes.h` (fetched pinned) |
| `shims/src/udev_phoenix.c` | 474 | libudev over a fixed table (§7.1) |
| `shims/src/libinput_phoenix.c`, `libinput_phoenix_hid.c` | 1254 | libinput-phoenix (§7.1); the HID→evdev table is xorg-drm's `phxhid_evdev_map.h` (FreeBSD, BSD-2), compiled in its own translation unit |
| `shims/src/libevdev_phoenix.c` | 41 | `libevdev_event_code_from_name` for BTN_ names |
| `shmsrv/shmsrv.c`, `shmsrv/shm_proto.h` | 578 | the `/shm` server + its protocol (§6) |
| `src/weston_builtin.c` | 43 | builtin-module table + `#include "weston_keymap.h"` (generated) |
| `patches/{wayland,seatd,weston}/` | | 1 + 4 + 6 `git format-patch` files (§4) |
| `conf/weston-drm.ini` | | stage as `/etc/xdg/weston/weston-drm.ini` |
| `pi/weston-m6a.sh` | ≈140 | the Pi-side cycle script: bash does the job control psh lacks (§9) |
| `hosttest/epoll_test.c`, `hosttest/run.sh` | | host test of the event-loop emulation (§5.1), no Pi, ~1 s |
| `../mesa-drm/build.sh` (`--wayland`), `../mesa-drm/patches/mesa/0012-…` | | §4.4 |

New files carry the Phoenix header (`%LICENSE%`) for the server/script/program sources and SPDX
BSD-3-Clause for the compat/shim files (the mesa-drm convention); patches stay MIT. No GPL/LGPL
source was copied (the libudev declarations were written for the shim).

Outputs (`build-out/`, gitignored; ≈715 MB, plus mesa-drm `build-out-wayland/` ≈1.4 GB):
`weston`, `weston-simple-shm`, `weston-simple-egl`, `shmsrv` (unstripped, keep for `addr2line`),
`*-stripped` (stage these), `*.map`, `*-link.log`, `<pkg>-full.patch` (each package's series as one
diff), `keymap-us.xkb` + `weston_keymap.h`, `prefix/` (every library, header and `.pc`).

## 3. Build and verification

```
tools/gpu-lane/weston-drm/build.sh             # everything; first run ≈ 15 min (Mesa --wayland ≈ 10)
tools/gpu-lane/weston-drm/build.sh --no-mesa   # reuse mesa-drm/build-out-wayland
tools/gpu-lane/weston-drm/build.sh --relink    # relink the four programs only
```

Inputs: the tree sysroot (checks `memExport`, `sys_fdpath`), the ports prefix, the toolchain, the E7
`phx-gcc` wrappers, libdrm-phoenix **`build-out-m5b/prefix`** (snapshotted: the one with the
sync-file `ioctl` interposer and G4a/G17), the mesa-drm compat headers; host `wayland-scanner`
1.24.0, `meson`, `ninja`, `bison`, xkeyboard-config.

**Verification [built]** (the script's `== verify` section; it fails the build on any miss):

| Check | `weston` | `weston-simple-shm` | `weston-simple-egl` | `shmsrv` |
|---|---|---|---|---|
| `nm -u` | **0** | **0** | **0** | **0** |
| size (text / data / bss) | 18 227 230 / 522 868 / 315 500 | 111 600 / 1 160 / 23 060 | 14 582 446 / 531 908 / 314 700 | 57 272 / 204 / 14 660 |
| file / **stripped** | 94 522 088 / **18 755 568** | 1 097 464 / **121 048** | 86 788 776 / **15 119 840** | 765 832 / **124 176** |
| sha256 (stripped, first 16) | `da1b568a56cf770b` | `726de04f92a35376` | `df2cd82fbebc8a88` | `c19a995265a8d6f6` |
| link warnings (beyond libphoenix's `sendmsg`/`recvmsg` attribute notes) | 0 | 0 | 0 | — |

(Weston embeds the git id of its extracted build tree, so a re-extraction changes `weston`'s hash
without changing code. Old lane for scale: `Xorg-drm` 20.8 MB, kmscube 16.5 MB.)

Symbols present in `weston`: `weston_builtin_modules`, `weston_builtin_xkb_keymap`,
`weston_backend_init`, `gl_renderer_interface`, `wet_shell_init`, `__wrap_mmap`, `__wrap_ioctl`,
`__wrap_close`, `__wrap_write`, `drm_phoenix_ioctl`, `drmPhoenixMmap`, `epoll_wait`,
`timerfd_settime`, `signalfd`, `eventfd`, `memfd_create`, `libseat_open_seat`,
`libinput_udev_assign_seat`, `udev_enumerate_scan_devices`, `di_info_parse_edid`,
`xkb_keymap_new_from_string`, `kmsro_drm_screen_create`, `v3d_drm_screen_create_renderonly`,
`gbmint_get_backend`, and both `os_create_anonymous_file` (weston) and
`mesa_os_create_anonymous_file` (Mesa's, renamed — §5.3). Strings: `drm-backend.so`,
`gl-renderer.so`, `kiosk-shell.so`, `linked into the program`, `using the builtin XKB keymap`,
`DRM backend`, `libdrm-phoenix:`, `DRMPHX_TRACE`, `/dev/dri/card0`, `/dev/dri/renderD128`,
`/kmsbuf`, `LIBINPUT-PHX`, `EGL_KHR_platform_gbm`, `V3D 4.2`, `/shm`. `weston-simple-egl`:
`dri2_initialize_wayland`, `wl_egl_window_create`, `__wrap_mmap`, `drm_phoenix_ioctl`,
`V3D_PHOENIX_SHARED_SCANOUT`; both clients: `memfd_create`. **Old-lane strings** (`v3d-winsys:`,
`phoenix_v3d_ioctl`, `peek_next_scanout`, `v3d-srv`, `/dev/v3d-srv`, `Xphoenix`, `[fbdev]`,
`glamor_phoenix`, `phxgl`) in all four stripped binaries: **0**.

Remaining compiler warnings in Weston (7, upstream code): `%ld` with a `time_t` (`long long` on
Phoenix) and with `int` arguments in log lines, one signedness compare, two maybe-uninitialized —
log text only.

## 4. Patches

### 4.1 libwayland 1.24.0 (`patches/wayland/`)

| # | Patch | Rationale |
|---|---|---|
| 0001 | `os: Phoenix-RTOS peer credentials and MSG_CMSG_CLOEXEC fallback` (+22/−1, `wayland-os.c`) | Phoenix AF_UNIX has no `SO_PEERCRED` (the file `#error`s otherwise): report own uid/gid, pid 0 (as FreeBSD without `cr_pid`). No `MSG_CMSG_CLOEXEC`: take the existing FD_CLOEXEC fallback when the flag is undefined. |

epoll/timerfd/signalfd/eventfd need **no** libwayland patch: the headers and implementations are in
compat (§5.1), as epoll-shim on FreeBSD.

### 4.2 seatd 0.9.1 (`patches/seatd/`)

| # | Patch |
|---|---|
| 0001 | `meson: librt is optional` (no librt on Phoenix) |
| 0002 | `connection: build where MSG_CMSG_CLOEXEC does not exist` (the seatd protocol code is always compiled; unused with noop) |
| 0003 | `libseat: allow a build with only the noop backend` (the `#error` required logind/seatd/builtin) |
| 0004 | `meson: do not build the unit tests by default in cross builds` (the poller test needs `SIGRTMIN`) |

### 4.3 Weston 14.0.2 (`patches/weston/`)

| # | Patch | Rationale |
|---|---|---|
| 0001 | `compositor: builtin-module table for programs without a dynamic linker` | weak `weston_builtin_modules[]` = {file name, entry point, address}, consulted by `weston_load_module()` before `dlopen()`; logs `Module '<name>': linked into the program`. Upstreamable (no effect when the table is absent). |
| 0002 | `meson: build libweston, the frontend and the DRM/GL/kiosk modules with library()` | 5 × `shared_library(` → `library(`: `-Ddefault_library=static` makes archives; default builds unchanged |
| 0003 | `input: fall back to a builtin XKB keymap` | weak `weston_builtin_xkb_keymap`, used only when the rule names fail to compile |
| 0004 | `meson: find dlopen() through the dl dependency` | `find_library('dl')` fails where libc has `dlopen` |
| 0005 | `meson: build without cairo, disabling only what uses it` | cairo/libpng/libutil were hard requirements even for DRM+GL+kiosk+simple clients; without them the cairo-shared library becomes a disabler (toytoolkit clients, screenshooter, desktop shell, nested-backend borders turn off) |
| 0006 | `gl-renderer: include <endian.h> for its byte-order tests` | `BYTE_ORDER == BIG_ENDIAN` with both undefined reads 0 == 0 (glibc pulls the header in implicitly) |

### 4.4 mesa-drm: `--wayland` and patch 0012

* `build.sh --wayland [--wayland-pkgconfig <dirs>]`: GLES build with `-Dplatforms=wayland` into
  `build-out-wayland/`; guarded against reuse of a non-wayland build dir and vice versa; no
  kmscube; writes `egl-link.txt` (the archive link order, gallium whole-archive first). Meson found
  wayland-protocols 1.45, wayland-client/server 1.24.0, wayland-egl-backend 3 from the weston-drm
  prefix; `dri2_initialize_wayland` and `EGL_EXT_image_dma_buf_import` are in `libEGL.a` [built].
  **No source patch was needed for the wayland platform itself.**
* **0012** `v3d: Phoenix-RTOS opt-in: allocate shared resources through renderonly
  (V3D_PHOENIX_SHARED_SCANOUT)` (+30/−2, `v3d_resource.c`, `DETECT_OS_PHOENIX` + env var, default
  off): with the variable set, a kmsro screen allocates `PIPE_BIND_SHARED` resources like scanout
  ones — linear, in rpi4-kms's dumb pool, imported into v3d — so a Wayland client's dma-buf export
  becomes a `/kmsbuf` export (works today) instead of a render-node export (G4, `-ENOSYS`). Cost:
  client buffers come out of the 32 MiB kms pool (a fullscreen 1080p client = 3 × 7.9 MiB; §9 uses
  `-p 96`). Remove with G4.

### 4.5 Link-time fix (no patch)

Mesa's `util/anon_file.c` and Weston's `shared/os-compatibility.c` both export
`os_create_anonymous_file()` with **different signatures** (invisible to each other in shared
builds). The link takes private copies of the Mesa archives that define or call it with Mesa's
renamed (`objcopy --redefine-sym …=mesa_os_create_anonymous_file`; meson's thin archives are
rebuilt from renamed members). The Wayland protocol `*_interface` objects both sides generate are
identical data in separate archive members and never collide.

## 5. The OS layer (libphoenix gaps; never edits to `sources/libphoenix`)

### 5.1 compat (`compat/`)

| Gap (tree sysroot 2026-09-27) | Used by | Shim | Limits |
|---|---|---|---|
| **no epoll** | libwayland event loop | `epoll_create1/ctl/wait` over `poll()`: interest list per epoll descriptor (a socketpair end), level-triggered | `EPOLLET`/`ONESHOT`/`EXCLUSIVE` → `EINVAL` (libwayland uses none); nested epoll not supported |
| **no timerfd** | libwayland timer heap (one per loop) | virtual timers: never polled, they bound the `poll()` timeout; EPOLLIN while expired and not re-armed | only observable via `epoll_wait` of the same process; `read()` of a timer → `EAGAIN` (libwayland re-arms, never reads [read: `event-loop.c:492-533`]) |
| **no signalfd** | Weston's SIGTERM/SIGINT/SIGQUIT/SIGCHLD sources | a handler per signal writes a `signalfd_siginfo` (Linux layout) into a socketpair; the signals are **unblocked** (callers block them first) | only `ssi_signo` filled; close restores the old action and blocks again |
| **no eventfd** | `wl_display_terminate` wake-up | socketpair; `write()` on it goes to the peer (`__wrap_write`) so the descriptor itself turns readable | one 8-byte record per write, not a summed counter |
| no `ppoll` | `wl_display_poll` (client) | `poll()` with the mask applied around it | not atomic |
| **no `memfd_create`** | Weston's/libwayland-cursor's anonymous files (wl_shm pools, keymaps, cursor themes) | over shmsrv (§6) | no seals |
| no `msync` | `wl_os_mremap_maymove` | validates, returns 0 (object mappings are coherent, never written back) | — |
| no `pipe2` | clipboard, client launcher | `pipe()` + `fcntl` | not atomic vs fork+exec |
| no `struct itimerspec`, `AF_LOCAL`/`PF_LOCAL`, `MSG_CMSG_CLOEXEC`, `SO_PEERCRED`, `O_NOFOLLOW`, file seals, `CLOCK_*_COARSE`, `RTLD_NOLOAD`, `BYTE_ORDER`/`LITTLE_ENDIAN`/`BIG_ENDIAN` (only the `__` names exist), `program_invocation_short_name` (libphoenix has `getprogname()`), `<values.h>` | libwayland, Weston, libseat | definitions in compat headers; wayland patch 0001 for the credential/cloexec code | coarse clocks use Linux's numbers so `clock_gettime` refuses them and Weston falls back to `CLOCK_MONOTONIC` — the only clock the DRM backend advertises [read: `kms.c:1858`] |
| inherited | `sys/file.h` `LOCK_*`, `static_assert`, `SCNxPTR`, `_SC_PHYS_PAGES`, `open_memstream` | mesa-drm `compat/include` (on the include path after ours) | as M3p3 |

**Host test** (`hosttest/run.sh`: `wlphx_epoll.c` built natively against its own headers, which
shadow glibc's, with ASan/UBSan): **31/31 PASS** — socket EPOLLIN/EPOLLOUT, level triggering,
`EEXIST`/`EINVAL` rules, a one-shot relative timer firing through `epoll_wait(-1)` after 50 ms,
expired timers staying readable until re-armed, disarm, an absolute deadline in the past firing at
once, a timeout shorter than the timer, a timer-only interest list, `signalfd` after the caller
blocked the signal (`raise` → readable → `ssi_signo`, 128-byte record), eventfd write → readable →
read → drained, `EBADF` after close. It found one Phoenix-specific trap by reading the kernel
alongside: Phoenix's `poll()` with **no** descriptors returns at once for an infinite timeout
(`posix.c:3220-3225`), so `epoll_wait` always keeps the epoll descriptor itself (never readable) in
the set — a timer-only list sleeps instead of spinning.

Two traps met on the way, both fixed in `build.sh`: (1) `git apply` run inside a directory of
*another* repository silently skips every path — each extracted tree gets its own `git init`;
(2) `-Wl,--wrap=close` makes libc's own `close()` calls reference `__wrap_close` after the compat
archive was scanned — `-Wl,-u,__wrap_close -Wl,-u,__wrap_write` pull it first (also in
`wlphx-compat.pc`, which `wayland-{server,client}.pc` require, so any consumer gets it).

### 5.2 shims (`shims/`)

`libudev` (fixed table: `drm`/`card0`/`/dev/dri/card0`, devnum = `st_rdev`, no parents/sysfs/
properties, a monitor that never fires), `libinput-phoenix` (§7.1), `libevdev_event_code_from_name`,
`<linux/input.h>` (FreeBSD's BSD-2 `input-event-codes.h` at commit `f492ef83`, sha256-pinned,
from `external/freebsd-src` or GitHub), `<linux/types.h>`, `<linux/limits.h>`, `<linux/ioctl.h>`
(the BSD `_IOC` layout; the sync_file requests reach libdrm-phoenix's `--wrap=ioctl` by their low
16 bits), an empty `<linux/vt.h>` (VT switching goes through libseat, which has none here).

## 6. wl_shm on Phoenix: `shmsrv`

**Why not a file on the RAM `/tmp`** (Weston's own fallback: `mkostemp` in `$XDG_RUNTIME_DIR` +
`unlink`) [read]: the kernel's page cache of a file is a `vm_object` keyed by the file's oid; both
processes' `mmap()`s share it, so pixels *would* be coherent — but (a) weston-simple-shm closes its
descriptor right after creating the pool and libwayland-server closes its copy after `mmap()`,
so dummyfs destroys the unlinked file (`refs == 0 && nlink == 0`, `dummyfs/object.c:101`) while the
kernel object lives on; any page first touched after that is fetched with `proc_read` from a
destroyed file → fault; (b) dummyfs reuses ids (`idtree_alloc`), so the next file with that id
finds the **old** kernel object — a new pool silently aliases an old one; (c) a file object's size
is fixed while mapped, so pool growth breaks.

**Design** (protocol in `shmsrv/shm_proto.h`):

| Step | Mechanism |
|---|---|
| `memfd_create()` | compat: `lookup("/shm")` → `mtDevCtl` `SHMSRV_OP_CREATE` → fresh id (monotonic, **never reused**) → `open("/shm/<id>", O_RDWR)` (the server answers `mtLookup`, `mtOpen` (reply 0), `mtGetAttr atMode/atType`, `mtGetAttrAll`) |
| `ftruncate(fd, n)` | `mtTruncate` → first time: `MAP_ANONYMOUS\|MAP_CONTIGUOUS` cached memory, capacity = a power of two ≥ max(n, 1 MiB), **zeroed**, `memExport(&{port,id}, va, round_page(n))`; later within the capacity: `memUnexport` + `memExport` of the new size over **the same pages** (existing windows keep working, new `mmap`s see the new size); beyond → `-EFBIG` |
| `mmap(fd)` (any process, after SCM_RIGHTS) | the kernel finds the export window (E1: same PA, cached type enforced, crosses AF_UNIX) — no shmsrv round trip |
| last close | `mtClose` (the open file's last reference: an in-flight SCM_RIGHTS message holds one — `fdpass_pack` → `posix_getOpenFile` takes a ref [read: `posix/fdpass.c:115`, `posix.c:332`]) → `memUnexport` + `munmap`; the pages live on in every mapping and are freed with the last one |
| `atSize` | refused (E1 §3 shadow-object rule); `fstat` size via `mtGetAttrAll`; `read`/`write` served from the server's mapping |
| tools | `shmsrv -f -v` (foreground, log create/truncate/destroy), `shmsrv -s` (stats: live objects, bytes), `shmsrv -q` |

Lifetime for weston-simple-shm [read: `simple-shm.c:160-185`, `wayland-shm.c:400-433`]: client
`memfd_create` → `ftruncate` (export) → `mmap` → `wl_shm_create_pool(fd)` (libwayland dup's the fd
into the message) → client closes its fd → flush (the pack holds a ref) → weston: `mmap` (window
found) and keeps `pool->mmap_fd` until the pool dies → `mtClose` only then. Contiguous-only is the
E1 limit (fine for pools; a fragmented system may fail large ones).

Alternatives weighed: per-client self-export (every client would have to serve its own port — a
thread per client); rpi4-kms dumb buffers as memfd (uncached memory for CPU drawing, the precious
<1 GiB scanout pool, no `ftruncate`); a kernel "anonymous shared object" call (the E1 follow-up for
non-contiguous memory — the long-term answer, `shm_open()` could then share the namespace).

## 7. Input, seat, modules, keymap

### 7.1 libinput-phoenix + libudev shim

Weston's libinput seat code (`libinput-seat.c`, `libinput-device.c`) and frontend device
configuration call ≈100 libinput functions [read]; all are implemented (the upstream `libinput.h`
is the contract; a signature mismatch is a compile error). Devices come from
`LIBINPUT_PHOENIX_DEVICES` (default `/dev/kbd0:keyboard,/dev/mouse0:mouse`; empty = none):

* `libinput_udev_assign_seat("seat0")` opens each through Weston's `open_restricted` (→ libseat →
  `open()`; a mouse that refuses `O_RDWR` is retried `O_RDONLY` directly), writes `0x01` (raw mode)
  to the keyboard, queues `DEVICE_ADDED`. A device that cannot be opened yet (pl011-tty holds
  `/dev/kbd0` while the console is in text mode) is logged once and retried once a second from
  `libinput_dispatch()` on Weston's thread (the reader thread nudges the wake socket).
* A reader thread polls open devices every 8 ms (non-blocking reads — device fds ride the kernel
  poll cycle, M3 G12), turns boot reports into events exactly as phxhid does (modifier bits and
  usage diffs → evdev codes; mouse → relative motion, BTN_LEFT/RIGHT/MIDDLE, wheel →
  `POINTER_AXIS` 15°/detent) and wakes Weston through a socketpair (AF_UNIX: immediate).
* Configuration answers "unavailable"; touch/tablet accessors exist for the linker only.
* Weston's `require-input=false` / `--continue-without-input` makes zero devices a warning.

### 7.2 libseat noop

`LIBSEAT_BACKEND=noop` selects it (noop is never auto-selected [read: `libseat.c`]); it opens
devices with `open(path, O_RDWR|O_NOCTTY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK)`, enables the seat at
once, never switches VTs.

### 7.3 Builtin modules

`src/weston_builtin.c`: `drm-backend.so`/`weston_backend_init`, `gl-renderer.so`/
`gl_renderer_interface`, `kiosk-shell.so`/`wet_shell_init`. Modules as archives (patch 0002), the
program hand-linked: `executable.c.o` + table + `libexec_weston.a`, `kiosk-shell.a`,
`drm-backend.a`, `gl-renderer.a`, `libweston-14.a`, Mesa (gallium whole-archive), the Wayland
stack, shims, compat. `dlopen` stays linked (the fallback for a module outside the table, and
`drm-gbm.c`'s `libglapi.so.0` workaround, which fails harmlessly).

### 7.4 Keymap

`keymap-us.xkb` (68 KB, `xkbcli-compile-keymap --rules evdev --model pc105 --layout us` against
the host's xkeyboard-config, from a native build of the same libxkbcommon when the host has no
`xkbcli`) → `weston_keymap.h` → `weston_builtin_xkb_keymap`. On the Pi `xkb_keymap_new_from_names`
fails (no `/usr/share/X11/xkb`) and Weston logs `using the builtin XKB keymap` — only when a
keyboard device exists (the keymap is built lazily). The keymap reaches clients as a memfd (shmsrv).

## 8. Cross-process buffers and sync: what works, what waits

| Feature | Today | Waits for |
|---|---|---|
| wl_shm clients (weston-simple-shm, cursor themes, toolkits' CPU buffers) | **expected to work**: shmsrv + E1 windows + SCM_RIGHTS (E1-proven) | Pi cycle m6a |
| Weston composition of dma-buf clients (linux-dmabuf → `EGL_EXT_image_dma_buf_import`) | a client buffer that is a `/kmsbuf` export imports on Weston's render connection: `BO_IMPORT ns=kmsbuf` (**G1 ✅**) | — |
| Client GPU rendering (weston-simple-egl, any wayland-egl app) | Mesa allocates back buffers `PIPE_BIND_SHARED` on the **render node** → export = `PRIME_HANDLE_TO_FD` there = **G4** (`-ENOSYS`). With **0012** (`V3D_PHOENIX_SHARED_SCANOUT=1`) they come from the kms pool instead → `/kmsbuf` → works (G4a covers the re-export of an imported BO) | **G4** (`V3DA_OP_BO_EXPORT` + `/v3dbuf`, `BO_IMPORT ns=v3dbuf`) to drop 0012 and the pool pressure |
| Direct scanout of a client buffer (kiosk fullscreen, overlay planes) | Weston's `drmModeAddFB2` needs `PRIME_FD_TO_HANDLE` on card0 of a buffer another process allocated = **G7** (`-ENOSYS`) → Weston **falls back to GL composition** (expected, not a failure) | **G7** (`KMS_OP_PRIME_IMPORT`) |
| Sync of client GPU work before Weston samples/flips it | Wayland relies on dma-buf implicit fences; none exist across processes here → a partially rendered client frame can be composited (tearing on the egl arm) | cross-process implicit sync (`BO_LAST_FENCE`, M3 G13) or explicit sync (below) |
| `linux-explicit-synchronization` / `wp_linux_drm_syncobj` (client acquire/release fences) | sync files are process-local (a `dup` of the render fd) | **G6** (`/v3dsync`, cross-process sync files/syncobjs) |
| Weston's own flips of GL output | GBM scanout BOs are kms dumb buffers imported on the render node (kmscube path); `IN_FENCE_FD` from `EGL_ANDROID_native_fence_sync` resolves in-process (`rpi4-kms -G`) | — |
| Pacing (`poll()`) | Weston's poll set = unix sockets + card0 (pollNotify in `rpi4-kms-gate`) + socketpairs + emulated timers → **no 20 ms quantum** | — (G12 mitigated) |
| DRI3-style fence sharing (xshmfence, G16) | **not needed**: Wayland has no xshmfence | — |
| Xwayland | not built | M4 + later |

## 9. Pre-registered Pi cycles

### Staging (coordinator)

```
tools/gpu-lane/weston-drm/build.sh            # or --no-mesa if mesa-drm/build-out-wayland is current
EXPORT=/srv/phoenix-rpi4-nfs-gcc16            # the live fsid=0 export
sudo mkdir -p "$EXPORT/etc/xdg/weston"
```

| Source (under `tools/gpu-lane/weston-drm/`) | Export path | mode |
|---|---|---|
| `build-out/weston-stripped` | `/bin/weston` | 755 |
| `build-out/weston-simple-shm-stripped` | `/bin/weston-simple-shm` | 755 |
| `build-out/weston-simple-egl-stripped` | `/bin/weston-simple-egl` | 755 |
| `build-out/shmsrv-stripped` | `/bin/shmsrv` | 755 |
| `pi/weston-m6a.sh` | `/bin/weston-m6a.sh` | 755 |
| `conf/weston-drm.ini` | `/etc/xdg/weston/weston-drm.ini` | 644 |
| already on the export (checked read-only 2026-09-27: `bash`, `sleep`, `rm`, `mkdir`, `chmod`, `kill`) and staged by earlier cycles: `/bin/rpi4-v3d-async-m3p2`, `/bin/rpi4-kms-gate`, `/bin/kmstest-poll`, `/bin/v3dasync-ping` | — | — |

(`cmp` after `install`; keep the unstripped binaries for `addr2line`.) Preconditions: netboot image
≥ build 11 (pollNotify kernel); no GPU app, no X, no SDL program, no old-lane `rpi4-v3d` running.

### Cycle `m6a-weston` (arms A and B; Bash `timeout: 600000`)

**Question:** does Weston's DRM backend bring up an output on HDMI through libdrm-phoenix and
`rpi4-kms`, with the pixman renderer (no Mesa at run time) and with the GL renderer (GBM/EGL on
V3D), serve a wl_shm client whose pixels travel through shmsrv, and exit cleanly on SIGTERM?

```
./scripts/test-cycle-psh-interact.sh --label m6a-weston --idle-secs 45 --max-cmd-secs 150 \
    --hdmi-dense-on 'WESTONDRM client start' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/weston-m6a.sh pixman shm noinput" \
    "/bin/shmsrv -s" \
    "/bin/bash /bin/weston-m6a.sh gl shm input" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

Wall clock ≈ boot 60–150 s + 3 × ~10 s + 2 × (~10 s start + 30 s hold + ≤ 17 s exit) + 4 × ~10 s
≈ 5–6 min. Grade:

```
grep -a -E '^(WESTONDRM|SHMSRV|KMS|V3DA|DRMPHX|KMSTEST|V3DAPING|LIBINPUT-PHX) |\[[0-9:.]+\] |libseat|simple-(shm|egl)' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m6a-weston.log
./scripts/uart-summary.sh m6a-weston
```

Allow ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice. Weston's own log
lines start with `[hh:mm:ss.mmm]`.

**Predictions** (per arm unless noted):

| Line / observation | Predicted | If instead… |
|---|---|---|
| `SHMSRV srv ready ns=/shm port=… proto=1 …`, `SHMSRV srv detached pid=…` | once | `served already`: a stale server — note, continue |
| `WESTONDRM start renderer=pixman client=shm … input=none`, `WESTONDRM weston pid=…` | once per arm | bash/staging |
| `Command line: /bin/weston --config=…`, `Using config file '/etc/xdg/weston/weston-drm.ini'`, possibly `XDG_RUNTIME_DIR "/tmp/xdg" is not configured correctly` (mode warning only) | early | `XDG_RUNTIME_DIR is not set`: script env — stop |
| `Module 'drm-backend.so': linked into the program` (and later `gl-renderer.so` in arm B, `kiosk-shell.so` in both) | builtin table (patch 0001) | `Failed to load module`: table miss — stale binary (check sha) |
| `Seat opened with backend 'noop'` (libseat's info lines go to Weston's log unprefixed) | once | `No backend matched`/`No backend was able to open a seat`: `LIBSEAT_BACKEND` not exported |
| `using /dev/dri/card0`, `DRM: supports atomic modesetting`, `DRM: supports GBM modifiers` (or `does not support`), `DRM: does not support Atomic async page flip` | init_kms_caps passes | `does not support DRM_CAP_TIMESTAMP_MONOTONIC` / `doesn't support universal planes`: libdrm-phoenix cap mapping — read the `DRMPHX ioctl … GET_CAP/SET_CLIENT_CAP` lines |
| `DRMPHX conn fd=… path=/dev/dri/card0 node=card0 …` | libdrm-phoenix identified the libseat-opened fd (O_NONBLOCK) | `rc=-…` on HELLO: identification of a descriptor opened with O_NONBLOCK — library bug |
| arm A: `warning: no input devices found, but none required as per configuration.`; arm B: `LIBINPUT-PHX dev=/dev/mouse0 kind=mouse open=ok fd=…` or `open=failed errno=… (retrying every 1000 ms)`, same for `/dev/kbd0` (**both outcomes acceptable**: without `rpi4-kms -C` the console holds the keyboard, M4 R5), then `LIBINPUT-PHX seat=seat0 devices=2 reader=1` | as listed | a crash in libinput-phoenix: `addr2line` |
| arm B with a keyboard opened: `using the builtin XKB keymap (rule names not compiled)` | once | `failed to compile global XKB keymap`: the embedded keymap does not parse on target libxkbcommon — report |
| `Output HDMI-A-1 …`, mode `1920x1080@60`, `Output 'HDMI-A-1' enabled with head(s) HDMI-A-1` | one output | `No available CRTCs` / `Failed to find primary plane`: plane/possible_crtcs marshalling (M3 §3.2) |
| arm A: `DRM: output HDMI-A-1 … shadow framebuffer` / pixman renderer lines; `KMS srv …` two 1920×1080 dumb BOs (≈16 MiB of the 32 MiB pool) | pixman path: dumb BO + `mmap` token (`__wrap_mmap`) + ADDFB2 | `CREATE_DUMB` failure: pool — `-p 64` next time |
| arm B: `EGL version: 1.5`, `EGL vendor: Mesa Project`, `GL version: OpenGL ES 3.x Mesa 26.2.0`, `GL renderer: V3D 4.2…`, `V3DA srv import handle=… ns=kmsbuf …` (GBM scanout BOs) | GL renderer up (kmscube's GBM path) | EGL/GLES errors: compare with kmscube's M3p3 table; the fallback to try is arm A's pixman |
| `WESTONDRM socket=up wait_s=<1–20>` (with `WESTONDRM waiting for the socket …` every 10 s before it, so psh-interact's 45 s idle cut never ends the script early) | within ~20 s | `socket=missing … weston=exited`: read Weston's last lines; `weston=running` after 60 s: a hang — graded from the last `DRMPHX`/`KMS`/`V3DA`/Weston line (the stuck request), not from the script |
| `SHMSRV create id=N pid=…` ×2 then `SHMSRV truncate id=N size=250000 exported=253952 cap=1048576` ×2 (simple-shm's two buffers; `-v` log) | right after `client start` | no `create`: `memfd_create` did not reach shmsrv → simple-shm used the `/tmp` fallback (its pixels may still show; note it). `simple-shm: mmap failed`/`creating a buffer file … failed`: shmsrv or the export window — the `SHMSRV FAIL` line says which |
| HDMI (dense snapshots from `client start`) | **black kiosk background with simple-shm's 250×250 animated pattern centred**, changing between snapshots; console text gone | console still visible: the first commit never applied (`KMS apply …`); black and static: frame callbacks/flip events not delivered (poll/pollNotify) — look for `Pageflip timeout reached` |
| `WESTONDRM hold … weston=running client=running` ×3 | heartbeats | client exited early: its stderr line above (`simple-shm exiting`, protocol error) |
| `WESTONDRM client exited rc=143`, then `WESTONDRM weston exited rc=0 after_term_s=<1–3> socket=gone` | **clean TERM exit = the emulated signalfd + eventfd/event loop proven**; rpi4-kms restores the console on client death | `weston still up … sending KILL`: the signal never reached the loop (signalfd emulation) — record, not fatal for display grading |
| `SHMSRV stats rc=0 live=0 bytes=0 ids=…` after each arm | all pool objects destroyed when the client and weston closed them | `live>0`: a descriptor kept open (Weston pool not released) — leak |
| `KMSTEST stats … apply_errors=0 … bos=0 exports=0`, `V3DAPING stats … parked=0` | no leaks after both arms | `bos>0`: dumb BOs outlive Weston |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | any EL0 fault: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/weston-drm/build-out/weston <pc>` |

**Decides:** arm A PASS (pattern on HDMI, clean exit) = Weston's DRM backend, the compat event loop,
libseat/udev/libinput shims and the shmsrv wl_shm path work; arm B adds the GL renderer. A failure
before the socket in A but not B (or the other way) localises the renderer; in both = backend/OS
layer.

### Cycle `m6b-weston-egl` (arm C; after m6a arm B passed)

```
./scripts/test-cycle-psh-interact.sh --label m6b-weston-egl --idle-secs 45 --max-cmd-secs 150 \
    --hdmi-dense-on 'WESTONDRM client start' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G -p 96" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/weston-m6a.sh gl egl noinput" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

`-p 96` departs from the plain `-G` on purpose: the kiosk shell makes simple-egl fullscreen, so
with 0012 its three 1920×1080 buffers (≈24 MiB) plus Weston's own GBM scanout buffers (≈24 MiB)
exceed the default 32 MiB pool (E3: 256 MiB contiguous below 1 GiB is available).

| Observation | Predicted | If instead… |
|---|---|---|
| `Using config: r8g8b8a8` (simple-egl), no `Failed to create EGLSurface` line | EGL wayland platform initialised: Mesa opened the device Weston's dmabuf feedback names (card0 → kmsro) | `failed to get driver name`/`no device`: the dmabuf-feedback `main_device` dev_t → libdrm-phoenix `drmGetDeviceFromDevId` (M3 §2.9) |
| `unable to load default theme` possible | cursor theme: libwayland-cursor's builtin fallback theme via memfd; growth past 1 MiB → `-EFBIG` → this line, harmless | — |
| client buffers created on card0 (in the `DRMPHX … CREATE_DUMB` trace of the client, if traced) and `V3DA srv import … ns=kmsbuf` twice per buffer (the client's render connection, then Weston's) | 0012 path: client allocates on card0, both processes import | `PRIME_HANDLE_TO_FD … node=render rc=-1 errno=38` in the client's trace: 0012 not active (variable not passed) = the G4 gap itself |
| `N frames in 5 seconds: X fps` every 5 s | X ≈ 50–60 (GLES triangle, vsync-paced through Weston's repaint loop) | ≈ 30: the flip completes a vblank late (see poll-wake.md); < 10: composition copies (GL import) or IPC per frame — read `DRMPHX` rates |
| HDMI | **the rotating RGB triangle, fullscreen** | black: Weston could not import the buffer (EGL dma-buf import) — `linux_dmabuf` errors in Weston's log; torn triangles: no cross-process implicit sync (§8) — expected risk, note |
| Weston log | no direct scanout of the client (`drmModeAddFB2` of a foreign buffer → G7 → GL composition) | direct scanout succeeded: G7 was implemented meanwhile |
| exit, stats, faults | as m6a | as m6a |

## 10. Risks only the Pi can show

| # | Risk | Where it shows |
|---|---|---|
| R1 | Weston's atomic TEST_ONLY storms (plane assignment every repaint) cost an IPC round trip each (~31 µs, E5) | frame time; `WESTON_DISABLE_ATOMIC=1` (legacy SETCRTC + PAGE_FLIP, drmprobe-proven) is the fallback knob |
| R2 | rpi4-kms's event timestamps vs Weston's `CLOCK_MONOTONIC` (`clock 0` on Phoenix): a different base gives `computed repaint delay is insane` warnings | Weston log |
| R3 | `EDID` blob empty/absent → libdisplay-info parse failure | a warning, make/model "unknown" |
| R4 | signal delivery into a thread blocked in `poll()` (libphoenix) | the TERM exit row |
| R5 | a cursor plane: with a mouse, Weston puts the cursor on rpi4-kms's cursor plane (a 64×64 GBM/dumb BO) — never exercised by a client before | arm B with a mouse; `WESTON_DISABLE_ATOMIC` does not change it; `--renderer=pixman` uses the same plane |
| R6 | memory: 18.8 MB static weston + Mesa compiler state | first frame delay |
| R7 | Weston's `%ld` format warnings (`time_t` is `long long`) — log text only | odd numbers in log lines |

## 11. What remains for M6 (honest estimate)

| Step | Size | Notes |
|---|---|---|
| m6a/m6b cycles + fixes they find | 1–3 cycles, 0.5–2 days | the integration surface is kmscube's (GBM/EGL/KMS) + Xorg's (static modules) + new (event loop emulation, shmsrv, libseat/udev/libinput shims) |
| Input on the Pi (`rpi4-kms -C` console handover, keys into a Wayland client) | 0.5–1 day | libinput-phoenix is written; untested; needs a keyboard-reading client (e.g. weston-terminal needs cairo — ports have cairo, so weston patch 0005's disabler turns back on once cairo is exposed) |
| **G4** render-node export → drop 0012 | ~120 + ~40 lines server + library, 1–2 days (M3 §4 spec) | shared with M4 DRI3, M5 external memory |
| **G7** kms import of foreign buffers (direct scanout of fullscreen clients, overlays) | ~150 lines + library, 2–3 days | performance, not function (GL composition works) |
| Cross-process implicit sync (`BO_LAST_FENCE`) or **G6** explicit sync (`wp_linux_drm_syncobj_v1` in Weston 14) | 2–5 days | tear-free GPU clients |
| Desktop shell (cairo toytoolkit: `weston-desktop-shell`, `weston-terminal`) from the ports' cairo/pango/fontconfig | 1–2 days | view-based private dep views, as here |
| Xwayland (needs M4's X server pieces) | later | out of M6's first gate |
| Porting into the ports framework (`weston` port next to `xorg_server`), SDL2 Wayland video driver for the games | 2–3 days | the migration step |

Total to a usable Wayland desktop with GPU clients: **≈ 2–3 weeks**, of which the first cycle(s)
are days and the rest is the server gap work (G4, G7, G6/implicit sync) that M4 and M5 need too.
The compositor itself is done up to its first cycle.

## 12. Notes for the coordinator

* **mesa-drm patch 0012 changed the patch-set stamp of every mesa-drm build dir.** The next
  `mesa-drm/build.sh` run of `build-out`, `build-out-vulkan` or xorg-drm's private Mesa re-extracts
  and rebuilds Mesa fully (≈10 min). 0012 is gated by `DETECT_OS_PHOENIX` + the environment
  variable, so no behaviour changes — the M4/M5 owners should just expect the rebuild.
* `mesa-drm/build.sh` gained `--wayland` (additive; the default and `--opengl`/`--vulkan` paths are
  unchanged apart from the archive list moving above the kmscube section).
* The libinput 1.26.2 tarball is a GitLab *archive* (only `libinput.h` is used); GitLab can
  regenerate archives with a different byte stream, so its sha256 pin may need refreshing one day —
  every other pin is a release tarball.
* libphoenix gaps met here, for the libc backlog (all shimmed, none blocking): epoll, timerfd,
  signalfd, eventfd, ppoll, memfd_create (→ shmsrv), msync, pipe2, `struct itimerspec`,
  `AF_LOCAL`, `MSG_CMSG_CLOEXEC`, `SO_PEERCRED`, `O_NOFOLLOW`, file seals, coarse clocks,
  `RTLD_NOLOAD`, public `BYTE_ORDER` names, `program_invocation_short_name`, `<values.h>`,
  `<linux/input.h>`.
* PLAN.md's M6 row is left for the coordinator to update.

## Result — `m6a-weston`

*(to be filled: log path, snapshot paths, the tagged lines, the rows that applied)*

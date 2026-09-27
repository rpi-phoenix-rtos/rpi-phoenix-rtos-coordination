# M6 preparation — `weston-drm`: Weston 14 (Wayland) on the new lane

Milestone M6 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §5 (M6:
"Wayland — Weston DRM backend"). Builds on [M3](M3-libdrm-phoenix.md) (libdrm-phoenix, `/dev/dri`
nodes, the static Mesa GBM/EGL/GLES, the `--wrap=mmap/ioctl` rules), [M4](M4-xorg-modesetting.md)
(builtin-module table, `phxhid` input, baked keymap, the libphoenix-gap shim pattern),
[M5](M5-vulkan.md) (the sync-file `ioctl` interposer, G4a/G17), [E1](E1-vm-object-export.md)
(`memExport`, fd per buffer across AF_UNIX) and [poll-wake](poll-wake.md) (`pollNotify`,
`rpi4-kms-gate`).

**Latest (2026-09-27, G6):** cross-process implicit sync implemented and host-tested (fail-then-pass),
Pi cycle `g6-sync` pre-registered — [G6-cross-process-sync.md](G6-cross-process-sync.md) (§17 here is a pointer).
Weston's composition of a client buffer waits for the client's render (render-server implicit
dependencies) and a direct-scan-out flip waits for it too (`weston-g6`).

**Earlier (2026-09-27, G7):** card0 import of a foreign buffer (`KMS_OP_PRIME_IMPORT`, direct scan-out of
client buffers) implemented and host-tested, Pi cycle `m6h-g7` pre-registered — [§16](#16-g7--card0-import-of-a-foreign-buffer-kms_op_prime_import-and-cycle-m6h-g7). G4 (§15) is pending `m6g-g4`.

**Status (2026-09-27, earlier):** first Pi cycle **`m6a-weston` FAILED at compositor init** in both
arms — `failed to create XKB context` 2 s after start (libxkbcommon refuses a context when no include
path exists, which is always the case on Phoenix). Fixed by weston patch **0007** (context without
default includes; the baked keymap path unchanged), reproduced and verified on the host
(`hosttest/xkb_test.c`), rebuilt; the rest of the start-up path was walked statically (§13).
Cycles **re-registered as `m6c-weston` / `m6d-weston-egl`** (§9). See [Result — m6a](#result--m6a-weston-queue31-2026-09-27-1013-fail-fixed-by-weston-patch-0007).

**Earlier:** code complete, builds, statically verified. The whole
Wayland stack — libwayland 1.24.0 (server, client, egl, cursor), wayland-protocols 1.45,
libxkbcommon 1.7.0, libdisplay-info 0.2.0, libseat 0.9.1 (noop backend) — and **Weston 14.0.2 with
only the DRM backend, the GL (and pixman) renderer and the kiosk shell** cross-build static for
aarch64-phoenix; `weston`, `weston-simple-shm`, `weston-simple-egl` (GLES on the new Mesa
`--wayland` EGL platform) and a new `/shm` server `shmsrv` link with **0 undefined symbols**, carry
the libdrm-phoenix and weston tags, and contain **no old-lane string**. Nothing committed, nothing staged, no server, old-lane file or sibling repo
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
| `patches/{wayland,seatd,weston}/` | | 1 + 4 + 7 `git format-patch` files (§4) |
| `conf/weston-drm.ini` | | stage as `/etc/xdg/weston/weston-drm.ini` |
| `pi/weston-m6a.sh` | ≈140 | the Pi-side cycle script: bash does the job control psh lacks (§9) |
| `hosttest/{epoll_test,xkb_test}.c`, `hosttest/run.sh` | | host tests, no Pi, seconds: the event-loop emulation (§5.1, 31/31) and Weston's XKB start-up with no include path (§7.4, 8/8) |
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
| size (text / data / bss) | 18 227 358 / 522 868 / 315 500 | 111 600 / 1 160 / 23 060 | 14 582 446 / 531 908 / 314 700 | 57 272 / 204 / 14 660 |
| file / **stripped** | 94 522 312 / **18 755 696** | 1 097 464 / **121 048** | 86 788 776 / **15 119 840** | 765 832 / **124 176** |
| sha256 (stripped, first 16) | **`2699d5e8ea831cdc`** (patches 0001–0007; m6a ran `1bb4cdb067a551c4`, 0001–0006) | `726de04f92a35376` | `df2cd82fbebc8a88` | `6a89f2610a5ad80d` |
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
| 0007 | `input: create the XKB context even when no XKB include path exists` | **the m6a failure.** `xkb_context_new(XKB_CONTEXT_NO_FLAGS)` returns NULL when none of `$XDG_CONFIG_HOME/xkb`, `$HOME/.config/xkb`, `$HOME/.xkb`, the extra path or the root exists (`libxkbcommon src/context.c:306-313` [read]); Weston then exits in `weston_compositor_init_config` before any backend. Retry with `XKB_CONTEXT_NO_DEFAULT_INCLUDES` and log `XKB: no include path exists, keymaps can only come from strings`; rule-name compilation then fails cleanly and 0003's baked keymap is used. The only `xkb_context_new` caller in the four binaries (`simple-im`/toytoolkit are not built; libxkbcommon's tools are not built for the target) [read + `nm`]. |
| 0008 | `frontend, compositor: WLPHX_TRACE shutdown-step trace` | diagnostic, off by default: with `WLPHX_TRACE=1`, one `WLPHX shutdown step=<name>` line on stderr per shutdown step (`shared/phoenix-trace.h`; §14). Drop once m6e grades a clean exit. |

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
| **no signalfd** | Weston's SIGTERM/SIGINT/SIGQUIT/SIGCHLD sources | a handler per signal writes a `signalfd_siginfo` (Linux layout) into a socketpair; the caller keeps the signals blocked (as a real signalfd wants) and **`epoll_wait` unblocks them around its `poll()`** (§14) | only `ssi_signo` filled; taken only by a thread waiting in `epoll_wait`; close restores the old action |
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
read → drained, `EBADF` after close. **`sigterm_test`** (8 checks, §14) sets the sources up in
libwayland's order and sends SIGTERM with every thread blocking it. It found one Phoenix-specific trap by reading the kernel
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
* M7 (labwc-drm, 2026-09-27) extended the shim additively: a wheel detent now queues `POINTER_SCROLL_WHEEL`
  after `POINTER_AXIS` (libinput ≥ 1.19 sends both; wlroots reads only the former, Weston only the latter, so
  Weston's behaviour is unchanged), plus `get_scroll_value{,_v120}`, `get_id_bustype` and ≈70 "unavailable"
  accessors/config defaults; `memfd_create()`'s shmsrv request is now `wlphx_shm_create()` (shared with labwc-drm's
  `shm_open`). Frozen M6 binaries are not affected ([M7 doc](M7-wayland-desktop.md#stage-1-built-wlroots-020--labwc-020--foot-128-toolsgpu-lanelabwc-drm)).

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
keyboard device exists (the keymap is built lazily). The context itself is created without include
paths (patch 0007; m6a showed the default creation fails). Host-verified (`hosttest/xkb_test.c`,
8/8): the default context fails exactly as on the Pi, the no-includes context succeeds, rule names
fail, the baked keymap compiles, `KEY_A` → `a`, and it re-serialises (68 121 bytes) for clients. The keymap reaches clients as a memfd (shmsrv).

## 8. Cross-process buffers and sync: what works, what waits

| Feature | Today | Waits for |
|---|---|---|
| wl_shm clients (weston-simple-shm, cursor themes, toolkits' CPU buffers) | **expected to work**: shmsrv + E1 windows + SCM_RIGHTS (E1-proven) | Pi cycle m6c |
| Weston composition of dma-buf clients (linux-dmabuf → `EGL_EXT_image_dma_buf_import`) | a client buffer that is a `/kmsbuf` export imports on Weston's render connection: `BO_IMPORT ns=kmsbuf` (**G1 ✅**) | — |
| Client GPU rendering (weston-simple-egl, any wayland-egl app) | Mesa allocates back buffers `PIPE_BIND_SHARED` on the **render node** → export = `PRIME_HANDLE_TO_FD` there = **G4**: implemented (§15), pending Pi cycle `m6g-g4`. m6d showed 0012 (`V3D_PHOENIX_SHARED_SCANOUT=1`) never engages for a wayland-egl client; `weston-m6a.sh` no longer sets it (knob `SHARED_SCANOUT=1`) | Pi cycle `m6g-g4` |
| Direct scanout of a client buffer (kiosk fullscreen, overlay planes) | Weston's `drmModeAddFB2` needs `PRIME_FD_TO_HANDLE` on card0 of a buffer another process allocated = **G7**: **implemented** (§16, `KMS_OP_PRIME_IMPORT`, LINEAR below 1 GiB; UIF → `EINVAL` → Weston adds the scanout tranche or keeps GL composition), pending Pi cycle `m6h-g7` | Pi cycle `m6h-g7`; tear-free needs cross-process implicit sync |
| Sync of client GPU work before Weston samples/flips it | Wayland relies on dma-buf implicit fences; none existed across processes → a partially rendered client frame could be composited or scanned out. **G6** (implemented, pending Pi `g6-sync`, [G6 doc](G6-cross-process-sync.md)): the render server makes Weston's composite job wait for the client's pending render of a shared BO, and `weston-g6`'s library gates a direct-scan-out flip on the client's fence (`BO_LAST_FENCE` → `IN_FENCE`, `rpi4-kms -G`) | Pi cycle `g6-sync` |
| `linux-explicit-synchronization` / `wp_linux_drm_syncobj` (client acquire/release fences) | sync files are process-local (a `dup` of the render fd) | **G6b** (`/v3dsync`, cross-process sync-file / syncobj descriptors; G6 covers the implicit half) |
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

### Cycle `m6c-weston` (arms A and B; Bash `timeout: 600000`) — re-registration of m6a

Same staging and commands as m6a (which stopped at the XKB context, see its Result) with the
0007 binary: re-stage **only `/bin/weston`** (`weston-stripped` sha256 `2699d5e8ea831cdc…`); the
clients, `shmsrv`, the script and the ini are unchanged since m6a. New label only.

**Question:** does Weston's DRM backend bring up an output on HDMI through libdrm-phoenix and
`rpi4-kms`, with the pixman renderer (no Mesa at run time) and with the GL renderer (GBM/EGL on
V3D), serve a wl_shm client whose pixels travel through shmsrv, and exit cleanly on SIGTERM?

```
./scripts/test-cycle-psh-interact.sh --label m6c-weston --idle-secs 45 --max-cmd-secs 150 \
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
    artifacts/rpi4b-uart/rpi4b-uart-*-m6c-weston.log
./scripts/uart-summary.sh m6c-weston
```

Allow ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice. Weston's own log
lines start with `[hh:mm:ss.mmm]`.

**Predictions** (per arm unless noted):

| Line / observation | Predicted | If instead… |
|---|---|---|
| `SHMSRV srv ready ns=/shm port=… proto=1 …`, `SHMSRV srv detached pid=…` | once | `served already`: a stale server — note, continue |
| `WESTONDRM start renderer=pixman client=shm … input=none`, `WESTONDRM weston pid=…` | once per arm | bash/staging |
| `Command line: /bin/weston --config=…`, `Using config file '/etc/xdg/weston/weston-drm.ini'`, possibly `XDG_RUNTIME_DIR "/tmp/xdg" is not configured correctly` (mode warning only) | early (**proven by m6a**) | `XDG_RUNTIME_DIR is not set`: script env — stop |
| **`XKB: no include path exists, keymaps can only come from strings`**, then `Output repaint window is 7 ms maximum.` — **no** `failed to create XKB context` | patch 0007 (the m6a stop) | `failed to create XKB context` again: the m6a binary was staged (sha) |
| `Module 'drm-backend.so': linked into the program` (and later `gl-renderer.so` in arm B, `kiosk-shell.so` in both) | builtin table (patch 0001) | `Failed to load module`: table miss — stale binary (check sha) |
| `initializing drm backend`, `Seat opened with backend 'noop'` (libseat's info lines go to Weston's log unprefixed), `libseat: session control granted` | once | `No backend matched`/`No backend was able to open a seat`/`libseat: could not open seat`: `LIBSEAT_BACKEND` not exported; `dispatch failed`: the noop socketpair |
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

### Cycle `m6d-weston-egl` (arm C; after m6c arm B passed) — re-registration of m6b

m6b (queue, same commands) ran the 0001–0006 binary and is expected to stop at the same XKB line;
its log goes into its Result section. m6d = m6b with the 0007 `weston`.

```
./scripts/test-cycle-psh-interact.sh --label m6d-weston-egl --idle-secs 45 --max-cmd-secs 150 \
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
| exit, stats, faults | as m6c | as m6c |

## 10. Risks only the Pi can show

| # | Risk | Where it shows |
|---|---|---|
| R1 | Weston's atomic TEST_ONLY storms (plane assignment every repaint) cost an IPC round trip each (~31 µs, E5) | frame time; `WESTON_DISABLE_ATOMIC=1` (legacy SETCRTC + PAGE_FLIP, drmprobe-proven) is the fallback knob |
| R2 | rpi4-kms's event timestamps vs Weston's `CLOCK_MONOTONIC` (`clock 0` on Phoenix): a different base gives `computed repaint delay is insane` warnings | Weston log |
| R3 | `EDID` blob empty/absent → libdisplay-info parse failure | a warning, make/model "unknown" |
| R4 | signal delivery into a thread blocked in `poll()` (libphoenix) | the TERM exit row; m6c failed it for another reason (§14) |
| R5 | a cursor plane: with a mouse, Weston puts the cursor on rpi4-kms's cursor plane (a 64×64 GBM/dumb BO) — never exercised by a client before | arm B with a mouse; `WESTON_DISABLE_ATOMIC` does not change it; `--renderer=pixman` uses the same plane |
| R6 | memory: 18.8 MB static weston + Mesa compiler state | first frame delay |
| R7 | Weston's `%ld` format warnings (`time_t` is `long long`) — log text only | odd numbers in log lines |

## 11. What remains for M6 (honest estimate)

| Step | Size | Notes |
|---|---|---|
| m6c/m6d cycles + fixes they find | 1–3 cycles, 0.5–2 days | the integration surface is kmscube's (GBM/EGL/KMS) + Xorg's (static modules) + new (event loop emulation, shmsrv, libseat/udev/libinput shims) |
| Input on the Pi (`rpi4-kms -C` console handover, keys into a Wayland client) | 0.5–1 day | libinput-phoenix is written; untested; needs a keyboard-reading client (e.g. weston-terminal needs cairo — ports have cairo, so weston patch 0005's disabler turns back on once cairo is exposed) |
| **G4** render-node export → drop 0012 | **implemented** (§15), pending `m6g-g4` | shared with M4 DRI3, M5 external memory |
| **G7** kms import of foreign buffers (direct scanout of fullscreen clients, overlays) | **implemented** (§16), pending `m6h-g7` | performance, not function (GL composition works) |
| Cross-process implicit sync (`BO_LAST_FENCE`) or explicit sync (`wp_linux_drm_syncobj_v1` in Weston 14) | implicit: **G6 done**, pending Pi `g6-sync` ([G6 doc](G6-cross-process-sync.md)); explicit: G6b, 2–5 days | tear-free GPU clients |
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

## 13. Static walk of the start-up path after the XKB context (for m6c)

m6a proved everything before `weston_compositor_init_config()` (log, display, the three emulated
signal sources, the eventfd, config parsing). The rest of the path was read call by call against
what Phoenix and the shims answer [read: `frontend/main.c` `wet_main`/`load_drm_backend`,
`libweston/compositor.c`, `backend-drm/drm.c` `drm_backend_create`/`find_primary_gpu`/
`drm_device_is_kms`, `kms.c` `init_kms_caps`/atomic apply, `launcher-libseat.c`, libseat
`noop.c`, `libinput-seat.c`, `renderer-gl/*`, `kiosk-shell.c`, wayland `wayland-server.c`
socket code, libdrm-phoenix `drm_phoenix_logic.c` flattening, rpi4-kms property table, kernel
`usocket.c`/`posix.c`]. No further hard stop was found; the residual risks are listed per step.

| Step | What runs | Why it should pass / residual risk |
|---|---|---|
| `init_config` rest | repeat rate/delay, `repaint-window` (default 7 ms), color management off, touch calibrator off | pure config |
| seat | `weston_launcher_connect` → `libseat_open_seat` (`LIBSEAT_BACKEND=noop`) → `socketpair(AF_UNIX, SOCK_STREAM\|SOCK_CLOEXEC)` → `libseat_dispatch(0)` enables the seat (listener) + `poll(1 fd, 0)` | all proven primitives (socketpairs: m6a's signal sources used them) |
| udev / GPU | `udev_new`, enumerate `drm`/`card[0-9]*` = card0, `stat("/dev/dri/card0")`, `libseat_open_device` = `open(O_RDWR\|O_NOCTTY\|O_CLOEXEC\|O_NONBLOCK)`, `fstat` (G2), `drmModeGetResources` (1/1/1) | first libdrm-phoenix client whose card fd is **O_NONBLOCK**: identification is by path (`sys_fdpath`), unaffected; events are read only after poll readiness |
| `init_kms_caps` | `TIMESTAMP_MONOTONIC` = 1 (hard requirement; drmprobe `monotonic=1`), `CURSOR_WIDTH/HEIGHT` (errors → 64), `UNIVERSAL_PLANES` (hard; card0 ✅), `ATOMIC` + `CRTC_IN_VBLANK_EVENT` = 1 → atomic, `ADDFB2_MODIFIERS`, `WRITEBACK_CONNECTORS`/`ASPECT_RATIO`/`ASYNC_PAGE_FLIP` (errors ignored) | — |
| renderer | pixman: `pixman_renderer_init`; GL: `gbm_create_device(card0)` (kmscube's kmsro path), `eglGetPlatformDisplay(GBM)`, EGL device query (`drmGetDevice2` identity ✅), hard requirements `EGL_KHR_surfaceless_context` and `GL_EXT_texture_format_BGRA8888` (Mesa ES: both), ES ≥ 3 so `GL_EXT_unpack_subimage` is not needed; the dma-buf allocator reuses the GBM device | GL's explicit-sync capability depends on `EGL_ANDROID_native_fence_sync` + `EGL_KHR_wait_sync`; if Mesa exposes them, Weston attaches `IN_FENCE_FD` from an EGL native fence — the in-process sync-file path (M5 `--wrap=ioctl`) in a real compositor for the first time |
| CRTCs, planes | `GETPROPERTIES` per CRTC (MODE_ID, ACTIVE, OUT_FENCE_PTR, VRR_ENABLED), `GETPLANERESOURCES`, per plane `type`, `IN_FORMATS` blob (rpi4-kms builds the DRM `drm_format_modifier_blob` layout, LINEAR only), `zpos` 0–7, `alpha`, `rotation` | the IN_FORMATS blob is parsed by a client for the first time (`drmModeFormatModifierBlobIterNext`) — layout read and matches |
| input | noinput: 0 devices → `warning: no input devices found, but none required`; input: libinput-phoenix opens through the launcher | as §7.1 |
| heads | `GETCONNECTOR` HDMI-A-1, `EDID` (absent/empty → libdisplay-info warning), `non-desktop` 0, `GETENCODER` possible_crtcs | — |
| event sources | `wl_event_loop_add_fd` **dups** each fd (card0, libseat socket, libinput socketpair, udev monitor, and the eventfd) and polls the dup | poll on a dup = the same open file (pollNotify registered per file); the eventfd's dup is the read end, so a `__wrap_write` to the original wakes it [host-tested write → read] |
| APIs | output API, virtual output API, direct-display, explicit-sync protocol (if capable), content-protection (atomic) | registration only |
| `backends_loaded` | presentation clock: the DRM backend offers `1 << CLOCK_MONOTONIC` = bit 0 on Phoenix; `CLOCK_MONOTONIC_RAW` (1) and the coarse ids (5/6, compat) are skipped, `CLOCK_MONOTONIC` (0) is chosen | — |
| socket | `/tmp/xdg/wayland-0.lock` `open(O_CREAT)` + `flock` (fcntl record locks, kernel table), `lstat`, `socket(AF_UNIX, SOCK_STREAM\|SOCK_CLOEXEC)`, `bind` with `offsetof + strlen` (the kernel reads `sa_data` NUL-terminated from user memory; wayland's socket struct is zeroed), `listen(128)` | — |
| kiosk shell | layers, `weston_desktop_create`, output/seat listeners, screenshooter, bindings; `weston_config_parse` of `WESTON_CONFIG_FILE` | no system calls of note |
| first frame | output enable (`mode=current`), primary plane + CRTC, then atomic commits: the first one disables every plane (primary + cursor `CRTC_ID=0 FB_ID=0`) and sets `MODE_ID` (a created blob), `ACTIVE=1`, connector `CRTC_ID`, `VRR_ENABLED=0` (zero-ok), `zpos`, `alpha`, `rotation`; `TEST_ONLY` proposals every repaint | every property is accepted by libdrm-phoenix's flattening (`drm_phoenix_logic.c:103-207`: zpos ≤ 7, alpha ≤ 0xffff, rotation, VRR 0, link-status/DPMS no-ops) [read]; repaint-loop start uses `drmWaitVBlank` relative 0 (F1 patch) with a page-flip fallback |

## 14. Weston ignores SIGTERM: cause, fix, and cycle `m6e-weston-term`

**Cause (found by reading, reproduced on the host).** libwayland 1.24's
`wl_event_loop_add_signal()` calls `signalfd()` **first** and `sigprocmask(SIG_BLOCK)` **after**
it (`src/event-loop.c:733-734` [read]). The compat `signalfd()` installed its handler and then
*unblocked* the signals, on the assumption that the caller had already blocked them. libwayland's
`SIG_BLOCK` on the next line undid that. After that SIGTERM, SIGUSR2 and SIGCHLD stayed blocked in
Weston's main thread and in every thread created later (a Phoenix thread starts with its creator's
mask: `syscalls_beginthreadex` → `proc_threadCreate(…, proc_current()->sigmask, …)` [read]). The
kernel's `threads_sigpost` looks for a thread whose mask admits the signal, finds none, and leaves
it process-pending (`proc/threads.c:1757-1789` [read]). So the handler never ran, the socketpair
never became readable, and Weston's `on_term_signal` never logged `caught signal 15`. That line is
missing from both m6c arms. The old host test blocked *before* `signalfd()` (`epoll_test.c:140`), so
it tested the compat's assumption rather than libwayland's order.

Hypotheses, ranked:

| # | Hypothesis | Evidence | Status |
|---|---|---|---|
| H1 | the signal stays blocked in every thread (the ordering above) | code read on all three sides; no `caught signal 15` in m6c; `sigterm_test` FAILs 4/8 on the old compat | **definite bug, fixed** |
| H2 | the handler runs but the wake-up is lost (EINTR/SA_RESTART in the emulated `epoll_wait`) | libwayland retries on `EINTR` (`wl_event_loop_dispatch`, `event-loop.c:1016-1033`), and the handler writes to the socketpair *before* poll returns, so a retried poll sees it readable [read + host] | unlikely; traced |
| H3 | the signal goes to a thread that is not waiting (Mesa's or the libinput reader) | would still wake the loop: the handler writes the socketpair from any thread | not a hang cause |
| H4 | the loop ended and shutdown hung (DRM destroy, a pending flip, libseat, the libinput thread join, Mesa exit handlers) | m6c shows no `caught signal 15`, so the loop never got as far as shutdown | not seen yet; traced (0008) |

**Fix** (`compat/src/wlphx_epoll.c`, contract in `compat/include/sys/signalfd.h`). The caller owns
the mask, as it does with a real signalfd. `signalfd()` no longer unblocks, and `close()` no longer
re-blocks. `epoll_wait()` builds the union of the masks of the signal descriptors in its interest
list, unblocks it with `pthread_sigmask` for the duration of `poll()` only, and then restores the
previous mask. A signal that was already pending is delivered when the mask is lifted (Phoenix
checks for signals on every syscall return, `threads_setupUserReturn`). One sent during `poll()`
interrupts it. One sent after the mask is restored waits for the next call. So no wake-up is lost,
and the handler can never interrupt code outside `epoll_wait()`, such as DRM ioctls or libseat.

**Host test** `hosttest/sigterm_test.c` (in `run.sh`, ASan/UBSan). It builds two sources in
libwayland's order, starts a thread that inherits the mask, then (1) sends SIGTERM before the
wait, (2) sends it from another thread during `epoll_wait(-1)`, (3) sends SIGUSR2 to its own
source, and checks that SIGTERM is still blocked outside the wait.
Old compat: **`RESULT fails=4 verdict=FAIL`** (`n=0 after 1004 ms` for cases 1 and 2, then SIGUSR2
`n=0`). Fixed: **8/8 PASS**, case 1 after 0 ms, case 2 after 101 ms (`WLPHX epoll_wait eintr`,
then `dispatched`). `epoll_test` still passes 31/31.

What the host test **proves**: the mask logic. On Linux (glibc) the pending signal is taken on
unblock, the poll in progress is interrupted, the loop retries, and no wake-up is lost. Linux and
Phoenix pick the target thread by the same rule here (any thread whose mask admits the signal, else
process-pending). What it does **not** prove: Phoenix's own `poll()` interruption (R4: whether
`_thread_interrupt` wakes a thread sleeping in `poll`, and whether it returns EINTR or restarts),
and delivery at the unblock syscall on Phoenix. Nor does it cover one Phoenix window: a signal posted
after the unblock returns but before the thread sleeps in `poll()` finds it running, not
interruptible, and waits in `proc->sigpend` until `poll()` returns for another reason, unless the
kernel checks for pending signals before it sleeps. The Pi run answers those.

**Trace** (`WLPHX_TRACE=1`; `weston-m6a.sh` now passes it, default 1; off in the binary by
default). All lines start with `WLPHX `:

| Tag | Where |
|---|---|
| `WLPHX signalfd fd=<n> sig=<s>` | compat `signalfd()`, at start-up (s = 15, 31, 20 on Phoenix) |
| `WLPHX sig=<s> caught` | the handler, `write(2)` only |
| `WLPHX epoll_wait eintr fd=<n> signal_fds=<k>` | `poll()` returned EINTR |
| `WLPHX signalfd dispatched fd=<n> revents=0x1` | `epoll_wait` reports a signal descriptor |
| `WLPHX shutdown step=<name>` | weston patch 0008, in this order: `loop-exit`, `compositor-destroy`, `backends-shutdown`, `compositor-shutdown`, `backends-destroy`, `backends-destroyed`, `compositor-destroyed`, `signals-remove`, `display-destroy`, `display-destroyed`, `main-return`, `exit-handlers-done` (an `atexit` handler registered first, so it runs after every later one, Mesa's included) |

### Cycle `m6e-weston-term` (arms A and B; Bash `timeout: 600000`)

**Staging:** re-stage **only** `/bin/weston` (`build-out/weston-stripped`, sha256 `bde137cc330e57c3…`)
and `/bin/weston-m6a.sh` (`pi/weston-m6a.sh`, which now passes `WLPHX_TRACE`), both mode 755, on
`/srv/phoenix-rpi4-nfs-gcc16`, and `cmp` them after install. The clients, `shmsrv` and the ini are
unchanged in behaviour; the new relinks of the clients carry only the compat change, which they never
use.

**Question:** with the fixed signalfd emulation, does Weston exit cleanly on SIGTERM with both
renderers? If it does not, which of H1, H2 or H4 is left?

```
./scripts/test-cycle-psh-interact.sh --label m6e-weston-term --idle-secs 45 --max-cmd-secs 150 \
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

Grade: `grep -a -E '^(WLPHX|WESTONDRM|KMS srv client|SHMSRV stats|KMSTEST|V3DAPING) |caught signal' artifacts/rpi4b-uart/rpi4b-uart-*-m6e-weston-term.log`
and `./scripts/uart-summary.sh m6e-weston-term`.

**Predictions** (per arm; everything before `client exited` is as in m6c):

| Lines after `WESTONDRM client exited rc=143` | Reading |
|---|---|
| **`WLPHX sig=15 caught` → `WLPHX signalfd dispatched fd=…` → `[…] caught signal 15` → the 12 `WLPHX shutdown step=` lines in order → `WESTONDRM weston exited rc=0 after_term_s=<0–2> socket=gone`** | **predicted: H1 fixed, clean exit.** `KMS srv client 1 closed` comes before `rc=0` |
| start-up has no `WLPHX signalfd fd=… sig=15` line | stale binary (check the sha) or `WLPHX_TRACE` not passed (old script): stop |
| `WLPHX signalfd` lines at start-up, but no `WLPHX sig=15 caught` within 15 s → KILL | delivery still blocked on Phoenix: the unblock inside `epoll_wait` did not take effect, or a thread in `poll()` is not interrupted and the process has no finite timeout (R4). Next: kernel `signalMask`/`threads_sigpost` in gdb |
| `caught`, but no `dispatched` (possibly `WLPHX epoll_wait eintr` repeating) | H2: the socketpair write from the handler does not wake Phoenix's `poll()` |
| `dispatched`, but no `caught signal 15` (possibly `signalfd read error`) | libwayland's read of the emulated record failed: the compat read path |
| `caught signal 15` and steps that stop at `<X>` | H4, localised: `loop-exit` missing → `wl_display_terminate`'s eventfd wake-up; stops after `backends-shutdown` → `drm_shutdown` (arm B: libinput-phoenix reader thread); after `compositor-shutdown` → output destroy / pending flip; after `backends-destroy` → `drm_destroy` (`gbm_device_destroy`, libseat close); after `main-return` → an exit handler (Mesa's `util_queue` join; arm B); `exit-handlers-done` present but no exit → libc/kernel process teardown |
| `WESTONDRM weston exited rc=0` in arm A but a hang in arm B | the hang is in the GL/input teardown (the step name says which) |
| `SHMSRV stats live=0`, `KMSTEST … bos=0 exports=0`, `V3DAPING … parked=0`, 0 faults | as m6c |

**Decides:** arm A clean = the emulated signalfd/eventfd/event loop is proven end to end, and patch
0008 plus the `WLPHX_TRACE` lines can be dropped at the next cleanup.

## Result — `m6a-weston` (queue31, 2026-09-27 10:13): FAIL, fixed by weston patch 0007

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-101308-m6a-weston.log` (`grep -a`). Binary
`weston-stripped` `1bb4cdb067a551c4` (weston patches 0001–0006). 0 exceptions, 0 faults.

- Servers: `V3DA srv ready … proto=2` (+ `/dev/dri/renderD128`, `/dev/dri/card1` registered),
  `KMS srv ready … poll_notify=1 … pool_mib=32`, **`SHMSRV srv ready ns=/shm port=26 proto=1`**
  and `SHMSRV srv detached` — shmsrv's namespace registration and detach work.
- Arm A (`pixman shm noinput`) and arm B (`gl shm input`), identically: `WESTONDRM start …`,
  `weston 14.0.2`, `Command line: …`, `OS: Phoenix-RTOS, 3.3.1 …`, `Flight recorder: enabled`,
  `Using config file '/etc/xdg/weston/weston-drm.ini'`, then **`failed to create XKB context`**
  about 20 ms later → `WESTONDRM socket=missing wait_s=2 weston=exited` → `weston exited rc=1
  before its socket appeared`. The script's early exit detection worked.
- Proven on hardware by getting this far: the static weston binary starts; `wl_display_create`
  (the emulated **eventfd**), `wl_event_loop_add_signal` ×3 (SIGTERM, SIGUSR2, SIGCHLD: the
  emulated **signalfd**, i.e. socketpairs + `sigaction` + unblocking), the epoll descriptor itself,
  the log/flight-recorder setup and the config parser.
- `SHMSRV stats rc=0 live=0 bytes=0 ids=0` after each arm (no client ran), `KMSTEST stats …
  bos=0 exports=0 apply_errors=0`, `V3DAPING … bos_live=0 parked=0` — nothing leaked, nothing
  past compositor init ran.
- **Cause:** libxkbcommon's `xkb_context_new(XKB_CONTEXT_NO_FLAGS)` fails when none of its default
  include paths exists (`src/context.c:306-313`: `failed to add default include path` → NULL);
  the Pi has no `/usr/share/X11/xkb`, `$HOME/.xkb`, `$XDG_CONFIG_HOME/xkb` or extra path. Weston
  creates the context in `weston_compositor_init_config` → `weston_compositor_set_xkb_rule_names`,
  before any backend, so patch 0003's baked keymap (used later, when a keyboard appears) was never
  reached. I had assumed the context creation tolerates missing paths — it does not.
- **Fix:** weston patch 0007 (§4.3). Reproduced and verified on the host with the same libxkbcommon
  source and the same environment situation (`hosttest/xkb_test.c`: default context NULL, no-includes
  context OK, names fail, baked keymap compiles and maps `KEY_A` → `a`). The next steps of the path
  were walked statically (§13). Re-registered as **`m6c-weston`** (§9; re-stage only `/bin/weston`,
  `2699d5e8ea831cdc…`).

## Result — `m6b-weston-egl`

As predicted: `artifacts/rpi4b-uart/rpi4b-uart-20260927-102141-m6b-weston-egl.log` — `failed to create XKB
context` 2 s after start, `weston exited rc=1 before its socket appeared`, 0 exceptions (queue31, 10:21).

## Result — `m6c-weston` (queue34, 2026-09-27 11:27): ★ arms A and B DISPLAY; exit on SIGTERM FAILS

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-112701-m6c-weston.log`; HDMI
`artifacts/hdmi/20260927-113046-m6c-weston-tick.png` (arm A, pixman) and `…-113315-…` (arm B, GL).

**Weston 14 composites a Wayland client on HDMI on Phoenix, with both renderers.** Patch 0007 cleared
the XKB stop; the DRM backend brought up `HDMI-A-1` 1920×1080@60 through libdrm-phoenix and `rpi4-kms`.
`weston-simple-shm`'s pattern is on screen, **full-screen** (kiosk-shell fullscreens it, so the
prediction's "250×250 centred" row did not apply: its buffers are 1920×1080, `SHMSRV truncate
size=8294400` ×2 per arm). The pattern differs between snapshots, so frames are delivered.

| arm | renderer | evidence | flips / 30 s hold | exit |
|---|---|---|---|---|
| A | pixman (no Mesa) | two dumb BOs + `mmap` token, `shadow framebuffer`; `KMS srv flipstat flips=670 vbl1=442 vbl2=228 dropped_events=0` | 670 | ✗ `still up 15s after TERM: sending KILL` |
| B | GL | `EGL version: 1.5`, `GL version: OpenGL ES 3.1 Mesa 26.2.0`, `GL renderer: V3D 4.2.14.0`, `Using GL renderer`; V3DA 800 bin/render jobs, `wedges=0 err=0`; `flips=805 deferred=783 applied_gate=783` (fence-gated flips) | 805 | ✗ same |

- Input (arm B): `[libseat/backend/noop.c:57] Failed to open device: Device or resource busy` once a
  second: the console holds `/dev/kbd0` without `rpi4-kms -C` (acceptable row, M4 R5).
- Cleanup: `SHMSRV stats live=0 bytes=0` after each arm; `KMSTEST stats … apply_errors=0 dropped=0 bos=0
  exports=0`; `V3DAPING stats bos_live=0 parked=0 verdict=PASS`. **0 exceptions**, 0 kernel faults.
- ✗ **Exit:** both arms ignored SIGTERM for 15 s and were KILLed (`rc=137`, `socket=left`). The client died
  on TERM (`rc=143`). The DRMPHX trace is sampled (n = powers of 2), so it does not say whether frames
  continued. *Added in §14:* Weston's `on_term_signal` logs `caught signal 15` first thing
  (`frontend/main.c:831`), and that line is absent in both arms — the signal source never dispatched,
  so the loop never ended; the cause is in the compat signalfd (§14).

**Decides:** arm A PASS for display = the DRM backend, compat event loop, libseat/udev/libinput shims and
shmsrv wl_shm path work; arm B adds the GL renderer on V3D. Clean exit is still open. m6d (simple-egl) follows.

## Result — `m6d-weston-egl` (queue34, 2026-09-27 11:37): ✗ client GPU buffers hit G4

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-113714-m6d-weston-egl.log`. Weston came up exactly as in m6c
arm B (`GL renderer: V3D 4.2.14.0`, `Using GL renderer`, output on HDMI, `rpi4-kms-gate -G -p 96`).
`weston-simple-egl` started (`Using config: r8g8b8a8`, `has EGL_EXT_buffer_age …`) and then:

    MESA: error: Failed to export gem bo 8202 to dmabuf

Handle 8202 (`0x200a`) is in the **render-node** range, so the client allocated its back buffer on
renderD128 and the export went down the G4 path (`PRIME_HANDLE_TO_FD` on the render node). This is the
prediction table's "0012 not active" row, **although `V3D_PHOENIX_SHARED_SCANOUT=1` was passed** (the
script sets it on the client, and the string is in both binaries). Consequences: `KMS srv flipstat flips=1`
(only Weston's first frame), V3DA `render=3` jobs, no `N frames in 5 seconds` line. The client's own
`DRMPHX` trace is not on the UART, so which device it opened is inferred, not seen. The likely mechanism:
Mesa's Wayland platform opens the **render node** of the device named by dmabuf feedback, so the screen
has no renderonly (kmsro) instance, and 0012's card0 allocation has nothing to allocate through.

Clean: `SHMSRV stats live=0`, `KMSTEST … bos=0 exports=0`, `V3DAPING bos_live=0 verdict=PASS`, 0 exceptions.
Exit: the same SIGTERM failure as m6c.

**Decides:** 0012 does not cover wayland-egl clients. The fix is **G4** itself (render-node BO export
`V3DA_OP_BO_EXPORT` + `/v3dbuf`, import `ns=v3dbuf`), which M4 DRI3 and M5 external memory need too. It
retires 0012 and the `-p 96` pool pressure.

## Result — `m6e-weston-term` (queue36, 2026-09-27 12:43): ✅ PASS — Weston exits cleanly on SIGTERM

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-124309-m6e-weston-term.log` (starts with the known pre-boot
UART flood; boot normal). Staged `weston` `bde137cc330e57c3`, script with `WLPHX_TRACE`. Both arms (pixman +
shm, GL + shm + input), in the predicted order:

`WLPHX sig=15 caught` → `WLPHX signalfd dispatched fd=8 revents=0x1` → Weston's own `caught signal 15` →
the 12 `WLPHX shutdown step=` lines in order (`loop-exit` … `exit-handlers-done`) →
**`WESTONDRM weston exited rc=0 after_term_s=1 socket=gone`**. No `sending KILL`, 0 exceptions.
Display unchanged from m6c (`flips=667` pixman, `801` GL with 782 fence-gated). Cleanup: `SHMSRV stats live=0`,
`KMSTEST … apply_errors=0 dropped=0 bos=0 exports=0`, `V3DAPING bos_live=0 verdict=PASS`.

**Decides:** the m6c exit failure was the compat signalfd ordering bug (fixed `2b575ddaf`); the Weston
shutdown path itself has no hang. M6 exit row closed.

## 15. G4 — render-node PRIME export (`V3DA_OP_BO_EXPORT` + `/v3dbuf`), and cycle `m6g-g4`

m6d stopped at `MESA: error: Failed to export gem bo 8202 to dmabuf`: a wayland-egl client allocates its
back buffers on the render node, and `PRIME_HANDLE_TO_FD` there was gap G4. This section closes it in the
render server and libdrm-phoenix. It is host-tested; the Pi cycle below is pre-registered.

### 15.1 Design

**Export** (`V3DA_OP_BO_EXPORT` = 22, request `v3da_bo_req_t {handle}`, reply `v3da_bo_resp_t`). An M1a BO is
one `MAP_CONTIGUOUS | MAP_ANONYMOUS` block, so the server publishes the whole block with `memExport()` under
`{buf_port, handle}` (E1). `buf_port` is a third port of the server, registered as **`/v3dbuf`** and
served by one extra thread. It works exactly like rpi4-kms's `/kmsbuf` (E1 §1): `mtLookup "<id>"`, `atMode`
and `atType`, `mtOpen`/`mtClose` replying 0, `mtGetAttrAll` (character device, size), and `atSize`
**only while the BO is exported**, under `srv.lock`. The id is the BO handle, which is never reused
while the server lives, so an id is never exported twice (the shadow-object residual of G3 cannot name
another buffer). Exporting again answers the same id. Refused: a handle that is not the client's
(`-ENOENT`), an imported, scanout or cacheable BO (`-EINVAL`; libdrm-phoenix creates uncached BOs only,
and a dma-buf descriptor cannot carry a memory type), no namespace (`-ENODEV`).

The dma-buf descriptor is `open("/v3dbuf/<id>", O_RDONLY)`, done by libdrm-phoenix after the reply. It is
the same kind of descriptor as a `/kmsbuf` one, so the existing library paths work on it unchanged:
`fstat`, `lseek(SEEK_END)` (Mesa's dma-buf size), `mmap` with `MAP_UNCACHED` (the `__wrap_mmap` dma-buf
branch), `sys_fdpath` identification. It passes over `SCM_RIGHTS` with its path (`fdpass.c` packs the
`open_file_t`).

**Import `ns=v3dbuf`** (`BO_IMPORT`, formerly `-ENOSYS`). The server is the exporter, so it opens and maps
nothing. Under `srv.lock` it checks `port == buf_port` and that `id` names a live, exported BO. The
importer gets **that BO's handle**: one BO, one GPU VA, one last-use record, so `BO_WAIT` on the import
sees every client's jobs. It also gets a reference (a bit in the BO's `sharers` mask). If the importer
already holds the handle (its own export, or a second import), it gets the same handle and no extra
reference (DRM). The library short-circuits that case locally, since the id is the handle. The reply's
memref is the export's OID, so the importer's CPU mappings go through the E1 window.

**Lifetime.** `refs` = the creator's handle + one per importing client + one per open descriptor of the
name (`mtOpen`/`mtClose` are paired per `open_file_t`, `posix_fileDeref`). A Linux dma-buf holds its GEM
object in the same way: the BO outlives every handle while a descriptor is open, including one in
flight in a socket. At `refs == 0` the name is withdrawn: `exported = 0` then `memUnexport`, both
locked (G3 ordering). The block then goes through the ordinary G1 quarantine: PTEs cleared, TLB, every
queue past the release snapshot. Only after that does it return to the pool. Jobs of any sharer pin the
BO (`inflight`) exactly like the creator's. Client death drops that client's creator and sharer
references. A process that still maps the window after every handle and descriptor is gone sees the
block reused by a later BO. That is the same trade the server already makes for stale `MAP_PHYSMEM`
mappings: the late access lands in GPU memory, never in memory the kernel recycles.

**Protocol 3, compatibility.** `V3DA_PROTO_VERSION` = 3, and a new `V3DA_PROTO_BASE` = 2. The server
accepts HELLO 2..3 and replies with 3. The fence page keeps `version = 2`, because its layout is
unchanged.

| binary | against the G4 server | against a proto-2 server |
|---|---|---|
| proto-2 binaries already on the Pi: `rpi4-kms-gate -G` (fences), `v3dasync-ping` (checks fence-page `version == 2`), `drmprobe-m3p2/-m5/-m5b`, Mesa/Weston/kmscube/vkcube builds with the m5b libdrm | **unchanged** (HELLO 2 accepted; their render-node export stays the library-local `ENOSYS` gap) | unchanged |
| G4 libdrm-phoenix (`build-out-g4`, in `drmprobe-g4` and the new `weston-simple-egl`) | HELLO 3, export works | HELLO 3 → `EPROTO` → retries with 2: everything else works, `PRIME_HANDLE_TO_FD` of an own BO → `ENOSYS` as before |
| rebuilt `libv3da-client` (games, `v3dasync-ping`) and `rpi4-kms` | send `V3DA_PROTO_BASE` (they use nothing newer) | work |
| the old lane (`gpu/rpi4-v3d`, `v3d-srv`) | untouched | — |

**Not in G4:** importing a `/v3dbuf` descriptor on **card0** (rpi4-kms, for direct scan-out) is gap
**G7** (`KMS_OP_PRIME_IMPORT`) and still answers `ENOSYS` (*later:* implemented, §16). Mesa's renderonly import in Weston fails soft on
it (`v3d_resource.c:1107` ignores the NULL scanout), and Weston composites with GL. Importing on
renderD128 or card1 (the v3d primary node, the same server) works.

Files: `tools/gpu-lane/v3d-async/{v3da_proto.h, v3da.h, v3da_bo.c, v3da_main.c, v3da_sched.c,
libv3da-client.c, v3dasync-ping.c}`, `tools/gpu-lane/libdrm-phoenix/{src/drm_phoenix_v3d.c,
src/xf86drm_phoenix.c, include/drm_phoenix_ext.h, drmprobe/drmprobe.c, hosttest/*}`,
`tools/gpu-lane/kms/kms_main.c` (HELLO with `V3DA_PROTO_BASE`), `tools/gpu-lane/weston-drm/build.sh`
(the programs now link the `--libdrm-prefix` snapshot instead of the one named in Mesa's
`egl-link.txt`, so a libdrm change needs a relink, not a Mesa rebuild), `weston-drm/pi/weston-m6a.sh`
(`SHARED_SCANOUT` knob, default 0; `DRMPHX_TRACE` now also on the egl client).

### 15.2 Tests

`drmprobe` (`build-out-g4`). The old placeholder `prime_export_render` (a `gapcheck` on `ENOSYS`) is now a
real verdict, and two tests are new:

| key | what it checks |
|---|---|
| `prime_export_render` | a 64 KiB render BO with a known pattern written through its BO mapping → `PRIME_HANDLE_TO_FD` → path `/v3dbuf/<id>`, `lseek(SEEK_END)` = 65536, `fstat` `S_ISCHR`, `mmap(fd)` reads the pattern (`bad_words=0`), a write through each mapping is seen in the other (`xwrite=1`), a second export gives the same name, `PRIME_FD_TO_HANDLE` on the same connection returns **the same handle** |
| `prime_import_render2` | a second render connection of the same process (another server client) imports the descriptor, maps its handle and reads the pattern; after the creator's `close(fd)` + `GEM_CLOSE` the import still resolves in the server (`WAIT_BO`, `survives_creator_close=1`); one `GEM_CLOSE` on it releases the BO (`WAIT_BO` → `EINVAL`, `released=1`) |
| `prime_export_xproc` | `fork` + `socketpair(AF_UNIX, SOCK_STREAM)` + `SCM_RIGHTS` (exportprobe's helpers). The parent exports, sends the descriptor, then drops **everything** (descriptor, handle, mapping), so only the descriptor in flight keeps the BO. The child opens its own render node, imports, checks path, size, the pattern through the descriptor and through its handle, writes through one and reads through the other, runs `WAIT_BO`, closes. The parent then checks that the BO is gone (`released=1`) |

**Can they fail?** `hosttest/run.sh` has a **negative control**: the same probe against the fake render
server with `FAKE_V3DA_PROTO=2`, which is a proto-2 server (HELLO exactly 2, opcode 22 unknown, no
`/v3dbuf`). Output of that run:

    DRMPROBE prime_export_render rc=-1 errno=38 path=- size=-1 fstat_chr=0 mmap=0 bad_words=0 xwrite=0 reexport_same_name=0 self_import=-1 handle=0x0/0x18001 ok=0
    DRMPROBE prime_import_render2 skipped=1 (prime_export_render failed) ok=0
    DRMPROBE RESULT pass=35 fail=6 gap=0 failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,prime_export_render,prime_import_render2, secs=0 verdict=FAIL
    HOSTE2E g4-negative verdict=PASS (the G4 tests fail against a proto-2 server, the rest as before)

The library fell back to proto 2, and everything else passed as before. Against the fake G4 server,
both modes (legacy names and `/dev/dri`) pass:
`prime_export_render rc=0 … path=/v3dbuf/98305 size=65536 fstat_chr=1 mmap=1 bad_words=0 xwrite=1
reexport_same_name=1 self_import=0 handle=0x18001/0x18001 ok=1`,
`prime_import_render2 conn=1 rc=0 … survives_creator_close=1 released=1 ok=1`,
`HOSTE2E g4 … exports_live=0 v3dbuf_imports=1 bos_live=0`, and `HOSTTEST … checks=134 fails=0`. As
before, only the four fake-GPU pixel checks fail. The fake models the refcount rules of `v3da_bo.c`
(creator, sharers, descriptors), so a lifetime bug in the library's use of them shows up there. The
server's own code is only proven on the Pi. `prime_export_xproc` needs a real kernel (fork and
`SCM_RIGHTS` of mock descriptors), so it is compiled out on the host (`-DDRMPROBE_NO_FORK`) and prints
`skipped=1`.

### 15.3 Artifacts (built 2026-09-27; sha256, first 16 hex)

| file | sha256 | notes |
|---|---|---|
| `tools/gpu-lane/v3d-async/out-g4/rpi4-v3d-async` | `49f16a56a66a0957` | server, proto 3; `strings … \| grep -c ns=v3dbuf` = 3 |
| `tools/gpu-lane/v3d-async/out-g4/v3dasync-ping` | `89db1a2aaf2eedd9` | not staged (the staged proto-2 one is the compatibility check) |
| `tools/gpu-lane/libdrm-phoenix/build-out-g4/drmprobe` | `19e826ed614c6563` | G4 tests incl. `prime_export_xproc` (`strings -a … \| grep -c prime_export_xproc` = 3) |
| `tools/gpu-lane/libdrm-phoenix/build-out-g4/prefix/lib/libdrm.a` | `2b648f08c887224f` | the snapshot the programs below link (checked in `weston-simple-egl.map`) |
| `tools/gpu-lane/weston-drm/build-out-g4/weston-simple-egl-stripped` | `c1dadf865814a9ae` | Mesa `build-out-wayland` (unchanged) + the G4 libdrm |
| `tools/gpu-lane/weston-drm/build-out-g4/weston-stripped` | `1efe7d7525a7d42a` | built, **not staged**: Weston imports with its proto-2 library, which is itself a compatibility check |
| `tools/gpu-lane/weston-drm/pi/weston-m6a.sh` | `c7107c8d21dd7e65` | `SHARED_SCANOUT` knob (default 0), client trace |

`weston-drm/build-out-g4` was built with `--no-mesa --libdrm-prefix libdrm-phoenix/build-out-g4/prefix`
from the weston-drm sources at `2b575ddaf` (m6e compat fix included) plus the `build.sh` link change.
0 undefined symbols, 0 link warnings beyond the libphoenix attribute notes, no old-lane strings.

### Staging (coordinator)

```
G=/home/houp/phoenix-rpi/tools/gpu-lane
EXPORT=/srv/phoenix-rpi4-nfs-gcc16
sudo -n install -m 755 "$G/v3d-async/out-g4/rpi4-v3d-async"                "$EXPORT/bin/rpi4-v3d-async-g4"
sudo -n install -m 755 "$G/libdrm-phoenix/build-out-g4/drmprobe"            "$EXPORT/bin/drmprobe-g4"
sudo -n install -m 755 "$G/weston-drm/build-out-g4/weston-simple-egl-stripped" "$EXPORT/bin/weston-simple-egl"
sudo -n install -m 755 "$G/weston-drm/pi/weston-m6a.sh"                     "$EXPORT/bin/weston-m6a.sh"
cmp "$G/v3d-async/out-g4/rpi4-v3d-async" "$EXPORT/bin/rpi4-v3d-async-g4"
cmp "$G/libdrm-phoenix/build-out-g4/drmprobe" "$EXPORT/bin/drmprobe-g4"
cmp "$G/weston-drm/build-out-g4/weston-simple-egl-stripped" "$EXPORT/bin/weston-simple-egl"
cmp "$G/weston-drm/pi/weston-m6a.sh" "$EXPORT/bin/weston-m6a.sh"
```

Everything else is unchanged from m6d/m6e: `/bin/weston`, `/bin/shmsrv`, `/bin/rpi4-kms-gate`,
`/bin/kmstest-poll`, `/bin/v3dasync-ping`, the ini. `/bin/weston` may be m6c's or m6e's binary; the G4
path does not depend on which, because both import `ns=v3dbuf` with their proto-2 library. Note which
one is staged (`sha256sum $EXPORT/bin/weston`) for the TERM row. The script change is backward
compatible for m6e (shm clients are untouched). Preconditions as §9: netboot image ≥ build 11 (the E1
kernel), no GPU app and no old-lane `rpi4-v3d` running.

### Cycle `m6g-g4` (Bash `timeout: 600000`)

**Question:** does a render-node BO export on hardware (fd, size, mapping, same-process self-import,
second-client import, cross-process import over `SCM_RIGHTS`, release), and does weston-simple-egl then
put its GPU-rendered frames on HDMI through Weston, with 0012 off?

```
./scripts/test-cycle-psh-interact.sh --label m6g-g4 --idle-secs 45 --max-cmd-secs 150 \
    --hdmi-dense-on 'WESTONDRM client start' -- \
    "/bin/rpi4-v3d-async-g4 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G -p 96" \
    "/bin/shmsrv -v" \
    "/bin/drmprobe-g4 -n 30" \
    "/bin/bash /bin/weston-m6a.sh gl egl noinput" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

`-p 96` is kept, so that only G4 and the unset 0012 variable move against m6d. With G4 the client
buffers no longer come from the kms pool, so `-p 96` can be dropped in a later cycle. Grade:

```
grep -a -E '^(DRMPROBE|V3DA srv (ready|bufns|export|import|v3dbuf)|KMS v3d|WESTONDRM|DRMPHX (conn|ioctl .*PRIME)|MESA|KMSTEST|V3DAPING|SHMSRV stats) |frames in|caught signal|Failed to|dmabuf|Couldn.t' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m6g-g4.log
./scripts/uart-summary.sh m6g-g4
```

Allow for about 1.3 % UART line corruption (re-read the line, don't count it); EL0 dumps print twice.

**Predictions** (tags as printed; `<c>` = a client id, `<h>` = a handle):

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `V3DA srv bufns name=/v3dbuf port=<n> rc=0 registered=1 (G4)`, then `V3DA srv ready … proto=2..3 bufns=1` | once | `proto=2` / no `bufns` line: the m3p2 server was started (staging, `cmp`) — stop. `registered=0`: every export will fail `-ENODEV` |
| 2 | `KMS v3d connect=1 fence_pa=… client=<c> …` | rpi4-kms-gate (proto 2) HELLOs the proto-3 server | `connect=0 why=hello errno=…`: HELLO range check broken — compatibility blocker |
| 3 | drmprobe rows up to `prime_reexport_render … ok=1` as in m5b | unchanged | a regression outside G4: compare with m5b's log |
| 4 | `DRMPROBE prime_export_render rc=0 errno=0 path=/v3dbuf/<h> size=65536 fstat_chr=1 mmap=1 bad_words=0 xwrite=1 reexport_same_name=1 self_import=0 handle=<h>/<h> ok=1`; server: `V3DA srv export handle=<h> client=<c> ns=v3dbuf id=<h> pages=16 …`, `V3DA srv v3dbuf open id=<h> …` / `close` pairs | **G4 export on hardware** | `rc=-1 errno=38`: the library fell back to proto 2 (old server; row 1). `errno=19`: no namespace. `V3DA srv export FAIL … rc=-22`: `memExport` refused the pooled block (E1 `vm_mapObjectRange`: not one contiguous-anonymous entry, or `MAP_NEEDSCOPY`) — blocker, read the kernel. `mmap=0`: the window was not found, or the memory type does not match (the export must be uncached). **`bad_words>0`: the mapping shows other pages (a shadow object) — stop.** `self_import`/handle mismatch: the library short-circuit |
| 5 | `DRMPROBE prime_import_render2 conn=1 rc=0 errno=0 handle=<h> map=1 bad_words=0 survives_creator_close=1 released=1 ok=1`; server `V3DA srv import handle=<h> client=<c2> ns=v3dbuf … owner=<c> self=0 …`, then `V3DA srv export withdrawn handle=<h> live=0` | shared BO, reference rules | `survives…=0`: the creator's close freed a shared BO (refcount) — blocker; `released=0`: a leak |
| 6 | `DRMPROBE prime_export_xproc child pid=… path=/v3dbuf/<h2> import=0 errno=0 handle=<h2>`, then `DRMPROBE prime_export_xproc export=0 errno=0 fork=1 report=1 import=0 import_errno=0 path_ok=1 size_ok=1 fd_map=1 fd_bad=0 handle_map=1 handle_bad=0 xwrite=1 wait_bo=1 released=1 ok=1`; server: import by a third client with `owner=0 self=0 … opens=1` (the parent had closed its handle), then `withdrawn … live=0` | **cross-process dma-buf** with only the descriptor in flight keeping the BO | `import_errno=2` (and a `V3DA srv import FAIL … ns=v3dbuf … rc=-2`): the descriptor in flight did not hold the BO (the `mtOpen` count) — lifetime bug; `path_ok=0`: `SCM_RIGHTS` lost the path; `report=0`: the child died — `uart-summary.sh` for an EL0 dump, `addr2line` on the unstripped `build-out-g4/drmprobe`; `released=0`: a `mtClose` was not delivered (an `open` line without its `close`) |
| 7 | `DRMPROBE RESULT pass=42 fail=0 gap=0 failed=- … verdict=PASS` | m5b's 39 + the three G4 keys | any `failed=` key: its row |
| 8 | Weston up as in m6c arm B: `Using GL renderer`, output `HDMI-A-1`; `WESTONDRM start … shared_scanout=0 …` | as m6c | `shared_scanout=1` or no field: the old script is staged (`cmp`) |
| 9 | client trace: `DRMPHX conn … path=/dev/dri/renderD128 node=render …` (the render node, confirming m6d's inference), `DRMPHX ioctl node=render … name=DRM_IOCTL_PRIME_HANDLE_TO_FD rc=0 errno=0 … fdpath=/v3dbuf/<h>` | the client exports | `rc=-1 errno=38`: the m6d client binary is staged (sha `c1dadf86…`) |
| 10 | per client back buffer (2–4): `V3DA srv export … pages≈2000–2200` (1920×1080 UIF), then `V3DA srv import … ns=v3dbuf … self=0` from Weston's render client | Weston imports each buffer once (EGL dma-buf import → `BO_IMPORT ns=v3dbuf`) | an import `FAIL rc=-2`: the name was withdrawn before Weston imported (lifetime); `Couldn't get size of dmabuf fd` (Mesa): `atSize` not answered; a protocol error on `zwp_linux_buffer_params` in the client (e.g. `invalid buffer stride or height`): Weston's own `lseek(SEEK_END)` check of offset + stride × height against the whole-block size the namespace answers, not the export itself. The `V3DA srv v3dbuf open/close` lines are capped at 64 per server run (every OID mapping opens and closes the name once), so grade Weston's phase by the uncapped `export`/`import … opens=<n>` lines |
| 11 | **no** `MESA: error: Failed to export gem bo` | the m6d stop is gone | present: rows 9–10 say which half |
| 12 | `N frames in 5 seconds: X fps`, X ≈ 20–30 | Weston's repaint rate (m6c: about 27 flips/s) bounds it | < 10: an IPC per frame or a copy (read `DRMPHX`/`V3DA` rates); none: frame callbacks never came (Weston did not attach the buffer) |
| 13 | HDMI | **the rotating RGB triangle, full screen, GPU-rendered by the client** (dense snapshots) | black with the export/import lines present: Weston's EGL import or its sampling of the UIF buffer — Weston's log (`linux_dmabuf`, `EGL`). **Torn/partial triangles are an expected risk** (no cross-process implicit sync, §8), not a G4 failure — note it |
| 14 | no direct scan-out of the client (`drmModeAddFB2` of a foreign buffer would need G7 on card0) | GL composition | — |
| 15 | after the client exits and Weston closes: `V3DA srv export withdrawn … live=0` for each buffer | every export released | `live>0` left: a descriptor or import kept (Weston's `linux_dmabuf` buffer not destroyed) — leak |
| 16 | TERM exit: `caught signal 15` + `weston exited rc=0` if `/bin/weston` is m6e's (`bde137cc…`); the m6c KILL behaviour with m6c's (`2699d5e8…`) | not graded for G4 | — |
| 17 | `SHMSRV stats live=0`, `KMSTEST stats … apply_errors=0 … bos=0 exports=0`, `V3DAPING stats … bos_live=0 parked=0 … verdict=PASS` (the staged proto-2 `v3dasync-ping`) | no leaks; the old ping HELLOs the proto-3 server | `bos_live>0`: an exported BO outlived every reference (rows 5, 6, 15) |
| 18 | fault dumps | 0 kernel, 0 EL0 | EL0 in the server: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/v3d-async/out-g4/rpi4-v3d-async <pc>` |

**Decides:** rows 4–7 PASS = G4 closed on hardware, including the DRM lifetime rules. Rows 9–13 PASS =
wayland-egl clients work on the new lane without 0012. Then 0012 and `-p 96` can be retired: mesa-drm
patch 0012 has to be removed in a separate change, because it re-stamps every Mesa build (§12).

## Result — `m6g-g4` (queue38, 2026-09-27 13:14): ✅ PASS — a Wayland GL client on HDMI

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-131451-m6g-g4.log`; HDMI `artifacts/hdmi/20260927-131928-m6g-g4-tick.png`
(the rotating RGB triangle, fullscreen under kiosk-shell). The `WESTONDRM client start` line was lost to UART
corruption, so dense snapshots never started; the frame is placed by Weston's own timestamps (client 11:19:15–45
Pi time = 13:19 host).

- Server: `V3DA srv bufns name=/v3dbuf … registered=1 (G4)`, `proto=2..3 bufns=1`.
- **drmprobe-g4: `DRMPROBE RESULT pass=42 fail=0 gap=0 … verdict=PASS`** (export, self-import, second-connection
  import, cross-process import, fd mmap).
- Client: `PRIME_HANDLE_TO_FD node=render … rc=0 … fdpath=/v3dbuf/8202` (and 8209, …); Weston imports them
  (`V3DA srv import … ns=v3dbuf … owner=4 self=0`); **no `Failed to export gem bo`** (m6d's stop).
- `weston-simple-egl`: **`151 frames in 5 seconds: 30.2 fps`, `150 … 30.0 fps`**: vsync-paced at two vblanks per
  frame, the same pacing as mig-q2 (→ [frame-pacing.md](frame-pacing.md)).
- Exit: `weston exited rc=0`; every export withdrawn (`export withdrawn … live=0`); 0 exceptions.

**Decides:** G4 works on hardware; Weston composites a GPU client through the GL renderer. 0012 is not needed
(`SHARED_SCANOUT` unset). Next: G7 direct scanout (`m6h-g7`), and pacing.

## 16. G7 — card0 import of a foreign buffer (`KMS_OP_PRIME_IMPORT`), and cycle `m6h-g7`

With G4 a client's GPU buffer is a `/v3dbuf/<id>` descriptor. To put that buffer on a firmware plane
(Weston direct scan-out of a fullscreen client, Present flips of client pixmaps in Xorg, zero-copy HEVC
later), the display server must turn the descriptor into a card0 handle: `DRM_IOCTL_PRIME_FD_TO_HANDLE`
on card0 of a buffer another server allocated. That was gap **G7** (`-ENOSYS`, §15.1 "Not in G4"). This
section closes it in `rpi4-kms` and libdrm-phoenix. Host-tested; the Pi cycle below is pre-registered.

### 16.1 Design

**Protocol 2** (`kms_proto.h`): `KMS_OP_PRIME_IMPORT` = 38 (right after `PRIME_EXPORT`), request
`kms_prime_import_req_t` = the byte layout of `v3da_bo_import_req_t` ({port, cache, id, size, ns}),
reply `kms_dumb_resp_t`. `KMS_PROTO_VERSION` = 2, new `KMS_PROTO_BASE` = 1: the server accepts HELLO
1..2 and replies 2, so every proto-1 binary on the Pi (the staged `weston`, `kmstest-poll`, `drmprobe-*`,
Mesa/SDL/Xorg builds) keeps working. libdrm-phoenix HELLOs 2 and retries with 1 on `EPROTO` (an old
`rpi4-kms-gate`): then a foreign card0 import answers `ENOSYS` locally, exactly as before. `kmstest` now
HELLOs with `KMS_PROTO_BASE` (it uses nothing newer). The `_EXT` definition left
`drm_phoenix_ext.h`.

**Import (`ns=v3dbuf`).** Mapping another server's buffer is IPC (lookup, `open`, `lseek`, page
faults), so the op does not run in `handle_raw` under `srv.lock`: the dispatch loop peeks the opcode;
`op_prime_import` looks up under the lock (a re-import by the same client returns the same handle with
no extra reference, DRM; the client's own `/kmsbuf` export returns its original handle), maps with the
lock dropped, then relocks, rechecks the client (same pid) and installs. The vblank thread never waits
on another server. The mapping half copies `v3da_bo.c import_map`: `lookup("/v3dbuf/<id>")` must name
the port the client resolved, `open(O_RDONLY)`, `lseek(SEEK_END)` (G3) sizes it, `mmap(MAP_SHARED |
MAP_UNCACHED, PROT_READ)` (the export's memory type), every page is faulted in and resolved with
`va2pa`. An import is a new BO kind `KMS_BOK_IMPORT`; it is **never zeroed** (the pool path's memset
would wipe the client's frame). Its memref is the exporter's OID name, so `MAP_DUMB` of the handle maps
`/v3dbuf/<id>` and `PRIME_EXPORT` of it reopens that name (the re-export of an imported GEM object, as
G4a on the render node).

**Lifetime (the one place G7 differs from G1).** The server **keeps the `/v3dbuf/<id>` descriptor open
for the BO's whole life**. `rpi4-v3d-async` counts every open descriptor as a reference on the render BO
(`fd_opens`), while a mapping alone does not stop `bo_unref → export_withdraw → quarantine →
block_put`, and `block_get` zeroes reused blocks. So if the descriptor were closed after `mmap` (as the
render server's own kmsbuf import does), a client that drops its handle and descriptor while its
framebuffer is on screen would get its buffer repooled and zeroed under the HVS. With the descriptor
held, the ordinary rpi4-kms rules do the rest: a framebuffer holds its BO; a plane holds the framebuffer
until the flip that replaces it has completed at a vblank; RMFB of a shown framebuffer disables the
plane; client death drops the handle. When the last reference goes (`kms_bo_unref`), the mapping and the
descriptor are queued and released by `kms_reap()` **outside `srv.lock`** (`close()` is IPC to the
exporter): by the dispatch thread before it answers the request that dropped the reference (so a client
that RMFBs sees the name gone when the call returns), and by the vblank thread after it unlocks when a
completed flip dropped it. Lines: `KMS import client=<c> ns=v3dbuf id=<h> handle=<k> pages=<n>
pa0=0x… contiguous=1 scanout=<0|1> why=<-|above_1g> live=<n>`, `KMS scanout import fb=<f> handle=<k>
id=<h> … (first flip)` (the first commit that handed an import to the firmware), `KMS import released
handle=<k> id=<h> (descriptor closed)`; failures `KMS import FAIL … rc=… why=…` (every one logged,
successes capped at 64 per server run).

**Hardware limits** (`kms_scanout.h`, pure, host-tested):

| Rule | Where | Why |
|---|---|---|
| pages physically contiguous | import: `-EINVAL`, `why=noncontig` | the plane fetches one linear physical range; card0 can do nothing else with the buffer. M1a BOs are one `MAP_CONTIGUOUS` block, so this should never fire [inferred] |
| whole buffer below 1 GiB | import succeeds with `scanout=0 why=above_1g`; **ADDFB2 `-EINVAL`** + `KMS fb FAIL … why=above_1g pa=…` | the firmware scans nothing at or above 1 GiB (E3/E6). The render server's `block_get` does no placement: the kernel manages ~3.8 GiB, pools and BOs have so far landed at 0x06000000–0x3e000000 (every `KMS pool … tries=1 below_1g=1`), which is allocator luck, not a guarantee. Refused at ADDFB2, never at commit: no late `-ERANGE`, no plane fetching memory the firmware cannot reach |
| modifier LINEAR | ADDFB2 `-EINVAL` (the library already refuses non-LINEAR `DRM_MODE_FB_MODIFIERS` requests locally; the server checks again) | the HVS path here scans no Broadcom UIF/SAND/T-tiled layout. A compositor then falls back to GL composition (Weston: `FAILURE_REASONS_ADD_FB_FAILED` → it adds the scanout tranche to the client's dma-buf feedback, `state-propose.c` `dmabuf_feedback_maybe_update` [read]) |
| pitch and offset multiples of 64 B, `offset + pitch × height` within the buffer, pitch ≥ 4 × width, XRGB/ARGB8888 | ADDFB2 `-EINVAL`, `why=align` / `why=size` | what the pool's own BOs get (64-byte rows); the plane scans 32 bpp only |

`ns=kmsbuf` of **another** client's dumb buffer is refused (`-EINVAL`, `why=foreign_kmsbuf`): no current
path needs it (with G4 client buffers come from the render node); a follow-up would alias the BO.
An implicit-modifier ADDFB2 (no `DRM_MODE_FB_MODIFIERS`) of an import is taken as LINEAR, as Linux vc4
does for a buffer without tiling metadata; Mesa v3d allocates `PIPE_BIND_SHARED` buffers linear when no
modifier is given (`v3d_resource.c:900`) [read], and Weston never promotes a `MOD_INVALID` dma-buf to a
plane (`fb.c:397`) [read].

**Implicit sync.** In-process (one program renders on the render node and flips on card0): the render
export now records the BO in the G13 table, so a flip of the card0 import carries the BO's last-use
fence, as a flip of an imported dumb buffer does. **Cross-process (Weston direct scan-out of a client
buffer) has no fence**: the flip does not wait for the client's GPU job. Tearing or a partial frame on
direct scan-out is an expected risk until `BO_LAST_FENCE` or G6 explicit sync (§8). *Later:* **G6** closes this ([G6 doc](G6-cross-process-sync.md)): with a G6 library, a flip of a
card0 import of a render BO asks the render server for the BO's pending fences (`BO_LAST_FENCE`) and
carries the newest as its in-fence.

**Who calls it.** Weston's GL renderer imports every client dma-buf through EGL; Mesa v3d with kmsro
then also imports it on card0 (`renderonly_create_gpu_import_for_resource`, `v3d_resource.c:1107`)
[read] — so with G7 each client buffer gets one card0 import **even without direct scan-out** (it
failed soft with `ENOSYS` before). Weston's direct-scan-out attempt (`drm_fb_get_from_dmabuf`: GBM import
with the client's modifier, `gbm_bo_get_handle_for_plane`, `drmModeAddFB2WithModifiers`, `fb.c:428-480`
[read]) reuses that handle.

Files: `tools/gpu-lane/kms/{kms_proto.h, kms.h, kms_bo.c, kms_main.c, kms_vblank.c, kmstest.c,
kms_scanout.h (new), hosttest/ (new)}`, `tools/gpu-lane/libdrm-phoenix/{src/drm_phoenix_kms.c,
src/drm_phoenix_v3d.c, include/drm_phoenix_ext.h, drmprobe/drmprobe.c, hosttest/run.sh,
hosttest/e2e_main.c, hosttest/mock/fake.c}`. The render server is unchanged.

### 16.2 Tests

`drmprobe` (`build-out-g7`), two new keys:

| key | what it checks |
|---|---|
| `prime_import_card0` | a render BO of the mode's size (1920×1080, 32 bpp, pitch 7680) filled with 8 vertical colour bands (red, orange, yellow, green, cyan, blue, magenta, white) → `PRIME_HANDLE_TO_FD` → card0 `PRIME_FD_TO_HANDLE` (a second import returns the same handle) → `ADDFB2WithModifiers(BROADCOM_UIF)` = `EINVAL`, a half pitch = `EINVAL`, LINEAR XRGB8888 = 0 → page flip + event (the bands on HDMI, held 3 s) → **every client reference dropped while shown** (dma-buf fd, render handle, CPU mapping, card0 handle) → the name `/v3dbuf/<id>` still opens (`alive_while_shown=1`: rpi4-kms holds it) → flip back to a dumb buffer + event → RMFB → the name is gone (`gone_after_errno=2`). An `EINVAL` on the LINEAR ADDFB2 (a buffer above 1 GiB) grades `gap=1`, not a failure |
| `prime_import_card0_neg` | `FD_TO_HANDLE` of descriptor 1000 = `EBADF`, of the render node descriptor = `EINVAL`; a 64 KiB export imports, but a 1920×1080 ADDFB2 of it = `EINVAL`; the name is gone after the cleanup |

**Host harness** (`libdrm-phoenix/hosttest/run.sh`, `DRMPHX_OUT=…/build-out-g7`): the fake display server
now models G7 as `kms_bo.c` does (the import takes one `fd_opens` reference on the fake render BO,
released when the handle is closed and no framebuffer uses it; ADDFB2 applies the real
`kms_scanout.h` rules). Runs and verdicts:

    HOSTTEST libdrm-phoenix checks=134 fails=0 verdict=PASS
    DRMPROBE prime_import_card0 export=0 errno=0 path=/v3dbuf/106497 import=0 import_errno=0 reimport_same=1 handle=3 uif_errno=22 short_pitch_errno=22 addfb=0 addfb_errno=0 shown=1 alive_while_shown=1 flipped_off=1 rmfb=0 gone_after_errno=2 ok=1
    DRMPROBE prime_import_card0_neg badfd_errno=9 notbuf_errno=22 small_import=0 small_import_errno=0 small_addfb_errno=22 released=1 ok=1
    HOSTE2E g7 mode=dri kms_proto=2 import_high=0 card0_imports=2 imports_live=0 imports_released=2
    HOSTE2E legacy verdict=PASS / HOSTE2E dri verdict=PASS (only the fake-GPU pixel checks failed, as expected)
    HOSTE2E g4-negative verdict=PASS (the G4 tests fail against a proto-2 server, the rest as before)

**Can they fail?** Three controls, all in `run.sh`:

1. **Old server** (`FAKE_KMS_PROTO=1`: HELLO exactly 1, no op 38). The library falls back to proto 1,
   every KMS test passes as before, and exactly the two G7 keys fail:

       DRMPROBE prime_import_card0 export=0 … import=-1 import_errno=38 … ok=0
       DRMPROBE prime_import_card0_neg … small_import=-1 small_import_errno=38 … ok=0
       DRMPROBE RESULT pass=37 fail=6 gap=0 failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,prime_import_card0,prime_import_card0_neg, …
       HOSTE2E g7-negative verdict=PASS (the G7 tests fail against a proto-1 display server, the rest as before)

2. **Above 1 GiB** (`FAKE_KMS_IMPORT_HIGH=1`, the case the Pi cannot produce on demand): ADDFB2 `EINVAL`,
   graded `gap=1`, the import still released:

       DRMPROBE prime_import_card0 … import=0 … addfb=-22 addfb_errno=22 shown=0 … gone_after_errno=2 gap=1 (ADDFB2 refused: …) ok=0
       HOSTE2E g7-high verdict=PASS (an import above 1 GiB: ADDFB2 EINVAL, graded gap=1, released)

3. **Lifetime mutation** (done once by hand, not kept): the fake released the import at its handle's
   close even while a framebuffer used it → `alive_while_shown=0 … ok=0`, `HOSTE2E dri verdict=FAIL
   (failed-set g7-import)`. The test sees a buffer that dies while shown.

`tools/gpu-lane/kms/hosttest/run.sh` checks the scan-out rules themselves (17 checks: contiguity, the
1 GiB boundary to the page, wrap, UIF, pitch/offset alignment, sizes), and the same checks built with
`-DSCANOUT_TEST_NO_RULES` ("accept every import") fail 12 of 17 (`KMSHOST negative-control
verdict=PASS`). The server's own import code (the `/v3dbuf` IPC, `va2pa`, the held descriptor, the reap
from both threads) needs the Phoenix kernel and a second server: it is proven only on the Pi.

### 16.3 Artifacts (built 2026-09-27; sha256, first 16 hex)

| file | sha256 | notes |
|---|---|---|
| `tools/gpu-lane/kms/out-g7/rpi4-kms` | `51c9cbcc692e4e6b` | `build.sh --poll-notify --out out-g7` (the `rpi4-kms-gate` recipe: `pollNotify` linked); 0 warnings under `-Werror` |
| `tools/gpu-lane/kms/out-g7/kmstest` | `39e278441676137f` | HELLOs `KMS_PROTO_BASE`; not needed by the cycle |
| `tools/gpu-lane/libdrm-phoenix/build-out-g7/drmprobe` | `f47623e194c76b32` | `strings -a … \| grep -c prime_import_card0` = 5 |
| `tools/gpu-lane/libdrm-phoenix/build-out-g7/prefix/lib/libdrm.a` | `03d30ade6d6335cc` | the snapshot weston-g7 links (`weston-drm/build-out-g7/libdrm-snapshot.txt`) |
| `tools/gpu-lane/weston-drm/build-out-g7/weston-stripped` | `57fc4da3f774da5c` | `build.sh --no-mesa --libdrm-prefix libdrm-phoenix/build-out-g7/prefix --out build-out-g7`; Mesa `build-out-wayland` unchanged; warnings as the G4 build (0 link warnings beyond the libphoenix notes); unstripped `weston` `e85b52ab38be9179` for `addr2line` |

`weston-drm/build-out-g7/weston-simple-egl-stripped` (`f8250a79a6c88ca5`) was rebuilt too but is not
staged: the client uses only the render node, so m6g's G4 client is the right one.

### Staging (coordinator)

Needs §15's staging (`rpi4-v3d-async-g4`, the G4 `weston-simple-egl`, the G4 `weston-m6a.sh`) in place
— stage it first if `m6g-g4` has not run. New names only; nothing staged before is replaced:

```
G=/home/houp/phoenix-rpi/tools/gpu-lane
EXPORT=/srv/phoenix-rpi4-nfs-gcc16
sudo -n install -m 755 "$G/kms/out-g7/rpi4-kms"                         "$EXPORT/bin/rpi4-kms-g7"
sudo -n install -m 755 "$G/libdrm-phoenix/build-out-g7/drmprobe"         "$EXPORT/bin/drmprobe-g7"
sudo -n install -m 755 "$G/weston-drm/build-out-g7/weston-stripped"      "$EXPORT/bin/weston-g7"
cmp "$G/kms/out-g7/rpi4-kms" "$EXPORT/bin/rpi4-kms-g7"
cmp "$G/libdrm-phoenix/build-out-g7/drmprobe" "$EXPORT/bin/drmprobe-g7"
cmp "$G/weston-drm/build-out-g7/weston-stripped" "$EXPORT/bin/weston-g7"
cmp "$G/weston-drm/pi/weston-m6a.sh" "$EXPORT/bin/weston-m6a.sh"      # the G4 script (WESTON= knob)
```

Preconditions as §9: netboot image ≥ build 11, no GPU app, no X, no old-lane `rpi4-v3d`.

### Cycle `m6h-g7` (Bash `timeout: 600000`)

**Question:** does rpi4-kms scan out a render-node BO imported on card0 — import, refusals, flip,
survival while every client reference is gone, release after flip-off — and does Weston then put a
fullscreen weston-simple-egl client on the primary plane directly (direct scan-out) instead of
compositing it with GL?

```
./scripts/test-cycle-psh-interact.sh --label m6h-g7 --idle-secs 45 --max-cmd-secs 150 \
    --hdmi-dense-on 'DRMPROBE kms_flip start|WESTONDRM client start' -- \
    "/bin/rpi4-v3d-async-g4 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96" \
    "/bin/shmsrv -v" \
    "/bin/drmprobe-g7 -n 30" \
    "export WESTON=/bin/weston-g7" \
    "/bin/bash /bin/weston-m6a.sh gl egl noinput" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

`-p 96` as m6g (only G7 moves against it). `weston-m6a.sh` honours `WESTON=`; psh has `export`. Grade:

```
grep -a -E '^(DRMPROBE|KMS (srv (ready|flipstat|client)|v3d|import|scanout|fb FAIL)|V3DA srv (ready|bufns|export|import|v3dbuf)|WESTONDRM|DRMPHX (conn|ioctl .*(PRIME|ADDFB2))|MESA|KMSTEST|V3DAPING|SHMSRV stats) |frames in|caught signal|Failed to|dmabuf' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m6h-g7.log
./scripts/uart-summary.sh m6h-g7
```

Allow ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice. `<k>` = a card0
handle, `<h>` = a render handle / `/v3dbuf` id, `<c>` = a kms client id.

**Predictions:**

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `KMS srv ready … proto=1..2 import=v3dbuf`, `KMS v3d connect=1 …`; `V3DA srv bufns … registered=1 (G4)` | once each | `proto=1` / no `import=` field: the old server is staged (`cmp`) — stop |
| 2 | drmprobe rows up to `prime_export_xproc … ok=1` as predicted for m6g (§15 rows 3–6) | unchanged | a regression outside G7: compare with m6g's log |
| 3 | `DRMPROBE prime_import_card0 shown=1 fb=<f> handle=<k> path=/v3dbuf/<h> (HDMI: 8 vertical colour bands)`; before it `V3DA srv export handle=<h> … pages=2025 …`, `V3DA srv v3dbuf open id=<h> pid=<rpi4-kms pid> …`, **`KMS import client=<c> ns=v3dbuf id=<h> handle=<k> pages=2025 pa0=0x… contiguous=1 scanout=1 why=- live=1`**, a `KMS fb FAIL … pitch=3840 … why=size` (the half-pitch probe; the UIF probe is refused by the library, no server line), then **`KMS scanout import fb=<f> handle=<k> id=<h> pa0=0x… size=8294400 (first flip)`** | **G7 import and direct scan-out of a render BO on hardware**; `pa0` below `0x40000000` | `import=-1 import_errno=38`: the library fell back to proto 1 (row 1). `import_errno=2` + `KMS import FAIL … why=map rc=-2`: rpi4-kms could not open `/v3dbuf/<h>` (name/port). `why=map rc=-22`: `lseek`/`mmap` refused (E1 memory type). `why=noncontig`: the M1a one-block assumption is broken — blocker for direct scan-out of render BOs. `addfb_errno=22` + `KMS fb FAIL … why=above_1g pa=0x…`: the BO landed above 1 GiB — **graded gap=1**, note the PA (allocator placement is the follow-up) |
| 4 | HDMI (dense snapshots after `kms_flip start`): **8 vertical bands red, orange, yellow, green, cyan, blue, magenta, white** for ~3 s, full screen | the render BO's pixels scanned by the firmware | bands shifted/sheared: pitch mapping; red↔blue swapped: pixel order (XRGB vs the firmware type); black: the plane shows other pages (import mapping) — stop; the teal/magenta dumb frame instead: the flip did not land |
| 5 | `DRMPROBE prime_import_card0 export=0 errno=0 path=/v3dbuf/<h> import=0 import_errno=0 reimport_same=1 handle=<k> uif_errno=22 short_pitch_errno=22 addfb=0 addfb_errno=0 shown=1 alive_while_shown=1 flipped_off=1 rmfb=0 gone_after_errno=2 ok=1`; server order **after** row 3's `shown` line: `KMS import released handle=<k> id=<h> (descriptor closed)`, `V3DA srv v3dbuf close id=<h> pid=<rpi4-kms pid> opens=0 …` (the open/close lines are capped at 64 per render-server run, §15 row 10 — grade by the uncapped lines if it is missing), `V3DA srv export withdrawn handle=<h> live=0` | **the buffer outlives every client reference while shown, and goes at RMFB after flip-off** | `alive_while_shown=0` (and/or the bands turn black during the hold): the name died while scanned — lifetime bug, blocker. `gone_after_errno=0` / no `import released` line: rpi4-kms leaked the descriptor (reap not run) |
| 6 | `DRMPROBE prime_import_card0_neg badfd_errno=9 notbuf_errno=22 small_import=0 small_import_errno=0 small_addfb_errno=22 released=1 ok=1`, with `KMS fb FAIL … 1920x1080 pitch=7680 … size=65536 rc=-22 why=size` | refusals | `badfd_errno` ≠ 9: Phoenix `sys_fdpath` answers another errno for an unused descriptor (library mapping, not G7) — note |
| 7 | `DRMPROBE RESULT pass=44 fail=0 gap=0 failed=- … verdict=PASS` (m6g's 42 + the two G7 keys); `pass=43 … gap=1` with row 3's `above_1g` | as listed | any `failed=` key: its row |
| 8 | `WESTONDRM start renderer=gl client=egl weston=/bin/weston-g7 …`; Weston up as in m6g (`Using GL renderer`, `HDMI-A-1`) | the export reached the script | `weston=/bin/weston`: psh's `export` did not reach bash — then the G4 Weston (proto 1) ran and rows 9–12 read as m6g (card0 import `errno=38`) |
| 9 | Weston's trace: `DRMPHX ioctl node=card0 … name=DRM_IOCTL_PRIME_FD_TO_HANDLE rc=0 errno=0 n=1 handle=<k> … fdpath=/v3dbuf/<h>` (m6g: `rc=-1 errno=38`) and one `KMS import client=<weston's c> ns=v3dbuf id=<h> … scanout=1 …` per client buffer (2–4) | Mesa kmsro's card0 import of every client buffer Weston's EGL imports (§16.1 "Who calls it") | `KMS import FAIL`: as row 3 |
| 10 | **direct scan-out**, one of two paths: (a) the client's buffers are LINEAR from the start: `DRMPHX ioctl node=card0 … name=DRM_IOCTL_MODE_ADDFB2 rc=0 … 1920x1080 … flags=0x2 … pitch=7680 … mod=0x0` and `KMS scanout import fb=<f> … (first flip)` within ~2 s of `client start`; (b) **more likely**: they are UIF first (m6g row 10's `pages≈2000–2200`): `DRM_IOCTL_MODE_ADDFB2 rc=-1 errno=22 … mod=0x700000000000006` (refused in the library, no server line), Weston keeps compositing and after ~2 s adds the scanout tranche to the client's dma-buf feedback (`dmabuf_feedback_maybe_update`, `ADD_FB_FAILED`); Mesa re-allocates with `__DRI_IMAGE_USE_SCANOUT` + LINEAR (`platform_wayland.c:1234`) [read]: new `V3DA srv export …` + `KMS import …` lines, then (a). Weston itself prints nothing about planes (its `drm-backend` debug scope is off) | the `KMS scanout import … (first flip)` line with `client=<weston's c>` is the direct scan-out proof | only UIF `ADDFB2 … errno=22` lines and no re-allocation within the 30 s hold: the client ignored the scanout tranche (Mesa's feedback path) — GL composition, G7 still proven by rows 3–5; `ADDFB2 rc=-1 errno=22` with `mod=0x0` + `KMS fb FAIL … why=align`: the linear stride is not 64-aligned — note the pitch |
| 11 | `KMS srv flipstat client=<weston's c> flips=N … deferred=D …` at Weston's exit: **D ≪ N** (direct-scan-out flips carry no render fence; m6c arm B, all GL: `flips=805 deferred=783`) | Weston stopped compositing once the client was on the plane | D ≈ N: Weston composited all the time (row 10 did not happen) |
| 12 | `N frames in 5 seconds: X fps`, X ≥ m6g's; HDMI: the rotating triangle full screen | frame callbacks keep coming on the plane path | **tearing / partial triangles are an expected risk**: a direct-scan-out flip does not wait for the client's GPU job (no cross-process implicit sync, §16.1) — note, not a G7 failure |
| 13 | exit: `KMS import released …` for every import, `V3DA srv export withdrawn … live=0` for every client buffer, `caught signal 15` + `weston exited rc=0` (weston-g7 carries the m6e fix; not graded for G7) | no descriptor left | an `import` without its `released`: an RMFB/GEM_CLOSE path that skipped the reap — leak |
| 14 | `SHMSRV stats live=0`, `KMSTEST stats … apply_errors=0 … bos=0 exports=0` (imports count in `bos`), `V3DAPING stats … bos_live=0 parked=0 … verdict=PASS` | no leaks; the proto-1 `kmstest-poll` HELLOs the proto-2 server | `bos>0`: an import outlived Weston; `bos_live>0`: a render BO still referenced (row 13) |
| 15 | fault dumps | 0 kernel, 0 EL0 | EL0 in rpi4-kms: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/kms/out-g7/rpi4-kms <pc>`; in Weston: the unstripped `weston-drm/build-out-g7/weston` |

**Decides:** rows 3–7 PASS = G7 closed on hardware (import, the 1 GiB rule, refusals and the
on-screen lifetime). Rows 9–11 PASS = Weston direct scan-out of a GPU client works; M4's Present flips
of client pixmaps need only G7 + (for tear-free) cross-process implicit sync (`BO_LAST_FENCE`), which is
the next gap.

## Result — `m6h-g7` (queue41, 2026-09-27 14:44): ✅ PASS — direct scanout of client buffers, 45 fps

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-144453-m6h-g7.log` (pre-boot UART flood; boot normal); HDMI
`artifacts/hdmi/20260927-145037-m6h-g7-tick.png` (the triangle, clean).

- **drmprobe-g7: `DRMPROBE RESULT pass=44 fail=0 gap=0 … verdict=PASS`**: `prime_import_card0 … shown=1 …
  alive_while_shown=1` (a render-node buffer imported on card0 and flipped onto the plane, 8 colour bands), UIF and
  short-pitch ADDFB2 refused (`errno 22`), negatives `badfd=9 notbuf=22 small_addfb=22`.
- Weston (`weston-g7`, `shared_scanout=0`): **`KMS scanout import fb=… (first flip)` ×4** = client buffers
  scanned out directly, no GL composition. Two client buffers landed above 1 GiB and were refused cleanly
  (`KMS fb FAIL … why=above_1g rc=-22`); Weston fell back to composition for those, as designed.
- `weston-simple-egl`: `8.4` (the window with the imports), then **`45.2`, `45.0`, `45.0` fps** (m6g, GL
  composition: 30.0). `KMS srv flipstat flips=907 vbl1=614 vbl2=293 deferred=285 applied_gate=285 dropped_events=0`.
- Exit `weston exited rc=0 after_term_s=1`; `KMSTEST … apply_errors=0 dropped=0 bos=0 exports=0`;
  `V3DAPING bos_live=0`; 0 exceptions.

**Decides:** G7 works on hardware. Direct scanout lifts the GPU client from 30 to 45 fps. Placement above 1 GiB
is a real, measured case (2 of ~6 client buffers), so render-server placement below 1 GiB for shareable BOs is
worth doing. No tearing seen, though the cross-process fence (G6) is not in yet (agent).

## 17. G6 — cross-process implicit sync, and cycle `g6-sync`

Design, protocol (render server proto 4: `BO_LAST_FENCE`, `BO_ATTACH_FENCE`, implicit dependencies),
tests (drmprobe `dmabuf_sync_{probe,import,read,flip}`; host fail-then-pass against a proto-3 fake),
artifacts, staging and the pre-registered cycle `g6-sync` (servers → `drmprobe-g6` →
`weston-m6a-g6.sh gl egl noinput` with `weston-g6` + `weston-simple-egl-g6`) are in
[G6-cross-process-sync.md](G6-cross-process-sync.md).

## 18. Scanout placement below 1 GiB (render server proto 5), and cycle `m6i-low`

(Numbered 18: §17 is the G6 pointer.) In m6h-g7 one of the client's linear buffers came from a block at
`pa=0xf8000000` and card0 refused it (`KMS fb FAIL … pa=0xf8000000 … why=above_1g`). Weston composited that
buffer with GL while the other linear buffers went straight to the plane. The firmware plane fetches
nothing at or above 1 GiB, and the render server placed its blocks wherever the kernel put them. This
section makes the buffers that may be scanned out come from below 1 GiB. It is host-tested; the Pi cycle
below is pre-registered.

### 18.1 Design

**Why a hint from the client.** The other options fail on facts in the tree:

| option | why not |
|---|---|
| kernel support for an address limit | there is none. `vm_objectContiguous` → `vm_pageAlloc` → `_page_alloc` (`vm/page.c`) is a buddy allocator that takes the first free block of the order, with no address argument. Adding one would change the kernel |
| migrate at `BO_EXPORT` | the wayland-egl platform exports a buffer **after** its first frame: `create_wl_buffer` → `__DRI_IMAGE_ATTRIB_FD` runs in `dri2_wl_swap_buffers_with_damage` (`platform_wayland.c:1886`), after `dri2_flush_drawable_for_swapbuffers` submitted the render. Migrating would mean waiting for that job and copying 8 MB through an uncached mapping. It would also break the client's own mapping: memrefs are `V3DA_MEM_PHYS`, so the client maps the old physical pages directly |
| server heuristic (big BOs low) | useless. In m6h the UIF first round (never scannable, the library refuses `mod=0x0700000000000006` at ADDFB2) and the linear second round are **both 2026 pages**. Games allocate many large textures too |

Mesa knows which buffers can be scanned out. Weston's scan-out dma-buf feedback tranche makes it
re-allocate with `__DRI_IMAGE_USE_SCANOUT` (`platform_wayland.c:1237`), which `dri2.c:984` turns into
`PIPE_BIND_SCANOUT`. The hint is therefore keyed on `PIPE_BIND_SCANOUT` only, not on `SHARED` (the UIF
round is `SHARED`).

**The chain.**

1. **mesa-drm patch 0016** (`v3d_resource.c`, `v3d_bufmgr.[ch]`). On Phoenix, `v3d_resource_bo_alloc` of a
   `PIPE_BIND_SCANOUT` resource calls the new `v3d_bo_alloc_flags(…, V3D_PHOENIX_CREATE_BO_SCANOUT)`, which
   puts `1u << 31` in `drm_v3d_create_bo.flags`. The renderonly path (a compositor with kmsro) never gets
   here: it allocates on card0, in the kms pool. Such BOs bypass the BO cache, because a cached BO may lie
   anywhere. If the library answers `EINVAL` (an older libdrm), Mesa retries without the flag, so the
   placement is only a hint. Linux never sets the flag.
2. **libdrm-phoenix** (`drm_phoenix_v3d.c ioc_create_bo`, `drm_phoenix_ext.h DRM_PHOENIX_V3D_CREATE_BO_SCANOUT`).
   The DRM flag becomes the wire flag `V3DA_BO_LOWMEM` (bit 2), and only when the server's HELLO says 5 or
   more; against an older server it is dropped. Any other flag is still `EINVAL`, as on Linux.
3. **Protocol 5** (`v3da_proto.h`): `V3DA_PROTO_VERSION` = 5, `V3DA_PROTO_BO_LOWMEM` = 5, `V3DA_BO_LOWMEM`,
   `V3DA_LOWMEM_LIMIT` = 1 GiB (= `KMS_SCANOUT_LIMIT`). The server accepts HELLO 2..5. Servers before 5
   ignored unknown create flags, so even an unconditional flag would have been harmless. The G6b
   descriptor ops reserved "proto ≥ 5" in `drm_phoenix_ext.h`; that is now ≥ 6.
4. **Server placement** (`v3da_lowmem.h`, pure and host-tested; used by `v3da_bo.c block_get`). A
   `LOWMEM` BO takes, in order:
   - a **pooled** block of its size and memory type that lies below 1 GiB;
   - otherwise up to **16 fresh** `MAP_CONTIGUOUS` blocks until one lands low. This is the retry that
     `kms_pool_init` does for the kms pool. The rejects are **held** while trying, so each try gets a
     different buddy block, and are then handed back to the kernel (safe since kernel build 8, E1 §6).
     They are counted as `rejected`, **not** in `pages_to_kernel`, which the ping's verdict requires to
     be 0. Only the first and last page of a reject are touched (for `va2pa`); only the kept block is
     zeroed;
   - if no low block turns up, the last contiguous one is kept. The BO is created anyway and card0 refuses
     it as before, logged as a `FALLBACK`.

   The budget is **`-L` MiB, default 64**: the low memory that live and quarantined `LOWMEM` BOs may hold,
   counted as the buddy footprint. A 2026-page 1080p buffer is one **8 MiB** block, so the default is
   eight buffers. Over budget, a BO takes the ordinary path (`why=budget`); `-L 0` = the pre-proto-5
   behaviour. To keep low blocks for the next scan-out BO, an **ordinary** BO now takes a pooled block
   **above** 1 GiB when the pool has both.

**Lines** (tagged, `V3DA srv low …` capped at 64 per server run, counters uncapped):

    V3DA srv ready … proto=2..5 bufns=1 lowmem_mib=64
    V3DA srv low BO handle=0x… client=<c> pages=2026 pa=0x… src=pool|fresh tries=<t> rejected=<r> low=<N>/65536KiB peak=<P>KiB bos=<n> from_pool=<p>
    V3DA srv low FALLBACK handle=0x… client=<c> pages=… pa=0x… below_1g=0|1 why=budget|tries tries=… rejected=… low=…/…KiB fallbacks=<f>
    V3DA srv low FALLBACK handle=- client=<c> pages=… why=nomem …               (no memory at all: BO_CREATE -ENOMEM)
    V3DA srv qstat … low=<N>/<M>KiB lowbos=<n> lowfb=<f>                       (the qstat counter)
    V3DA srv low stats client=<c> live=<N>/<M>KiB peak=… bos=… from_pool=… fallbacks=… tries=… rejected=…   (at a client close, when changed)

**Compatibility.**

| binary | against the proto-5 server | against a proto-4 (G6) server |
|---|---|---|
| everything staged (proto 2: `rpi4-kms-g7 -G`, `v3dasync-ping`, games; proto 3/4: `drmprobe-g4/-g7/-g6`, `weston-g7/-g6`, the G4/G6 clients) | unchanged (HELLO accepted; they never set the flag, so placement is as before, except that ordinary BOs now prefer high pooled blocks) | — |
| new library + Mesa 0016 (`drmprobe-low`, `weston-simple-egl-low`) | HELLO 5: scan-out BOs placed low | HELLO 5 → `EPROTO` → 2 (reply 4): the flag is dropped, placement anywhere (host control `lowmem-negative`) |
| Mesa 0016 objects + a pre-proto-5 `libdrm.a` | — (never built: libdrm is static per binary, and weston-drm links the `--libdrm-prefix` snapshot) | the library answers `EINVAL`, Mesa retries without the flag |

Files: `tools/gpu-lane/v3d-async/{v3da_proto.h, v3da_lowmem.h (new), v3da.h, v3da_bo.c, v3da_jobs.c,
v3da_main.c, hosttest/ (new)}`, `tools/gpu-lane/libdrm-phoenix/{src/drm_phoenix_v3d.c,
include/drm_phoenix_ext.h, drmprobe/drmprobe.c, hosttest/{run.sh, e2e_main.c, mock/fake.c}}`,
`tools/gpu-lane/mesa-drm/patches/mesa/0016-v3d-Phoenix-RTOS-place-scanout-BOs-below-1-GiB.patch`.

### 18.2 Tests

**Server policy** (`tools/gpu-lane/v3d-async/hosttest/run.sh`, native + ASan/UBSan). A mock allocator hands
out scripted blocks (low, high, torn, none). The checks: footprint (a 2026-page block is 8 MiB; the first
run caught an expectation of 16 MiB: 2026 × 4 KiB < 8 MiB), the limit to the byte, the budget, pool
choice (LOWMEM only low, ordinary prefers high), fresh blocks (kept block, `tries`, `rejected`, rejects
**held while trying** (`held_max`), every non-kept block unmapped exactly once, torn blocks never kept,
fallback after 16, a block `va2pa` could not resolve never kept). Result `LOWHOST RESULT checks=45 fails=0
verdict=PASS`. **Negative control**: `-DLOWMEM_TEST_NO_POLICY` (take the first block, as before proto 5)
fails 14 of 45 → `LOWHOST negative-control verdict=PASS`. The unresolved-`va2pa` check was also mutated by
hand (the `first != UINT64_MAX` test removed): 2 checks fail.

**drmprobe `scanout_lowmem`** (`build-out-low`). A render BO of the mode's frame + one page (2026 pages at
1080p, like a Mesa client buffer) created **with** the hint → export → card0 import → LINEAR ADDFB2 must
succeed. A plain BO of the same size is tried for comparison; its verdict depends on where its block
landed, so it is informational on the Pi. An unknown create flag must be `EINVAL`.

**Host harness** (`DRMPHX_OUT=…/build-out-low libdrm-phoenix/hosttest/run.sh`). The fake render server now
models placement. `FAKE_V3DA_HIGH=1` reports every BO it did not place above 1 GiB (the same arena,
`PA_HIGH`), and the fake display server's import applies the real `kms_import_why` to that address. That
is m6h's case, on demand. Results [host]:

    HOSTTEST libdrm-phoenix checks=134 fails=0 verdict=PASS
    HOSTE2E legacy / dri verdict=PASS            (scanout_lowmem … low_addfb_errno=0 plain_addfb_errno=0 ok=1, lowmem_bos=1)
    DRMPROBE scanout_lowmem 1920x1080 pages=2026 bogus_flag_errno=22 create=0 low_addfb_errno=0 plain_create=0 plain_addfb_errno=22 ok=1
    HOSTE2E lowmem-high verdict=PASS             (FAKE_V3DA_HIGH=1: the hinted BO placed and taken, the plain one and the
                                                  G7/G6-flip BOs refused, gap=2)
    DRMPROBE scanout_lowmem … low_addfb_errno=22 … plain_addfb_errno=22 (the placed BO was refused: …) ok=0
    HOSTE2E lowmem-negative verdict=PASS         (FAKE_V3DA_HIGH=1 FAKE_V3DA_PROTO=4: the library falls back, drops the
                                                  hint, the buffer lands high - scanout_lowmem FAILS; lowmem_bos=0)
    HOSTE2E g4-negative / g7-negative / g7-high / g6-negative verdict=PASS   (expected sets updated: scanout_lowmem
                                                  needs G4 + G7; card0 import counts 3 → 5)

**Not host-testable:** Mesa 0016 (no host Mesa). Its proof is on the Pi: `V3DA srv low BO … client=<the egl
client>` lines. The client's `DRMPHX` trace of `CREATE_BO … flags=0x80000000` is capped at the first 16
calls per request number, so it will probably not show the second round. Also not host-testable: the
server's `mmap`/`va2pa`/pool/quarantine accounting (needs the Phoenix kernel). `rpi4-kms` compiles
against the new header (`kms/out-lowchk`, not staged).

### 18.3 Artifacts (built 2026-09-27; sha256, first 16 hex)

| file | sha256 | notes |
|---|---|---|
| `tools/gpu-lane/v3d-async/out-low/rpi4-v3d-async` | `fe27bea1b44f4819` | server, proto 5 (G6 sources + placement); `-Werror`; `strings -a … \| grep -c 'V3DA srv low'` = 4 |
| `tools/gpu-lane/v3d-async/out-low/v3dasync-ping` | `94df667c929bab43` | not staged |
| `tools/gpu-lane/libdrm-phoenix/build-out-low/drmprobe` | `4521658f7b233ee3` | G6 probe + `scanout_lowmem` (`strings -a … \| grep -c scanout_lowmem` = 3) |
| `tools/gpu-lane/libdrm-phoenix/build-out-low/prefix/lib/libdrm.a` | `cda443dc8dfd6bce` | the flag mapping; the snapshot Mesa and Weston link (9 pre-existing compiler warnings, as G6/G7) |
| `tools/gpu-lane/mesa-drm/patches/mesa/0016-…patch` | `76a239da4dc30777` | applies after 0001–0015 (`git apply --check`) |
| `tools/gpu-lane/mesa-drm/build-out-wayland-low/` (`libgallium-26.2.0.a`) | `fea4df158f17a3d8` | `mesa-drm/build.sh --wayland --out …/build-out-wayland-low --libdrm-prefix libdrm-phoenix/build-out-low/prefix`; patch set stamp `4a457a1efe6f3903`; no warning in the patched files (59 warning lines vs 56 in `build-out-wayland`: three extra in `threads_posix.c` / `blake3.c`, untouched code) |
| `tools/gpu-lane/weston-drm/build-out-low/weston-simple-egl-stripped` | `feb43b9bfaad3a20` | `weston-drm/build.sh --no-mesa --mesa-out …/build-out-wayland-low --libdrm-prefix …/build-out-low/prefix --out …/build-out-low`; `nm` shows `v3d_bo_alloc_flags`; the map names only `build-out-wayland-low` archives; 0 link warnings beyond the libphoenix notes; unstripped `4b3247c298666c43`. **The Phoenix branch is compiled in**: `strings -a weston-simple-egl \| grep -c V3D_PHOENIX_SHARED_SCANOUT` = 1 (0012's literal in the same `#if DETECT_OS_PHOENIX` file), and `objdump -d v3d_resource.c.o` shows `v3d_resource_bo_alloc` computing the flag as `ubfx x3, x3, #19, #1; lsl w3, w3, #31` (bind bit 19 = `PIPE_BIND_SCANOUT` → bit 31) before `bl v3d_bo_alloc_flags` |
| `tools/gpu-lane/weston-drm/build-out-low/weston-stripped` | `33cd2d1a8f4ffba9` | built, **not staged** (the compositor's scan-out buffers come from card0 through kmsro and never reach patch 0016; the cycle keeps `weston-g6`) |

Frozen copies under the staged names: `/home/houp/.claude/jobs/c8f1289c/tmp/low-frozen/` (same sha).

### Staging (coordinator)

Needs G6 §8's staging in place (`weston-g6`, `weston-m6a-g6.sh` = `b5dc486c…`, unchanged) plus M6 §16's
(`rpi4-kms-g7`, `shmsrv`, the ini). New names only:

```
F=/home/houp/.claude/jobs/c8f1289c/tmp/low-frozen
EXPORT=/srv/phoenix-rpi4-nfs-gcc16
sudo -n install -m 755 "$F/rpi4-v3d-async-low"    "$EXPORT/bin/rpi4-v3d-async-low"
sudo -n install -m 755 "$F/drmprobe-low"          "$EXPORT/bin/drmprobe-low"
sudo -n install -m 755 "$F/weston-simple-egl-low" "$EXPORT/bin/weston-simple-egl-low"
cmp "$F/rpi4-v3d-async-low"    "$EXPORT/bin/rpi4-v3d-async-low"
cmp "$F/drmprobe-low"          "$EXPORT/bin/drmprobe-low"
cmp "$F/weston-simple-egl-low" "$EXPORT/bin/weston-simple-egl-low"
cmp /home/houp/phoenix-rpi/tools/gpu-lane/weston-drm/build-out-g6/weston-stripped "$EXPORT/bin/weston-g6"
cmp /home/houp/phoenix-rpi/tools/gpu-lane/weston-drm/pi/weston-m6a.sh "$EXPORT/bin/weston-m6a-g6.sh"
```

Preconditions as §9: netboot image ≥ build 11, no GPU app, no X, no old-lane `rpi4-v3d`. **Order: after
`g6-sync` (queue48).** The new server carries all of G6. If `g6-sync` has not run, a G6 failure shows up
here too; grade G6 rows by the G6 doc, and do not read them as placement faults.

### Cycle `m6i-low` (Bash `timeout: 600000`)

**Question:** does the render server put every buffer that may be scanned out below 1 GiB? Specifically,
drmprobe's hinted BO, and each linear back buffer that weston-simple-egl re-allocates for Weston's
scan-out tranche. Does Weston then scan out **every** such client buffer directly, with no `above_1g`
refusal, at ≥ 45 fps in every steady window?

```
./scripts/test-cycle-psh-interact.sh --label m6i-low --idle-secs 45 --max-cmd-secs 150 \
    --hdmi-dense-on 'DRMPROBE kms_flip start|WESTONDRM client start' -- \
    "/bin/rpi4-v3d-async-low -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96" \
    "/bin/shmsrv -v" \
    "/bin/drmprobe-low -n 30 -g 1024" \
    "export WESTON=/bin/weston-g6" \
    "export EGL_CLIENT=/bin/weston-simple-egl-low" \
    "/bin/bash /bin/weston-m6a-g6.sh gl egl noinput" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

Against `g6-sync`, only the server (+ placement), the probe (+ one key) and the client (Mesa 0016 +
the proto-5 library) change. The m6h reference: `artifacts/rpi4b-uart/rpi4b-uart-20260927-144453-m6h-g7.log`
(`KMS fb FAIL … pa=0xf8000000 … why=above_1g`, 45.2/45.0/45.0 fps). Grade:

```
grep -a -E '^(DRMPROBE|V3DA srv (ready|bufns|low|export|export withdrawn|g6 stats)|V3DA srv qstat|KMS (srv (ready|flipstat)|v3d|import|scanout|fb FAIL)|DRMPHX ioctl .*(ADDFB2|CREATE_BO .*flags=0x8)|WESTONDRM|MESA|KMSTEST|V3DAPING|SHMSRV stats) |frames in|caught signal|Failed to' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m6i-low.log
./scripts/uart-summary.sh m6i-low
```

Allow ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice. `<e>` = the egl client's
render client id; `<h>` = a render handle / `/v3dbuf` id; "low PA" = below `0x40000000`.

**Predictions:**

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `V3DA srv ready … proto=2..5 bufns=1 lowmem_mib=64`, `V3DA srv bufns … registered=1 (G4)` | once | `proto=2..4` / no `lowmem_mib`: the G6 server was started (staging, `cmp`) — stop |
| 2 | `KMS v3d connect=1 …` (proto-2 HELLO), `KMS srv ready … proto=1..2 import=v3dbuf` | as m6h | `connect=0 why=hello`: the HELLO range — compatibility blocker |
| 3 | drmprobe rows as `g6-sync` predicts (m6h's 44 + the 4 G6 keys) | unchanged | a regression outside placement: compare with the g6-sync log |
| 4 | `DRMPROBE scanout_lowmem 1920x1080 pages=2026 bogus_flag_errno=22 create=0 low_addfb_errno=0 plain_create=0 plain_addfb_errno=<0\|22> ok=1`; server `V3DA srv low BO handle=<h> client=<drmprobe's c> pages=2026 pa=<low PA> src=<pool\|fresh> tries=<0..16> rejected=<tries−1 or 0> low=8192/65536KiB …`; `KMS import … id=<h> … scanout=1 why=- …` | **the hint reaches the server and the block is low** | `low_addfb_errno=22` + `KMS fb FAIL … why=above_1g`: the BO was not placed — with a `V3DA srv low FALLBACK … why=tries`: 16 fresh blocks all high (low memory exhausted of 8 MiB blocks — note `tries`/`rejected`, then re-run with a smaller `-p` kms pool); with **no** `V3DA srv low` line: the flag did not arrive (library proto fallback: row 1) — blocker. `plain_addfb_errno=22`: the plain BO landed high by chance — informational only, **not** counted in row 9 |
| 5 | `DRMPROBE RESULT pass=49 fail=0 … verdict=PASS` (g6-sync's 48 + `scanout_lowmem`); `gap=1..2` allowed if `prime_import_card0` / `dmabuf_sync_flip` (unhinted BOs) landed high | as listed | any `failed=` key: its row |
| 6 | `WESTONDRM start renderer=gl client=egl weston=/bin/weston-g6 … egl_client=/bin/weston-simple-egl-low` | the exports reached the script | `egl_client=/bin/weston-simple-egl`: psh's `export` did not reach bash — the old client ran, rows 7–10 read as m6h |
| 7 | client round 1 (UIF, `SHARED` only): `V3DA srv export … client=<e> … pages=2026` for handles with **no** `V3DA srv low` line; Weston's `DRM_IOCTL_MODE_ADDFB2 rc=-1 errno=22 … mod=0x700000000000006` | as m6h: the UIF round is not hinted (it can never be scanned out) | `V3DA srv low BO … client=<e>` for UIF handles: Mesa sets `PIPE_BIND_SCANOUT` on the UIF round — budget pressure, note |
| 8 | client round 2 (linear, after the scan-out tranche): **per buffer (3–4)** `V3DA srv low BO handle=<h> client=<e> pages=2026 pa=<low PA> …`, then `V3DA srv export handle=<h> … pa=<same PA>`, `KMS import … id=<h> … scanout=1 why=-`, Weston's `ADDFB2 rc=0 … mod=0x0`, `KMS scanout import fb=<f> handle=<k> id=<h> pa0=<same PA> … (first flip)`; **0 `V3DA srv low FALLBACK`** | **every client buffer that may be scanned out is placed low and scanned out** | no `V3DA srv low` line for `client=<e>` and linear exports still at any PA: Mesa 0016 is not in the staged client (`sha256sum`, `strings`) or `__DRI_IMAGE_USE_SCANOUT` did not reach `v3d_resource_bo_alloc` — the design's assumption, blocker; `FALLBACK why=budget`: more than eight live hinted buffers (read `low=`), raise `-L`; `why=tries`: as row 4 |
| 9 | **0 `KMS fb FAIL … why=above_1g`** for Weston's kms client (m6h: 1, `pa=0xf8000000`) | zero | one: read its `import_id` against row 8. Placed low but refused = a rule mismatch (`kms_scanout.h` vs `V3DA_LOWMEM_LIMIT`); not placed = row 8 |
| 10 | `N frames in 5 seconds: X fps`: **X ≥ 45 in every steady window**, i.e. every window after the first two following `WESTONDRM client start` (m6h: 1.2, 8.4 = client start + UIF round + re-allocation, not graded; then 45.2, 45.0, 45.0) | direct scan-out all the time | a steady window at ~30: that buffer was composited (row 9) or G6's gate waited a frame (G6 doc row 14) — read `KMS srv flipstat … deferred=` |
| 11 | `V3DA srv qstat … low=<N>/65536KiB lowbos=<n> lowfb=0` while Weston runs, N = 8192 × live hinted buffers (≤ 32768) | the counter | `lowfb>0`: rows 4/8 |
| 12 | exit: `V3DA srv export withdrawn … live=0` for every client buffer; the last `V3DA srv low stats client=<c> live=0/65536KiB peak=<≤ 40960>KiB bos=<4..5> … fallbacks=0 …` (printed at a client close when changed, at the latest at `v3dasync-ping`'s) | every low block released back to the pool | `live>0` in the last line: a hinted BO outlived its references (compare the `withdrawn` lines) — an accounting leak, not a display fault |
| 13 | `SHMSRV stats live=0`, `KMSTEST stats … apply_errors=0 … bos=0 exports=0`, `V3DAPING stats … bos_live=0 … to_kernel=0 … verdict=PASS` (proto-2 ping) | no leaks; rejects not counted in `to_kernel` | `to_kernel>0`: the pool-full path, not placement (rejects are counted apart) |
| 14 | fault dumps | 0 kernel, 0 EL0 | EL0 in the server: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/v3d-async/out-low/rpi4-v3d-async <pc>`; in the client: `weston-drm/build-out-low/weston-simple-egl` |

**Decides:** rows 4, 8, 9 and 10 PASS = shareable scan-out buffers are placed below 1 GiB by the render
server, and Weston's direct scan-out covers every client buffer. The placement path should then become
the default for DRI3 in Xorg-drm (a Mesa x11 rebuild with 0016) and for v3dv WSI, which needs a separate
v3dv hook (below).

### 18.4 Risks

- **Low memory is finite.** Each hinted 1080p buffer holds one 8 MiB buddy block below 1 GiB, next to the
  kernel, the VideoCore carve-out and rpi4-kms's pool (`-p 96` = one 128 MiB block). The budget caps what
  the server takes; beyond it, or when 16 tries find no low 8 MiB block, the BO is created anyway and
  composited, as before this change (a logged `FALLBACK`, never a failed allocation).
- **DRI3 sets `__DRI_IMAGE_USE_SCANOUT` on every back buffer** (`loader_dri3_helper.c:1506`). Once the
  x11 Mesa build carries 0016, every GL window's buffers in Xorg-drm will ask for low memory. That is
  right for Present flips, but it puts pressure on the budget; watch `lowfb` there before raising `-L`.
- **Ordinary BOs now prefer high pooled blocks.** This changes which pooled block a same-size ordinary BO
  gets (placement only). An unhinted buffer that used to reuse a low block by luck may now reuse a high
  one. That affects `prime_import_card0` / `dmabuf_sync_flip` (graded `gap=1`, row 5) and nothing that
  asks for scan-out correctly.
- **Mesa's BO cache on the client side.** A hinted BO that is freed without ever being exported stays
  `private` and goes into Mesa's BO cache, so a later ordinary `v3d_bo_alloc` of that size may get the
  low block. This is harmless, but it holds low memory until the cache times the BO out (~2 s). Exported
  buffers (every Wayland client buffer) are freed at once.
- **v3dv (Vulkan WSI)** allocates through `v3dv_bo_alloc`, not the gallium path: patch 0016 does not cover
  it. vkcube on `VK_KHR_display` uses kms dumb buffers (already low), so only a Wayland Vulkan client would
  need the same hook.
- **Rejects go back to the kernel.** This relies on the E1 §6 object-tree fix (kernel build ≥ 8, which
  every netboot image since build 11 carries), as kms's pool retry already does.

## Result — `m6i-low` (chain52, 2026-09-27 17:07): ✅ PASS — every client buffer scanned out, 60 fps

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-170639-m6i-low.log`. `DRMPROBE RESULT pass=49 fail=0 gap=0 … verdict=PASS` (incl. `scanout_lowmem`). **3 `V3DA srv low BO`
lines, 0 `FALLBACK`, 0 `why=above_1g`**, 6 `KMS scanout import … (first flip)`. weston-simple-egl **30.4 (the
import window), then 59.8, 60.0, 60.2 fps**: m6h 45, m6g (composited) 30. `weston exited rc=0`, 0 exceptions.

**Decides:** scanout placement below 1 GiB works on hardware. With G4 + G7 + low placement a Wayland GL client runs
at the display rate with no composition copy. Mesa patch 0016 + render-server proto 5 are the new-lane default.

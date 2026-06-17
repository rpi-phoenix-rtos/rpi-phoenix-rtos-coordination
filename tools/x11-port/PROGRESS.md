# X11 (tinyx/kdrive) library port — progress

**Goal:** a software X server (kdrive `Xfbdev`) on the Pi 4 `/dev/fb0`, per
`docs/todo/tinyx-x11-demo.md`. The OS prereqs are done (AF_UNIX HW-validated, fb0,
USB HID kbd+mouse). The remaining cost is porting the X11 **library stack** (it is
NOT in `phoenix-rtos-ports` — PR #82 never landed). This dir cross-compiles that
stack for aarch64-phoenix, bottom-up.

**Isolation (hard rule):** builds into `/tmp/x11-phoenix` only. X11 is **never** added
to the rpi4b default components / any `ports.yaml` that feeds the flagship netboot
image. Host-side build work only — no Pi boots, no flagship rebuilds.

Build: `tools/x11-port/build-x11-phoenix.sh` (fetches x.org release tarballs;
needs host internet; idempotent).

## Ladder + status (2026-06-17)

| Brick | Status | Notes |
|---|---|---|
| xorgproto 2023.2 | ✅ builds | headers-only; 129 headers + 29 `.pc` installed |
| libXau 1.0.11 | ✅ builds | `libXau.a` aarch64-phoenix; config.sub knows `phoenix` |
| xtrans 1.5.0 | ✅ builds | transport headers (AF_UNIX path is the Phoenix-relevant one) |
| libXdmcp 1.1.5 | ✅ builds | `libXdmcp.a` |
| xcb-proto 1.16.0 | ✅ builds | host python codegen; `.pc` in `share/pkgconfig`, `xcbgen` under `local/lib/python3.x` |
| libpthread-stubs 0.5 | ✅ builds | provides `pthread-stubs.pc` (pthread is in Phoenix libc → stubs are no-ops) |
| libxcb 1.16 | ✅ builds | `libxcb.a` + ~24 extension libs (randr/render/shm/shape/xfixes/xkb/xinput/…); needed 2 Phoenix-gap patches (below) + `--disable-mitshm` |
| **libX11 1.8.7** | ✅ builds | **the core Xlib** (`libX11.a`, 2.2 MB). Needed: `xorg_cv_malloc0_returns_null=no` cache, `-DMAXHOSTNAMELEN=256`, `-DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS` (POSIX getpw*_r path) + libphoenix getpw*_r/sys/poll.h (below) |
| libXext 1.3.5 | ✅ builds | `libXext.a` |
| libXrender 0.9.11 | ✅ builds | `libXrender.a` |
| pixman 0.42.2 | ✅ builds (lib) | `libpixman-1.a` (3.1 MB) + `.pc`; only pixman's *test* `utils.c` fails (its `gettime` clashes with Phoenix's non-standard `sys/time.h` `gettime`), so build `make -C pixman install` |
| libXfont2 / freetype / fontconfig | ⬜ next | font libs (or PCF bitmaps only for the MVD) |
| kdrive Xfbdev server | ⬜ | the actual server; shadow-FB + `write()`-blit to `/dev/fb0` |

## Findings / cross-compile recipe (proven)

- Toolchain `aarch64-phoenix-gcc 14.2.0` + sysroot
  `.buildroot/_build/aarch64a72-generic-rpi4b/sysroot`. `config.sub` accepts
  `--host=aarch64-phoenix` (no triple-faking needed).
- Static libs (`--disable-shared --enable-static`) — matches the Phoenix static-link
  model; `dlopen` is absent so a static kdrive is the only viable server anyway.
- `CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include"`, `LDFLAGS` likewise; `PKG_CONFIG_PATH`
  points at the isolated prefix so each lib finds its already-built deps.

## Phoenix-gap patches applied (in `patches/`)

- **libxcb-1.16-phoenix.patch** — (1) `xcb_conn.c`: add `#include <arpa/inet.h>` (Phoenix
  defines `htonl` as a macro there and doesn't pull it transitively). (2) `xcb_in.c`: no-op
  guards for `MSG_TRUNC`/`MSG_CTRUNC` (Phoenix `sys/socket.h` lacks them; recvmsg doesn't
  report them; SCM_RIGHTS fd-passing is unused by a software X server).

### libphoenix fixes made for the port (committed, libphoenix 89d1543)

- **getpwuid_r** added + **getpwnam_r** implemented (was a `return -1` stub) — proper
  POSIX-reentrant wrappers. libX11's `Xos_r.h` needs them on the MT-safe path.
- **`sys/poll.h`** compat alias added (→ `<poll.h>`) — libX11's `Xpoll.h` includes it.
- These ship in the on-device libc on the next image rebuild (additive; needed when the X
  server eventually links/runs). The frozen flagship image is unaffected until then.

### libphoenix gaps noted (upstream-worthy, not yet fixed)

- `sys/socket.h` lacks `MSG_TRUNC`/`MSG_CTRUNC` (patched locally in libxcb instead — see above).

## Known Phoenix walls to expect (from tinyx-x11-demo.md)

- `shm_open` missing → build XCB/X with `--disable-mitshm`.
- `dlopen`/`dlfcn.h` absent → static-link kdrive only (no loadable DDX/modules).
- `mmap(fd,0)` of `/dev/fb0` gives a private copy, not the live FB → the server must
  use a shadow framebuffer + `write()`-blit to `/dev/fb0` (kdrive shadow-FB layer).

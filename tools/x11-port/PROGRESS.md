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
| xcb-proto | ⬜ next | python codegen (host python); produces the XCB protocol descriptions |
| libxcb | ⬜ next | needs xcb-proto + libXau + libXdmcp; `--disable-mitshm` (no `shm_open`) |
| libX11 | ⬜ | needs xcb + xtrans + xorgproto; expect `--disable-xcb-sloppy-lock` etc. |
| libXext/libXrender/libXfont2 | ⬜ | extension + font libs |
| pixman | ⬜ | software rasteriser (NEON path on aarch64) |
| freetype/fontconfig | ⬜ | font rendering (or PCF bitmaps only for the MVD) |
| kdrive Xfbdev server | ⬜ | the actual server; shadow-FB + `write()`-blit to `/dev/fb0` |

## Findings / cross-compile recipe (proven)

- Toolchain `aarch64-phoenix-gcc 14.2.0` + sysroot
  `.buildroot/_build/aarch64a72-generic-rpi4b/sysroot`. `config.sub` accepts
  `--host=aarch64-phoenix` (no triple-faking needed).
- Static libs (`--disable-shared --enable-static`) — matches the Phoenix static-link
  model; `dlopen` is absent so a static kdrive is the only viable server anyway.
- `CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include"`, `LDFLAGS` likewise; `PKG_CONFIG_PATH`
  points at the isolated prefix so each lib finds its already-built deps.

## Known Phoenix walls to expect (from tinyx-x11-demo.md)

- `shm_open` missing → build XCB/X with `--disable-mitshm`.
- `dlopen`/`dlfcn.h` absent → static-link kdrive only (no loadable DDX/modules).
- `mmap(fd,0)` of `/dev/fb0` gives a private copy, not the live FB → the server must
  use a shadow framebuffer + `write()`-blit to `/dev/fb0` (kdrive shadow-FB layer).

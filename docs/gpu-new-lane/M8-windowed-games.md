# M8 — windowed GPU games on the desktop

Milestone M8 of the [new-lane plan](PLAN.md). Owner goal (2026-09-28): the Quakes and SuperTuxKart
**in a window** on the desktop, GPU-accelerated, next to Thunar and foot, as a showcase demo.
Builds on [M6](M6-wayland.md) (the Wayland stack, `/v3dbuf` export G4, cross-process sync G6, a GPU
Wayland client at 60 fps: m6g/m6i), [M7](M7-wayland-desktop.md) (XFCE 4.20 on labwc 0.20, composited
by the V3D: m7k/m7l, `/bin/xfce-session-2`) and [MIGRATION §2](MIGRATION.md) (the `-drm` game clones).

Evidence tags as elsewhere: **[Pi]** measured on hardware, **[built]** cross build / link / static
check, **[read]** read in source, **[inferred]** reasoning only.

**Status (2026-09-28):** route chosen (Wayland). **Four windowed clones build and are staged**
(`quakespasm-wl`, `quake2-wl`/`yquake2-wl`, `quake3-wl`/`quake3e-wl`, `stk-wl`/`supertuxkart-wl`),
plus the launcher `/bin/game-window.sh` and a labwc configuration `/etc/xdg/labwc-xfce-m8/` that lays
the desktop out. All static checks pass [built]; the three control relinks are byte-identical to the
shipped engines. `m8a-quake-window` **passed** on the Pi (result at the end). `m8b-quake23-window` and
`m8b-stk-window` **passed** 2026-09-28 (quake2 60 fps, quake3 90, the STK menu 90, all windowed beside Thunar and foot; quake3e hangs in its signal-handler shutdown — result at the end). vkQuake is deferred (§5).

---

## 1. The route: Wayland (SDL2's Wayland video driver on labwc)

| Question | Wayland (labwc / XFCE) | X11 (Xorg-drm) |
|---|---|---|
| Next to Thunar and foot? | **yes**: Thunar, foot, the panel are Wayland clients of labwc (m7h–m7l) | **no**: there is no Xwayland; the X route needs its own desktop (Window Maker) with X clients only |
| Compositor / present path proven | labwc composites on the V3D with GLES2 (m7k, m7l) [Pi]; a GPU Wayland client (`weston-simple-egl`) at 60 fps with direct scan-out under Weston (m6i) [Pi] | DRI3/Present GL window at 60.00 fps vsynced (m4p2a) [Pi] |
| Mesa pieces | EGL wayland platform exists (`mesa-drm --wayland`), GLES only → **one new build**: `--wayland --opengl` (§2.1) | EGL x11 platform exists (`--x11`), GLES only: the same new build would be needed for desktop GL |
| SDL | SDL 2.30.12's stock Wayland driver + 3 small patches (§2.2) | SDL's x11 driver: libX11/Xext/Xrandr/Xi/Xcursor/Xfixes from the ports prefix, not built for SDL so far |
| Input | wl_seat keyboard (xkbcommon keymap from labwc, as foot's) + pointer, relative pointer + pointer constraints: labwc-2 advertises `zwp_relative_pointer_manager_v1`, `zwp_pointer_constraints_v1` (strings in `/bin/labwc-2`) [built]; labwc's libinput-phoenix reads `/dev/kbd0` + `/dev/mouse0` (typing in foot, m7b2) [Pi] | Xorg-drm's `phxhid` input driver: checked statically, **never exercised on the Pi** (MIGRATION §1) |
| Decorations | server-side: labwc draws the title bar (`<decoration>server</decoration>`, SDL asks through `zxdg_decoration_manager_v1`); no libdecor | the window manager's |
| Audio | unchanged: SDL's Phoenix driver, `/dev/audio0` | the same |

**Chosen: Wayland.** It is the only route that puts the game next to Thunar and foot. It is also what
Linux does (`SDL_VIDEODRIVER=wayland` under any wlroots compositor), and each piece except the SDL
build had already run on the Pi. Less work, too: one Mesa variant plus an SDL build, where X11 needs
the same Mesa work plus SDL's X11 dependencies and an untested input driver.

**How the games create their context** [read]:

| Game | API | Link half (`link-inputs.txt`) |
|---|---|---|
| quakespasm | desktop GL (fixed function + GLSL); SDL asks EGL for `EGL_OPENGL_API` | `mesa-gl`: `libglapi_bridge.a` (static desktop `gl*`) |
| quake3e | desktop GL (opengl1 renderer, `qgl*` through `SDL_GL_GetProcAddress`) | `mesa-gl` |
| yquake2 | GLES 3 (ref_gl3 built for ES, glad) | `mesa-es`: `libGLESv2.a` |
| STK | GLES 3 (Irrlicht `COGLES2Driver`, `-DUSE_GLES2=ON`) | `mesa-es` |

So Mesa must give desktop GL **on the wayland platform**; that is the new Mesa build (§2.1). One
Mesa build serves all four: `libglapi_bridge.a` and `libGLESv2.a` are alternatives at link time.

## 2. What was built [built]

### 2.1 Mesa: `mesa-drm/build.sh --wayland --opengl`

`--wayland` used to refuse `--opengl` ("a GLES build of its own"). The guard now allows the two
together (default dir `build-out-wayland-gl/`); nothing else in the script changed, and the existing
`--wayland` / `--opengl` builds are unaffected (their dirs and options are as before). The command:

```
tools/gpu-lane/mesa-drm/build.sh --wayland --opengl --out tools/gpu-lane/mesa-drm/build-out-wayland-gl \
    --libdrm-prefix tools/gpu-lane/libdrm-phoenix/build-out-low/prefix \
    --wayland-pkgconfig <labwc-drm/build-out>/prefix/lib/pkgconfig:<…>/prefix/share/pkgconfig:<…>/deps/libffi/lib/pkgconfig
```

- Mesa 26.2.0 + the committed patch set `4a457a1efe6f3903` (0001–0016).
- meson: `OpenGL: YES`, EGL platforms `wayland surfaceless drm`.
- 59 compiler warning lines, the same count as `build-out-wayland-low` (untouched code).
- `dri2_initialize_wayland` yes, `EGL_EXT_image_dma_buf_import` yes, `libglapi_bridge.a` built.
- libdrm-phoenix `build-out-low` (`cda443dc8dfd6bce`): the one labwc-2 links (G4 export, G6
  sync-file ioctls, shareable BOs below 1 GiB).
- Wayland headers: the labwc-drm prefix (libwayland 1.24.0, wayland-protocols 1.49), the stack
  labwc-2 and foot-2 link.
- `libgallium-26.2.0.a` `0bc87a0e9e839bed`.

`scripts/check-gpu-lane-ports-sync.sh` maps mesa-drm's `patches/` and `compat/`, not `build.sh`,
so this edit adds no ports drift. The `mesa_drm` port recipe would need the same option only if the
windowed games become a port (follow-up).

### 2.2 SDL: `tools/gpu-lane/sdl2-wl/build.sh` (new; sdl2-drm unchanged)

SDL 2.30.12 from the ports tarball. First sdl2-drm's `patches/0001–0009` + `overlay/` (read by path,
so KMSDRM, the Phoenix HID and audio drivers and the swap reorder are all as in the `-drm` clones),
then three new patches:

| patch | what, why |
|---|---|
| `0101-cmake-phoenix-wayland-video-driver` | `CheckWayland()` in the PHOENIX cmake branch (sdl2-drm 0002/0006 skip the host probes). Enforces `SDL_WAYLAND_SHARED=OFF` and `SDL_WAYLAND_LIBDECOR=OFF`: no `SDL_LoadObject`, no plugin loader, so labwc draws server-side decorations. `HAVE_MEMFD_CREATE 1`: the compat library's `memfd_create` over shmsrv, so SDL's cursor pools are shared memory. Builds `src/core/unix` (`SDL_IOReady`, the display-fd poll). **Drops SDL's own generated `wayland-protocol.c`**: statically linked, its `wl_*_interface` tables collide with libwayland-client's (22 interfaces, all at a version ≤ libwayland 1.24's) |
| `0102-wayland-clipboard-pipe-without-sigtimedwait` | libphoenix has no `sigtimedwait()` and no `PIPE_BUF`. The clipboard pipe writer ignores SIGPIPE around the write (sigaction save/restore) instead of blocking it and consuming it, and uses `_POSIX_PIPE_BUF` (512). A `sigtimedwait` stub returning EAGAIN would have let a pending SIGPIPE kill the process on unmask |
| `0103-wayland-fractional-scale-uint32_t` | `uint` (a glibc/BSD typedef, absent in libphoenix) → `uint32_t` in the fractional-scale listener |

Configure: `-DSDL_WAYLAND=ON -DSDL_WAYLAND_SHARED=OFF -DSDL_WAYLAND_LIBDECOR=OFF -DSDL_KMSDRM=ON
-DSDL_KMSDRM_SHARED=OFF`, GL + GLES on, the rest as sdl2-drm. pkg-config sees only the Mesa prefix,
its libdrm snapshot and zlib, the labwc-drm prefix and its libffi view. The C flags add
weston-drm's `compat/include` (the `memfd_create`/`pipe2` declarations) and a private
`wl-include/linux/input.h` (weston-drm's shim, alone). `SDL_config.h` asserts:

- present: `SDL_VIDEO_DRIVER_WAYLAND`, `SDL_VIDEO_DRIVER_KMSDRM`, EGL, GL, GLES2, `HAVE_MEMFD_CREATE`,
  Phoenix HID + audio;
- absent: every `*_DYNAMIC`, `HAVE_LIBDECOR_H`, `SDL_LOADSO_DLOPEN`, X11, Vulkan.

25 compiler warning lines, the same 25 as sdl2-drm (`math_private.h`, `SDL_hidapi.c`): none in the
Wayland sources. `libSDL2.a` `543aef39ed9a498d` (set `afda850993ea66ad`).

**XKB without data files.** Phoenix has no `/usr/share/X11/xkb`, which made m6a's Weston (xkbcommon
1.7) fail with `failed to create XKB context` (weston patch 0007; GTK has the same fix, gtk3-wayland
`gtk/0003`). SDL calls `xkb_context_new(0)` (`SDL_waylandvideo.c:944`) and fails the same way on a
NULL. It links the labwc-drm prefix's **xkbcommon 1.13.2**, where the default include paths are only
added when first needed (`pending_default_includes`, `src/context.c`) [read]. foot-2 makes the same
call against the same library and types in labwc (m7b2) [Pi]. The keymap labwc sends is a complete
string, so no include path is ever needed and SDL needs no patch for this.

The Phoenix HID poll (`/dev/kbd0`, `/dev/mouse0`) sits only in `KMSDRM_PumpEvents`/`VideoInit`
(sdl2-drm 0007) [read]. So a game on the Wayland driver never opens the HID nodes that labwc's
libinput-phoenix holds; its input comes only through `wl_seat`.

**The link group** (`build-out/link-inputs.txt`, also used by the relink scripts):

- libgallium whole-archive.
- one group of: `libSDL2.a`; the Mesa EGL/GBM/dri_gbm/glapi/v3d/broadcom/winsys/util archives with
  `libglapi_bridge.a` (desktop GL) or `libGLESv2.a` (GLES); libwayland-client, -egl and -cursor;
  xkbcommon; the labwc and weston compat libraries; libffi; libdrm-phoenix; the mesadrm compat
  library; zlib.
- flags `--wrap=mmap,ioctl,close,write` + `-u __wrap_close,__wrap_write`: weston-simple-egl's
  `LINK_DRM`, the one Pi-proven GPU Wayland client shape.
- Mesa's `util/anon_file.c` and libwayland-cursor both define a global `os_create_anonymous_file()`,
  with different signatures. As in weston-drm's simple-egl link, private copies of the Mesa archives
  that define or call it rename Mesa's to `mesa_os_create_anonymous_file` (`mesa-link/`).
- objdump check: `wl_cursor_theme_load → os_create_anonymous_file` (libwayland's own, one argument)
  and `swrast_update_buffers → mesa_os_create_anonymous_file` (Mesa's).

### 2.3 The clones

| clone | built by | how | control relink |
|---|---|---|---|
| `quakespasm-wl` | `sdl2-wl/build.sh` | quakespasm at the port's commit + its patch, the 67 TUs of sdl2-drm/build.sh, `gamewl_hooks.c`, 32 MiB stack | — (compiled, as quakespasm-drm) |
| `yquake2-wl` + launcher `quake2-wl` | `sdl2-wl/build-quake2-wl.sh` | the port's own final link from its `build.log` with the new group, GLES half (`gamewl/relink-sdl-gl-game-wl.sh`, = sdl2-drm's relink body with the Wayland stack) | **byte-identical to shipped `prog/yquake2`** |
| `quake3e-wl` + launcher `quake3-wl` | `sdl2-wl/build-quake3-wl.sh` | the same, desktop-GL half | **byte-identical to shipped `prog/quake3e`** |
| `supertuxkart-wl` + launcher `stk-wl` | `sdl2-wl/build-stk-wl.sh` | STK's CMake `link.txt` with the new group, GLES half (= build-stk-drm.sh with the Wayland stack) | **byte-identical to shipped `prog/supertuxkart`** |

`gamewl/gamewl_hooks.c` is sdl2-drm's `gamedrm_hooks.c` with two changes:

- the banner `<name>: windowed GPU game -- SDL 2.30.12 Wayland (KMSDRM fallback) + Mesa 26.2 EGL
  wayland (<API>) …`;
- a first-swap line naming the **video driver**, window/drawable size, windowed/fullscreen, the
  context (GL/GLES M.m) and the swap interval.

The `<name> flipstat … fps (total N)` / `swapstat` counter is unchanged, behind
`--wrap=SDL_GL_SwapWindow`, and so is `GAMEWL_EXIT_SECS`. The launchers are the shipped ones with only
the exec target rewritten (STK's also its two message prefixes). The window size comes from the
arguments they forward (§3).

**Checks on every clone** (the scripts fail on any):

- `nm -u` = 0, no `PT_INTERP`.
- Symbols present: `Wayland_CreateDevice`, `Wayland_GLES_SwapWindow`, `Wayland_PumpEvents`,
  `KMSDRM_CreateDevice`, `wl_display_connect`, `wl_egl_window_create`, `wl_cursor_theme_load`,
  `xkb_keymap_new_from_string`, `dri2_initialize_wayland`, `memfd_create`, both
  `os_create_anonymous_file`s, `__wrap_{mmap,ioctl,close,write}`, `drm_phoenix_ioctl`, kmsro/v3d.
- Strings present: `SDL Wayland video driver`, `KMS/DRM Video Driver`, `xdg_wm_base`,
  `zxdg_decoration_manager_v1`, `zwp_relative_pointer_manager_v1`, `zwp_pointer_constraints_v1`,
  `zwp_linux_dmabuf_v1`, `libdrm-phoenix:`, `<name>: windowed GPU game`, `<name> flipstat`.
- Old-lane strings (`v3d-winsys:`, `phxgl`, `/dev/fb0`, `RPI4FB_GETMODE`, …) and `libdecor-`: 0.
  The shipped engines are the reverse (inverse control).
- objdump: the engine's swap goes `GL_EndRendering` / `GL3_SwapWindow` / `GLimp_EndFrame` /
  `COGLES2Driver → __wrap_SDL_GL_SwapWindow → SDL_GL_SwapWindow`, the wrapper being the only
  caller; only `__wrap_ioctl` / `__wrap_mmap` call the real ones.
- No global symbol is defined by both the engine's objects and the new stack. For STK the ports
  `libz.a` is excluded from that comparison: STK links the same archive itself.
- The guarded shared inputs are unchanged.

## 3. Launching: `/bin/game-window.sh` and the M8 session configuration

`/bin/bash /bin/game-window.sh <quakespasm|quake2|quake3|stk|simple-egl> [args]`, from foot, labwc's
root menu or `Super+G`:

- **Environment.** `SDL_VIDEODRIVER=wayland`, `XDG_RUNTIME_DIR` (default `/tmp/xdg`, the session's)
  and `WAYLAND_DISPLAY` (labwc's, else the first `wayland-N` socket there), as the XFCE session
  sets them. `SDL_VIDEO_WAYLAND_WMCLASS` = the app_id the window rules match.
- **Window size.** 1280×720 by default (`GAME_W`/`GAME_H`) through each engine's own options:
  - quakespasm: `-window -width W -height H`;
  - yquake2: `+set vid_fullscreen 0 +set r_mode -1 +set r_customwidth/height`;
  - quake3e: `+set r_fullscreen 0 +set r_mode -1 +set r_customWidth/Height +map q3dm1`;
  - STK: `--windowed --screensize=WxH` (`--windowed` is read after the launcher's `--fullscreen`,
    `main.cpp:889/904`).

  The bare `/usr/bin/quakespasm-wl` follows the shared `id1/autoexec.cfg` (1920×1080,
  `vid_fullscreen 1`): an xdg fullscreen window.
- **Quitting.** `GAME_SECS=N` ends the game with SIGTERM after N s. SDL turns SIGTERM into `SDL_QUIT`,
  which the engines answer with their own shutdown (quakespasm: `in_sdl.c:1150` →
  `Sys_Quit` → `VID_Shutdown` + `exit(0)`) [read].
- **Tracking.** `$XDG_RUNTIME_DIR/game-window.pid` holds the running game. Lines start with
  `M8 game=<g> start …` and `M8 game=<g> exited rc=<n> (<clean exit|killed …>) ran_s=<s>`.
- **Host dry run** (stand-in binaries that trap TERM like SDL): autostart list, time-up TERM,
  quit-script TERM, `rc=0 (clean exit)`, logout: as designed.

`/etc/xdg/labwc-xfce-m8/` (`CONF_DIR` of `/bin/xfce-session`) = labwc-xfce-demo plus the M8 parts:

- **rc.xml:** window rules by app_id. The first Thunar window goes to 620×560 at (8, 40), the first
  foot to 620×420 at (8, 640), and a game window at (636, 40), so a 1280×720 game sits beside the
  left column under the 30 px panel. `Super+G` starts Quake in a window.
- **menu.xml:** root-menu entries *Quake / Quake II / Quake III / SuperTuxKart (window)*.
- **autostart:** xfdesktop and the panel as the demo, then `foot-2` and
  `/bin/game-window-autostart.sh`, which runs `M8_GAMES` (default `quakespasm`; items
  `<game>[:<secs>]`) one after another after `M8_DELAY` (default 15 s).
- **environment:** the demo's.

The game's stdout is labwc's, so its lines reach the UART live.
`/bin/game-window-quit.sh` is the session's `LOGOUT_CMD`. It raises a stop flag for the list,
SIGTERMs the running game, waits ≤ 30 s for it to exit (SIGKILL after that), then runs the demo's
`loginctl terminate-session`.

Nothing already staged was changed. `/bin/xfce-session{,-2}`, `/bin/xfce-desktop-2.sh`, labwc-xfce-demo
and every `-drm` binary are as before: M8 is reached only through the psh `export`s of §6.

## 4. Staged (2026-09-28, `/srv/phoenix-rpi4-nfs-gcc16`, new paths only: each checked absent, `sudo -n install`, then `cmp`)

| staged path | source | sha256 |
|---|---|---|
| `/usr/bin/quakespasm-wl` | `sdl2-wl/build-out/quakespasm-wl.stripped` (18 409 736 B) | `021cb81659fcfafab77f463ba4143dece4aedcef8fdbb5764fefda71a8fb32a8` |
| `/usr/bin/yquake2-wl` | `build-out/quake2-wl/yquake2-wl.stripped` (18 970 152 B) | `f1f40130071b0a23833df09fd6224ef51e9d799f5bc3644ac80b7ade57782cb2` |
| `/usr/bin/quake2-wl` | `build-out/quake2-wl/quake2-wl` (launcher) | `f618d33535519f38a5532fa261a5047f66bcbc532ccd0cb57af2fd5c5d138048` |
| `/usr/bin/quake3e-wl` | `build-out/quake3-wl/quake3e-wl.stripped` (19 037 208 B) | `9c9c68e79cb81374dc303f619d863ee8c001f6803705a7270ca38b9d78595462` |
| `/usr/bin/quake3-wl` | `build-out/quake3-wl/quake3-wl` (launcher) | `86204741959cd309fa9ce4202b07846480cb6efc475d594b15facf89e80b04ad` |
| `/usr/bin/supertuxkart-wl` | `build-out/stk-wl/supertuxkart-wl.stripped` (38 638 664 B) | `9e7d92fc8b45e01f5e90aa68e992352615004f44c7898eefce25ee4f5cbaeb89` |
| `/bin/stk-wl` | `build-out/stk-wl/stk-wl` (launcher) | `b42afb8298b1b8b606e071a0075f710c4ab24bdedde52a6dd697b864ce425160` |
| `/bin/game-window.sh` | `sdl2-wl/pi/game-window.sh` | `5a7e54dc4b547418e2ecad8d19270915bbccd009d51a819fd2c6232351c5a584` |
| `/bin/game-window-autostart.sh` | `sdl2-wl/pi/game-window-autostart.sh` | `aad83680efae20be589e1140ba830e4f1b61392873282d5343f16b914080b57c` |
| `/bin/game-window-quit.sh` | `sdl2-wl/pi/game-window-quit.sh` | `3cdbae19f01d47dd56f7fb6e890a91f25068d4b83c8a3dcd2031ef9625c5542b` |
| `/etc/xdg/labwc-xfce-m8/rc.xml` | `sdl2-wl/conf/labwc-xfce-m8/rc.xml` | `699b57eab8ef7ab9204434decea1d10ee6135981ced15fcd283e6b03e04f739e` |
| `/etc/xdg/labwc-xfce-m8/menu.xml` | `…/menu.xml` | `b55b8d44a6f734e72ef13af035e6a3fd701cc042b953e91cdfb2b3e3d62541a6` |
| `/etc/xdg/labwc-xfce-m8/autostart` | `…/autostart` | `c24f0337cfd3898a30f4560e628bac782f58b13fa7b89fd4ec667c325c365cf0` |
| `/etc/xdg/labwc-xfce-m8/environment` | `…/environment` | `a6f419274bdfdf3e23a0dc1a93a4c038fbf55d977e9326de3b3b84a037443511` |

Also used, staged earlier: `/bin/xfce-session-2` (g8 + gles2), `/bin/labwc-2`, `/bin/foot-2`, the XFCE demo
programs, `/bin/weston-simple-egl-low` (the control client), the three servers. Unstripped ELFs and `.map` files
for `addr2line` stay in `tools/gpu-lane/sdl2-wl/build-out/` (keep them until m8a is graded). Built against the
sysroot `libphoenix.a` `83c07cf81b47e3f8` (image build of 2026-09-28 07:36).

## 5. What is left / what blocks the other games

- **Nothing blocks the other three at build level**: they are built and staged. What is unknown is
  runtime, and it is the same for all four: the first GPU client of labwc (below), the load time
  (Quake II and III ram-stage their data to `/tmp` before the engine starts, and STK loads for
  minutes with a cold shader cache), and in-window input. m8a runs quakespasm only; m8b/m8c (§7)
  take the rest.
- **First GPU Wayland client under labwc.** `m7d-gl-client` never ran; m6g/m6i were under Weston. A
  client's buffer reaches labwc as a dma-buf through `zwp_linux_dmabuf_v1` (G4 `/v3dbuf` export).
  wlroots' GLES2 renderer imports it with `EGL_EXT_image_dma_buf_import` (render server `import …
  ns=v3dbuf`) and composites it; the frames are ordered by G6 implicit sync. m8a's first arm is
  therefore `weston-simple-egl-low` alone: it separates "labwc cannot show a GPU client" from "SDL
  or the game broke".
- **Mouse in quakespasm.** In play (the attract demo too) quakespasm uses SDL relative mouse mode,
  so on Wayland the pointer is locked in the focused game window (pointer constraints), as on
  Linux. Esc (the menu) or the console frees it; Alt+Tab moves the focus away.
- **vkQuake (`vkq-wl`), deferred.** It needs:
  - a v3dv build with the Wayland WSI (`mesa-drm/build.sh --vulkan` sets no platform; the guard and
    `-Dplatforms=wayland` for the Vulkan build);
  - `VK_KHR_wayland_surface` in the SDL-Vulkan variant (`patches-sdl-vulkan/` + Wayland);
  - `build-vkquake-drm.sh`'s phxvk/trampoline link redone with the Wayland stack;
  - on the Pi, WSI buffers shared as dma-bufs with the compositor (G4 export from v3dv; G6 implicit
    sync; Vulkan WSI's `wp_linux_drm_syncobj` would need G6b).

  That is a Vulkan-WSI step of its own, not cheap. It goes after m8a–m8c.
- **Ports.** The build lives in `tools/gpu-lane/sdl2-wl/`. A framework port (the `sdl2_kmsdrm`
  pattern) follows once m8a passes.

## 6. Pre-registered Pi cycle `m8a-quake-window` (≈ 7 min from a fresh boot; Bash `timeout: 600000`, or from a chain script)

**Question:** in the GPU-composited XFCE session, does a GPU client get a decorated window from
labwc? First `weston-simple-egl-low` for 15 s (the control), then `quakespasm-wl` in a 1280×720 window
beside Thunar and foot. Specifically: SDL selects its Wayland driver, the EGL desktop-GL context is
on the V3D, the fps comes from the game's own counter, and the game quits cleanly on SIGTERM before
the session logs out.

```
./scripts/test-cycle-psh-interact.sh --label m8a-quake-window --idle-secs 60 --max-cmd-secs 480 \
    --hdmi-dense-on 'M8 game=' -- \
    "export HOLD=90" \
    "export VERBOSE=1" \
    "export CONF_DIR=/etc/xdg/labwc-xfce-m8" \
    "export LOGOUT_CMD=/bin/game-window-quit.sh" \
    "export M8_GAMES=simple-egl:15,quakespasm" \
    "/bin/bash /bin/xfce-session-2" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

- `xfce-session-2` starts the three servers (`rpi4-kms-g8`, gles2), as in m7l.
- `CONF_DIR` selects the M8 labwc configuration and `LOGOUT_CMD` the game-quitting logout. Both are
  one word, because psh does not strip quotes.
- The hold starts once Thunar is up. The autostart starts the control about 15 s after labwc, and
  quakespasm about 15 s later, so quakespasm renders for about 60 s before the logout at hold 90 s.

Grade:

- `grep -a -E '^M8 |^XFCE|^XFCE-SESSION|quakespasm-wl|quakespasm:|frames in .* fps|GL_RENDERER|GL_VERSION|Wayland|wayland|EGL|foreign_kmsbuf|alias=1|console handover|V3DA srv import|os_same_file|^SHMSRV |^KMSTEST ' …m8a-quake-window.log`;
- `./scripts/uart-summary.sh m8a-quake-window`.

Allow ~1.3 % UART line corruption; EL0 dumps print twice.

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `XFCE-SESSION start … renderer=gles2 … conf=/etc/xdg/labwc-xfce-m8`, `server start: /bin/rpi4-kms-g8`, `servers v3d-async=up kms=up shm=up`; `XFCE labwc start conf=/etc/xdg/labwc-xfce-m8 files=rc.xml,menu.xml,autostart,environment`, `socket=up`; labwc `Creating GLES2 renderer` | m7l's session with the M8 configuration | `files=` short or `missing=`: staging (§4); labwc exits: an rc.xml it rejects (the window rules are the new part) |
| 2 | `M8 autostart games=simple-egl:15,quakespasm delay=15s display=wayland-<N>` (the name in `XFCE labwc socket=up name=`) | labwc ran the M8 autostart with the psh exports | absent: labwc did not run the autostart (`run session script` line), or `display=unset`: then game-window.sh falls back to `/tmp/xdg/wayland-0` |
| 3 | **control:** `M8 game=simple-egl start …`, then `Using config: r8g8b8a8` and `… frames in 5 seconds: <f> fps` (2–3 lines), `M8 game=simple-egl time up (15 s): SIGTERM`, `M8 game=simple-egl exited rc=<n>` | a GPU client under labwc: **f ≈ 55–60** (composited at 60 Hz; m6i: 60 under Weston) | no fps lines, or an EGL error: the compositor-side import (G4 / linux-dmabuf) under wlroots fails. **Stop grading quakespasm's display rows** (they share the path) and read labwc's `[render/…]`/`[types/…dmabuf…]` lines and `V3DA srv import FAIL`. `rc=143` is expected for simple-egl (it has no SIGTERM handler): not a failure |
| 4 | `quakespasm-wl: windowed GPU game -- SDL 2.30.12 Wayland (KMSDRM fallback) + Mesa 26.2 EGL wayland (desktop GL) …` then `quakespasm: main() entered`, `LOAD-TIME main->Host_Init = <s>` | once each, s ≈ 5–20 | two banners or a `-drm` banner: wrong binary staged (`cmp`, §4) |
| 5 | **★ SDL `DEBUG:` video lines and `quakespasm-wl: first swap … video_driver wayland window 1280x720 drawable 1280x720 windowed context GL <M.m> swap_interval 0 flipstat on`** | **the Wayland driver, a windowed desktop-GL context** (M ≥ 2; quakespasm asks for no ES profile; `vid_vsync` defaults to 0) | `video_driver KMSDRM`: no Wayland socket or `wl_display_connect` failed (row 2's display, `XDG_RUNTIME_DIR`), and KMSDRM then takes the planes from labwc; `fullscreen`: `-window` lost; `context GLES`: the EGL config lacks `EGL_OPENGL_BIT` (Mesa built without desktop GL: check `/usr/bin/quakespasm-wl`'s sha); no first-swap line: the context failed (quakespasm's `Sys_Error` text); `Failed to create XKB context`: an older xkbcommon (1.7, weston-drm's) was linked (§2.2) |
| 6 | quakespasm's `GL_VENDOR: Broadcom`, `GL_RENDERER: V3D 4.2…`, `GL_VERSION: … Mesa 26.2.0` | the V3D through Mesa's EGL wayland | `llvmpipe`/`softpipe`: kmsro did not find the render node (libdrm-phoenix `/dev/dri/renderD128`) |
| 7 | **★ `quakespasm-wl flipstat <N> frames in ~5000 ms = <X> fps (total <M>)`** every 5 s during the demo, `total` rising; `swapstat … swap_us_avg=<a>` | **X = 40–70** [inferred]: 1280×720 is 44 % of the pixels of quakespasm-drm's 1080p (46.1 fps, vsync-bound there); with swap interval 0 SDL does not wait for the frame callback, and Mesa takes a free buffer of up to 4; the V3D also runs labwc's 1080p composition | < 25: GPU or present contention (compare `swap_us_avg` with the render time); exactly 30.00 / 60.00: frame-callback pacing after all (SDL's `swap_interval` not 0) |
| 8 | render server: `V3DA srv import … ns=v3dbuf` lines from labwc's client for the game's buffers (≥ 2), no `import FAIL`; **0 `os_same_file_description` lines from quakespasm** (it opens renderD128 once) | the dma-bufs reach the compositor (G4) | `import FAIL`: the export path (G4) under labwc |
| 9 | **HDMI** (dense from the control's `M8 game=simple-egl start`: its 250×250 spinning triangle at about (1100, 300) for 15 s, then): the m7l desktop (panel with a local clock, the dithered wallpaper); **Thunar at the top left (620×560), foot below it, and on the right a window with labwc's title bar `QuakeSpasm 0.97.0` showing the lit, textured attract demo** at 1280×720; the pointer moves over the desktop | **the showcase frame** | window centred / overlapping: the window rules did not match (app_id: SDL sets it from `SDL_VIDEO_WAYLAND_WMCLASS`; Thunar's may differ from `*thunar*`); a black window: EGL on the client renders nothing (compare row 7); no title bar: the decoration protocol was not used (SDL → client-side "none"); tearing or half frames in the game window: G6 implicit sync (the compositor sampled before the render ended) |
| 10 | **clean quit:** `XFCE hold over: /bin/game-window-quit.sh rc=0`; `XFCE log logout-cmd: M8 quit: SIGTERM to quakespasm pid=<p>`; on the UART in real time `quakespasm-wl: exit after <n> swaps in <ms> ms since the first swap` (the atexit line, i.e. `exit()` ran) and **`M8 game=quakespasm exited rc=0 (clean exit) ran_s=<s>`**; `XFCE log logout-cmd: M8 quit: quakespasm gone after <≤5>s result=rc=0 …`, `M8 quit: logout requested rc=0`; `M8 autostart done` | SIGTERM → `SDL_QUIT` → `Sys_Quit` → `exit(0)` | `rc=143`: SDL's SIGTERM handler not installed / the event loop did not see SDL_QUIT (the game was killed by the signal); `still running after 30s: SIGKILL`: the game hung in `VID_Shutdown` (Wayland teardown) — record where (addr2line of a later dump, if any) |
| 11 | the session's stop as m7l: `XFCE session end reason=logout held=90s`, `thunar exited rc=0`, `quit panel_rc=0 xfdesktop_rc=0`, `labwc exited rc=0 … socket=gone`, `dbus exited rc=0`, **`XFCE-SESSION done rc=0`**; `foreign_kmsbuf` 0, `alias=1` ≥ 1, `console handover disable rc=0` | the desktop is unaffected by the game | a session row fails only with the game: compare m7l |
| 12 | `SHMSRV stats rc=0 live=0 bytes=0`, `KMSTEST stats … bos=0 exports=0` | every buffer of the session and the two clients released | `live>0`: the game's cursor pool or keymap map was not released (SDL's Wayland teardown) |
| 13 | fault dumps | **0 kernel, 0 EL0** | EL0 in quakespasm-wl: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/sdl2-wl/build-out/quakespasm-wl <pc>` (unstripped); in labwc: the m7b2 build |

Not graded: `configure request … did not respond` warnings from labwc (m7j: cosmetic), SDL's
`Wayland: … xdg_activation` notes, and the `libseat … Device or resource busy` line of a second session.

**Decides:** rows 3, 5, 7, 9 and 10 = M8's first goal: a GPU game in a decorated window on the XFCE
desktop, at a measured fps, with a clean quit. Row 3 alone failing = the next step is the compositor
import, not the games.

**Bench-only checklist** (a person at the Pi, USB keyboard and mouse; otherwise **n/a**):

1. `export CONF_DIR=/etc/xdg/labwc-xfce-m8`, `export LOGOUT_CMD=/bin/game-window-quit.sh`,
   `/bin/xfce-session-2` (HOLD 0: until Log Out). Quake appears on the right about 30 s after the desktop.
2. Click into the Quake window. The mouse is captured (the demo is in game mode). Press **Esc**: the
   menu opens and the pointer is free. **Arrow keys + Enter**: *Single Player → New Game* starts
   e1m1. Walk (arrows/WASD), look with the mouse, fire with the left button. Sound on HDMI.
3. **Alt+Tab** to Thunar: the Quake window keeps rendering (unfocused). Type in foot.
4. Drag the Quake window by its title bar; **maximize** (double-click the title): the game resizes
   (SDL window events). Restore.
5. Root menu → **Quake II (window)** after quitting Quake (menu → Quit, or `Super+G` starts Quake
   again): a second game only after the first has exited (one game at a time: `game-window.pid`).
6. Log Out (panel): the game is quit first (`M8 quit:` lines), then the desktop.

## 7. Next cycles (sketch; pre-register each in full before it runs)

**Superseded for m8b/m8c** by the section *Pre-registered: `m8b-quake23-window` and `m8b-stk-window`*
at the end of this file (budgets `quake2:90,quake3:120` HOLD 270, and `stk:200` HOLD 300). The
bullets below are the original sketch.

- `m8b-quake23-window`: `M8_GAMES=quake2:120,quake3`, `HOLD=300`. Quake II ram-stages
  `/usr/share/quake2`; Quake III `+map q3dm1`. Grade rows 4–10 per game. yquake2's context is
  `GLES 3.x`.
- `m8c-stk-window`: `M8_GAMES=stk`, `HOLD=420`. STK loads for minutes with a cold shader cache
  (C1 note: grade by the cycle's own lines). The main menu shows in a window.
- `m8d` (after m8a–c): the showcase recording. One session: Quake on the right, Thunar and foot
  left, then Quake II through the root menu.

## Result — `m8a-quake-window` (chain89, build 27b, 2026-09-28 11:10): ✅ PASS — a GPU-accelerated game in a window on the XFCE desktop

Log `artifacts/rpi4b-uart/*-m8a-quake-window.log`; 0 exceptions.
- **Control first:** `weston-simple-egl` inside labwc runs at **57–60 fps** (`285 frames in 5 seconds: 57.0`, `301 … 60.2`). This is the first GPU client ever under labwc. It ended by the script's SIGTERM, as designed.
- **quakespasm-wl:**
  - `GL_RENDERER: V3D 4.2.14.0`;
  - `first swap … video_driver wayland window 1280x720 drawable 1280x720 windowed context GL 2.1`;
  - its own flipstat: **45.5–66.9 fps** (228, 286, 250, 240, 335 frames per 5 s).
- **HDMI** (`artifacts/hdmi/20260928-111546-m8a-quake-window-tick.png`):
  - the XFCE panel's task list shows `File System - Thunar`, `foot` and `QuakeSpasm 0.97.0`;
  - Thunar on `/` top left, a foot terminal below it;
  - **QuakeSpasm in a decorated labwc window on the right, rendering the level in 3D**, its counter at **61 FPS**.
- The session logged out cleanly (`XFCE-SESSION done rc=0`).
- Next: `m8b` (quake2-wl, quake3-wl, stk-wl in a window), then fold `-wl` builds into the default image with the migration.

## Pre-registered: `m8b-quake23-window` and `m8b-stk-window` (2026-09-28; host work only, no Pi cycle yet)

**Question:** do the other three `-wl` clones behave like quakespasm-wl in m8a, each in a decorated
1280×720 labwc window beside Thunar and foot? For each: SDL's Wayland driver, a V3D context, the fps
from its own flipstat, and a clean exit when its time budget ends. m8a is the control (weston-simple-egl
57–60 fps under labwc, same image and same session), so `simple-egl:15` is dropped.

### Two cycles, not one or three [read + Pi timings]

- **Quake II and III share a cycle.** Their `/tmp` staging is short: `ram-stage: … 47.64 MiB in
  2.102 s` (mig-all-q2) and `45.77 MiB in 2.026 s` (mig-all-q3). The first swap comes 4.7 s and 1.5 s
  after exec. Both trees fit in `/tmp` together: 93 MiB against dummyfs `DUMMYFS_SIZE_MAX` 256 MiB
  (`board_config.h:158`). A cycle of their own would cost a whole boot plus the exports (≈ 7 min)
  to save about 5 s of load.
- **STK gets its own cycle.**
  - Its load is long on every run: the Mesa-DRM lane has no shader disk cache (MIGRATION §3 table).
    mig-all-stk: first swap +9.2 s, then init at 0.1–0.8 fps until `main: You chose to start in track`
    about 52 s later.
  - Its teardown is about 30 s (m9b §6.2).
  - It needs a HOLD of its own, and it is the likeliest to fault.
- **One list for all three would tie their results together.** `game-window.sh` `wait`s on the game
  with no timeout; the watchdog only sends TERM. A game that hangs at exit blocks every later item until
  logout. Quake III's exit has never run on Phoenix (every q3 cycle ended at power-off), and it runs
  inside a signal handler (below). So quake3 goes **last** in its cycle, and STK does not follow it.
- **Launch chain:** `quake2-wl` / `quake3-wl` (ELF launchers) → `ram-stage-play` → `yquake2-wl` /
  `quake3e-wl`, all by `exec` (`ram-stage-play.c:230`). `stk-wl` → `supertuxkart-wl` by `exec`. The pid
  that `game-window.sh` signals is therefore the engine.

### Budgets, from m8a's timeline

m8a's session timeline (seconds after `/bin/xfce-session-2` started):

- the labwc socket is up at t≈8, and the autostart prints `M8 autostart` right after;
- Thunar is up and the hold starts at t≈19 (`held=10s` at t=29);
- the first game starts at t≈23 (`M8_DELAY` 15 s), about 4 s into the hold;
- `game-window-autostart.sh` sleeps 2 s between items.

| cycle | `M8_GAMES` | per game | list done | HOLD | slack | session ≈ | `--max-cmd-secs` |
|---|---|---|---|---|---|---|---|
| `m8b-quake23-window` | `quake2:90,quake3:120` | q2: start t≈23, SIGTERM ≈113, gone ≈116. q3: start ≈118, SIGTERM ≈238, gone ≈240 | t≈242 | **270** (hold over t≈289) | ≈ 47 s | 300 s | **420** |
| `m8b-stk-window` | `stk:200` | start t≈23, first swap ≈33, menu from ≈90–120 [inferred: +52 s init measured standalone, ×1.5 for the desktop], SIGTERM ≈223, gone ≤ ≈255 | t≈257 | **300** (hold over t≈319) | ≈ 62 s | 330 s | **450** |

- quake3 gets 120 s, not 90, because q3-drm at 1080p had two phases: ≈ 40 fps for the first ~65 s
  (13 windows), then 60. A 90 s slot would be mostly phase 1.
- The STK budget must pass the point where `main_loop` exists. STK's SIGTERM handler is
  `main_abort()` → `main_loop->requestAbort()` (`main.cpp:2060/2154`), and `main_loop` is created
  only **after** `initRest()` (`main.cpp:2259–2273`). A TERM during the load is lost (row S7).
- The STK slack is ≥ 60 s so that the quit script's own 30 s grace (`game-window-quit.sh`) is never
  what ends STK. Otherwise a budget mistake would read as rc=137.

**Wall clock:** each `export` and each trailing command waits the full `--idle-secs 60`. m8a's log
shows `capture-window ended after 60.8s` five times, and m8a took 11:07:36 → 11:19:18 ≈ **12 min**, not
the "≈ 7 min" in §6. Estimate for m8b: ≈ 50 s boot + 5 × 66 s exports + the session + 2 × 66 s:
**≈ 14 min for quake23 and ≈ 14.5 min for stk**. Both are over the 600 s Bash cap, so run them
**detached** (chain script / `setsid`, as m8a and m9b). Do not add a `--ready-line` to cut the export
waits: the option applies to every command, and with it silence is no longer an end condition, so each
`export` would wait out `--max-cmd-secs`.

### Commands (the m8a shape; only label, HOLD, `M8_GAMES` and `--max-cmd-secs` change)

```
./scripts/test-cycle-psh-interact.sh --label m8b-quake23-window --idle-secs 60 --max-cmd-secs 420 \
    --hdmi-dense-on 'M8 game=' -- \
    "export HOLD=270" \
    "export VERBOSE=1" \
    "export CONF_DIR=/etc/xdg/labwc-xfce-m8" \
    "export LOGOUT_CMD=/bin/game-window-quit.sh" \
    "export M8_GAMES=quake2:90,quake3:120" \
    "/bin/bash /bin/xfce-session-2" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"

./scripts/test-cycle-psh-interact.sh --label m8b-stk-window --idle-secs 60 --max-cmd-secs 450 \
    --hdmi-dense-on 'M8 game=' -- \
    "export HOLD=300" \
    "export VERBOSE=1" \
    "export CONF_DIR=/etc/xdg/labwc-xfce-m8" \
    "export LOGOUT_CMD=/bin/game-window-quit.sh" \
    "export M8_GAMES=stk:200" \
    "/bin/bash /bin/xfce-session-2" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

Run them one after the other (one UART). Order: quake23 first. It is the cheaper one, and a clean
quake2 → quake3 hand-over in one session is itself a result.

**STK is menu-only in this cycle.** `game-window.sh` passes STK nothing but
`--windowed --screensize=1280x720`, and there is no way to add a race from psh:

- `M8_GAMES` items are `<game>[:<secs>]`, and the autostart passes only `<game>`.
- `GAME_ARGS` would be word-split, but psh `export` takes one `NAME=value` per word and does not strip
  quotes (`psh/pshapp/env.c:84–101`), so a value with spaces cannot be exported.
- A one-word `GAME_ARGS` replaces the defaults and so drops `--windowed`/`--screensize`.

The m9b race figure (22.26 fps at 1280×720, fullscreen scaled) therefore has **no like-for-like row
here**. A race in a window is a follow-up that needs a staged launcher item (for example a
`stk-race` case in `game-window.sh`). It is not pre-registered.

### Staging check (2026-09-28, `ls` + `sha256sum` on `/srv/phoenix-rpi4-nfs-gcc16`)

- All 14 paths of §4 exist, and each sha256 **matches §4 exactly**:
  - the binaries: `yquake2-wl` `f1f40130…`, `quake2-wl` `f618d335…`, `quake3e-wl` `9c9c68e7…`,
    `quake3-wl` `86204741…`, `supertuxkart-wl` `9e7d92fc…`, `stk-wl` `b42afb82…`,
    `quakespasm-wl` `021cb816…`;
  - the three scripts: `5a7e54dc…`, `aad83680…`, `3cdbae19…`;
  - the four labwc-xfce-m8 files: `699b57ea…`, `b55b8d44…`, `c24f0337…`, `a6f41927…`.
- Dependencies not in §4, recorded so a later re-stage is visible:

| path | sha256 | note |
|---|---|---|
| `/bin/ram-stage-play` | `978d6f899aa8e811792922d2bb01c27ebe766a9fafcbaba255df4b6a317114b0` | re-staged 2026-09-28 10:05, **after** §4's table; both Quake launchers exec it |
| `/bin/xfce-session-2` | `41bbc44750850583300b9e691530dd9297c5368981db20a9726329f521ded580` | as in m8a |
| `/bin/labwc-2` | `3632cb541660af7132941ef37f486ea78705222df7f431e6189b17f219f205e2` | as in m8a |
| `/bin/foot-2` | `ec603ce5f8f4dc1c895c7fcb5f1d243151af038ae0d7b49c536c002fa72f0ed4` | as in m8a |

- Data present: `/usr/share/quake2/baseq2/pak0.pak`, `/usr/share/quake3/demoq3/{pak0,pak1}.pk3` +
  `autoexec.cfg`, and `/usr/share/supertuxkart/{data,stk-assets}` (46 + 149 MB).
- q3's `autoexec.cfg` sets bots (`bot_minplayers 5`, `g_spSkill 3`), a third-person orbiting camera,
  and `cg_drawFPS 1`: the same scene as mig-all-q3 (`quake3-drm +map q3dm1`).
- Also present: `/bin/bash`, `/bin/shmsrv`, `/bin/kmstest-poll`, `/usr/lib/xfce-demo/bin/{thunar,loginctl}`.

### Exit paths, and P16 [read]

| game | on SIGTERM | UART at exit | rc |
|---|---|---|---|
| yquake2 | `registerHandler()` (`glue/pl_phoenix_main.c:46`, before `SDL_Init`, so SDL's parachute leaves it) → `terminate()` sets `quitnextframe` → next frame `Cbuf_AddText("quit")` → `Sys_Quit` → `exit(0)` (`glue/pl_phoenix_sys.c:132`) | `quake2-wl: exit after <n> swaps …` (the atexit hook runs) | 0 |
| quake3e | `InitSig()` (again after `SDL_Init` in `sdl_glimp.c:604/699`) → `signal_handler`: prints `Received signal 15, exiting...`, runs `CL_Shutdown` + `SV_Shutdown` **inside the handler**, then `Sys_Exit(0)` = **`_exit(0)`** (NDEBUG: the staged ELF has no `code == 0` assert string) | **no `quake3-wl: exit after` line** (atexit is skipped), and that is **not** a failure | 0 |
| STK | `main_abort` → `requestAbort` → the loop ends (`main_loop.cpp:571`) → `cleanSuperTuxKart()` → `fclose(stderr/stdout)` → exit | `stk-wl: exit after <n> swaps …` after ≈ 5–30 s | 0 |

- **P16 does not apply on this path** [read]. `KMSDRM_DestroySurfaces` belongs to the KMSDRM device.
  With `SDL_VIDEODRIVER=wayland` only `Wayland_CreateDevice` runs. Its teardown is
  `Wayland_DestroyWindow`: `SDL_EGL_DestroySurface`, then `wl_egl_window_destroy`
  (`SDL_waylandwindow.c:2236–2243`). Mesa's `dri2_wl_destroy_surface` clears the window's
  `driver_private`, `resize_callback` and `destroy_window_callback` before it frees
  (`platform_wayland.c:971–974`). There is no GBM surface and no locked-buffer release after the free.
- **But sdl2-wl's `libSDL2.a` (`543aef39…`) has no patch 0010** (BUILD-INFO: set `afda8509…` =
  0001–0009). If any first-swap line says `video_driver KMSDRM`, P16's exit fault is expected on top of
  the plane takeover (m8a row 5's "if instead").
- **Rebuild warning.** `sdl2-wl/build.sh` globs `sdl2-drm/patches/*.patch`, so its next run will pick
  up 0010 by itself. That changes the stamp and overwrites `build-out/quakespasm-wl`, the addr2line
  reference. Its BUILD-INFO line still hardcodes the text "0001-0009". Do not rebuild before both m8b
  cycles are graded.
- The **`fclose(stdout)` UAF** (STK's exit, M3) is fixed by libphoenix `bf35aaf` (2026-09-27, on
  `master`). The sysroot `libphoenix.a` `83c07cf81b47e3f8` that the clones link comes from the
  2026-09-28 07:36 image build, so the fix is predicted present [inferred]. m9b-stk-1080's clean exit
  agrees.

### Grade

- `grep -a -E '^M8 |^XFCE|^XFCE-SESSION|ram-stage:|quake2-wl|quake3-wl|stk-wl|Yamagi|Refresh:|SDL video driver|SDL using driver|GL_RENDERER|GL_VERSION|IrrDriver: OpenGL|Using renderer|Received signal|DOUBLE SIGNAL|frames in .* fps|V3DA srv import|os_same_file|foreign_kmsbuf|alias=1|console handover|^SHMSRV |^KMSTEST ' <log>`
- `./scripts/uart-summary.sh <label>`
- `./scripts/flipstat-summary.sh --seq <label>`
- Allow ~1.3 % UART line corruption; EL0 dumps print twice.

**fps metric** (as m9b):

- Use the `<name> flipstat … = X fps` windows of the steady phase, dropping the first steady window and
  the last one (cut by the SIGTERM).
- quake2: windows after the demo ramp (fps > 20).
- quake3: all windows after the first.
- STK: the contiguous run of windows with fps > 3 at the end, i.e. the menu.
- Report the median and the range.

**Why each fps prediction is what it is** [inferred]:

- **Scaling by pixel count only works for an uncapped rate.** quakespasm went from 46 fps at 1080p
  (vsync-bound in `-drm`) to 45–68 at 720p in a window (m8a).
- **q2** asks for `swap_interval 1`. SDL's Wayland GLES swap then waits for the compositor's frame
  callback, so labwc's 60 Hz output caps it, as weston-simple-egl in m8a (57–60). Here
  `60.00` is the *expected* reading, not the pacing anomaly it was in m8a row 7. q2-drm was already
  60.00 at 1080p (vsync), so 720p leaves only headroom.
- **q3** uses `swap_interval 0`, so it is not paced. At 1080p it was capped at 60 by one flip in flight
  in phase 2 and ran ≈ 40 in phase 1. At 44 % of the pixels, with labwc's 1080p composition also on the
  V3D: phase 1 ≈ 50–75, phase 2 ≈ 65–100.
- **STK**: the main menu has no 3D scene, no deferred RTTs and no physics, so neither the GPU term
  (≈ 37 ms/frame at 720p RTTs) nor most of the ≈ 40 ms CPU term of the race applies. The throttle is
  `max_fps` 120 (`user_config.hpp:659`), `swap-interval` 0. No lane has ever measured this, hence a
  wide band.

### Rows: `m8b-quake23-window`

m8a rows 1, 2, 11, 12 and 13 apply unchanged (session, autostart line
`M8 autostart games=quake2:90,quake3:120 delay=15s display=wayland-0`, stop, SHMSRV/KMSTEST, faults).
Game rows:

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| Q1 | `M8 game=quake2 start bin=/usr/bin/quake2-wl app_id=quake2-wl window=1280x720 secs=90 display=wayland-0 driver=wayland args=+set vid_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720`; `ram-stage: … DONE creating RAM disk: 1 files, 47.64 MiB in <2–6> s`, `ram-stage: exec /usr/bin/yquake2-wl` | once | `FAIL … not staged` / `no Wayland socket`: staging or row 2 |
| Q2 | `quake2-wl: windowed GPU game -- … EGL wayland (GLES) …`, `SDL video driver is "wayland".`, `Refresh: Yamagi Quake II OpenGL ES3 Refresher` | once each | `"KMSDRM"`: no socket/`wl_display_connect` failed; KMSDRM then takes the planes, and **P16 applies at exit** (no 0010 in sdl2-wl) |
| Q3 | `GL_RENDERER: V3D 4.2.14.0`, `GL_VERSION: OpenGL ES 3.1 Mesa 26.2.0 …` | as mig-all-q2 | `llvmpipe`: m8a row 6 |
| Q4 | **`quake2-wl: first swap … video_driver wayland window 1280x720 drawable 1280x720 windowed context GLES 3.0 swap_interval 1 flipstat on`** | the hook prints the **requested** attributes (`SDL_GL_GetAttribute` returns `gl_config`, `SDL_video.c:3984`); ref_gl3-ES asks for 3.0 ES (`gl3_sdl.c:249–251`) | `1920x1080` / `fullscreen`: the later `+set`s lost to the launcher's `vid_fullscreen 2` + 1920×1080 (yquake2 applies them in order) |
| Q5 | **`quake2-wl flipstat …`: a ramp of ≈ 15–20 s at 0.2–3 fps (demo1 loading, as q2-drm's first 3 windows), then steady** | **steady 55–60, most windows 59–60.0** (frame-callback paced), ≈ 12 steady windows | exactly **30.00**: labwc repaints the client at half rate; **> 61**: the swap interval was not honoured (SDL did not wait for the frame callback); < 45: a composition or present stall (`swapstat swap_us_avg`, labwc's own frame time) |
| Q6 | render server `V3DA srv import … ns=v3dbuf` for the new client (≥ 2), no `import FAIL`; 0 `os_same_file_description` from quake2-wl | as m8a row 8 | as m8a row 8 |
| Q7 | **HDMI** (dense from `M8 game=quake2 start`): the m7l desktop, Thunar top left, foot below it, and at (636, 40) a labwc-decorated window **titled `Yamagi Quake II`** (`glimp_sdl2.c:102`) with demo1 playing, lit and textured; the panel's task list shows it beside `File System - Thunar` and `foot` | the showcase frame, q2 | centred or overlapping: the `quake2-wl` window rule did not match; black: compare Q5; half frames: G6 |
| Q8 | **exit:** `M8 game=quake2 time up (90 s): SIGTERM`, then `quake2-wl: exit after <n> swaps in <ms> ms since the first swap`, **`M8 game=quake2 exited rc=0 (clean exit) ran_s=<90–93>`** | clean: `quitnextframe` → `quit` → `exit(0)` | `rc=143`: the TERM landed before `registerHandler()` (not possible at 90 s) or the handler was replaced; no `exited` line: a hang in `VID_Shutdown` / Wayland teardown, and quake3 never starts (the list waits): record it, and **void rows T1–T8**, not fail them |
| T1 | `M8 game=quake3 start bin=/usr/bin/quake3-wl app_id=quake3-wl window=1280x720 secs=120 … args=+set r_fullscreen 0 +set r_mode -1 +set r_customWidth 1280 +set r_customHeight 720 +map q3dm1`; `ram-stage: … 4 files, 45.77 MiB in <2–6> s`; `ram-stage: exec /usr/bin/quake3e-wl` | about 2 s after Q8 | `staging FAILED … tmpfs full?`: `/tmp` (93 MiB of the 256 MiB cap should fit, together with the session's files) |
| T2 | `quake3-wl: windowed GPU game -- … EGL wayland (desktop GL) …`, **`SDL using driver "wayland"`** | once each | `"KMSDRM"`: as Q2 |
| T3 | `GL_RENDERER: V3D 4.2.14.0`, `GL_VERSION: 3.1 Mesa 26.2.0 …` | as mig-all-q3 | GLES / an ES version: the desktop-GL half not linked (sha, §4) |
| T4 | **`quake3-wl: first swap … video_driver wayland window 1280x720 drawable 1280x720 windowed context GL 2.1 swap_interval 0 flipstat on`** | `GL 2.1` = SDL's default request, as quakespasm-wl in m8a (whose `GL_VERSION` was 3.1); `r_swapInterval` 0 | `fullscreen`/1920×1080: `r_fullscreen 0` / `r_mode -1` lost (the launcher adds no video args, so this would be the engine) |
| T5 | **`quake3-wl flipstat …`**, ≈ 22 windows | **phase 1 ≈ 50–75, phase 2 ≈ 65–100; median ≥ 55** (not paced: SDL does not wait, and Mesa keeps up to 4 buffers) | ≤ 40 throughout: no gain from the smaller window. Compare `swapstat swap_us_avg` with m8a's 1.5 ms, and check `V3DA srv qstat` busy. Exactly 60.00: frame-callback pacing although `swap_interval 0` |
| T6 | `V3DA srv import … ns=v3dbuf` for the third client; no `import FAIL` | as Q6 | — |
| T7 | **HDMI**: the window **titled `Quake 3: Arena`** (`CLIENT_WINDOW_TITLE`) at (636, 40): q3dm1 in third person with the camera orbiting the player, bots, and the `cg_drawFPS` counter at top right; quake2's window gone from the task list | the showcase frame, q3 | the q2 window still listed: the q2 client's teardown left an xdg toplevel (labwc lines) |
| T8 | **exit:** `M8 game=quake3 time up (120 s): SIGTERM`, `Received signal 15, exiting...`, q3's shutdown lines (`RE_Shutdown`), **no `quake3-wl: exit after` line**, **`M8 game=quake3 exited rc=0 (clean exit) ran_s=<120–123>`**, then `M8 autostart done` **before** `XFCE hold over` | `_exit(0)` from the handler | no `exited` line before the hold ends: the shutdown hung **inside the signal handler** (for example a lock that the interrupted frame held). The quit script's TERM then gives `DOUBLE SIGNAL FAULT: Received signal 15, exiting...` → `rc=1`, or a SIGKILL → `rc=137`. Record the last q3 line before the hang |
| QZ | at logout: `XFCE log logout-cmd: M8 quit: no game running`, `M8 quit: logout requested rc=0`; `XFCE-SESSION done rc=0`; `SHMSRV stats rc=0 live=0 bytes=0`; `KMSTEST stats … bos=0 exports=0`; **0 kernel, 0 EL0 dumps** | both games already gone | `M8 quit: SIGTERM to quake3`: T8 hung; `live>0`: one of the two clients leaked its SDL cursor pool or keymap map (m8a row 12) |

### Rows: `m8b-stk-window`

m8a rows 1, 2, 11, 12 and 13 apply unchanged (autostart line `M8 autostart games=stk:200 delay=15s display=wayland-0`).

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| S1 | `M8 game=stk start bin=/bin/stk-wl app_id=stk-wl window=1280x720 secs=200 … args=--windowed --screensize=1280x720`; `stk-wl: DATADIR=/usr/share/supertuxkart ASSETS_DIR=/usr/share/supertuxkart/stk-assets SAVEDIR=/tmp/stk`; `stk-wl: exec /usr/bin/supertuxkart-wl` | once | `stk: DATADIR` / `stk-drm:`: the wrong launcher staged |
| S2 | `stk-wl: windowed GPU game -- … EGL wayland (GLES) …`; `IrrDriver: OpenGL renderer: V3D 4.2.14.0`, `Using renderer: OpenGL ES 3.1 Mesa 26.2.0 …` | once | llvmpipe: m8a row 6 |
| S3 | **`stk-wl: first swap … video_driver wayland window 1280x720 drawable 1280x720 windowed context GLES 3.0 swap_interval 0 flipstat on`**, about 10–15 s after start | Irrlicht asks for GLES 3.0 (`CIrrDeviceSDL.cpp:523–524`); STK's `swap-interval` defaults to 0 | `fullscreen` / 1920×1080: `--windowed` did not override the launcher's `--fullscreen` (`main.cpp:889/904`), or `--screensize` was rejected as a duplicate (`Invalid parameter`: the launcher's `user_overrides` did not drop its default); `KMSDRM`: as Q2, P16 at exit |
| S4 | load: `ShaderFilesManager: Compiling shader: …` lines (cold, every run), `stk-wl flipstat` at 0.1–1 fps for ≈ 60–90 s | as mig-all-stk's 52 s of init, slower beside the desktop | `wedge`/`TIMEOUT` in `V3DA srv qstat`: record it and grade nothing after |
| S5 | **menu fps:** a contiguous run of `stk-wl flipstat` windows > 3 fps after the load, ≈ 16–22 windows | **20–60, median ≈ 35** [inferred, never measured on any lane]. Informative outcome: **above the 22.26 fps race rate at 720p** (the menu has neither the GPU term nor most of the CPU term) | < 10: the 2D GUI path is expensive on this lane (a profile question, not an M8 failure); ≈ 120: the `max_fps` throttle is the bound; exactly 60.00: frame-callback pacing despite `swap_interval 0` |
| S6 | **HDMI**: during the load, the window at (636, 40) with STK's loading screen (a black or partial frame in the first ≈ 80 s is the load, not a failure); then the **main menu** (logo, *Story Mode / Singleplayer / …* buttons, peach skin) in a window **titled `SuperTuxKart`** (`irr_driver.cpp:825`) beside Thunar and foot; no first-run dialogs (the launcher seeds `players.xml` + `config.xml`) | the showcase frame, STK | a register/tutorial/internet dialog: the seed files were not written to `/tmp/stk/config-0.10/`; the window centred: the `stk-wl` rule |
| S7 | **exit:** `M8 game=stk time up (200 s): SIGTERM`, then within ≈ 30 s `stk-wl: exit after <n> swaps in <ms> ms since the first swap`, **`M8 game=stk exited rc=0 (clean exit) ran_s=<200–235>`**, `M8 autostart done` before `XFCE hold over` | `requestAbort` → loop ends → `cleanSuperTuxKart` → exit | STK still at < 1 fps when time runs up: the TERM came before `main_loop` existed and **was lost**. STK then runs until logout, and the quit script's TERM (clean) or its 30 s SIGKILL (`rc=137`) ends it; that is a budget miss, not an exit bug. An EL0 dump at exit: addr2line against `tools/gpu-lane/sdl2-wl/build-out/stk-wl/supertuxkart-wl`. A pc in `release_buffer`/`KMSDRM_*` is impossible on the Wayland path; `fclose`/`fflush` would mean the UAF fix is missing from the sysroot |
| SZ | `M8 quit: no game running`; `XFCE-SESSION done rc=0`; `SHMSRV … live=0 bytes=0`; `KMSTEST … bos=0 exports=0`; `V3DA srv qstat … err=0 wedges=0 rej=0`; **0 kernel, 0 EL0 dumps** | clean | as QZ |

Not graded (as m8a): labwc's `did not respond to configure request` warnings, SDL `xdg_activation`
notes, STK's `FontManager … NotoColorEmoji.ttf doesn't have color` and `kartDirt shader is missing`
(both in all 5 STK runs of 2026-09-27/28: mig-all-stk, m9b-stk-*).

**Decides:**

- Q4/Q5/Q7/Q8, T4/T5/T7/T8 and S3/S5/S6/S7 = the four M8 games each GPU-rendered in a decorated window
  on the XFCE desktop, at a measured fps, with a clean quit. That leaves m8d (the showcase recording)
  and the STK race-in-a-window follow-up.
- A T8 hang alone = the next step is quake3e's shutdown-in-a-signal-handler (a `quitnextframe`-style
  deferral, as yquake2 does), not the Wayland path.

## Result — `m8b-quake23-window` + `m8b-stk-window` (chain93, 2026-09-28 13:24 / 13:39): ✅ PASS, one pre-registered exit hang

**All three games render on the V3D in decorated labwc windows on the XFCE desktop, next to
Thunar and foot.** HDMI (`artifacts/hdmi/20260928-133218-m8b-quake23-window-tick.png`: Quake II
demo1 at 59.26 fps; `…133429…`: `Quake 3: Arena` q3dm1 at 63 fps, quake2 already gone from the task
list; `20260928-134920-m8b-stk-window-tick.png`: the SuperTuxKart main menu in a window titled
`SuperTuxKart`).

| Row | Reading | vs prediction |
|---|---|---|
| Q4 / T4 / S3 | first swap: `video_driver wayland window 1280x720 drawable 1280x720 windowed`, contexts GLES 3.0 / GL 2.1 / GLES 3.0, swap_interval 1 / 0 / 0 | as predicted |
| Q5 | quake2-wl **median 59.99 fps** (n = 7 steady windows) | 55–60 ✅ (fewer windows than the ≈ 12 expected: the demo ramp was longer) |
| T5 | quake3-wl **median 89.94 fps** (n = 21) | 65–100 ✅ |
| S5 | stk-wl menu **median 89.7 fps** (n = 25) | above the 20–60 band, below the 120 `max_fps` throttle: the menu is far cheaper than guessed |
| Q8 | `quake2-wl: exit after 2060 swaps`, `exited rc=0 (clean exit) ran_s=90` | ✅ |
| **T8** | `Received signal 15, exiting...`, `----- Client Shutdown (Signal caught (15)) -----`, **last line `RE_Shutdown( 3 )`**, then nothing until the quit script's SIGKILL: `exited rc=137 (killed by SIGKILL) ran_s=210` | ❌ **the pre-registered "if instead"**: quake3e's shutdown hangs **inside the signal handler**, in the renderer shutdown |
| S7 | `stk-wl: exit after 11032 swaps in 183889 ms`, `exited rc=0 (clean exit) ran_s=201` | ✅ |
| QZ / SZ | `XFCE-SESSION done rc=0` both; **0 kernel, 0 EL0 dumps** (`exc=0`) | ✅ |

**Decides** (as registered): the Quake II, Quake 3 and STK windowed goals are met; the one defect
is quake3e's signal-handler shutdown, so the next step is a `quitnextframe`-style deferral (set a
flag in the handler, quit from the main loop, as yquake2 does), not the Wayland path. Left: that
fix, m8d (the showcase recording), an STK race in a window, and vkQuake (V3DV WSI, §5).

## Pre-registered: `m8b2-quake3-exit` (2026-09-29) — quake3e quits on SIGTERM from its main loop

**Change:** external/quake3e `Phoenix: quit on SIGTERM from the main loop` (fork, local) → ports
`fix/quake3-sigterm` `fea5ce2` (regenerated patch + glue). The SIGTERM/SIGHUP/SIGQUIT handler now only
sets a flag, and the glue's main loop calls `Com_Quit_f()` between frames. A second request or a
fault still takes the in-handler path. **Built** with no image build: the shipped port tree was
copied, `linux_signals.o` and `pl_phoenix_main.o` were recompiled with the recorded
`Q3_BASE_CFLAGS`, and `build-quake3-wl.sh --no-control` relinked (`Q3WL_PORT_SHADOW`).
**Staged** `/usr/bin/quake3e-wl` sha `a988e6f9…`; the old one is kept as
`/usr/bin/quake3e-wl.pre-sig`. **Gate string:** `Termination requested` is in the new stripped
binary (1) and not in the old one (0).

```
./scripts/test-cycle-psh-interact.sh --label m8b2-quake3-exit --idle-secs 60 --max-cmd-secs 330 \
    --hdmi-dense-on 'M8 game=' -- \
    "export HOLD=180" "export VERBOSE=1" "export CONF_DIR=/etc/xdg/labwc-xfce-m8" \
    "export LOGOUT_CMD=/bin/game-window-quit.sh" "export M8_GAMES=quake3:90" \
    "/bin/bash /bin/xfce-session-2" "/bin/shmsrv -s" "/bin/kmstest-poll stats"
```

| # | Line | Predicted | If instead… |
|---|---|---|---|
| E1 | `M8 game=quake3 time up (90 s): SIGTERM`, then **`Termination requested, quitting from the main loop`** within one frame | the flag path | `Received signal 15, exiting...`: the old binary (check the sha) |
| E2 | q3's normal quit (`----- Client Shutdown (Client quit) -----` or similar), `RE_Shutdown( 1 )`, **`quake3-wl: exit after <n> swaps`** (the atexit hook now runs, since `Com_Quit_f` → `Sys_Quit` → `exit`), **`M8 game=quake3 exited rc=0 (clean exit) ran_s=90–93`** | a clean exit | a hang after E1: the normal shutdown path also blocks → record its last line (then the problem is RE_Shutdown itself, not the signal context) |
| E3 | `M8 quit: no game running`, `XFCE-SESSION done rc=0`, 0 kernel / 0 EL0 dumps | clean | — |
| E4 | quake3-wl fps as m8b T5 (median 65–100) | unchanged by the fix | < 55: a regression from the relink (compare the sha / BUILD-INFO) |

**Result `m8b2-quake3-exit` (chain95, 2026-09-29 21:25): ✅ PASS.** `M8 game=quake3 time up (90 s): SIGTERM` →
`Termination requested, quitting from the main loop` → `RE_Shutdown( 3 )` → **`exited rc=0 (clean exit) ran_s=91`**;
`M8 quit: no game running`, `XFCE-SESSION done rc=0`, 0 dumps. fps median **70.2** (n = 15, E4 band 65–100).
E2's `quake3-wl: exit after` line is absent and that is correct: `Sys_Exit()` is `_exit()` under NDEBUG
(`pl_phoenix_main.c:279`), so the atexit hook never runs. Fix merged: ports `master` (`fix/quake3-sigterm`
`fea5ce2`). `quake3_drm` and the old `quake3` pick it up at their next port build (the P1 image).

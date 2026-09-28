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
shipped engines. **No Pi cycle yet**: `m8a-quake-window` is pre-registered (§6). vkQuake is deferred (§5).

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

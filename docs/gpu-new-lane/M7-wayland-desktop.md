# M7 — a lightweight Wayland desktop on the new GPU lane

Owner request 2026-09-27: "a selected, modern, lightweight desktop environment / window manager for Wayland …
light and minimal, but modern and currently maintained", scheduled **as a continuation** of the current work.
The M1–M6 queue and the migration continue in parallel. M7 is new-lane only (new dirs and binaries). The old X
desktop (`startx_gpu`) stays untouched until the migration retires it.

## Choice

| role | pick | why |
|---|---|---|
| compositor / WM | **labwc** (wlroots, stacking, Openbox-style) | small C codebase, monthly releases, and Raspberry Pi OS's default desktop since 2024, so it is proven on this SoC; configured by XML/INI files, with no settings daemon |
| library | **wlroots** 0.18/0.19 (whichever labwc release pins) | the DRM/GBM/EGL/libinput/libseat backends are the same set M6 already ported for Weston |
| terminal (**required**, owner 2026-09-27) | **foot** | a modern terminal: Wayland-native, true colour, full Unicode with font fallback (fcft + harfbuzz), fast CPU rendering, scrollback, clipboard, URL detection; small (fontconfig/freetype/pixman/xkbcommon + fcft/tllist, MIT) |
| file manager (**required**, owner 2026-09-27) | **PCManFM** (GTK3, native Wayland backend), with **Midnight Commander** (`mc`, already ported) in foot as the stage-1 file manager | PCManFM is what Raspberry Pi OS ships with labwc: light, maintained, single-process; GTK3 also enables the GTK panel (sfwbar). Thunar needs the XFCE libraries; Nautilus is far heavier |
| launcher | **fuzzel** | Wayland-native, small, same dependencies as foot |
| wallpaper | **swaybg** | tiny; cairo only |
| panel (stage 2) | **sfwbar** (GTK3) or **yambar** (no GTK) | a panel is optional for "minimal"; yambar first if GTK3 is too much |

**Toolkit: GTK3 now, GTK4 next (owner question 2026-09-27).** The file manager decides the toolkit. PCManFM,
Thunar, Nemo and Caja are all GTK3, as are the light GTK panels (sfwbar), and Raspberry Pi OS's labwc desktop
is GTK3 for that reason. The GTK4 file manager is Nautilus, which pulls in libadwaita and expects
tracker/localsearch and gvfs, so it is not "light". GTK4 is added as the **next M7 stage** (`m7g-gtk4`:
gtk4-demo / a GTK4 window). It reuses the GTK3 dependencies (glib, pango, cairo, harfbuzz, fontconfig, libepoxy,
the Wayland stack and compat layer), and its GL renderer (GLES ≥ 3.0; V3D provides 3.1) makes it a real
new-lane GPU test. Pin a GTK4 release whose image loading still builds with gdk-pixbuf, not only glycin (Rust).
If the owner prefers a GTK4-only desktop, the file manager becomes Nautilus, and that weight is accepted knowingly.

Rejected: sway (tiling; unfamiliar for a demo), Hyprland (heavy C++, fast-moving ABI), Wayfire (heavier plugin
stack), niri/cosmic (Rust toolchain for Phoenix is a separate project), Weston's desktop-shell (already here, but
it is a reference compositor, not a desktop).

## What M6 already gives (reused, not rebuilt)

wayland 1.24, wayland-protocols 1.45, libxkbcommon 1.7 + baked keymap, pixman, libinput 1.26 + libinput-phoenix
shim, libseat noop, libudev shim, the epoll/timerfd/signalfd/eventfd compat layer (with the m6e signalfd-ordering
fix), shmsrv for wl_shm, Mesa GBM/EGL/GLES (static, `--wayland`), libdrm-phoenix with G4 (render export), G6
(implicit sync) and G7 (card0 import → direct scanout), low-memory scanout placement (0016).

## New work

1. **wlroots**: static build against the M6 stack. The DRM backend (atomic KMS through libdrm-phoenix), the GLES2
   renderer and the pixman renderer, the libinput backend, and the session through libseat (noop). Expected gaps:
   `udev` enumeration (extend the libudev shim), `drmGetDevice2`, DRM leases (disable), Xwayland (disable in stage
   1), `timerfd`/`signalfd` (compat layer).
2. **labwc** on wlroots: static; builtin theme and config staged under `/etc/xdg/labwc/` (rc.xml, menu.xml,
   autostart that starts swaybg + foot).
3. **foot** (required, with fcft/tllist; labwc's autostart opens one foot window), then **fuzzel**, **swaybg**: static clients; fonts from the X11 port's fontconfig setup (the NFS
   fontconfig trap is in memory).
4. **File manager**: `mc` in foot from the labwc root menu at once; then **GTK3 with only the Wayland GDK backend** (new: pango, fribidi, gdk-pixbuf, libepoxy, gtk3; already built: glib2, cairo, harfbuzz, fontconfig, freetype, pixman, libpng, libffi, expat), then **libfm + menu-cache + PCManFM**. GTK3 is tested under Weston (M6) first, so it does not wait for labwc.
5. A launcher script `labwc-desktop.sh` in the same shape as `weston-m6a.sh`: it starts the servers, sets
   `XDG_RUNTIME_DIR`, starts labwc, holds, stops on SIGTERM.

## Pi milestones (pre-registered as each piece lands)

| cycle | shows |
|---|---|
| `m7a-labwc` | labwc starts on HDMI (output enabled, cursor, root menu) with pixman then GLES2 |
| `m7b-foot` | foot opens in labwc and draws text; keyboard input reaches it (`rpi4-kms -C` frees the console keyboard) |
| `m7c-desktop` | wallpaper + foot + fuzzel launcher; window move/resize with the mouse; clean exit |
| `m7e-gtk3` | a GTK3 demo window (gtk3-demo / a minimal GtkWindow) under Weston, then under labwc |
| `m7f-pcmanfm` | PCManFM browses `/` and `/usr/share` under labwc: icons, a folder open by double-click, a file copy |
| `m7g-gtk4` | GTK4 (GL renderer on V3D) window / gtk4-demo under labwc |
| `m7d-gl-client` | weston-simple-egl / kmscube-style GL client inside labwc (G4/G6/G7 in a real compositor) |

## Scheduling

Code work (builds) starts now in parallel with subagents. Pi cycles are queued after the current Pi queue
(queues 46–51: the P10 gates, vkq compute, g6-sync, m6i-low, build 18 with C3 arm B + c1b18, mig-all). Status is
tracked in [PLAN.md](PLAN.md) (M7 row) and the weekly log.

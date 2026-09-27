# M7 — a lightweight Wayland desktop on the new GPU lane

Owner request 2026-09-27: "a selected, modern, lightweight desktop environment / window manager for Wayland …
light and minimal, but modern and currently maintained", scheduled **as a continuation** of the current work.
The M1–M6 queue and the migration continue in parallel. M7 is new-lane only (new dirs and binaries). The old X
desktop (`startx_gpu`) stays untouched until the migration retires it.

## ★ Decision 2026-09-27 (owner: "analyse more deeply and choose"): **XFCE 4.20 on labwc**

XFCE's own window manager (xfwm4) is X11-only. Since 4.20 (Dec 2024) the XFCE project's Wayland session
runs its components under a wlroots compositor, and **labwc is the one it documents**. The panel and desktop use
layer-shell (gtk-layer-shell) and the window list uses libxfce4windowing (wlr-foreign-toplevel, which labwc
implements). So the labwc base below stays. XFCE on top gives a recognisable, complete desktop for a public
demo: a panel with app menu, clock and window buttons; the desktop background; **Thunar** (the file manager,
replacing the PCManFM plan); the settings manager; the app finder. The terminal stays **foot** (xfce4-terminal
needs VTE, which is heavy).

| extra piece | status / risk |
|---|---|
| gtk-layer-shell (MIT) | small |
| libxfce4util, xfconf, libxfce4ui, garcon, exo, libxfce4windowing | plain GTK3/GLib C libraries |
| xfce4-panel, Thunar, xfdesktop, xfce4-settings, xfce4-appfinder | same toolchain |
| **D-Bus session bus** | **built, host-proven, Pi cycle `m7f-dbus` pre-registered** (see [D-Bus session bus](#d-bus-session-bus-stage-3) below). xfconf (every XFCE setting) needs a bus. Phoenix AF_UNIX has `SCM_RIGHTS` but no `SO_PEERCRED`/`SCM_CREDENTIALS` (D-Bus's EXTERNAL auth). Stage 1: `dbus-daemon` with ANONYMOUS auth on `/tmp/dbus-session`. `SO_PEERCRED` is on kernel branch `feat/dbus-peercred` (not merged) |

**Staged so that something is always demo-able:**
1. labwc + foot + `mc` (the first Wayland desktop) — in progress;
2. GTK3 + gtk3-hello — in progress;
3. D-Bus session bus (`dbus-daemon` + libdbus; GIO's GDBus on top);
4. XFCE libraries, then xfce4-panel + Thunar + xfdesktop + xfce4-settings + xfce4-appfinder under labwc: **the showcase**.

Fallback if D-Bus/xfconf proves hard: the Raspberry Pi OS recipe (labwc + PCManFM + a small panel, no D-Bus).
GTK4 is not needed (owner, 2026-09-27): the light desktop ecosystem is GTK3.

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

## D-Bus session bus (stage 3)

`tools/gpu-lane/dbus/` (build.sh, conf/, hosttest/, pi/, patches/dbus/). **D-Bus 1.16.2** (latest stable,
2025-02-27; `dbus-1.16.2.tar.xz` sha256 `0ba2a1a4b16afe7b…`; AFL-2.1/GPL-2.0, tarball in tools/ only), meson
cross build, all static: `dbus-daemon`, `dbus-send`, `dbus-monitor`, `dbus-run-session`, `dbus-uuidgen`,
`libdbus-1.a` (in `build-out/destdir/usr`). Expat 2.5.0 from the ports prefix (private view).

- **Options**: unix transport only; `epoll`/`kqueue`/`inotify`/`launchd`/`systemd`/`x11_autolaunch`/`selinux`/
  `apparmor`/`libaudit` disabled, no tests or docs, `user_session=false`; `traditional_activation` **on**
  (XFCE starts `xfconfd` by bus activation: `.service` files go to `/usr/share/dbus-1/services`); `verbose_mode`
  on (`DBUS_VERBOSE=1` traces authentication). The daemon's main loop is `poll()`; config reload is SIGHUP only.
- **One patch** (`0001`): `MSG_CTRUNC`, `SOMAXCONN`, `_SC_GETPW_R_SIZE_MAX`/`_SC_GETGR_R_SIZE_MAX` for `__phoenix__`
  (libphoenix lacks them). They cannot go in a force-included header: meson's `has_function()` probes then all
  answer NO (`socket`, `socketpair`, `accept4`…), which is the "compat header in configure CFLAGS" trap; build.sh
  now refuses a build whose `socket`/`socketpair`/`accept4`/`poll` probe says NO.
- Phoenix has no credentials mechanism, so dbus-sysdeps-unix.c compiles its `#warning Socket credentials not
  supported` branch: the daemon learns no peer uid/pid.
- **Static checks**: all five programs `nm -u` = 0, no `PT_INTERP`, no `DT_NEEDED`. Stripped sha256 (first 16):
  dbus-daemon `0abfed003a78214d` (673 504 B), dbus-send `9bdc383d4c6a97c2`, dbus-monitor `be12fbefa7da2aeb`,
  dbus-run-session `10b706d7ccd28a39`, dbus-uuidgen `0dc89c58de302ae1`; `dbus-m7f.sh` `87c866f618e5c9fe`.

**Authentication (the decision).** `conf/session-phoenix.conf` (stage 1): `<listen>unix:path=/tmp/dbus-session</listen>`,
`<auth>ANONYMOUS</auth>`, `<allow_anonymous/>`, one explicit `<servicedir>`, an allow-all default policy, no
`<include>`s or standard dirs. **Lab only**: any process that can reach the socket gets onto the bus with no
identity. That is acceptable on this single-user test system and must never become a default.
`conf/session-phoenix-external.conf` (stage 2) offers EXTERNAL first and ANONYMOUS as the fallback; drop ANONYMOUS
once SO_PEERCRED ships. Clients find the bus only through `DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-session`
(no X11 autolaunch, no systemd user bus), so **the labwc/XFCE launcher must export it** before it starts anything.

- libdbus clients start with EXTERNAL and, on `REJECTED ANONYMOUS`, retry ANONYMOUS.
- **xfconf 4.20 uses GDBus only** (gio-2.0; no libdbus in `xfconf/` or `xfconfd/`). GDBus (glib 2.84 `gdbusauth.c`)
  adds ANONYMOUS, DBUS_COOKIE_SHA1 and EXTERNAL and picks, by priority, the first that the server's `REJECTED`
  list contains. There is no environment switch for this (only a `GDBusAuthObserver` in code), and none is
  needed. `G_DBUS_DEBUG=authentication` shows the choice. On Phoenix, glib has no credentials support
  (`gcredentialsprivate.h` has no `__phoenix__` case): GIO sends the NUL byte without a control message, and
  EXTERNAL would claim uid `-1`. So **GDBus needs ANONYMOUS even after SO_PEERCRED** unless glib gets a Phoenix case
  (use the Linux `struct ucred` path). That is a note for whoever builds GIO: the GTK3 work (`tools/gpu-lane/gtk3-wayland`) pins glib 2.88.3, the same series as the host test's gdbus.
- **Host test** (`hosttest/run.sh`): the same source and options built natively, running the shipped configs.
  `nopeercred.so` (LD_PRELOAD) makes `SO_PEERCRED`/`SO_PEERSEC`/`SO_PEERPIDFD`/`SO_PEERGROUPS` fail with
  ENOPROTOOPT, as on Phoenix. The results are ALL PASS:
  - A, stage-1 conf without credentials: dbus-send and GDBus 2.88 both authenticate ANONYMOUS and get the
    ListNames reply.
  - B, stage-2 conf without credentials: the daemon logs "no credentials, mechanism EXTERNAL can't
    authenticate" and both clients fall back to ANONYMOUS.
  - C, stage-2 conf with credentials (= Phoenix after SO_PEERCRED): EXTERNAL succeeds.
  - D, negative control (EXTERNAL only, no credentials): refused. So A and B are not an artefact of a preload
    that did nothing.
  The Pi script itself also ran on the host (host binaries, both arms, and once more without gdbus = 6/0/6): every step answered as predicted below. The kernel change also compiles to an object under the real -O2 -Werror flags.
  This proves the configuration, not Phoenix.
- AF_UNIX facts checked for the port: libphoenix `sendmsg`/`recvmsg` gather and scatter multi-iovec messages
  (the kernel takes one iovec), so D-Bus's two-vector header+body writes are fine. `sysconf(_SC_OPEN_MAX)` = 1024
  bounds the fd-closing loops. `getrlimit` reports unlimited, and the daemon copes.

**SO_PEERCRED (kernel follow-up, branch only).** `phoenix-rtos-kernel` `feat/dbus-peercred` **63b35c27**:
`SO_PEERCRED` = 0x1022 (OpenBSD's value, outside lwIP's 0x1001–0x100b) and `struct ucred {pid, uid, gid}` (the
Linux layout) in `include/posix-socket.h`, which libphoenix includes as `<phoenix/posix-socket.h>`, so libphoenix
needs no change. In `posix/usocket.c`, Linux semantics: the accepted socket reports the `connect()`er, the
connector reports the `listen()`er, and both ends of a `socketpair()` report its creator. No peer gives ENOTCONN.
uid/gid are 0 (the kernel has no users, the same answer `getuid()` gives). It passes the kernel's -Werror flags
(`usocket.c`, `posix.c`, `syscalls.c`, `inet.c`). dbus-sysdeps-unix.c compiled against the new header takes the
SO_PEERCRED path (no `#warning`).

Test: `phoenix-rtos-tests` `feat/dbus-peercred` **73531f5**, `test-libc-unix-socket`: `peercred_socketpair`
(stream/dgram/seqpacket), `peercred_connect` (fork; both directions; stream/seqpacket), `peercred_unconnected`.
It has a local fallback definition, so on today's kernel it builds and fails with ENOPROTOOPT. It passes on
Linux (41/41), and has not run on Phoenix yet.

Two consequences:
1. userspace built against this header needs the new kernel. libwayland's `wl_os_socket_peercred()` then takes
   its `#elif defined(SO_PEERCRED)` branch, and on an old kernel every Wayland client would be refused. Merge the
   kernel and the sysroot together.
2. The stage-1 binaries were built against the master sysroot, so EXTERNAL needs a D-Bus rebuild after the
   merge (`dbus-daemon` strings then contain `SO_PEERCRED`; build.sh prints the count).

### Pi cycle `m7f-dbus` (pre-registered)

**Staged 2026-09-27**, new names only; nothing existed before (checked). `/srv/phoenix-rpi4-nfs-gcc16/bin/{dbus-daemon,
dbus-send,dbus-monitor,dbus-run-session,dbus-uuidgen,dbus-m7f.sh}` (`sudo -n install -m 755 …`),
`/etc/dbus-1/session-phoenix{,-external}.conf`, the empty `/usr/share/dbus-1/services/`, and `/etc/machine-id` (was
absent; `GetMachineId` reads it). There is **no `gdbus` in the export** and no GIO in the ports prefix (glib 2.56,
core only), so step 5 reports `gdbus=absent` until the GTK3 work stages GIO. Re-run with `GDBUS=<path>` then.

Commands (psh; no Mesa and no KMS needed): `/bin/bash /bin/dbus-m7f.sh anon`, then
`/bin/bash /bin/dbus-m7f.sh external`. `--capture-secs 240`; the script takes about 30 s per arm, and prints a
`DBUSPHX waiting…` line every 10 s while it waits for the socket.

| # | `DBUSPHX` line | prediction (anon) | prediction (external) |
|---|---|---|---|
| 1 | `socket=up wait_s=` | ≤ 5 s; `printed_address=unix:path=/tmp/dbus-session,guid=…` | same |
| 2 | `listnames rc=` | 0, reply contains `"org.freedesktop.DBus"` | 0 |
| 3 | `ping rc=` / `busid rc=` / `creds rc=` | 0 / 0 (32-hex id) / 0 (`ProcessID` = daemon pid) | same |
| 4 | `monitor seen_member= seen_payload=` | 1 / 1 (the bus routed a signal between two clients) | same |
| 5 | `gdbus` | `absent` (today); with GIO: rc 0, `tried_anonymous=1`, `tried_external=0` | with GIO: `tried_external=1 tried_anonymous=1` |
| 6 | `daemon exited rc= … socket=` | rc 0, `after_term_s` ≤ 5, `socket=gone` | same |
| 7 | `auth anonymous= external= external_no_credentials=` | ≥ 6 / 0 / 0 | ≥ 6 / **0** / ≥ 6 (EXTERNAL rejected: no SO_PEERCRED) |
| 8 | `log: … Credentials:` | pid and uid UNSET (`Socket credentials not supported`) | same |

Failure reading: a daemon that exits before its socket appears prints its last 40 log lines (config parse,
`bind()`, or `/tmp` problems). `rc≠0` on step 2 with `auth anonymous=0` means an authentication problem. With
`anonymous≥1` it means the dispatch/transport side (e.g. `sendmsg`/`poll` wakeups). After SO_PEERCRED is merged and
D-Bus rebuilt, the external arm must flip to `external≥6 external_no_credentials=0`, and line 8 must show the
client pid.

## Pi milestones (pre-registered as each piece lands)

| cycle | shows |
|---|---|
| `m7a-labwc` | labwc starts on HDMI (output enabled, cursor, root menu) with pixman then GLES2 |
| `m7b-foot` | foot opens in labwc and draws text; keyboard input reaches it (`rpi4-kms -C` frees the console keyboard) |
| `m7c-desktop` | wallpaper + foot + fuzzel launcher; window move/resize with the mouse; clean exit |
| `m7e-gtk3` | a GTK3 demo window (gtk3-demo / a minimal GtkWindow) under Weston, then under labwc |
| `m7f-dbus` | `dbus-daemon --session` up; `dbus-send` ping round trip; GDBus client connects |
| `m7h-xfce` | ★ labwc + xfce4-panel + xfdesktop + Thunar + foot: the showcase desktop; Thunar browses `/`, the panel's app menu launches foot |
| `m7d-gl-client` | weston-simple-egl / kmscube-style GL client inside labwc (G4/G6/G7 in a real compositor) |

## Scheduling

Code work (builds) starts now in parallel with subagents. Pi cycles are queued after the current Pi queue
(queues 46–51: the P10 gates, vkq compute, g6-sync, m6i-low, build 18 with C3 arm B + c1b18, mig-all). Status is
tracked in [PLAN.md](PLAN.md) (M7 row) and the weekly log.

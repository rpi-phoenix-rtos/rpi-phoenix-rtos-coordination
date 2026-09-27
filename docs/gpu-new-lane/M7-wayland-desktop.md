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
1. labwc + foot + `mc` (+ fuzzel, swaybg) (the first Wayland desktop) — **built and staged, `m7a-labwc`/`m7b-foot`/`m7c-desktop` pre-registered** ([stage 1](#stage-1-built-wlroots-020--labwc-020--foot-128-toolsgpu-lanelabwc-drm));
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

## Stage 1 built: wlroots 0.20 + labwc 0.20 + foot 1.28 (`tools/gpu-lane/labwc-drm/`)

**Status 2026-09-27:** built static for aarch64-phoenix, host-tested where the compat layer is new, staged under
new names, Pi cycles `m7a-labwc` and `m7b-foot` pre-registered below. No Pi run yet, no sibling repo touched.

### Versions and licences

| package | version | licence | from |
|---|---|---|---|
| **labwc** | **0.20.2** (2026-08-21, latest) | GPL-2.0-only (tools/ only) | GitHub tag tarball |
| **wlroots** | **0.20.2** (labwc 0.20.2 pins `>=0.20.1 <0.21`) | MIT | gitlab release |
| **foot** / fcft / tllist | **1.28.0** / 3.3.3 / 1.1.0 | MIT | codeberg tag tarballs |
| wayland (M6 patch) | 1.24.0 (= host scanner) | MIT | as M6 |
| wayland-protocols | **1.49** (wlroots needs ≥ 1.47) | MIT | gitlab release |
| libxkbcommon | **1.13.2** (wlroots needs ≥ 1.8) | MIT | GitHub tag |
| pixman | **0.46.4** (wlroots' pixman renderer needs ≥ 0.46; ports has 0.42.2) | MIT | cairographics.org |
| libdisplay-info, seatd/libseat (M6 patches), libinput header | 0.2.0, 0.9.1, 1.26.2 | MIT | as M6 |
| libxml2 | 2.15.4 (tree API only) | MIT | gnome |
| fribidi | 1.0.16 | LGPL-2.1+ | GitHub release |
| pango | **1.44.7** (see below) | LGPL-2.0+ | gnome |
| cairo 1.16.0, harfbuzz 14.4.0, fontconfig 2.14.2, freetype, libpng16, **GLib 2.56.4** (+gobject), expat, zlib, libffi, libiconv | ports prefix | MPL/LGPL/MIT/FTL | private per-library views (`build-out/deps/`) |
| Mesa GBM/EGL/GLES | `mesa-drm/build-out-wayland-low` (0012 + 0016), not rebuilt | MIT | M6 §18 |
| libdrm-phoenix | `libdrm-phoenix/build-out-low/prefix` (proto 5: G4 + G6 + G7 + low placement), snapshotted | MIT | M6 §18 |

All tarballs are sha256-pinned in `build.sh`. wlroots options: `-Dbackends=drm,libinput` (headless is always
built) `-Drenderers=gles2` (pixman always) `-Dallocators=gbm` (dumb always) `-Dsession=enabled
-Dxwayland=disabled -Dlibliftoff=disabled -Dcolor-management=disabled -Dexamples=false`; the setup log confirms
`drm-backend libinput-backend gles2-renderer gbm-allocator session egl: YES`, `render/dmabuf_fallback.c`
(not the Linux sync-file path), `HAVE_LINUX_SYNC_FILE 0`. labwc: `-Dxwayland=disabled -Dsvg=disabled
-Dicon=disabled -Dnls=disabled -Dlabnag=disabled`. foot: `-Dterminfo=disabled -Ddefault-terminfo=xterm-256color
-Dgrapheme-clustering=disabled -Dutmp-backend=none -Dthemes=false`; fcft `-Dsvg-backend=none
-Drun-shaping=disabled -Dgrapheme-shaping=enabled`.

**Text stack choice (labwc hard-requires pangocairo, libxml2, GLib).** No pango/fribidi/libxml2 in the ports tree;
cairo, harfbuzz, fontconfig, freetype and GLib are (static). pango ≥ 1.44 needs GLib ≥ 2.59, the ports GLib is 2.56
(the last autotools series). **pango 1.44.7 builds and links against 2.56 unchanged** once its meson requirement is
lowered (patch 0001; `-Werror=implicit` is on, so no newer GLib call slipped through). 1.44 is also the first
pango with `pango_context_set_round_glyph_positions()`, which labwc calls. Two small stand-ins, declared where
the real ones would be, in the private views only: `hb_glib_script_{to,from}_script()` (the ports HarfBuzz has no
`hb-glib`; two ISO 15924 round trips) and GLib 2.68's `g_string_replace()` (labwc). Once the GTK3 lane
(`tools/gpu-lane/gtk3-wayland/`, GLib 2.88) stages its own GLib/pango, labwc can switch to them (one `--glib`
prefix change) and drop both stand-ins.

### What was patched and why

| package | patch | why |
|---|---|---|
| wayland-protocols | 0001 `meson: validate strictly only with wayland-scanner >= 1.25` | 1.49's XML has the `frozen` interface attribute (1.25 DTD); the 1.24 scanner's `--strict` makes every enum header fail. The attribute does not change generated code (checked: it is the only DTD difference) |
| wlroots | 0001 `meson: librt is optional` | no librt on Phoenix |
| wlroots | 0002 `util/shm: tolerate fchmod() failure on Phoenix-RTOS` | `allocate_shm_file_pair()` (the keymap fd pair) `fchmod(rw, 0)`s the object; shmsrv objects have no mode (mtSetAttr → ENOSYS), so **every keymap would fail** and no client gets a keyboard. Host-proven with a negative control |
| wlroots | 0003 `allocator: share the backend's DRM client on Phoenix-RTOS` | wlroots gives its allocator a **second DRM descriptor** (an empty lease, else a plain `open()`). On Phoenix each `open()` is a separate rpi4-kms client, and rpi4-kms refuses another client's `/kmsbuf` export (`kms_bo.c` `why=foreign_kmsbuf`), so **every buffer would fail ADDFB2 in both renderer arms**. `F_DUPFD_CLOEXEC` shares the backend's client (libdrm-phoenix and rpi4-kms key clients by open file: `mtOpen` → `oid.id`) |
| labwc | 0001 `server: reap children with waitpid() where waitid() does not exist` | no `waitid()`/`WNOWAIT` in libphoenix; the peek only protects Xwayland's start-up |
| labwc | 0002 `keyboard: fall back to a builtin XKB keymap` | no xkeyboard-config data: the rule names and the `us` fallback both fail and the keyboard group stays without a keymap. Weak `labwc_builtin_xkb_keymap` (evdev/pc105/us compiled on the build host by a native libxkbcommon 1.13.2), as Weston 0003. (libxkbcommon ≥ 1.8 creates contexts with lazy include paths, so Weston's 0007 is not needed) |
| foot | 0001 `shm: no scrollable pool size without FALLOC_FL_PUNCH_HOLE` | foot sizes a scrollable pool to 512 MiB **before** it learns hole punching is unavailable; on a shmsrv memfd (contiguous) that first `ftruncate()` fails and foot never gets a buffer. `foot.ini` also sets `max-shm-pool-size-mb=0` |
| fribidi | 0001 `meson: no -ansi on Phoenix-RTOS` | `-ansi` makes `inline` an identifier; libphoenix's `<stdio.h>` has `static inline` |
| pango | 0001 `meson: accept GLib 2.56`, 0002 `meson: build the programs only natively`, 0003 `meson: redundant declarations are a warning, not an error` | above; pango-view & co. are not needed; the compat headers repeat two libc prototypes |

**M6 sources extended (additive; frozen M6 binaries unaffected):** `weston-drm/compat` — `wlphx_shm_create()`
split out of `memfd_create()` (shm_open reuses it); `weston-drm/shims/include/linux/input.h` — `BUS_*`;
`weston-drm/shims/src/libinput_phoenix.c` — the wheel now also emits `LIBINPUT_EVENT_POINTER_SCROLL_WHEEL` (libinput
≥ 1.19 sends both; **wlroots reads only SCROLL_WHEEL, Weston only AXIS**, so Weston is unchanged),
`get_scroll_value{,_v120}`, `get_id_bustype` (BUS_USB), and ≈70 "unavailable" accessors/config defaults that
wlroots' libinput backend and labwc's `<libinput>` configuration call (gestures, switches, tablet pads, tap/click/
scroll/send-events defaults).

**New compat (`labwc-drm/compat/`, first on the include path, SPDX BSD-3):** `shm_open`/`shm_unlink` (per-process
name table over shmsrv objects; wlroots creates, opens a read-only twin and unlinks at once), `posix_openpt`
(`/dev/ptmx`), `<uchar.h>` with **UTF-8** `mbrtoc32`/`c32rtomb` (libphoenix's multibyte layer is byte = code point;
foot requires UTF-8 and does all its char32 conversions through these; foot is built with
`-DLWPHX_UTF8_MB_CUR_MAX` so its `MB_CUR_MAX` buffers hold 4 bytes, and `-D__STDC_ISO_10646__`), C11 `<threads.h>`
over pthreads (fcft, foot), unnamed `<semaphore.h>` (foot's render workers; posts always signal), `newlocale`/
`uselocale` (C locale only; fcft), `wcsncat`/`wcscasecmp`/`wcsncasecmp`, `epoll_pwait` (mask swapped around
`epoll_wait`, not atomic, like M6's ppoll), `pthread_setname_np` (no-op), `SIGRTMAX` = NSIG (foot sizes tables with
it), `SO_DOMAIN`, `<sys/ioctl.h>` pulling `<termios.h>` (winsize), `<regex.h>` fix-up (size_t/off_t).

**Link fix (no patch):** libstdc++.a (HarfBuzz is C++) carries a `hypotf` stub that collides with libm's; g++
moves a plain `-lm` behind `-lstdc++`, so libm is linked **by path** before it.

### Build, checks, artifacts

```
tools/gpu-lane/labwc-drm/build.sh --out tools/gpu-lane/labwc-drm/build-out-m7a   # ≈ 12 min from nothing
tools/gpu-lane/labwc-drm/build.sh --relink                                        # programs only
tools/gpu-lane/labwc-drm/hosttest/run.sh                                           # host tests, seconds
```

Programs are hand-linked (meson builds objects only, as weston-drm): labwc and tinywl with the Mesa closure
(`egl-link.txt`, gallium whole-archive) and `--wrap=mmap/ioctl` (libdrm-phoenix) + `--wrap=close/write`
(compat); foot needs no Mesa and no libdrm. The script's `== verify` fails the build on any miss:

| check | `labwc` | `foot` | `tinywl` |
|---|---|---|---|
| `nm -u` / `PT_INTERP` | **0 / 0** | **0 / 0** | **0 / 0** |
| text / data / bss | 21 534 434 / 527 804 / 339 624 | 3 970 020 / 21 236 / 30 388 | 15 895 742 / 520 656 / 320 684 |
| stripped size | 22 069 992 | 3 996 560 | 16 422 664 |
| sha256 stripped (first 16) | **`c8a78d3d7e047711`** | **`54d4232567932fc9`** | **`4dab8a085f2ba15b`** |
| sha256 unstripped (addr2line) | `5676cf7300bea7f7` | `71fd65c89e5991ae` | `9963192b0b998726` |
| link warnings beyond libphoenix notes | 0 | 0 | 0 |

Symbols present in labwc: `wlr_drm_backend_create`, `wlr_libinput_backend_create`, `wlr_headless_backend_create`,
`wlr_gles2_renderer_create_with_drm_fd`, `wlr_pixman_renderer_create`, `wlr_gbm_allocator_create`,
`wlr_drm_dumb_allocator_create`, `wlr_session_create`, `libseat_open_seat`, `udev_enumerate_scan_devices`,
`di_info_parse_edid`, `labwc_builtin_xkb_keymap`, `pango_cairo_show_layout`, `xmlReadMemory`, `g_string_replace`,
`shm_open`, `epoll_wait`, `signalfd`, `timerfd_settime`, `eventfd`, `__wrap_mmap`, `__wrap_ioctl`,
`drm_phoenix_ioctl`, `kmsro_drm_screen_create`, `gbmint_get_backend`; strings `libdrm-phoenix:`, `/dev/dri/card0`,
`EGL_KHR_platform_gbm`, `V3D 4.2`, `LIBINPUT-PHX`, `/shm`, `using the builtin XKB keymap`, `Failed to duplicate the
DRM descriptor` (0003), `fchmod() of a shared memory object failed` (0002), `spawned child %ld exited` (labwc 0001).
foot: `fcft_from_name`, `memfd_create`, `epoll_pwait`, `posix_openpt`, `mbrtoc32`, `newlocale`, `thrd_create`,
`sem_init`, strings `xterm-256color`, `/dev/ptmx`. **Old-lane strings in all three: 0.** Frozen copies (same sha):
`/home/houp/.claude/jobs/c8f1289c/tmp/m7a-frozen/`.

**Host tests** (`hosttest/run.sh`, ASan/UBSan, all **PASS**): `uchar` 22 checks (every scalar value U+0000–U+10FFFF
round-trips; bytes equal glibc's C.UTF-8 `c32rtomb`; split sequences, overlongs, surrogates, > U+10FFFF);
`shm` 25 checks with a file-backed shmsrv stand-in, **wlroots' own `util/shm.c`** compiled in
(`allocate_shm_file`, `allocate_shm_file_pair`: the client's read-only twin sees the keymap and refuses a writable
shared mapping) — run again with Phoenix's failing `fchmod()`: patched build PASS, unpatched build fails as on
the Pi (**negative control** `shm-negative` PASS); `glib` 15 cases of our `g_string_replace` against the host
GLib 2.88's own; `sync` 19 checks (2000 rounds of foot's 4-worker start/done semaphores, thread results, condvar,
recursive mutex, wcs*); `epoll_pwait` 7 checks over the M6 epoll emulation in foot's signal pattern. The M6 host
tests still pass with the extended compat.

**Protocols for XFCE** (coordinator question): labwc creates all of them unconditionally (no build option) —
`zwlr_layer_shell_v1` (xfce4-panel, xfdesktop via gtk-layer-shell), `zwlr_foreign_toplevel_manager_v1` and
`ext_foreign_toplevel_list_v1` (libxfce4windowing: window buttons), `zxdg_output_manager_v1`, `xdg_activation_v1`,
`zxdg_decoration_manager_v1` + KDE server-decoration, `zwlr_output_manager_v1`, data-control (wlr + ext),
screencopy, ext-workspace, session-lock, idle-notify, virtual keyboard/pointer, input-method v2, fractional scale,
cursor-shape, linux-drm-syncobj (only if the renderer has timelines: not here).

### Runtime design notes (what the cycles test)

- **Seat/devices:** `LIBSEAT_BACKEND=noop`; `WLR_DRM_DEVICES=/dev/dri/card0` (skips udev enumeration; the
  script's `DRM_DEVICES=` knob restores it); `LIBINPUT_PHOENIX_DEVICES` as M6.
- **pixman arm:** the dumb allocator on the dup'd card0 client (0003) → `MAP_DUMB` + `__wrap_mmap` token (M6's
  pixman path) → the backend imports its own export (short-circuited to the original handle) → ADDFB2.
- **gles2 arm:** EGL: no `EGL_EXT_device_drm` match for card0 (Mesa lists only render-node devices), so wlroots
  falls back to `EGL_PLATFORM_GBM_KHR` on a **third** card0 open (card0 has no render node, as on Linux Pi 4) →
  kmsro (the kmscube/Weston path). Buffers: GBM allocator on the dup'd client → kms dumb BOs imported into v3d;
  the renderer imports them by dma-buf (`BO_IMPORT ns=kmsbuf`, G1). The GLES2 renderer needs
  `EGL_EXT_image_dma_buf_import` and `GL_EXT_texture_format_BGRA8888` (both in Mesa V3D).
- **Keymap to clients:** a shmsrv pair (0002); clients `mmap(MAP_PRIVATE, PROT_READ)` the read-only descriptor —
  untested on the Pi so far (no M6 cycle had a keyboard client).
- **Clients from labwc** (autostart, menu): `fork()` of the compositor + `setsid()` + `execvp()` (labwc's double
  fork), autostart through `/bin/sh` = busybox ash (has `&`).
- **foot:** TERM=xterm-256color (Phoenix's ncurses has it compiled in; no terminfo files exist), shell `/bin/bash`,
  DejaVu Sans Mono 11 through `/etc/fonts/fonts.conf` (the truetype-only config, not the NFS-slow parent dir),
  80×24 (~720×456 px: every wl_shm buffer is one contiguous shmsrv object, a power of two ≥ 1 MiB); window size
  reaches the pty (libtty implements `TIOC[GS]WINSZ`). Known limits: `wcwidth()` = 1 for every printable
  character (libphoenix), so CJK/emoji misalign; `fallocate` absent (no scrollback pool trick; slower scrolling).
- **`/etc/xdg/labwc/environment` is load-bearing** for clients labwc starts itself (autostart, menu): it gives them
  `FONTCONFIG_FILE` and `TERM`; the script sets the same for the clients it starts.
- **Not exercised by these cycles:** the libudev shim's `card[0-9]*` enumeration and monitor (the script sets
  `WLR_DRM_DEVICES`; `export DRM_DEVICES=` to exercise it). **Unverified by design until the Pi runs:** whether
  Phoenix `poll()` returns `EINTR` for a signal admitted by `epoll_pwait()`'s mask (foot's SIGCHLD path; if not,
  foot sees the shell's exit at the next pty event), and whether the export window maps an `O_RDONLY` shmsrv
  descriptor `MAP_PRIVATE` (the keymap twin, m7b row 5).
- **Why `deps/` views:** labwc links the ports GLib 2.56 while the GTK3 lane builds GLib 2.88; the private views
  keep each build on exactly one GLib's headers and archive (separate processes in one session are fine).
- **Logs:** labwc `-V` (info). wlroots lines are `hh:mm:ss.mmm [file.c:line] text`, possibly wrapped in ANSI colour
  codes (stderr is the UART tty): grade with patterns that tolerate them.

### Staging (done 2026-09-27; new names only, nothing of these existed before — checked)

```
F=/home/houp/.claude/jobs/c8f1289c/tmp/m7a-frozen
EXPORT=/srv/phoenix-rpi4-nfs-gcc16
sudo -n install -m 755 "$F/labwc"            "$EXPORT/bin/labwc"
sudo -n install -m 755 "$F/foot"             "$EXPORT/bin/foot"
sudo -n install -m 755 "$F/tinywl"           "$EXPORT/bin/tinywl"
sudo -n install -m 755 "$F/labwc-desktop.sh" "$EXPORT/bin/labwc-desktop.sh"
sudo -n install -m 755 "$F/m7b-colors.sh"    "$EXPORT/bin/m7b-colors.sh"
sudo -n install -d -m 755 "$EXPORT/etc/xdg/labwc" "$EXPORT/etc/xdg/foot"
for f in rc.xml menu.xml autostart environment; do sudo -n install -m 644 "$F/conf/$f" "$EXPORT/etc/xdg/labwc/$f"; done
sudo -n install -m 644 "$F/conf/foot/foot.ini" "$EXPORT/etc/xdg/foot/foot.ini"
# cmp each against $F: all equal (2026-09-27 17:2x)
```

| file | sha256 (first 16) |
|---|---|
| `/bin/labwc` / `/bin/foot` / `/bin/tinywl` | `c8a78d3d7e047711` / `54d4232567932fc9` / `4dab8a085f2ba15b` |
| `/bin/labwc-desktop.sh` (`pi/` as of `18224fcfb`; the repo file has since gained m7c's clients and is staged as `/bin/labwc-desktop-m7c.sh`) | `da67b3202c41dbd1` |
| `/bin/m7b-colors.sh` (`pi/`) | `cbc892b94cbe7de2` |
| `/etc/xdg/labwc/{rc.xml,menu.xml,autostart,environment}` | `0bf18540…`, `c9ea858a…`, `d68df0df…`, `2a965f59…` |
| `/etc/xdg/foot/foot.ini` | `fd91a434…` |
| reused, unchanged: `/bin/shmsrv` (M6, `6a89f2610a5ad80d`, same wire protocol), `/bin/weston-simple-shm` (`726de04f92a35376`), `/bin/rpi4-kms-g7` (`51c9cbcc692e4e6b`), `/bin/rpi4-v3d-async-g6` (`dc88c71a94b883d8`), `/bin/mc`, `/bin/bash`, `/bin/sh` (busybox), `/bin/cp`, DejaVu fonts + `/etc/fonts/fonts.conf` | — |

Menu (`menu.xml`, right click on the desktop): **Terminal** (`/bin/foot`), **Files** (`/bin/foot -e /bin/mc`),
**File Manager (GUI)** (`/bin/thunar-wl`, placeholder until the GTK3/XFCE lane stages it), Reconfigure, Exit.
Keys: labwc defaults + Super+Return (foot) + Super+E (mc). Autostart: one foot.

Preconditions: netboot image ≥ build 11; no GPU app, no X, no old-lane `rpi4-v3d`; after the current Pi queue.

### Cycle `m7a-labwc` (Bash `timeout: 600000`)

**Question:** does labwc (wlroots' DRM backend through libdrm-phoenix and rpi4-kms) bring up HDMI with the pixman
renderer and with the GLES2 renderer, show a cursor and a wl_shm client in a server-side-decorated window
(pango-drawn title), and exit cleanly on SIGTERM?

```
./scripts/test-cycle-psh-interact.sh --label m7a-labwc --idle-secs 45 --max-cmd-secs 200 \
    --hdmi-dense-on 'LABWC client start' -- \
    "/bin/rpi4-v3d-async-g6 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/labwc-desktop.sh pixman shm noinput" \
    "/bin/shmsrv -s" \
    "/bin/bash /bin/labwc-desktop.sh gles2 shm noinput" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

**Arm 0 (fallback, run only if labwc dies before its socket in both arms):** the same with
`"export LABWC=/bin/tinywl"` before the two script lines: tinywl is wlroots without pango/GLib/libxml2/labwc config.

Grade:

```
grep -a -E 'LABWC |\[(backend|render|types|util)/|DRMPHX (conn|ioctl .*(ADDFB2|PRIME|CREATE_DUMB|ATOMIC))|^KMS |^SHMSRV |^V3DA srv (ready|import|export)|LIBINPUT-PHX|libseat|KMSTEST|V3DAPING|builtin XKB' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m7a-labwc.log
./scripts/uart-summary.sh m7a-labwc
```

Allow ~1.3 % UART line corruption; EL0 dumps print twice; wlroots lines may carry ANSI colour escapes.

| # | Line / observation | Predicted (per arm unless noted) | If instead… |
|---|---|---|---|
| 1 | `LABWC start renderer=pixman client=shm … conf=/tmp/labwc-conf files=rc.xml,menu.xml,environment … drm_devices=/dev/dri/card0 hw_cursors=0 atomic=1`, `LABWC labwc pid=…` | once per arm | `LABWC FAIL`/bash errors: staging |
| 2 | `[backend/session/session.c…] Successfully loaded libseat session` (libseat's own lines are routed into wlroots' log); labwc's keyboard group is created at seat init even with `noinput`, so **`using the builtin XKB keymap (rule names not compiled)`** appears here too (labwc 0002) | early | `Unable to create seat` / `libseat: … could not open seat`: `LIBSEAT_BACKEND` not exported |
| 3 | `[backend/drm/backend.c…] Initializing DRM backend for /dev/dri/card0 (…)`, `DRMPHX conn fd=… path=/dev/dri/card0 node=card0 …` | once | `drmGetVersion() failed`/`drmGetDeviceNameFromFd2() failed`: libdrm-phoenix identification — stop |
| 4 | `Found 1 DRM CRTCs`, `Found N DRM planes`, no `DRM universal planes unsupported`/`DRM_CRTC_IN_VBLANK_EVENT unsupported`; atomic used (no `falling back to legacy`) | as Weston (M6 §9 rows) | an unsupported cap: rerun with `export ATOMIC=0` (WLR_DRM_NO_ATOMIC) |
| 5 | `Scanning DRM connector … on /dev/dri/card0`, `Detected modes:` incl. `1920x1080@60`, EDID via libdisplay-info, `Modesetting with 1920x1080 @ 60.000 Hz` | one output HDMI-A-1 | `No CRTC possible`: possible_crtcs marshalling |
| 6 | pixman arm: no EGL lines; `DRMPHX ioctl … CREATE_DUMB` on the **same client id** as the backend's `conn` line, ADDFB2 rc=0; gles2 arm: `[render/egl.c…] Using EGL 1.5`, `EGL vendor: Mesa Project`, `[render/gles2/renderer.c…] Creating GLES2 renderer`, `Using OpenGL ES 3.1 Mesa 26.2.0`, `GL renderer: V3D 4.2…` (the platform choice, `Using EGL_PLATFORM_GBM_KHR`, is a debug line: `export VERBOSE=2`), `V3DA srv import … ns=kmsbuf` | **patch 0003 at work: every ADDFB2 rc=0** | `KMS … import FAIL … why=foreign_kmsbuf` / `Failed to import DMA-BUF` / `ADDFB2 rc=-1 errno=22`: the allocator did not get the dup'd client (0003 not in the binary: `strings -a /bin/labwc \| grep 'duplicate the DRM'`) — **stop**; gles2 only: `Failed to initialize EGL context` → the arm is a GBM/EGL fault, pixman stays graded |
| 7 | `LABWC socket=up name=wayland-0 wait_s=<1–30>` (heartbeats `LABWC waiting…` every 10 s before) | within ~30 s (fontconfig scan of `/usr/share/fonts/truetype` over NFS on first title) | `socket=missing … labwc=exited`: read labwc's last lines; `labwc=running` after 90 s: a hang — grade from the last `DRMPHX`/`KMS`/wlroots line |
| 8 | before `client start`: `SHMSRV create` + `truncate … size≈68 KB … cap=1048576` for the keymap (wlroots `shm_open` pair, wlroots 0002 — one object, two descriptors); after it: `SHMSRV create id=N`, `truncate id=N size=250000 … cap=1048576` ×2 (simple-shm's buffers) | as listed | no `create`: memfd_create did not reach shmsrv |
| 9 | HDMI (dense snapshots from `client start`) | **black desktop** (labwc paints no background without swaybg), a **software cursor** (the built-in wlroots cursor image, `WLR_NO_HARDWARE_CURSORS=1`) at screen centre, **simple-shm's 250×250 animated pattern in a window with a labwc title bar** (`weston-simple-shm`, DejaVu Sans, close/max/iconify buttons) | console text still visible: the first commit never applied; black without cursor: the cursor plane/composition path; window without title text: pango/fontconfig (look for `Fontconfig error`) — the rest still graded |
| 10 | `LABWC hold … labwc=running client=running` ×3 | heartbeats | client exited: its stderr line above |
| 11 | `LABWC client exited rc=143`, `LABWC labwc exited rc=0 after_term_s=<1–3> socket=gone` | clean TERM exit through the emulated signalfd | `still up … sending KILL`: note, not fatal for display grading |
| 12 | `SHMSRV stats rc=0 live=0 bytes=0` after each arm | all objects released | `live>0`: a descriptor kept |
| 13 | `KMSTEST stats … apply_errors=0 … bos=0 exports=0`, `V3DAPING stats … bos_live=0 … verdict=PASS` | no leaks | `bos>0`: dumb BOs outlive labwc (the dup'd client's close) |
| 14 | fault dumps | 0 kernel, 0 EL0 | EL0 in labwc: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/labwc-drm/build-out-m7a/labwc <pc>` |

**Decides:** pixman arm rows 3–9 PASS = wlroots' DRM backend, the compat event loop, the seat/udev shims, shmsrv
and labwc's text stack work on Phoenix; gles2 adds the EGL/GBM renderer. Then `m7b-foot`.

### Cycle `m7b-foot` (after m7a's pixman arm passed; Bash `timeout: 600000`)

**Question:** does foot draw in labwc — a prompt, 24-bit colour, Unicode — and does mc run in it (the "Files" menu
command)? Does keyboard input reach it once rpi4-kms frees the console keyboard?

```
./scripts/test-cycle-psh-interact.sh --label m7b-foot --idle-secs 45 --max-cmd-secs 200 \
    --hdmi-dense-on 'LABWC client start|LABWC socket=up' -- \
    "/bin/rpi4-v3d-async-g6 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96 -C" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/labwc-desktop.sh pixman colors input" \
    "/bin/bash /bin/labwc-desktop.sh pixman mc input" \
    "/bin/bash /bin/labwc-desktop.sh pixman autostart input" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

Arms: **A** `colors` = foot running `m7b-colors.sh` (no keyboard needed); **B** `mc` = `foot -e /bin/mc /`, the
command the menu's "Files" entry runs; **C** `autostart` = labwc's own autostart (`sh` → `/bin/foot`, an
interactive bash): the fork/exec-from-the-compositor path. Keyboard and the menu click need a person at the bench
(or the USB keyboard/mouse attached): graded when present, otherwise **n/a**. Grade as m7a plus
`grep -a -E 'foot|fcft|LABWC ' …m7b-foot.log`.

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `LABWC client start: /bin/foot --log-level=info  -e /bin/bash /bin/m7b-colors.sh` (A) | once | — |
| 2 | foot's own log on the UART (stderr, `--log-level=info` from the script; the autostarted foot of arm C logs warnings only): `info: … locale: POSIX` and **no** `not a UTF-8 locale` (compat `mbrtoc32` decodes UTF-8, so `locale_is_utf8()` is true); `info: fcft: … DejaVu Sans Mono` | early | `not a UTF-8 locale, and failed to find a fallback`: the compat UTF-8 layer is not linked (`nm foot \| grep mbrtoc32`) |
| 3 | `warning: … failed to seal SHM backing memory file` per buffer (not fatal: shmsrv has no seals); `SHMSRV truncate … size=<≈1.3 MB> … cap=2097152` for foot's 80×24 buffers | a few objects | `SHMSRV FAIL alloc … cap=…`/`refuse grow`: contiguous memory — note the size; `failed to set size of SHM backing memory file` with size 536870912: foot 0001 not in the binary |
| 4 | no `failed to configure controlling terminal` / `failed to open pseudo terminal slave device` (posixsrv `/dev/ptmx` + libtty `TIOCSCTTY`); window resize → no `TIOCSWINSZ` error | clean pty start | a pty error: posixsrv not running or `/dev/pts` missing (M4/psh note in memory) |
| 5 | keymap: no `failed to mmap keyboard keymap` (foot's string) and no xkbcommon compile error from foot; labwc (with `/dev/kbd0` opened) `using the builtin XKB keymap (rule names not compiled)` | the shmsrv read-only twin maps MAP_PRIVATE (untested before) | foot logs a keymap failure: the export window refuses `MAP_PRIVATE` of an `O_RDONLY` descriptor — keyboard dead, display still graded |
| 6 | HDMI, A: a foot window (title "foot", SSD) with `foot on Phoenix-RTOS: TERM=xterm-256color`, **two smooth 64-cell colour ramps** (red→green, green→blue; banding = the 24-bit path lost), the Unicode line (Polish, Greek, Cyrillic, box drawing, arrows, ✓ ✗ €) rendered — glyphs absent from DejaVu Sans Mono would show as boxes, none expected — the 16 ANSI colours and bold/italic/underline/reverse | the terminal draws | black window: foot's buffers never committed (row 3); text but grey ramps: 256-colour fallback — `TERM`/foot SGR parsing |
| 7 | HDMI, B: mc's blue two-panel screen listing `/` (bin, dev, etc, …), function-key bar at the bottom, line drawing intact | mc runs in foot (xterm-256color from ncurses' compiled-in fallbacks) | `Unknown terminal`: TERM not passed (foot.ini not read: `XDG_CONFIG_DIRS`) |
| 8 | HDMI, C: the autostarted foot with a bash prompt; UART `run session script /etc/xdg/labwc/autostart` | labwc forks `sh` → foot | no window and `unable to fork()`/`spawned child … exited with 127`: fork of the compositor or busybox `sh` failed |
| 9 | (person at the bench) typing into foot shows the characters; right click on the desktop → menu (Terminal / Files / File Manager (GUI) / Reconfigure / Exit) drawn with DejaVu Sans; **Files → mc opens in a new foot and lists `/`**; File Manager (GUI) → nothing yet (`spawned child … exited with 127`: `/bin/thunar-wl` not staged) | with `-C` the console keyboard is labwc's (`LIBINPUT-PHX dev=/dev/kbd0 … open=ok`) | `open=failed … retrying`: `-C` missing; menu without text: pango |
| 10 | exits: `LABWC client exited rc=143`, `LABWC labwc exited rc=0 … socket=gone` per arm; `SHMSRV stats … live=0`; `KMSTEST stats … bos=0` | clean | as m7a rows 11–13 |
| 11 | fault dumps | 0 kernel, 0 EL0 | EL0 in foot: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/labwc-drm/build-out-m7a/foot <pc>` |

### Stage 1b: fuzzel 1.15.0 + swaybg 1.2.2, cycle `m7c-desktop` (built and staged 2026-09-27)

**Builds** (same `build.sh`, `--out build-out-m7c`, a clean run from nothing after the core build of that hour
finished): **fuzzel 1.15.0** (codeberg, MIT; fcft + pixman + xkbcommon, PNG icons through libpng, the bundled
nanosvg, `-Denable-cairo=disabled`, built with foot's `-DLWPHX_UTF8_MB_CUR_MAX -D__STDC_ISO_10646__`) and
**swaybg 1.2.2** (GitHub release, MIT; cairo's PNG loader, `-Dgdk-pixbuf=disabled`). Both are wl_shm clients on
wlr-layer-shell; no Mesa, no libdrm. Patches: fuzzel 0001 `meson: man pages only when scdoc is available`
(doc/ required scdoc unconditionally), swaybg 0001 `meson: librt is optional`. New compat: `reallocarray`,
`dirfd` (libphoenix's DIR holds a descriptor only after `fdopendir()`; otherwise `ENOTSUP`, which fuzzel treats as
"skip this PATH directory"), `O_DIRECTORY` = 0 (libphoenix has none; `open()` of a directory works without it),
`LC_MESSAGES` (an unknown category to libphoenix's `setlocale()`, answered NULL).

| check | `fuzzel` | `swaybg` |
|---|---|---|
| `nm -u` / `PT_INTERP` / link warnings | 0 / 0 / 0 | 0 / 0 / 0 |
| text / data / bss | 3 786 612 / 19 836 / 26 804 | 2 665 984 / 2 064 / 30 564 |
| stripped / sha256 (first 16) | 3 811 752 / **`bc4e09ea56cb430a`** | 2 673 760 / **`4ff077961372b021`** |
| unstripped (addr2line: `build-out-m7c/`) | `37ffb5fb132fd30c` | `76b6c5ff84301947` |
| symbols | `fcft_from_name2`, `zwlr_layer_shell_v1_interface`, `png_read_info`, `memfd_create`, `epoll_wait`, `timerfd_settime`, `mbrtoc32`, `sem_init`, `__wrap_close` | `cairo_image_surface_create_from_png`, `zwlr_layer_shell_v1_interface`, `shm_open`, `__wrap_close` |

Old-lane strings: 0. The m7c build's labwc/foot/tinywl are **not** staged (m7c runs the m7a `/bin/labwc` and
`/bin/foot`); their hashes differ from m7a's only through the out directory pango embeds.

**Wallpaper:** `conf/backgrounds/make-wallpaper.py` (stdlib only, deterministic, output CC0) draws a 1920×1080
diagonal dark-blue→ember gradient with a soft orange glow lower right and faint rings: smooth at 24 bpp (banding =
a colour-depth problem), and **an R/B swap turns the ember blue**. `phoenix-gradient-1920x1080.png`, 184 873 B,
sha256 `713e715e3227ee1c`.

**Configuration** (the m7a/m7b set in `/etc/xdg/labwc/` is unchanged): `/etc/xdg/labwc-m7c/` =
`conf/labwc-m7c/`. rc.xml adds **Super+D and Alt+F2 → fuzzel** (Super+Return foot, Super+E mc as before);
menu.xml adds **"Run…" → fuzzel** at the top; autostart runs `swaybg -i …/phoenix-gradient-1920x1080.png -m fill`
then one foot. fuzzel reads `/etc/xdg/fuzzel/fuzzel.ini` (DejaVu Sans 14, `terminal=/bin/foot -e`, no icons, an
ember accent) and lists `/usr/share/applications/{foot,mc,bash}.desktop` (mc and bash are `Terminal=true`: they
open in foot). The launcher `pi/labwc-desktop.sh` gains two clients — `desktop` (labwc's autostart **and** fuzzel
started by the script) and `fuzzel` — and is staged under a new name, `/bin/labwc-desktop-m7c.sh`; `/bin/labwc-desktop.sh`
(`da67b3202c41dbd1`, the m7a/m7b registration) is untouched. The configuration is selected with
`CONF_DIR=/etc/xdg/labwc-m7c` (default stays `/etc/xdg/labwc`).

**Staging (done; nothing of these existed before — checked; every file `cmp`-equal to the frozen copy in
`/home/houp/.claude/jobs/c8f1289c/tmp/m7c-frozen/`; the m7a set re-checked untouched):**

```
F=/home/houp/.claude/jobs/c8f1289c/tmp/m7c-frozen
EXPORT=/srv/phoenix-rpi4-nfs-gcc16
sudo -n install -m 755 "$F/fuzzel" "$EXPORT/bin/fuzzel"
sudo -n install -m 755 "$F/swaybg" "$EXPORT/bin/swaybg"
sudo -n install -m 755 "$F/labwc-desktop-m7c.sh" "$EXPORT/bin/labwc-desktop-m7c.sh"
sudo -n install -d -m 755 "$EXPORT/etc/xdg/labwc-m7c" "$EXPORT/etc/xdg/fuzzel" "$EXPORT/usr/share/applications" \
    "$EXPORT/usr/share/backgrounds/phoenix"
for f in rc.xml menu.xml autostart environment; do sudo -n install -m 644 "$F/conf/labwc-m7c/$f" "$EXPORT/etc/xdg/labwc-m7c/$f"; done
sudo -n install -m 644 "$F/conf/fuzzel/fuzzel.ini" "$EXPORT/etc/xdg/fuzzel/fuzzel.ini"
for f in foot mc bash; do sudo -n install -m 644 "$F/conf/applications/$f.desktop" "$EXPORT/usr/share/applications/$f.desktop"; done
sudo -n install -m 644 "$F/backgrounds/phoenix-gradient-1920x1080.png" "$EXPORT/usr/share/backgrounds/phoenix/"
```

| file | sha256 (first 16) |
|---|---|
| `/bin/fuzzel`, `/bin/swaybg` | `bc4e09ea56cb430a`, `4ff077961372b021` |
| `/bin/labwc-desktop-m7c.sh` | `4c3a79989f55d84e` |
| `/etc/xdg/labwc-m7c/{rc.xml,menu.xml,autostart,environment}` | `36233f3c953a2806`, `9ae23b7f81130938`, `fee08999626181bb`, `d8dc8ca5d06cdbde` |
| `/etc/xdg/fuzzel/fuzzel.ini` | `f549b34fff03e153` |
| `/usr/share/applications/{foot,mc,bash}.desktop` | `ec202513f6696763`, `930ccaa72c2c51ab`, `8cd7f375fa3f590e` |
| `/usr/share/backgrounds/phoenix/phoenix-gradient-1920x1080.png` | `713e715e3227ee1c` |
| reused: `/bin/labwc` `c8a78d3d7e047711`, `/bin/foot` `54d4232567932fc9`, the m7a servers, `/bin/shmsrv` | — |

#### Cycle `m7c-desktop` (after `m7b-foot`; Bash `timeout: 600000`)

**Question:** does the desktop come up as a desktop — wallpaper on the layer-shell background, the autostarted
foot, and the fuzzel launcher on the overlay layer, all at once — and does it exit cleanly?

```
./scripts/test-cycle-psh-interact.sh --label m7c-desktop --idle-secs 45 --max-cmd-secs 200 \
    --hdmi-dense-on 'LABWC socket=up' -- \
    "/bin/rpi4-v3d-async-g6 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96 -C" \
    "/bin/shmsrv -v" \
    "export CONF_DIR=/etc/xdg/labwc-m7c" \
    "export HOLD=60" \
    "/bin/bash /bin/labwc-desktop-m7c.sh pixman desktop input" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

(A GLES2 arm, `… gles2 desktop input`, is added once m7a's gles2 arm has passed.) Grade as m7a plus
`grep -a -E 'LABWC |swaybg|fuzzel|foot|run session script|spawned child|SHMSRV (create|truncate|FAIL|stats)' …m7c-desktop.log`.
Rows marked **bench** need a person with the USB keyboard/mouse (otherwise **n/a**, not FAIL).

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `LABWC start … client=desktop … conf=/etc/xdg/labwc-m7c files=rc.xml,menu.xml,autostart,environment …` | the m7c configuration | `conf=/etc/xdg/labwc`: psh's `export` did not reach bash — m7b's run again |
| 2 | `run session script /etc/xdg/labwc-m7c/autostart`; no `spawned child … exited with 127` | busybox `sh` starts swaybg and foot | `127`: a path in autostart |
| 3 | swaybg: `SHMSRV create` + `truncate … size=8294400 … cap=16777216` (one 1920×1080 XRGB buffer; shmsrv rounds to 16 MiB **contiguous**) | once | `SHMSRV FAIL alloc … cap=16777216`: no 16 MiB contiguous block — the wallpaper is missing, the rest still graded; a solid-colour fallback (`swaybg -c`, single-pixel-buffer + viewporter, no big buffer) is the next registration |
| 4 | `LABWC client start: /bin/fuzzel --log-level=info`, fuzzel's `info:` lines (fcft DejaVu Sans), no `failed to …` about `/usr/share/applications`; its buffer `SHMSRV truncate … cap=1048576` or `2097152` | fuzzel maps on the overlay layer with keyboard focus (layer-shell `keyboard-interactivity`) | `compositor does not support layer shell`: not labwc (staging); `no applications found`: `.desktop` staging/`XDG_DATA_DIRS` |
| 5 | HDMI (dense from `socket=up`): **the ember gradient wallpaper fills the screen**, a foot window (bash prompt, SSD title "foot"), and **fuzzel's box centred on top** — prompt `Run: `, three entries Foot / Midnight Commander / Bash, the first highlighted in ember; software cursor | the three layers composite in the right order (background < windows < overlay) | black background: row 3; blue glow instead of ember: R/B swapped in the XRGB path (report it, do not grade the rest as broken); fuzzel absent while foot shows: row 4 |
| 6 | `LABWC hold … labwc=running client=running` ×6 (`HOLD=60`) | fuzzel stays open | `client=exited` early: fuzzel's stderr above (e.g. keymap mmap: m7b row 5) |
| 7 | **bench:** typing `mid` in fuzzel narrows to Midnight Commander, Enter opens mc in a new foot | the launcher launches (fork/exec from fuzzel, `terminal=/bin/foot -e`) | nothing: `spawned`/`execvp` errors on the UART |
| 8 | **bench:** Super+D / Alt+F2 opens fuzzel again; right click on the wallpaper → root menu with **Run…** at the top | labwc keybinds and menu from the m7c rc.xml/menu.xml | the m7b menu (no Run…): the wrong conf dir |
| 9 | **bench:** dragging foot's title bar moves the window; dragging an edge resizes it (foot redraws at the new size: new `SHMSRV truncate` lines) | interactive move/resize | the window jumps back: pointer button/motion events (libinput-phoenix) |
| 10 | exit: `LABWC client exited rc=143`, `LABWC labwc exited rc=0 … socket=gone`; swaybg and foot lose the display and exit; `SHMSRV stats … live=0`; `KMSTEST stats … bos=0` | clean | `live>0`: an orphaned autostart client still holds an object (it should exit on display loss) |
| 11 | fault dumps | 0 kernel, 0 EL0 | EL0 in fuzzel/swaybg: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/labwc-drm/build-out-m7c/<prog> <pc>` |

### Next

`m7c-desktop` above; then the XFCE stages on top (GTK3 lane, D-Bus, gtk-layer-shell).

## GTK3 + PCManFM → stage 2 built: GTK 3.24 (Wayland only) + gtk-layer-shell (`tools/gpu-lane/gtk3-wayland/`)

Built 2026-09-27; staged; cycle `m7e-gtk3` pre-registered below. PCManFM (libfm, menu-cache) was **dropped** after
the XFCE decision (the file manager is now Thunar): nothing of it was built. The prefix is laid out for the XFCE
libraries to build on it (static `.a` + installed `.pc` for every library, a reusable cross file and pkg-config
wrapper). No Pi cycle has run any of it yet.

### Versions and licences

All release tarballs, sha256-pinned in `build.sh` (checked against upstream's published sums). LGPL/GPL sources
live only in `build-out/src/` (fetched, gitignored), never in `sources/`.

| package | version | licence | why this one |
|---|---|---|---|
| GTK | **3.24.52** | LGPL-2.1+ | latest 3.24; `-Dx11_backend=false -Dwayland_backend=true`, no broadway/win32/quartz, `-Dintrospection=false`, `-Dprint_backends=none` (patch 0002), no colord/cloudproviders/tracker, immodules built in (`all`), demos on |
| GLib + GIO | **2.88.3** | LGPL-2.1+ | ports GLib is 2.56.4 **without GIO**; GTK needs ≥ 2.57.2 + GIO; 2.88 = the host's `glib-compile-*`/`gdbus-codegen` series. `-Dnls=disabled`, no xattr/libmount/selinux/libelf/sysprof/introspection; file monitor backend: none exists (no inotify/kqueue) → GIO's poll monitor for files, directory monitors report an error |
| pcre2 | 10.47 | BSD-3 | GLib ≥ 2.74 needs PCRE2 (ports has PCRE1); 8-bit, no JIT |
| pango | **1.54.0** | LGPL-2.0+ | the newest pango that accepts fontconfig 2.14 (ports); 1.56 needs fontconfig 2.15 + cairo 1.18 |
| cairo | **1.18.4** | LGPL-2.1/MPL-1.1 | ports cairo 1.16 has **no PDF/PS surfaces** (GTK's print-operation code includes `cairo-pdf.h`/`cairo-ps.h` unconditionally) and no `cairo-gobject`; image/png/ft/fc/pdf/ps/svg + gobject, no xlib/xcb |
| harfbuzz | 14.4.0 (= ports) | MIT | rebuilt with meson: the ports (CMake) objects reference `__gxx_personality_v0`, so every C program needs libstdc++, whose `hypotf` stub then collides with libphoenix libm; meson's build is `-fno-exceptions` and needs no C++ runtime; also gives hb-glib |
| gdk-pixbuf | 2.42.12 | LGPL-2.1+ | PNG + JPEG loaders **built in** (`-Dbuiltin_loaders=png,jpeg`, no loader modules), `-Dgio_sniffing=false` (format by loader signature: no shared-mime-info on Phoenix) |
| fribidi | 1.0.16 | LGPL-2.1+ | |
| atk | 2.38.0 | LGPL-2.0+ | GTK 3 links ATK; no at-spi bridge (X11-only in GTK 3's meson) |
| gtk-layer-shell | **0.10.1** | MIT | layer-shell for GTK 3 (xfce4-panel, xfdesktop); supports GTK up to 3.24.52 (its `gtk-priv` table), no libwayland interposition in the GTK 3 branch |
| reused | — | — | libwayland 1.24 client/cursor/egl, wayland-protocols 1.45, libxkbcommon 1.7, the M6 compat archive (snapshot of `weston-drm/build-out-g6/prefix`); libepoxy 1.5.10 static-EGL (snapshot of `xorg-drm/build-out/deps-prefix`); fontconfig 2.14, freetype, pixman, libpng16, libjpeg, libffi, expat, zlib, libiconv (ports, private views) |

### What was patched and why (`patches/<pkg>/`, `git format-patch` files; applied with `git am`)

| patch | why |
|---|---|
| glib 0001 `g_unix_fd_query_path() reports NOSYS on Phoenix-RTOS` | `#error` otherwise (no `/proc/self/fd`, no `F_GETPATH`) |
| glib 0002 `gio: build on Phoenix-RTOS networking and file headers` | `<netinet/in.h>` lacks the IPv4 multicast options, `struct ip_mreq`, `IN_MULTICAST`, `SOMAXCONN`, `IPV6_TCLASS` (lwIP's values in `gnetworking.h`); `ntohl` from `<arpa/inet.h>` in xdgmime; `O_NOFOLLOW` in the trash portal |
| fribidi 0001 `meson: do not force -ansi` | libphoenix headers use C99 `static inline` |
| gdk-pixbuf 0001 `built-in loaders carry their libraries to static links` | a static libgdk_pixbuf with built-in loaders linked neither libpng/libjpeg in its own programs nor in its `.pc` |
| gtk 0001 `wayland: create shm pools with memfd_create()` | Phoenix has no `shm_open()` and no memfd syscall; `memfd_create()` is the M6 compat function (a shmsrv object) |
| gtk 0002 `meson: allow print_backends=none` | the mandatory `file` backend needs cairo PDF/PS output modules; no printing stack here |
| gtk 0003 `wayland: work without XKB data files` | **would abort every GTK program at `gdk_display_open`**: `xkb_context_new(0)` returns NULL when no XKB include directory exists (the m6a Weston failure), and GDK `g_error`s "Failed to create XKB context" in `gdk_wayland_display_init`. Retry with `XKB_CONTEXT_NO_DEFAULT_INCLUDES` (3 call sites); GDK's default keymap (before/without a `wl_keyboard`) falls back to the build host's evdev/pc105/us keymap (M6's `keymap-us.xkb`, a generated header) and logs `Using the built-in XKB keymap (evdev/pc105/us): no XKB data files` |

Not patches (this directory's own code, BSD-3 / Phoenix header): `src/intl/` (`<libintl.h>` + identity gettext:
GLib's meson requires an intl provider and `<glib/gi18n.h>` includes `<libintl.h>`), `src/resolv/` (`<resolv.h>`,
`<arpa/nameser.h>` and stand-ins that make GIO's DNS **record** queries — SRV/MX/TXT — fail cleanly; host lookups
are `getaddrinfo()`), `src/gtkphx_noegl.c` (the linked "EGL" for GTK programs without Mesa: epoxy resolves through
`eglGetProcAddress`; this one answers only GDK's lazy GL probe so a `GtkGLArea` gets "No GL implementation is
available" instead of an epoxy abort; plain widgets never touch EGL), `bin/phx-gcc` (drops `-pthread`, also inside
meson's `@response` files), `src/gtk3-hello.c`. The mesa-drm `compat/include` is **not** used: libphoenix has had
what it shims since build 10 and its duplicate `open_memstream` declaration breaks pango's `-Werror=redundant-decls`.

Cross answers for GLib's run-time probes: libphoenix's printf family (GLib's gnulib replacement needs `frexpl`,
which libphoenix lacks; its `vsnprintf` is C99; positional `%1$s` occur only in translations, none here).

### Build, checks, artifacts

```
tools/gpu-lane/gtk3-wayland/build.sh            # everything, ≈ 12 min on this host (GLib ≈ 2, GTK ≈ 5)
tools/gpu-lane/gtk3-wayland/build.sh --relink   # gtk3-hello + the demo copies only
```

`== verify` (fails the build on any miss), all three programs: **`nm -u` = 0, no `PT_INTERP`, 0 X11/broadway
symbols** (`XOpenDisplay`, `XInternAtom`, `xcb_connect`, `gdk_x11_display_get_type`, `_gdk_broadway_display_open`),
and present: `gdk_wayland_display_get_type`, `_gdk_wayland_display_open`, `memfd_create`, `__wrap_close`,
`eglGetProcAddress` (the stand-in), `wl_display_connect`, `xkb_keymap_new_from_string`, `g_vfs_get_local`,
`pango_cairo_font_map_get_default`, `gdk_pixbuf_new_from_file`; gtk3-hello also `gtk_layer_init_for_window`,
the built-in keymap message and text, and GTK's resource bundle (`_gtk_register_resource`, the uncompressed gvdb path
`/org/gtk/libgtk/theme/Adwaita`: the built-in theme and icons are in the binary). Link warnings beyond libphoenix's
attribute notes: 0.

| artifact (`build-out/`) | file / **stripped** | sha256 stripped (first 16) | staged as |
|---|---|---|---|
| `gtk3-hello` (ours, `--gc-sections`) | 107 508 576 / **16 879 256** | **`8d49230d71c91d8d`** | `/bin/gtk3-hello` |
| `gtk3-demo` (GTK's, meson link) | 114 063 480 / **20 755 696** | `471391f7af09d035` | `/bin/gtk3-demo` |
| `gtk3-widget-factory` (GTK's) | 111 647 544 / **19 221 352** | `0295cdf31ebf9303` | `/bin/gtk3-widget-factory` |
| `data/glib-2.0/schemas/gschemas.compiled` (host `glib-compile-schemas --strict`: GTK's `org.gtk.Settings.*`, GLib's) | 5 schema files | `c01630db539f2b87` | `/usr/share/glib-2.0/schemas/gschemas.compiled` |
| `pi/weston-gtk3.sh` | — | `cd579f9a19d19063` | `/bin/weston-gtk3.sh` |
| `conf/settings.ini` (Adwaita, DejaVu Sans 11, no animations) | — | `96cac8c810456be8` | `/etc/xdg/gtk-3.0/settings.ini` |

The binaries are reproducible: a second full run after the 2026-09-27 core build gave byte-identical files.
`SHA256SUMS` and `snapshots.txt` (the reused archives' sums) are written next to them. Size: the static GTK stack
is ≈ 16 MB per program (text), mostly GTK itself plus its built-in Adwaita CSS and icons.

**For the XFCE follow-up:** `build-out/prefix` holds every library as `.a` with its `.pc` (glib-2.0, gio-2.0,
gio-unix-2.0, gobject-2.0, gmodule-2.0, gthread-2.0, cairo(+gobject/ft/fc/png/pdf/ps/svg), pango, pangocairo,
pangoft2, harfbuzz, fribidi, atk, gdk-pixbuf-2.0, gdk-3.0, gtk+-3.0, gtk+-wayland-3.0, gtk-layer-shell-0,
libpcre2-8); `build-out/pkg-config-phoenix` (static, restricted to the prefix + views) and
`build-out/phoenix-aarch64-wl.cross` (meson, with the compat layer) are reusable; autotools packages need
`CC=bin/phx-gcc`, `PKG_CONFIG=build-out/pkg-config-phoenix`, `-I build-out/deps/sys/include` (libintl/iconv/resolv)
and a phoenix-aware `config.sub`. GLib's `.pc` tool variables point at the host's 2.88 tools. D-Bus is **not** in
this stage (GLib's GDBus is built; there is no bus to talk to — see "D-Bus session bus").

### Runtime design notes (what m7e tests)

- `GDK_BACKEND=wayland` (the only backend), `GTK_THEME=Adwaita` (GTK's built-in resource theme — no theme files
  are staged), `GSETTINGS_BACKEND=memory` + `GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas` (no dconf; the schemas
  are needed by the file chooser, which aborts on a missing schema), `NO_AT_BRIDGE=1`, fonts from
  `/etc/fonts/fonts.conf` (the truetype-only config). Icons: GTK's built-in resource icons (incl. the CSD
  `window-*-symbolic` PNGs); no Adwaita/hicolor icon theme is staged yet (Thunar will need one).
- Buffers: one wl_shm pool per buffer (`memfd_create` → shmsrv, contiguous, capacity a power of two ≥ 1 MiB). Under
  Weston's **kiosk shell every toplevel is fullscreen**: 1920×1080×4 = 8.3 MB → a **16 MiB contiguous** shmsrv object
  per buffer (GTK keeps 1–2) — the largest wl_shm objects any cycle has asked for so far.
- No `wl_keyboard` in the `noinput` arms: GDK uses its default keymap = the built-in one (patch 0003). With a
  keyboard, the compositor's keymap arrives as a descriptor that GDK maps `PROT_READ, MAP_SHARED` (not foot's
  `MAP_PRIVATE`).
- GtkApplication programs (gtk3-demo, gtk3-widget-factory): no session bus → GLib's `g_application_register`
  proceeds as a non-unique local application (`Cannot autolaunch D-Bus without X11 $DISPLAY` is handled
  internally). gtk3-hello uses plain `gtk_init` and GIO's local `GFile` (lists `/` in its tree view).

### Staging (done 2026-09-27; new names only — checked none existed)

```
G=tools/gpu-lane/gtk3-wayland; EXPORT=/srv/phoenix-rpi4-nfs-gcc16
sudo -n install -m 755 $G/build-out/gtk3-hello-stripped          $EXPORT/bin/gtk3-hello
sudo -n install -m 755 $G/build-out/gtk3-demo-stripped           $EXPORT/bin/gtk3-demo
sudo -n install -m 755 $G/build-out/gtk3-widget-factory-stripped $EXPORT/bin/gtk3-widget-factory
sudo -n install -m 755 $G/pi/weston-gtk3.sh                      $EXPORT/bin/weston-gtk3.sh
sudo -n install -d -m 755 $EXPORT/usr/share/glib-2.0/schemas $EXPORT/etc/xdg/gtk-3.0
sudo -n install -m 644 $G/build-out/data/glib-2.0/schemas/gschemas.compiled $EXPORT/usr/share/glib-2.0/schemas/
sudo -n install -m 644 $G/conf/settings.ini                      $EXPORT/etc/xdg/gtk-3.0/settings.ini
# cmp each: all equal (2026-09-27 17:28)
```

Reused, unchanged: `/bin/weston-g6` (m6e TERM fix), `/bin/rpi4-v3d-async-low`, `/bin/rpi4-kms-g7`, `/bin/shmsrv`
(proto 1, same as the snapshot's compat), `/etc/xdg/weston/weston-drm.ini`, `/bin/bash`, DejaVu +
`/etc/fonts/fonts.conf`. `weston-gtk3.sh` = `weston-m6a.sh` + client `gtk` (`GTK_APP`, default `/bin/gtk3-hello`;
`GTK_ARGS`, default `--seconds 0`, `none` = no arguments), `WESTON` default `/bin/weston-g6`, `HOLD` default 40.

### Cycle `m7e-gtk3` (under Weston; Bash `timeout: 600000`)

**Question:** does a static GTK 3 program start on Phoenix, connect to Weston over Wayland, draw its Adwaita window
into wl_shm buffers that reach HDMI, list a directory through GIO, handle its own button click, and die cleanly —
and does GTK's full widget set (gtk3-widget-factory) render?

```
./scripts/test-cycle-psh-interact.sh --label m7e-gtk3 --idle-secs 45 --max-cmd-secs 150 \
    --hdmi-dense-on 'WESTONDRM client start' -- \
    "/bin/rpi4-v3d-async-low -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/weston-gtk3.sh pixman gtk noinput" \
    "/bin/shmsrv -s" \
    "export GTK_APP=/bin/gtk3-widget-factory" \
    "export GTK_ARGS=none" \
    "/bin/bash /bin/weston-gtk3.sh pixman gtk noinput" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

Arm **A** = gtk3-hello, arm **B** = gtk3-widget-factory, both on the pixman renderer (GL composition adds nothing
for wl_shm clients; `gl` is one argument away). Optional arm **C** (only with a USB keyboard attached, after A
passed): `rpi4-kms-g7 -G -p 96 -C` and `weston-gtk3.sh pixman gtk input` — the compositor keymap reaching GDK.
Wall clock ≈ boot 60–150 s + 2 × (~15 s start + 40 s hold + ≤ 17 s exit) + ~40 s ≈ 5 min. Grade:

```
grep -a -E '^(GTK3HELLO|WESTONDRM|SHMSRV|KMSTEST|V3DAPING) |Gtk-|Gdk-|GLib-|GLib-GIO-|Pango-|Fontconfig|cairo|\[[0-9:.]+\] ' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m7e-gtk3.log
./scripts/uart-summary.sh m7e-gtk3
```

Allow ~1.3 % UART line corruption; EL0 dumps print twice. GTK/GLib warnings go to stderr as
`(gtk3-hello:<pid>): Gtk-WARNING **: hh:mm:ss.mmm: …` (with `-Message`/`-CRITICAL` variants).

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `WESTONDRM start renderer=pixman client=gtk weston=/bin/weston-g6 … gtk_app=/bin/gtk3-hello gtk_args=--seconds 0`, then Weston's usual start (M6 m6e rows: `Module 'drm-backend.so': linked into the program`, kiosk shell, `Output 'HDMI-A-1' enabled`), `WESTONDRM socket=up wait_s=<1–20>` | as m6e | Weston fails: not a GTK problem — the M6 rows decide |
| 2 | `WESTONDRM gtk env GDK_BACKEND=wayland GTK_THEME=Adwaita GSETTINGS_BACKEND=memory schemas=staged settings_ini=staged` | once per arm | `missing`: staging |
| 3 | `GTK3HELLO start gtk=3.24.52 glib=2.88.3 GDK_BACKEND=wayland WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/tmp/xdg` | within ~2 s of `client start` (a 16.9 MB exec over NFS) | nothing at all: exec failed (bash error line) or a crash before `main` (EL0 dump: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/gtk3-wayland/build-out/gtk3-hello <pc>`) |
| 4 | possibly `Gtk-WARNING **: Locale not supported by C library. Using the fallback 'C' locale.` (GTK's `setlocale(LC_ALL, "")`) | allowed | — |
| 5 | allowed first: libxkbcommon's own stderr, `xkbcommon: ERROR: failed to add default include path /usr/share/X11/xkb` (the exact m6c/m6e line; GDK creates 3 contexts, so up to 3×) and a rules-lookup error for `evdev` (the names compile that fails before the fallback); then **`Gdk-Message: …: Using the built-in XKB keymap (evdev/pc105/us): no XKB data files`**, and **no** `Failed to create XKB context` | **GTK patch 0003** (without it every GTK program aborts here) | `Failed to create XKB context` / `Failed to create XKB keymap`: a binary without 0003 (sha `8d49230d…`) — stop |
| 6 | `GTK3HELLO init=ok t=<0.1–3> display=wayland-0 backend=wayland` | the GDK Wayland backend connects | `GTK3HELLO init=failed (no display)` + `Gdk-WARNING …cannot open display`/`Failed to connect to Wayland display`: socket/env — compare row 1; a `g_error`/abort with `xdg_wm_base`/`wl_shm` missing: a Weston global GTK requires |
| 7 | `GTK3HELLO gio dir=/ rc=0 entries=<≈15–25>` | GIO's local `GFile` enumerates the NFS root | `rc=error msg=…`: the GIO local backend on Phoenix — note the message; display still graded |
| 8 | `GTK3HELLO shown t=… rows=<same N>`; `SHMSRV create id=… ` + `SHMSRV truncate id=… size=8294400 … cap=16777216` (1–2 of them, the fullscreen buffers), smaller ones for the cursor theme are not expected (Weston draws the pointer) | GTK's `memfd_create` reaches shmsrv (patch 0001) | `creating shared memory file (using memfd_create) failed` (Gdk-CRITICAL): shmsrv not running / `ENOSYS`; `SHMSRV FAIL alloc … cap=16777216`: **no 16 MiB contiguous block** — then `Truncating shared memory file failed`, a black screen, and the finding is shmsrv's contiguous-only limit (E1) |
| 9 | `GTK3HELLO mapped t=… size=1920x1080 scale=1` (kiosk fullscreen; `640x480` first is allowed if the configure arrives after the map) and `GTK3HELLO first-draw t=<1–8>` | the first frame is drawn | no `mapped`: the xdg-shell configure/ack loop did not complete — look for Weston protocol errors (`error in client communication`) |
| 10 | HDMI (dense snapshots from `client start`) arm A: **a light grey (#f6f5f4) full-screen Adwaita window**: bold large "Hello from GTK 3 on Phoenix-RTOS" at the top, a rounded "Click me" button under it, then a list with column headers **Name / Kind / Size** and the entries of `/` (bin, dev, etc, …) in DejaVu Sans; ~3 s later the label reads "Hello from GTK 3.24.52 on Phoenix-RTOS — clicked 1 time" | GTK draws on Phoenix | black screen with row 9 present: buffers never attached (row 8) or frame callbacks lost; text as boxes: fontconfig found no font (`Fontconfig error` lines) — widgets still graded; no rounded corners/gradients: GTK_THEME not honoured (Raleigh-like flat look) |
| 11 | `GTK3HELLO clicked n=1 t=<3–6>` | GLib timers + the GTK signal path | missing: the main loop is stuck (no `hold` lines either → hang: note the last line) |
| 12 | `GTK3HELLO hold t=… ticks=<N> draws=<M> clicks=1` every 5 s, ticks growing ≈ 60/s·5 while the frame clock runs (Wayland frame callbacks at the display rate; pixman composition may pace lower, ≥ 20/s) | frame clock paced by `wl_surface.frame` | ticks stuck at a small number: frame callbacks not delivered — note; `ticks` growing ≫ 60/s: the frame clock free-runs (GTK's 1 s fallback timer only would give ≈ 1/s) |
| 13 | `WESTONDRM hold … weston=running client=running` ×4, then `WESTONDRM client exited rc=143` (SIGTERM kills a GTK program: no handler) | as m6e | `client=exited` early: the client's last stderr lines say why |
| 14 | arm B: `WESTONDRM start … gtk_app=/bin/gtk3-widget-factory gtk_args=`, row 5's keymap line; **no** `Failed to register:` (no session bus is not fatal for GApplication); allowed: `GLib-GIO-WARNING`/`Gtk-WARNING` about the unix mount monitor (`/proc/self/mountinfo`, `/etc/mtab`) or a missing icon (`Could not load a pixbuf from icon theme` / `image-missing`) | the widget factory starts | `Failed to register: …` + client exited rc=1: GIO treats the bus failure as fatal — record; `Settings schema 'org.gtk.Settings.*' is not installed` abort: `GSETTINGS_SCHEMA_DIR` not reaching it (row 2) |
| 15 | HDMI arm B: **the GTK 3 Widget Factory page 1**: header bar, entries, spin buttons, check/radio buttons, switches, sliders, progress bars, a notebook, a tree view, a calendar/colour row; icons in buttons either GTK's built-in symbolic ones or "missing image" squares (no icon theme staged — allowed) | the full Adwaita widget set renders | a crash in a particular widget: EL0 dump → `addr2line -e build-out/gtk3-widget-factory` |
| 16 | `WESTONDRM weston exited rc=0 after_term_s=<1–3> socket=gone` per arm; `SHMSRV stats rc=0 live=0 bytes=0` after each arm | all GTK pools released when the client dies | `live>0`: a pool outlived the client (shmsrv close accounting) |
| 17 | `KMSTEST stats … apply_errors=0 … bos=0 exports=0`, `V3DAPING stats … bos_live=0 … verdict=PASS` | no leaks (GTK does not touch the GPU: the no-EGL stand-in) | `bos>0`: Weston's dumb BOs (M6 rows) |
| 18 | fault dumps | 0 kernel, 0 EL0 | EL0 in a GTK program: addr2line on the unstripped binary in `build-out/` (stripped ones are staged) |

**Decides:** arm A rows 3, 5–12 PASS = GTK 3 (+ GLib/GIO/pango/cairo/gdk-pixbuf) works on Phoenix over Wayland:
the base for Thunar/XFCE; arm B = the full widget set. Then the same two arms under labwc (`labwc-desktop.sh`
gains a `gtk` client the same way) with `gtk3-hello --layer` (gtk-layer-shell: `GTK3HELLO layer=supported
protocol_version=<4|5>`, a full-width bar anchored at the top) — the xfce4-panel path.

### What remains (M7 GTK/XFCE lane)

1. **Prefix paths — done** (`build.sh --usr`, see [stage 4](#stage-4-xfce-420-on-labwc-toolsgpu-lanexfce-wayland) step 0): every package was configured with `--prefix <build-out>/prefix`, so
   the binaries carry host paths (14 strings in gtk3-hello: GTK's sysconf/data/lib dirs, GIO's module dir, the
   locale dir). Harmless for GTK 3 (all built in; `XDG_*` take over) but XFCE's libxfce4util/xfconf/garcon read
   their compiled-in `/etc/xdg/xfce4`, `/usr/share/xfce4` with no override: reconfigure with `--prefix /usr
   --sysconfdir /etc`, install with `DESTDIR`, and let `pkg-config-phoenix` rewrite the prefix
   (`--define-prefix`/`PKG_CONFIG_SYSROOT_DIR`), as weston-drm does with `--prefix /usr`.
2. `m7e-gtk3` on the Pi (above), then its labwc arm with `--layer`.
3. An icon theme subset (Adwaita/hicolor PNGs — Adwaita ≥ 40 is SVG-only and needs librsvg (Rust); use a PNG
   release, e.g. adwaita-icon-theme 3.38, or GTK's built-ins) and shared-mime-info data (GIO content types for Thunar).
4. The XFCE libraries on this prefix (libxfce4util, xfconf — needs the D-Bus stage — libxfce4ui, garcon, exo,
   libxfce4windowing), then Thunar, xfce4-panel, xfdesktop.
5. GL in GTK (`GtkGLArea`, gtk3-demo's GL page): link the Mesa `--wayland` EGL closure instead of
   `libgtkphx-noegl.a` (as weston-simple-egl does); not needed by XFCE.

## Stage 4: XFCE 4.20 on labwc (`tools/gpu-lane/xfce-wayland/`)

**Status 2026-09-27: everything built, staged, cycles `m7f-thunar` and `m7h-xfce` pre-registered.** Delivered
in the order of the brief, one commit per step: the GTK prefix rebuild → the XFCE libraries → Thunar + cycle
`m7f-thunar` → xfce4-panel (part 2) → xfdesktop, xfce4-settings, xfce4-appfinder + cycle `m7h-xfce` (part 3). No Pi
cycle has run any of it yet; no sibling repo is touched. XFCE is GPL/LGPL: the tarballs are sha256-pinned in `build.sh`, the sources live only in
`build-out/src/` (gitignored); what is committed is the build script, our patches (`patches/<pkg>/`, `git
format-patch`), the compat layer (SPDX BSD-3), configuration, the Pi script and the host test.

### Step 0: the GTK stack with target paths (`gtk3-wayland/build.sh --usr`)

`build.sh --usr --out build-out-usr` configures every package `--prefix /usr --sysconfdir /etc --localstatedir /var`
(pcre2: `CMAKE_INSTALL_PREFIX=/usr`) and installs with `DESTDIR=<out>/destdir`; `pkg-config-phoenix` runs with
`--define-prefix` (each `.pc` says `prefix=/usr`; pkgconf takes the prefix from where the file lies, so the destdir
and the `deps/` views resolve alike — no `PKG_CONFIG_SYSROOT_DIR`, which would double-prefix the views). The
default mode is unchanged: `build-out/gtk3-hello-stripped` is still `8d49230d71c91d8d` (the m7e binary). Build-host
path strings in gtk3-hello: **14 → 4** (the 4 left come from the reused M6 snapshot and the ports fontconfig:
libxkbcommon's `…/weston-drm/build-out-g6/prefix/etc/xkb`, libwayland-cursor's icon path, fontconfig's
`conf.avail`, one GObject `__FILE__`; all harmless on the Pi). The `--usr` programs are not staged (m7e keeps its
binaries); `build-out-usr/SHA256SUMS`: gtk3-hello `2a7962050417205f`.

### Versions and licences

| package | version | licence | build | what is off, and why |
|---|---|---|---|---|
| libxfce4util | **4.20.1** (latest 4.20.x) | LGPL-2.0+ | meson | introspection, vala, gtk-doc |
| xfconf | **4.20.0** | LGPL-2.0+ (lib), GPL-2.0+ (daemon) | autotools | the GSettings backend (a GIO **module** `.so`), introspection, vala, tests, bash completion. **GDBus only** (no libdbus); per-channel XML backend compiled in |
| libxfce4ui | **4.20.2** | LGPL-2.0+ | autotools | **X11**, **libSM/ICE** (session management), startup-notification, libgtop (xfce4-about's system info), epoxy, gudev, glade, introspection/vala; `--enable-wayland`, libxfce4kbd-private kept (Thunar needs it) |
| garcon | **4.20.0** | LGPL-2.0+ | autotools | introspection (garcon + garcon-gtk3 built) |
| exo | **4.20.0** | LGPL-2.0+ / GPL-2.0+ | autotools | — (gio-unix on) |
| libxfce4windowing | **4.20.7** | LGPL-2.1+ | meson | **X11/libwnck** (`-Dx11=disabled -Dwayland=enabled`: wlr-foreign-toplevel), tests, introspection |
| Thunar | **4.20.10** | GPL-2.0+ | autotools | X11 + libSM (patch 0001/0002), gudev (volume management), libnotify, libexif, **every plugin** (apr, sbr, tpa, uca, wallpaper: loadable modules); pcre2 on (bulk rename); thunarx built in |
| adwaita-icon-theme | **3.38.0** | CC-BY-SA-3.0 / LGPL-3 | data | the **last release with PNG full-colour icons** (≥ 40 is SVG only and needs librsvg, Rust); elementary-xfce (XFCE's default) is SVG only |
| shared-mime-info | **2.4** | GPL-2.0+ (data) | data | `mime.cache` only (GIO's xdgmime reads it alone when it is valid) |
| GTK stack | gtk3-wayland `--usr` | — | snapshot | copied to `build-out/gtk/` (destdir + views); `snapshots.txt` records the archives' sums |

**No GNU gettext on the build host.** `bin/msgfmt` (ours, BSD-3) stands in: `--desktop`/`--xml --template IN -o OUT`
copies the template (the merge with no translations), `.po → .mo` writes a valid empty catalogue, `--version`
satisfies meson's ≥ 0.19 check, `--statistics` autoconf's probe. Nothing is translated on Phoenix (`--disable-nls`).
`xdt-gen-visibility` (xfce4-dev-tools, GPL) is taken from the libxfce4util tarball at build time.

### What was patched and why

| patch | why |
|---|---|
| xfconf 0001 `common: use guint, not the non-standard uint` | `uint` is a glibc/BSD `<sys/types.h>` extension; libphoenix has none (a compile error) |
| Thunar 0001 `configure: X11 is optional (Wayland-only builds)` | `XDT_CHECK_LIBX11_REQUIRE` stops configure without libX11, although only the wallpaper plugin links it and libSM is already optional. Only the generated `configure` is changed (so make never re-runs autoconf) |
| Thunar 0002 `session-client: include gdkx.h only with libSM` | the one X11 GDK call (`gdk_x11_set_sm_client_id`) is under `HAVE_LIBSM`; a Wayland-only GTK has no `gdk/gdkx.h` |

**New compat (`xfce-wayland/compat/`, first on the include path, whole-archive, SPDX BSD-3):** `daemon()`
(libxfce4ui's `xfce_spawn()` detaches non-child launches with it; fork, `setsid`, optional `chdir("/")`, stdio to
`/dev/null`) and `NGROUPS_MAX` = 8 (POSIX's minimum; Thunar sizes an on-stack `gid_t` array with it; Phoenix has
no supplementary groups). Build flags: `-std=gnu11` for the autotools packages (GCC 16 defaults to C23),
`-fmacro-prefix-map` (no build-host paths in `g_return_if_fail()` messages), `-Wl,--gc-sections`, `.la` files
deleted after every install (they would point later static links at the host's `/usr/lib`). Warnings: 96–101 per
package are libphoenix's duplicate `getprogname` declaration against GLib's (`-Wredundant-decls`); the rest are 1–4
sign-compare/unused lines.

### Build, checks, artifacts

```
tools/gpu-lane/gtk3-wayland/build.sh --usr --out tools/gpu-lane/gtk3-wayland/build-out-usr   # ≈ 12 min, once
tools/gpu-lane/xfce-wayland/build.sh --until thunar      # ≈ 6 min; --until panel|desktop|all for the next parts
tools/gpu-lane/xfce-wayland/hosttest/run.sh              # seconds
```

`== programs` fails the build on any miss: **`nm -u` = 0, no `PT_INTERP`, 0 X11 symbols** (`XOpenDisplay`,
`XInternAtom`, `xcb_connect`, `gdk_x11_display_get_type`, `SmcOpenConnection`, `wnck_screen_get_default`), and per
program the symbols that prove the pieces are linked in.

| program (staged as) | text / data / bss | stripped | sha256 stripped (first 16) | symbols checked |
|---|---|---|---|---|
| xfconfd (`/usr/lib/xfce4/xfconf/xfconfd`, the path the `.service` file names) | 2 870 240 / 2 004 / 28 436 | 2 878 576 | **`a943c314ca559f4a`** | `g_bus_own_name`, `xfconf_backend_factory_get_backend`, `g_dbus_connection_register_object` |
| xfconf-query (`/bin/xfconf-query`) | 2 826 960 / 2 644 / 27 220 | 2 834 896 | **`87fc390bc7f9754d`** | `xfconf_channel_get_property`, `xfconf_init` |
| thunar (`/bin/thunar-wl`, the name labwc's m7a/m7c menus already use) | 17 614 248 / 88 832 / 70 976 | 17 711 232 | **`f840499d5352b1e3`** | `thunar_application_get`, `gdk_wayland_display_get_type`, `xfconf_channel_get`, `exo_icon_view_new`, `xfce_dialog_show_error`, `thunarx_provider_factory_get_default`, `g_file_monitor_directory` |

Build-host path strings: xfconfd / xfconf-query 0, thunar 10 (the 4 of the GTK stack above plus 6 `inkscape:export-filename`
comments inside GTK's built-in SVG sources). Unstripped binaries (addr2line) in `build-out/bin/`.

**Icons** (`tools/pngify-icon-theme.py`, ours): GTK 3 skips every `.svg` when gdk-pixbuf cannot load SVG, so the
themes are PNG only. Adwaita 3.38's full-colour PNGs at 16/22/24/32/48 as shipped; its 549 **symbolic** SVGs
encoded on the build host by `gtk-encode-symbolic-svg` into `*.symbolic.png` at 16 and 24 px (GTK 3 recolours
those like the SVG); hicolor = the XFCE programs' own `org.xfce.*` icons, scalable ones rendered to 16–48 px PNG
by the host's GdkPixbuf. `index.theme` lists every directory as `Type=Fixed`; `gtk-update-icon-cache` writes
`icon-theme.cache` for both (without it GTK walks the whole tree over NFS on the first lookup). Adwaita: 2 894 PNG
(12 MB), cache 66 332 B; hicolor 17 PNG. **MIME:** shared-mime-info 2.4's `freedesktop.org.xml` compiled by the
host's `update-mime-database` into `/usr/share/mime/mime.cache` (157 560 B, sha `6fd7ef67f7be559e`): Thunar's
content types and file icons.

**Host test** (`hosttest/run.sh`, **ALL PASS**): libxfce4util + xfconf from the same sources and patches built
natively (static), the host's dbus-daemon with a copy of `session-phoenix.conf` whose servicedir holds the built
`org.xfce.Xfconf.service`, and **the Pi script unchanged** (`session none`, no compositor): bus up; `ListChannels`
starts xfconfd **by bus activation**; `names=org.xfce.Xfconf`; `xfconf set_rc=0 get_rc=0 value=hello-1
thunar_thumbnail_mode=THUNAR_THUMBNAIL_MODE_NEVER` (the staged default read through `XDG_CONFIG_DIRS`);
`channels=thunar,xfce-phx-probe`; the bus stops on TERM with `socket=gone`; xfconfd saved `xfce-phx-probe.xml`
under `XDG_CONFIG_HOME`. Negative control `ACTIVATION=0`: `via=explicit`. This proves the script and the
configuration, not Phoenix. The same run also tests the compat **`daemon()`** natively under ASan/UBSan
(`hosttest/daemon_test.c`, compiled as `xfphx_daemon` so glibc's is not the one tested): for `daemon(1,0)` (libxfce4ui's
call), `daemon(0,0)` and `daemon(1,1)` the caller exits 0 at once, the detached process continues, returns 0, is a
session leader, is in `/` only when asked, and has stdio on `/dev/null` only when asked — **18/18 PASS**.

### Session design (`pi/xfce-desktop.sh`, `conf/`)

- **Bus:** `dbus-daemon --config-file=/etc/dbus-1/session-phoenix.conf --nofork` (the m7f stage-1 configuration:
  ANONYMOUS on `unix:path=/tmp/dbus-session`; GDBus picks ANONYMOUS from the server's list), then
  `DBUS_SESSION_BUS_ADDRESS` for everything after it. **xfconfd by activation** (a `ListChannels` call;
  `/usr/share/dbus-1/services/org.xfce.Xfconf.service`, `Exec=/usr/lib/xfce4/xfconf/xfconfd`), else started by the
  script (`via=explicit`); heartbeats list the `org.xfce.*` names on the bus (which XFCE programs registered).
- **Environment:** `GDK_BACKEND=wayland`, `GSETTINGS_BACKEND=memory` + the staged schemas, `NO_AT_BRIDGE=1`,
  `XDG_CURRENT_DESKTOP=XFCE`, `XDG_CONFIG_DIRS=/etc/xdg` (XFCE's defaults), `XDG_DATA_DIRS=/usr/share` (icons, MIME,
  `.desktop` files), and **`XDG_CONFIG_HOME`/`XDG_CACHE_HOME`/`XDG_DATA_HOME` under `/tmp/xfce-home/`** (xfconfd,
  Thunar and the panel write there, not on the NFS root). The same set is in `/etc/xdg/labwc-xfce/environment` for
  programs labwc starts from its menu or autostart.
- **labwc:** `-C /etc/xdg/labwc-xfce` (rc.xml: Super+Return foot, Super+E Thunar, Super+D / Alt+F2 the app finder;
  menu.xml: Run… = `xfce4-appfinder`, Terminal = foot, Files = `thunar-wl`, Settings = `xfce4-settings-manager`,
  Midnight Commander; autostart: xfdesktop + xfce4-panel, logs to `/tmp/xfce-logs/`). The `thunar` session uses a
  copy without the autostart. The m7a/m7c `/etc/xdg/labwc*` sets are untouched.
- **Thunar defaults** (`/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/thunar.xml`): thumbnails **never** (no tumbler
  on the bus), volume management off (no gudev/udisks), icon view, a 900×560 first window.
- **Degrades by design:** no gvfs (only GIO's local `file://`; no trash:/network:/ in the side pane), no directory
  monitor (GLib has no inotify/kqueue backend on Phoenix: Thunar shows a folder as read and does not refresh it by
  itself; F5 reloads), GIO's unix mount monitor has no `/proc/self/mountinfo`/`/etc/mtab` (a warning at most).

### Staging (done 2026-09-27; new names only — every path checked absent first, then `sha256sum -c` of all 2 928)

```
X=tools/gpu-lane/xfce-wayland/build-out; EXPORT=/srv/phoenix-rpi4-nfs-gcc16
# build.sh writes $X/stage (a tree mirroring the NFS root) and $X/stage.MANIFEST (sha256 + path per file)
while read -r sum path; do m=644; [ -x "$X/stage/$path" ] && m=755
  sudo -n install -D -m $m "$X/stage/$path" "$EXPORT/$path"; done < $X/stage.MANIFEST
(cd $EXPORT && sha256sum -c --quiet $OLDPWD/$X/stage.MANIFEST)    # all 2928 verified
```

| staged file | sha256 (first 16) |
|---|---|
| `/bin/thunar-wl`, `/bin/xfconf-query`, `/usr/lib/xfce4/xfconf/xfconfd` | `f840499d5352b1e3`, `87fc390bc7f9754d`, `a943c314ca559f4a` |
| `/bin/xfce-desktop.sh` (`pi/`; re-staged in part 3 as `b2455f524d7d5779`: the graceful `--quit` of the panel and xfdesktop) | `370af7cd29051919` |
| `/etc/xdg/labwc-xfce/{rc.xml,menu.xml,autostart,environment}` | `98dafbc45ac6fe26`, `41b30eae42fae8d9`, `de8c2b4dc6a1aefd`, `0361b39ceb64bbfb` |
| `/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/{thunar,xfce4-keyboard-shortcuts}.xml` | `6a1c666d029da52f`, `418368a0659e9d30` (libxfce4ui's default) |
| `/usr/share/dbus-1/services/org.xfce.Xfconf.service` (the directory m7f-dbus staged empty; one activatable name more does not change m7f's rows) | `bcb714d1448a9f17` |
| `/usr/share/applications/thunar.desktop` (`Exec=/bin/thunar-wl %F`) | `382d5e979f4025c2` |
| `/usr/share/mime/mime.cache` | `6fd7ef67f7be559e` |
| `/usr/share/icons/{Adwaita,hicolor}/` (index.theme, icon-theme.cache, PNGs) | see `stage.MANIFEST` |
| reused: `/bin/labwc` `c8a78d3d7e047711`, `/bin/foot`, `/bin/dbus-daemon` `0abfed003a78214d`, `/bin/dbus-send`, `/etc/dbus-1/session-phoenix.conf`, `/etc/machine-id`, `/usr/share/glib-2.0/schemas/gschemas.compiled`, `/etc/xdg/gtk-3.0/settings.ini` (Adwaita icons, hicolor fallback, DejaVu Sans 11), the servers, DejaVu + `/etc/fonts/fonts.conf` | — |

### Cycle `m7f-thunar` (Thunar under labwc; after `m7a-labwc`'s pixman arm; Bash `timeout: 600000`)

**Question:** does the XFCE file manager run on Phoenix — xfconfd on the session bus (by activation), a static
GTK 3 program built on the XFCE libraries mapping a window in labwc, listing `/` with Adwaita icons and MIME
types — and does the whole session stop cleanly?

```
./scripts/test-cycle-psh-interact.sh --label m7f-thunar --idle-secs 45 --max-cmd-secs 300 \
    --hdmi-dense-on 'XFCE thunar start' -- \
    "/bin/rpi4-v3d-async-low -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96 -C" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/xfce-desktop.sh thunar input" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

Wall clock ≈ boot 60–150 s + ~10 s bus/xfconf + ≤ 30 s labwc + 60 s hold + ≤ 25 s stop. Grade:

```
grep -a -E '^XFCE |Thunar|thunar|xfconf|Gtk-|Gdk-|GLib-|GLib-GIO-|Fontconfig|LABWC |^SHMSRV |^KMSTEST ' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m7f-thunar.log
./scripts/uart-summary.sh m7f-thunar
```

Allow ~1.3 % UART line corruption; EL0 dumps print twice; wlroots lines may carry ANSI colour escapes. Rows marked
**bench** need a person with the USB mouse/keyboard (otherwise **n/a**, not FAIL).

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `XFCE start session=thunar renderer=pixman … missing=none` | once | `missing=<paths>`: staging |
| 2 | `XFCE dbus=up pid=… wait_s=<1–5>` | as m7f-dbus row 1 | `dbus=missing` + the daemon's log lines: m7f-dbus decides |
| 3 | `XFCE xfconfd activation rc=0 took_s=<1–10>`, `XFCE xfconfd via=activation names=org.xfce.Xfconf` | **bus activation works on Phoenix** (dbus-daemon forks + execs xfconfd from the `.service` file) | `rc=1` with `Spawn.ChildExited`/`Spawn.ExecFailed`/`timed out` (printed): activation broken — then `via=explicit` must follow (the script starts xfconfd itself) and the rest is still graded; `via=failed`: xfconfd cannot own its name (its log is printed at the end) |
| 4 | `XFCE xfconf set_rc=0 get_rc=0 value=hello-<n> thunar_thumbnail_mode=THUNAR_THUMBNAIL_MODE_NEVER (rc 0)`, `XFCE xfconf channels=thunar,…` (possibly `xfce4-keyboard-shortcuts`) | GDBus round trip through xfconfd; the staged default read through `XDG_CONFIG_DIRS` | `set_rc≠0` with GLib CRITICALs about a NULL proxy: xfconf could not reach the bus (the host test's failure shape); `thumbnail_mode` empty: the defaults dir |
| 5 | `XFCE labwc socket=up name=wayland-0 wait_s=<1–30>` and labwc's m7a rows 2–8 | as m7a | m7a decides |
| 6 | `XFCE thunar start: /bin/thunar-wl /`, then GTK's allowed lines (m7e rows 4–5: locale fallback, libxkbcommon include-path errors, **`Using the built-in XKB keymap (evdev/pc105/us)`** until labwc's keymap arrives) | the 17.7 MB exec starts within ~5 s | nothing and `thunar=exited` in the next heartbeat: exec/crash — EL0 dump: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/xfce-wayland/build-out/bin/thunar <pc>` |
| 7 | allowed Thunar/GIO warnings: no directory monitor (`Unable to find default local directory monitor type` or similar), the unix mount monitor (`/proc/self/mountinfo`/`/etc/mtab`), the thumbnailer service absent (`org.freedesktop.thumbnails…`), no trash (`Operation not supported`) | degraded features, not failures | a `g_error`/abort (`Trace/breakpoint`), `Failed to register: …` + exit: GApplication on the bus — record the message |
| 8 | `XFCE hold … labwc=running thunar=running names=org.xfce.Xfconf,org.xfce.Thunar[,org.xfce.FileManager…]` ×6 | **Thunar registered its GApplication name on the bus** (GDBus, ANONYMOUS) | no `org.xfce.Thunar`: Thunar ran as a non-unique local application (GIO fell back without the bus) — note it, display still graded |
| 9 | `SHMSRV create` + `truncate … size≈2016000 … cap=2097152` (900×560 XRGB), more for resizes/popovers | GTK's wl_shm pools (memfd_create → shmsrv) | `SHMSRV FAIL alloc`: contiguous memory (E1) |
| 10 | HDMI (dense from `thunar start`): **a Thunar window with a labwc title bar**: a toolbar with symbolic arrow/home/search icons, the location bar `/`, a side pane (Places: `File System`, the home folder `root`; no Trash/Network), and the **icon view of `/`**: bin, dev, etc, root, tmp, usr, … as **Adwaita folder icons** with DejaVu labels; a status bar (`N folders`) | Thunar draws on Phoenix | folders as "missing image" squares: the icon theme (`/usr/share/icons/Adwaita/icon-theme.cache`, `XDG_DATA_DIRS`) — the rest still graded; empty view with a spinner forever: the GIO directory enumeration job (threads) — note the last GIO line; black window: row 9 |
| 11 | **bench:** double click on `usr` opens it (the location bar shows `/usr`), Back returns; right click on a file → context menu; F5 reloads | GIO jobs + GTK input | a hang: the heartbeats keep running, note the time of the click |
| 12 | stop: `XFCE thunar exited rc=143`, `XFCE labwc exited rc=0 … socket=gone`, `XFCE after labwc names=org.xfce.Xfconf`, `XFCE dbus exited rc=0 … socket=gone`; `XFCE log dbus-daemon: … Activating service name='org.xfce.Xfconf'` / `Successfully activated service`; `XFCE saved channels=xfce-phx-probe.xml[,thunar.xml]` (xfconfd saves when it loses the bus) | clean shutdown; xfconfd wrote its XML under `/tmp/xfce-home` | `saved channels=` empty: xfconfd's write path (rename/fsync) — its log lines above |
| 13 | `SHMSRV stats rc=0 live=0 bytes=0`, `KMSTEST stats … bos=0` | all released | as m7a rows 12–13 |
| 14 | fault dumps | 0 kernel, 0 EL0 | EL0 in thunar/xfconfd: addr2line on `build-out/bin/<prog>` |

**Decides:** rows 2–4 = the XFCE settings system works on Phoenix (the base of every XFCE program); rows 6–10 =
Thunar, i.e. the XFCE libraries (libxfce4util, xfconf, libxfce4ui, exo) on GTK 3 Wayland. Then `m7h-xfce`.

### Part 2: xfce4-panel 4.20.8 (built, staged)

meson, `-Dx11=disabled -Dwayland=enabled -Dgtk-layer-shell=enabled -Ddbusmenu=disabled -Dbuiltin-plugins=true`.
Panel plugins are normally loadable modules (`/usr/lib/xfce4/panel/plugins/lib<name>.so`, `g_module_open`); a
static Phoenix program has no loader for them, so **patch 0001 `panel: optionally link the internal plugins into
xfce4-panel`** adds the meson option `builtin-plugins`: every plugin becomes a static library compiled with
`-Dxfce_panel_module_init=xfce_panel_builtin_<module>_init` (they all export the same symbol otherwise), a generated
`panel-builtin-plugins.c` maps each `X-XFCE-Module` name to its init function, and
`panel_module_new_from_desktop_file()` consults the table before it looks for a module file (such plugins always
run internally: no wrapper process). Built-in plugins skip their copy of the panel's GResource (the panel has it);
the plugins' meson subdirectories are processed before `panel/`. The default (`false`) builds what it did.
**All 11 plugins are linked in**: actions, applicationsmenu, clock, directorymenu, launcher, pager, separator,
showdesktop, systray (StatusNotifier over D-Bus only: the XEmbed tray is X11), tasklist, windowmenu (the last two
via libxfce4windowing's Wayland backend, wlr-foreign-toplevel).

| program | text / data / bss | stripped | sha256 stripped (first 16) | symbols checked |
|---|---|---|---|---|
| xfce4-panel (`/bin/xfce4-panel`) | 17 879 528 / 82 912 / 69 456 | 17 967 976 | **`408469d488270c9e`** | `panel_builtin_plugins`, `xfce_panel_builtin_{applicationsmenu,clock,tasklist,windowmenu,launcher,separator,actions}_init`, `gtk_layer_init_for_window`, `xfw_screen_get_default`, `garcon_menu_new_for_path` |

**Layout** (`conf/xfconf/xfce4-panel.xml` → `/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml`,
`configver=2` = the panel's `XFCE4_PANEL_CONFIG_VERSION`, so the panel does **not** spawn its `migrate` helper on the
first start — another 17 MB GTK program, not staged): one 30 px panel on the top edge: applications menu, launchers
for foot (`/usr/share/applications/foot.desktop`, m7c's) and Thunar, window buttons (tasklist), a transparent
expanding spacer, the clock (`%a %d %b  %H:%M`). Left out of the layout: pager (workspaces), systray, actions
(`xfce4-session-logout` does not exist). The panel's own `default.xml`, garcon's `xfce-applications.menu` and the 16
`desktop-directories` are staged for the applications menu.

Staged (new paths; the hicolor theme's `index.theme`/`icon-theme.cache` were **updated** — `73fdaad42f2996a8` /
`ac221a33f8357347` — to include the panel's `org.xfce.panel.*` icons; a superset, m7f-thunar is unaffected):
`/bin/xfce4-panel` `408469d488270c9e`, `/usr/share/xfce4/panel/plugins/*.desktop` (11),
`/etc/xdg/xfce4/panel/default.xml` `54dbf7527908ddce`, `/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml`
`ae5caafbfa4383c6`, `/etc/xdg/menus/xfce-applications.menu` `7371a09dcb7bccc0`, `/usr/share/desktop-directories/`.
All 3 042 files of `stage.MANIFEST` verified on the export.

### Part 3: xfdesktop 4.20.2, xfce4-settings 4.20.5, xfce4-appfinder 4.20.0 (built, staged)

- **xfdesktop** (meson): `-Dx11=disabled -Dwayland=enabled` (the backdrop window on gtk-layer-shell's background
  layer, one per monitor), `-Ddesktop-menu=enabled` (garcon: right click on the desktop = the applications menu),
  desktop icons on but **file icons off** (they need libyaml + thunarx; the icon style then defaults to *window icons*,
  i.e. minimised windows), no thunarx/libnotify; `-Ddefault-backdrop-filename=backgrounds/phoenix/phoenix-gradient-1920x1080.png`
  (m7c's staged PNG: the compiled-in default is XFCE's SVG, which Phoenix cannot load). Patch **xfdesktop 0001
  `windowlist: do not include gdkx.h`** (unused there; no `gdk/gdkx.h` without the X11 backend).
- **xfce4-settings** (autotools): `--disable-x11 --enable-wayland --enable-gtk-layer-shell`, no xrandr/xcursor/
  xorg-libinput/libxklavier/libnotify/upower/colord/sound settings. Configure asks pkg-config for the (host)
  `wayland-scanner`: build.sh writes a `hostpc/wayland-scanner.pc`. It builds xfce4-settings-manager,
  xfce4-appearance-settings, xfce4-display-settings (wlr-output-management), xfce4-mime-settings,
  xfce4-settings-editor and xfsettingsd; **staged: the manager and the appearance dialog** (their `.desktop` files +
  `xfce-settings-manager.menu`, `xsettings.xml` default). xfsettingsd is not started: on Wayland it has no XSETTINGS
  to serve, so the appearance dialog writes the `xsettings` channel but running GTK programs do not restyle.
- **xfce4-appfinder** (autotools): plain; `xfce4-appfinder.desktop` + `xfce4-run.desktop` (`--collapsed`, Alt+F2).
- **Build trap fixed:** pkgconf `--define-prefix` also moves every `.pc` variable that starts with the old prefix,
  so GLib's `glib_compile_resources=/usr/bin/…` (the host tool, set by gtk3-wayland's `fix_glib_pc`) became the
  **target** binary in the snapshot, which then segfaulted under binfmt/qemu (xfce4-settings' resources). The
  snapshot's `usr/bin/{glib-compile-resources,glib-compile-schemas,glib-mkenums,glib-genmarshal,gdbus-codegen,
  gobject-query}` are now symlinks to the host's tools.
- Also staged: **`/bin/gdbus-wl`** = GIO's own `gdbus` from the GTK snapshot (static, `nm -u` 0), for m7f-dbus
  step 5 (`export GDBUS=/bin/gdbus-wl`; the m7f script's default `/bin/gdbus` stays absent, so m7f as registered
  is unchanged).

| program (staged as) | text / data / bss | stripped | sha256 stripped (first 16) | symbols checked |
|---|---|---|---|---|
| xfdesktop (`/bin/xfdesktop`) | 17 209 248 / 82 960 / 68 376 | 17 299 776 | **`124ce9a5e201c7a2`** | `xfce_desktop_new`, `gtk_layer_init_for_window`, `xfw_screen_get_default`, `gdk_wayland_display_get_type` |
| xfce4-settings-manager (`/bin/`) | 16 983 976 / 80 936 / 67 128 | 17 070 360 | **`acbb4a6f5fab6614`** | `garcon_menu_new_for_path`, `xfconf_channel_get` |
| xfce4-appearance-settings (`/bin/`) | 17 063 832 / 80 232 / 66 712 | 17 149 680 | **`3c6e3af48b1bc23a`** | `xfconf_channel_get`, `gtk_icon_theme_get_default` |
| xfce4-appfinder (`/bin/`) | 17 003 000 / 81 192 / 67 160 | 17 089 640 | **`c34db94e1856c659`** | `garcon_menu_new_applications`, `xfconf_channel_get` |
| gdbus (`/bin/gdbus-wl`) | 4 115 564 / 2 440 / 38 780 | 4 123 304 | `121453f45b97fb47` | `g_dbus_connection_new_for_address_sync`, `_g_dbus_auth_mechanism_anon_get_type` |

All: `nm -u` 0, no `PT_INTERP`, 0 X11 symbols. **Staged 2026-09-27** (new paths only, plus the hicolor
`index.theme`/`icon-theme.cache` updated again — `3db4d3982f4e1101`/`aee72feb7a9c20fe` — for the XFCE settings and
xfdesktop icons); `stage.MANIFEST` = **3 168 files, all verified** on the export; frozen copy of the tree:
`/home/houp/.claude/jobs/c8f1289c/tmp/m7h-frozen/`. Other staged files: `/etc/xdg/menus/xfce-settings-manager.menu`
`dcf1bb4b6c63564b`, `/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml` `d6209f9b4f7e9bbe`,
`/usr/share/applications/{xfce-settings-manager,xfce-ui-settings,xfce4-appfinder,xfce4-run}.desktop`
`264e748b0c9159ce`, `4028a7505c942498`, `144d2f101aa311ab`, `05e29141ef057715`.

### Cycle `m7h-xfce` (★ the showcase; after `m7f-thunar`; Bash `timeout: 600000`)

**Question:** does XFCE come up as a desktop under labwc — xfconfd on the bus, the backdrop (xfdesktop) and the
panel (xfce4-panel with its built-in plugins) on layer-shell surfaces, the window buttons fed by
wlr-foreign-toplevel, and a Thunar window listing `/` — and does it all stop cleanly?

```
./scripts/test-cycle-psh-interact.sh --label m7h-xfce --idle-secs 45 --max-cmd-secs 420 \
    --hdmi-dense-on 'XFCE labwc socket=up' -- \
    "/bin/rpi4-v3d-async-low -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96 -C" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/xfce-desktop.sh xfce input" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

Wall clock ≈ boot 60–150 s + ~10 s bus/xfconf + ≤ 30 s labwc + ≤ 60 s until the panel registers (two 17 MB
execs over NFS at once) + 60 s hold + ≤ 30 s stop. Grade as m7f-thunar plus
`grep -a -E 'xfdesktop|xfce4-panel|panel|layer|run session script|spawned child' …m7h-xfce.log`. The panel's and
xfdesktop's own stderr is in `/tmp/xfce-logs/` and printed by the script at the end (`XFCE log xfce4-panel: …`).
Rows marked **bench** need a person with the USB mouse/keyboard (otherwise **n/a**).

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `XFCE start session=xfce … missing=none`; rows 2–4 of m7f-thunar (bus, `xfconfd via=activation`, round trip, `channels=` now incl. `xfce4-panel,xsettings`) | as m7f | m7f's rows decide |
| 2 | `XFCE labwc start conf=/etc/xdg/labwc-xfce files=rc.xml,menu.xml,autostart,environment`, `socket=up`; labwc: `run session script /etc/xdg/labwc-xfce/autostart`, no `spawned child … exited with 127` | busybox `sh` starts xfdesktop and xfce4-panel | `127`: a path in autostart / staging |
| 3 | `XFCE waiting for the panel … names=org.xfce.Xfconf[,org.xfce.xfdesktop]` (≤ 12 lines), then `XFCE thunar start` | the panel registers `org.xfce.Panel` within 60 s | 60 s without it: Thunar still starts; read `XFCE log xfce4-panel:` at the end |
| 4 | `XFCE hold … names=` containing **`org.xfce.Panel`, `org.xfce.xfdesktop`, `org.xfce.Thunar`, `org.xfce.Xfconf`** ×6 | every component on the bus (GDBus/ANONYMOUS from four programs) | a name missing: that program's log |
| 5 | xfdesktop: `SHMSRV create` + `truncate … size=8294400 … cap=16777216` (the 1920×1080 backdrop; as m7c row 3) | once (per redraw at most 2) | `SHMSRV FAIL alloc … cap=16777216`: no 16 MiB contiguous block — the known E1 limit; the backdrop is black, the rest still graded |
| 6 | panel log: **no** `Wayland detected without layer-shell support`, **no** `…without foreign-toplevel-management support`, **no** `Failed to load module` / `There was no module found` / `lacks a plugin register function` (patch 0001: all 8 plugins come from the built-in table), no migrate dialog (`configver=2`) | the panel on layer-shell with its built-in plugins | a module message: the builtin table missed a name (`strings -a /bin/xfce4-panel \| grep builtin:`); the layer-shell warning: labwc/gtk-layer-shell (m7e's `--layer` row) |
| 7 | allowed in the logs: GTK's m7e rows 4–5 lines, `Using the built-in XKB keymap`, a garcon/`exo` warning about a missing `.directory` or icon, the thumbnailer/directory-monitor lines of m7f row 7 | — | a `g_error`/abort: record it; EL0 dump: addr2line on `build-out/bin/<prog>` |
| 8 | HDMI (dense from `labwc socket=up`): **the ember gradient wallpaper** (xfdesktop, on the background layer), **a 30 px panel along the top**: at the left the applications-menu button (XFCE logo), a separator, a **foot** and a **Thunar** launcher icon, then window buttons, and at the right **the clock** (`<Day> <dd> <Mon>  <hh:mm>` in DejaVu Sans: the date is whatever the Pi's clock says — ntpclient or none); a **Thunar window** (labwc title bar) showing `/` as Adwaita folder icons (m7f row 10), with **its button in the panel's tasklist**; the software cursor | the XFCE desktop | wallpaper black but panel present: row 5; no panel: row 6 / its log; the tasklist empty while Thunar is open: wlr-foreign-toplevel → libxfce4windowing (labwc offers it: stage 1 notes); the clock text boxes: fontconfig |
| 9 | **bench:** the applications-menu button opens the menu (categories from garcon: System → Foot / File Manager / Midnight Commander, Settings → Settings Manager / Appearance, Accessories → Application Finder …); choosing Foot opens a terminal (fork/exec from the panel: `xfce_spawn`, compat `daemon()` for detached launches) | the panel's menu launches programs | a menu without entries: `/etc/xdg/menus/xfce-applications.menu` or `XDG_DATA_DIRS`; nothing happens: `spawned`/`exec` errors |
| 10 | **bench:** clicking Thunar's button in the tasklist minimises/raises it; right click on the wallpaper → xfdesktop's menu; Alt+F2 → xfce4-appfinder (collapsed); Super+E → a second Thunar window (in the running instance: GApplication) | desktop interaction | the menu of labwc instead of xfdesktop's: xfdesktop did not take the click (layer-shell input region) — note |
| 11 | **bench:** Settings Manager (from the menu or the labwc root menu) shows its grid with *Appearance* (the only staged dialog besides the manager); Appearance opens with Style/Icons/Fonts tabs listing Adwaita and DejaVu | xfce4-settings on Wayland | icons list empty: the theme scan of `/usr/share/icons` |
| 12 | stop: `XFCE thunar exited rc=143`; **`XFCE quit panel_rc=0 xfdesktop_rc=0 wait_s=<0–5> names=org.xfce.Xfconf`** (the script runs `xfce4-panel --quit` and `xfdesktop --quit`: each asks the running instance over the bus — GTK programs abort rather than exit when their display goes, and an aborted panel saves nothing); `XFCE labwc exited rc=0 … socket=gone`, `XFCE after labwc names=org.xfce.Xfconf`, `XFCE dbus exited rc=0 … socket=gone`, `XFCE saved channels=` incl. `xfce4-panel.xml` (the panel saves its layout on quit) | clean shutdown | `quit … names=` still listing Panel/xfdesktop: the remote quit did not arrive (the `XFCE log panel-quit:` lines); they then abort when labwc goes (`Error reading events from display` / `Lost connection to Wayland compositor`) — note, row 14 may then show their abort |
| 13 | `SHMSRV stats rc=0 live=0 bytes=0`, `KMSTEST stats … bos=0` | all released | `live>0`: an orphaned autostart client |
| 14 | fault dumps | 0 kernel, 0 EL0 | addr2line on `tools/gpu-lane/xfce-wayland/build-out/bin/<prog>` |

**Decides:** rows 4, 6, 8 = the XFCE desktop runs on Phoenix-RTOS. A failing row 5 alone (no 16 MiB block) is a
shmsrv/E1 result, not an XFCE one.

### What remains (XFCE lane)

1. The Pi cycles: `m7f-thunar`, then `m7h-xfce` (and `m7f-dbus` step 5 with `GDBUS=/bin/gdbus-wl`).
2. Not built or not staged, by choice: xfsettingsd (X11 XSETTINGS; on Wayland the appearance settings reach new GTK
   programs only through `settings.ini`), xfce4-display-settings / mime-settings / settings-editor (built, not
   staged), xfdesktop-settings (the backdrop dialog), xfce4-session (X11 session manager; labwc is the session),
   xfce4-terminal (VTE; foot is the terminal), Thunar plugins, tumbler thumbnails, gvfs (trash/network), file icons
   on the desktop (libyaml + thunarx), the panel's migrate helper and wrapper (not needed with built-in plugins).
3. With SO_PEERCRED merged, glib still needs a Phoenix `struct ucred` case before GDBus can use EXTERNAL (D-Bus
   section above); until then every XFCE program authenticates ANONYMOUS.
4. Size: each XFCE program is its own 17 MB static GTK binary (≈ 120 MB staged for 8 programs); the dynamic-linking
   work (Phase B) would share one GTK copy.

## Pi milestones (pre-registered as each piece lands)

| cycle | shows |
|---|---|
| `m7a-labwc` | labwc starts on HDMI (output enabled, cursor, root menu) with pixman then GLES2 |
| `m7b-foot` | foot opens in labwc and draws text; keyboard input reaches it (`rpi4-kms -C` frees the console keyboard) |
| `m7c-desktop` | wallpaper + foot + fuzzel launcher; window move/resize with the mouse; clean exit |
| `m7e-gtk3` | a GTK3 window (gtk3-hello, then gtk3-widget-factory) under Weston, then under labwc — **pre-registered** in the GTK3 section |
| `m7f-dbus` | `dbus-daemon --session` up; `dbus-send` ping round trip; GDBus client connects |
| `m7f-thunar` | xfconfd on the bus by activation, Thunar under labwc lists `/` with Adwaita icons — **pre-registered** in stage 4 |
| `m7h-xfce` | ★ labwc + xfce4-panel + xfdesktop + Thunar + foot: the showcase desktop; Thunar browses `/`, the panel's app menu launches foot — **pre-registered** in stage 4 part 3 |
| `m7i-xfce-demo` | the demo session in one psh command (`/bin/xfce-session`): servers, XFCE, a second Thunar over the bus, local time, Log Out, clean quits; pixman then gles2 — **pre-registered** in stage 5 |
| `m7j-atril` | Atril 1.28 (Poppler 26.09, PDF backend built in) under XFCE on labwc: `sample.pdf` windowed, then `--fullscreen`, then `--presentation` — **pre-registered** in "Stage 6: Atril" |
| `m7d-gl-client` | weston-simple-egl / kmscube-style GL client inside labwc (G4/G6/G7 in a real compositor) |

## Scheduling

Code work (builds) starts now in parallel with subagents. Pi cycles are queued after the current Pi queue
(queues 46–51: the P10 gates, vkq compute, g6-sync, m6i-low, build 18 with C3 arm B + c1b18, mig-all). Status is
tracked in [PLAN.md](PLAN.md) (M7 row) and the weekly log.

## Result — `m7f-dbus` (chain54, build 18, 2026-09-27 19:45): ✅ PASS — a D-Bus session bus on Phoenix

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-194346-m7f-dbus.log`. Both arms (`anon`: ANONYMOUS only; `external`: EXTERNAL first, ANONYMOUS fallback):
`DBUSPHX socket=up wait_s=1`, `listnames rc=0`, `ping rc=0`, `busid rc=0`, `creds rc=0`, a signal routed
`dbus-send` → `dbus-monitor` (`seen_member=1 seen_payload=1`), `daemon exited rc=0 after_term_s=1 socket=gone`.
Auth: anon arm `anonymous=6 external=0`; external arm `anonymous=6 external=0 external_no_credentials=6`. EXTERNAL
is rejected for want of peer credentials and every client falls back to ANONYMOUS, as predicted. The daemon logs
peer pid/uid as unknown (`18446744073709551615`), so `BecomeMonitor` is refused ("unknown uid") and dbus-monitor
falls back to a match rule (it still saw the signal). `gdbus=absent`: the GIO client is exercised by m7f-thunar /
m7h-xfce (xfconfd, bus activation). **Decides:** the XFCE stage can rely on the bus. EXTERNAL auth waits for
`SO_PEERCRED` (kernel branch `feat/dbus-peercred`) plus a glib credentials patch.

## Result — `m7a-labwc` / `m7b-foot` (chain55, build 18, 2026-09-27 19:50–20:06)

**m7a-labwc ✗ (setup):** both arms stopped at `[backend/libinput/backend.c:111] libinput initialization failed, no
input devices` → `unable to start the wlroots backend`, after the DRM backend had already mode-set HDMI-A-1 to
1920x1080@60 (1 flip). The `noinput` arms need `WLR_LIBINPUT_NO_DEVICES=1`: fixed in both launchers (`62c15b58d`),
rerun `m7a2-labwc` queued.

**m7b-foot 🟡: labwc runs, foot does not map.** Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-195642-m7b-foot.log`.
All three arms (colors, mc, autostart): `LABWC hold … labwc=running` for 30 s, `labwc exited rc=0 after_term_s=1
socket=gone`, 0 exceptions. **HDMI (`…-200016-m7b-foot-tick.png`): labwc's black desktop with a software cursor**,
so labwc renders on the new lane. foot's window never appears: foot `err: shm.c:468: failed to seal SHM backing
memory file: Invalid argument`, labwc `[xdg.c:372] client (foot) did not respond to configure request in 100 ms`,
`KMS srv flipstat flips=4` over 30 s, and wlroots' `[backend/session/session.c:352] Stat failed: Function not
implemented` every second. Sent back to the labwc agent (foot map path + the session stat loop).

### m7b analysis and fixes (labwc agent, 2026-09-27 evening) — rebuilt as `*-2`, cycle `m7b2-foot`

**Root cause 1 — foot never draws: `read()` of an emulated timerfd always said `EAGAIN`.** The M6 compat timers
(`weston-drm/compat/src/wlphx_epoll.c`) are table entries over a socketpair that never carries data: epoll reports
them, but a `read()` goes to the empty socket. libwayland never reads its timers; **foot does**. When the shell's
prompt arrives, foot arms its delayed-render timers (`terminal.c:357`, `is_armed = true`) and renders the grid only
when a timer's `read()` returns an expiration count > 0 (`fdm_delayed_render`, `render.c:4349`: `grid &&
!is_armed`). Every read was `EAGAIN`, `is_armed` never cleared, so no grid frame was ever committed after the first
configure: `did not respond to configure request`, no window, and a busy epoll loop on the always-"expired" timer.
The seal error (`F_ADD_SEALS` → `EINVAL`, shmsrv has no seals) is non-fatal by foot's own code and unrelated.
**Fix:** real timerfd semantics in the M6 compat. Each timer counts expirations not yet read (one-shot: fires once
and disarms; periodic: every elapsed interval); readable while the count is non-zero; `timerfd_settime()` discards
it; `wlphx_timer_read()` returns the uint64 count and resets it (`EAGAIN` for an unexpired `TFD_NONBLOCK` timer, a
sleep for a blocking one). Programs reach it through `-Wl,--wrap=read` → `labwc-drm/compat/src/lwphx_read.c`
(labwc-drm links every program with it; Weston's links are unchanged and its behaviour too: libwayland re-arms,
never reads). **Host test** `hosttest/timerfd_read_test.c` (foot's pattern: arm, epoll, read = 1, then not
readable; periodic count; settime reset; blocking read; pipes untouched): 15/15 PASS; its **negative control**, the
same test linked without the wrapper (= the m7b binaries), fails the 4 "FOOT ROW" checks exactly as the Pi did.
The M6 host tests still pass.

**Root cause 2 — `session.c:352 Stat failed: Function not implemented` every second = the keyboard.**
`wlr_session_open_file()` `fstat()`s every device it opens and gives up on failure; Phoenix's usbkbd server does not
answer the attribute requests behind `fstat()` (usbmouse does: m7b configured `mouse0`). libinput-phoenix retried
the keyboard once a second, so labwc never had one. **Fix:** wlroots patch 0004 `session: open devices whose
fstat() fails on Phoenix-RTOS` (device number 0; it is only compared with udev events of DRM devices).

**Also:** foot and fuzzel are now built with buildtype `plain` + `-O2 -g`: meson's `debug*` buildtypes define
`_DEBUG`, which made foot's `UNITTEST` blocks constructors that ran at every start (m7b's `layout 'se'` XKB errors
were a unit test). Assertions stay on.

| file (`build-out-m7b2/`, frozen in `…/tmp/m7b2-frozen/`) | staged as | sha256 stripped (first 16) | unstripped |
|---|---|---|---|
| `labwc-stripped` (wlroots 0004) | `/bin/labwc-2` | **`3632cb541660af71`** | `f79fa6ab725f9fb9` |
| `foot-stripped` (timerfd read, no unit tests) | `/bin/foot-2` | **`ec603ce5f8f4dc1c`** | `81461b5d7e4693d9` |
| `fuzzel-stripped` (same; fuzzel reads its timers too) | `/bin/fuzzel-2` | **`180c52bc28242872`** | `65d158fd79960fe3` |
| `pi/labwc-desktop.sh` (+ `WLR_LIBINPUT_NO_DEVICES=1` from `62c15b58d`, + knobs below) | `/bin/labwc-desktop-2.sh` | **`8ab8a4f247bf8ba3`** | — |

swaybg needs no change (no timerfd). Nothing already staged was overwritten (`/bin/labwc` `c8a78d3d…`, `/bin/foot`
`54d42325…`, `/bin/fuzzel` `bc4e09ea…`, both earlier scripts re-checked). **New knobs** in `labwc-desktop-2.sh`:
`LABWC`, **`FOOT`**, **`FUZZEL`**, **`SWAYBG`**; with a non-default value the configuration copy in
`/tmp/labwc-conf` has `/bin/foot`, `/bin/fuzzel`, `/bin/swaybg` replaced in rc.xml, menu.xml and autostart (bash
only; the export has no sed), so labwc's own launches (autostart, menu, keybinds) use the new binaries too. fuzzel
started by the script gets `--terminal=$FOOT`. Not covered: the `.desktop` entries and `/etc/xdg/fuzzel/fuzzel.ini`
still name `/bin/foot` — once `m7b2` passes, re-staging `/bin/foot`, `/bin/labwc`, `/bin/fuzzel` in place from the
m7b2-frozen set is the clean end state.

#### Cycle `m7b2-foot` (re-registration of m7b; Bash `timeout: 600000`)

```
./scripts/test-cycle-psh-interact.sh --label m7b2-foot --idle-secs 45 --max-cmd-secs 200 \
    --hdmi-dense-on 'LABWC client start|LABWC socket=up' -- \
    "/bin/rpi4-v3d-async-g6 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96 -C" \
    "/bin/shmsrv -v" \
    "export LABWC=/bin/labwc-2" \
    "export FOOT=/bin/foot-2" \
    "/bin/bash /bin/labwc-desktop-2.sh pixman colors input" \
    "/bin/bash /bin/labwc-desktop-2.sh pixman mc input" \
    "/bin/bash /bin/labwc-desktop-2.sh pixman autostart input" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

Predictions: the m7b table, with these changes. Row 1 reads `LABWC start … labwc=/bin/labwc-2 foot=/bin/foot-2 …
conf=/tmp/labwc-conf`. Arm C's `run session script` names `/tmp/labwc-conf/autostart`, a copy that runs
`/bin/foot-2`. **No `Stat failed` lines.** Expect `configuring input device … (kbd0)` next to `mouse0` and
`using the builtin XKB keymap`. There are **no** `xkbcommon: ERROR … layout 'se'` lines, because the unit tests
are gone. `failed to seal SHM backing memory file` stays: it is expected and not fatal. **No `did not respond to
configure request`**, or at most one early line, and the foot window then maps. Rows 6–8 are unchanged: the colour
ramps and Unicode line, mc's two panels, and the autostarted prompt. If foot still does not map, grade from foot's
lines after `shm.c`. A new `err:` there, or a busy loop, means the timerfd fix did not reach the binary: check
`aarch64-phoenix-nm tools/gpu-lane/labwc-drm/build-out-m7b2/foot | grep wlphx_timer_read`.

**Cycles to rerun or re-point:**
- `m7b2-foot`: above.
- `m7c-desktop`: use `/bin/labwc-desktop-2.sh` and add `export LABWC=/bin/labwc-2`, `export FOOT=/bin/foot-2`,
  `export FUZZEL=/bin/fuzzel-2` before it (`CONF_DIR=/etc/xdg/labwc-m7c` as registered). With the old binaries,
  foot and fuzzel would not draw and the keyboard would not open.
- `m7a2-labwc`: runs as queued (weston-simple-shm has no timerfd reads, and it is `noinput`). `export
  LABWC=/bin/labwc-2` is optional; it also fixes the keyboard of the `input` runs.
- `m7f-thunar` and `m7h-xfce` (`xfce-desktop.sh`): export `LABWC=/bin/labwc-2` so the keyboard opens. Any
  foot/fuzzel those sessions start must come from `/bin/foot-2` / `/bin/fuzzel-2` (xfce-desktop.sh or its
  autostart), or wait for the in-place re-stage. GTK clients are not affected by the timerfd bug: GLib uses its own
  poll loop.

## Result — `m7e-gtk3` (chain56, build 18, 2026-09-27 20:09–20:17): ✅ PASS — GTK 3.24.52 renders on Phoenix

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-200617-m7e-gtk3.log`; HDMI `artifacts/hdmi/20260927-201007-m7e-gtk3-tick.png` (arm A) and `…-201419-…` (arm B), both under Weston
(pixman, kiosk fullscreen).

- **Arm A gtk3-hello:** `GTK3HELLO start gtk=3.24.52 glib=2.88.3 … backend=wayland`, `Using the built-in XKB keymap`,
  `gio dir=/ rc=0 entries=92`, `mapped … size=640x480`, `first-draw t=3.59`, `clicked n=1`; the main loop ticked through the
  hold; `weston exited rc=0`. HDMI: "Hello from GTK 3.24.52 on Phoenix-RTOS — clicked 1 time", a button and a GtkTreeView
  listing `/` through GIO (name/kind/size), Adwaita styling.
- **Arm B gtk3-widget-factory:** the complete widget factory drawn correctly: entries, combo boxes, toggle/check/radio
  buttons, spin button, font and colour buttons, switches, sliders and progress bars, a tree view with icons, a text
  view, notebooks in all four tab positions.
- Noise, not failure: repeated `Gdk-CRITICAL gdk_seat_get_keyboard: assertion 'GDK_IS_SEAT (seat)'` (the noinput arm
  has no seat), 4× `Could not find signal handler 'gtk_widget_hide_on_delete'` (no GModule symbol lookup in a
  static binary), one Adwaita asset pixbuf warning. 0 exceptions, 0 EL1.

**Decides:** GTK3 (Wayland backend) works on the new lane: the toolkit the XFCE stage is built on.

## Result — `m7b2-foot` (chain61, build 18, 2026-09-27 20:18–20:27): ✅ PASS — a Wayland terminal and file manager on Phoenix

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-201807-m7b2-foot.log`; HDMI `artifacts/hdmi/20260927-202331-m7b2-foot-tick.png` (arm A) and `…-202512-…` (arm B). Binaries
`labwc-2` / `foot-2` (compat timerfd `read()` + keyboard `fstat` fixes).

- labwc now **configures the USB keyboard** (`configuring input device Phoenix USB keyboard (kbd0)`), **0 `Stat failed`**
  lines, 0 exceptions / EL1; every arm `labwc exited rc=0 after_term_s=1 socket=gone`.
- **Arm A (colours):** foot in a labwc window (server-side title bar with menu / min / max / close) shows 24-bit colour
  gradients, Unicode (Polish, Greek, Cyrillic, box drawing, arrows, ✓ ✗ € °), the 16-colour palette and
  bold / italic / underline / reverse.
- **Arm B (`foot -e mc /`, the menu's "Files" entry):** **GNU Midnight Commander 4.8.31** in foot: both panels list `/`,
  menu bar and function-key bar drawn.
- Still logged, harmless: foot `failed to seal SHM backing memory file` (non-fatal) and one `did not respond to
  configure request in 100 ms` at start.

**Decides:** M7 stage 1 works: a modern Wayland terminal (foot) and a file manager (mc) under labwc on the new lane.

## Result — `m7c-desktop` (chain61, build 18, 2026-09-27 20:36–20:39): ✅ PASS — a Wayland desktop on HDMI

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-202908-m7c-desktop.log`; HDMI **`artifacts/hdmi/20260927-203751-m7c-desktop-tick.png`**: the CC0 gradient wallpaper (swaybg; the
orange glow is orange, so R/B is correct), a foot window with an interactive `bash-5.2#` prompt (labwc
title bar), and **fuzzel's launcher** over it listing Appearance, Application Finder, Bash, File Manager, Foot,
Midnight Commander, Run Program… and Settings Manager (the XFCE .desktop entries are already staged). `labwc exited
rc=0`, `SHMSRV stats live=0`, 0 exceptions / EL1 / `Stat failed`. swaybg's 1920×1080 buffer (8 MiB shmsrv) was
allocated without trouble (the predicted contiguous-memory risk did not occur).

## Result — `m7f-thunar` (chain61, build 18, 2026-09-27 20:44–20:46): ✅ PASS — XFCE's Thunar on Phoenix

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-204025-m7f-thunar.log`; HDMI **`artifacts/hdmi/20260927-204524-m7f-thunar-tick.png`**: "File System - Thunar" under labwc:
menu bar, navigation toolbar, location bar `/`, the root-account warning banner, the Places/Devices sidebar, Adwaita
(PNG) folder icons for `/`, and the status bar (`19 folders | 73 files: 15.4 GiB … | Free space: 10.5 GiB`).

- `XFCE xfconfd activation rc=0 took_s=1`, `via=activation names=org.xfce.Xfconf` (D-Bus bus activation works);
  `xfconf set_rc=0 get_rc=0 value=hello-2`, the staged Thunar default read back; channels
  `thunar,xfce4-keyboard-shortcuts,xfce4-panel,xsettings`.
- Thunar owns `org.xfce.FileManager` + `org.xfce.Thunar` for the whole hold; at stop: `labwc exited rc=0`,
  `dbus exited rc=0`, settings saved (`thunar.xml`, `xfce-phx-probe.xml`). 0 exceptions / EL1 / `Stat failed`.
- Expected warnings: no thumbnailer service (`org.freedesktop.thumbnails.Thumbnailer1`), no XKB data files (built-in
  keymap). **Defect found:** the status bar prints `(%'lu bytes)` literally: libphoenix's printf does not implement
  the POSIX `'` (thousands grouping) flag → libphoenix follow-up.

## ★ Result — `m7h-xfce` (chain61, build 18, 2026-09-27 20:52–20:54): ✅ PASS — XFCE 4.20 on Wayland on Phoenix-RTOS

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-204830-m7h-xfce.log`; HDMI **`artifacts/hdmi/20260927-205346-m7h-xfce-tick.png`**: the **XFCE panel** across the top
(Applications menu, launchers, the tasklist button "File System - Thunar", the clock "Sun 27 Sep 18:53"),
**xfdesktop**'s wallpaper, and a **Thunar** window browsing `/` — all under labwc 0.20.2, GTK 3.24.52 on Wayland, the
D-Bus 1.16.2 session bus, the new GPU lane's display server.

- Bus: `xfconfd via=activation`, then for the whole hold `names=org.xfce.Panel,org.xfce.FileManager,org.xfce.Thunar,
  org.xfce.xfdesktop,org.xfce.Xfconf` (five XFCE services on the bus).
- Stop: `quit panel_rc=0`, xfdesktop ignored `--quit` and was terminated (`xfdesktop_rc=143`), `labwc exited rc=0
  after_term_s=1 socket=gone`; 0 exceptions / EL1 / `Stat failed`.
- Known cosmetic items: the clock shows UTC (no `TZ` staged); Thunar's `(%'lu bytes)` (libphoenix printf `'` flag:
  fixed in `a41d8d5`, build 19; Thunar needs a relink to pick it up); xfdesktop warns it has no system bus
  (only a session bus exists); `xkbcommon` include-path errors (built-in keymap used).

**Decides:** the owner's Wayland-desktop goal is reached in its first form: a recognisable, maintained desktop
(XFCE 4.20 on labwc, the XFCE project's own Wayland setup) on HDMI. Next: xfdesktop quit, TZ, relink after build 19,
the bench-only rows (menu clicks, window moves, typing), xfsettingsd.

## Result — `m7a2-labwc` (chain61, build 18, 2026-09-27 21:00): ✅ pixman PASS; ↩ GLES2 **renderer** up but **never on screen** (corrected 23:59)

↩ **Correction (after m7i, 2026-09-27 23:59):** this arm was graded on log lines only. The same log has four
`KMS import FAIL client=1 ns=kmsbuf id=3..6 rc=-22 why=foreign_kmsbuf (not supported)`: labwc's GLES2 swapchain
buffers were refused by rpi4-kms, so nothing it composited reached the plane. m7i's gles2 arm shows the same four
lines and the HDMI stays on the text console. What stands: GLES2 on V3D 4.2 initialises inside labwc.

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-205659-m7a2-labwc.log`. With `WLR_LIBINPUT_NO_DEVICES=1` both noinput arms run the full hold: weston-simple-shm served, `labwc exited
rc=0 after_term_s=1 socket=gone`, 0 exceptions / EL1. The gles2 arm: `[render/gles2/renderer.c:538] Creating GLES2
renderer`, `Using OpenGL ES 3.1 Mesa 26.2.0`, `GL vendor: Broadcom`, `GL renderer: V3D 4.2.14.0`: labwc composites on
the V3D through the new lane (GBM/EGL → rpi4-v3d-async). The desktop cycles (m7b2/m7c/m7f/m7h) used pixman; a GLES2
XFCE run is a one-word change (`WLR_RENDERER=gles2`).

## Stage 5: the XFCE demo session (`/bin/xfce-session`, polish after m7h) — built, staged, `m7i-xfce-demo` pre-registered

Five things m7h left for a public demo, and one command for a person at the bench. Nothing already staged was
changed: every file of the demo is under a path of its own (checked absent, then installed and `sha256sum -c`).

### 1. `xfdesktop --quit` did nothing — a GLib fd-passing assumption, root cause in the kernel

m7h: `quit … xfdesktop_rc=143`, and `xfdesktop-quit.log` said **`The connection is closed`**, followed by
GLib-GIO-CRITICALs about `AddMatch()` on a closed connection. The chain:

1. **Phoenix kernel:** `usocket_getsockname()` / `usocket_getpeername()` (`posix/usocket.c`) return 0 **without
   filling in the address**. dbus-daemon's `_dbus_socket_can_pass_unix_fd()` asks `getsockname()` for the family,
   sees `sa_family == 0`, not `AF_UNIX`, and so never answers `NEGOTIATE_UNIX_FD` with `AGREE_UNIX_FD`: **no bus
   connection on Phoenix has fd passing** (the kernel does implement `SCM_RIGHTS`: `posix/fdpass.c`).
2. **GLib 2.88:** `g_application_impl_command_line()` (the remote instance of any `G_APPLICATION_HANDLES_COMMAND_LINE`
   program forwarding its argv to the primary) **always** attaches fd 0 (stdin). GDBus refuses to write a message
   with fds to a peer without the capability ("Tried sending a file descriptor but remote peer does not support
   this capability"), treats it as a write error and closes the connection; the remote prints `The connection is
   closed`, and GDBus's exit-on-close raises SIGTERM in it (rc 143). The primary never hears the command.
3. So the same failure hits **every second instance of Thunar** (Super+E, the panel's launcher, the menu while
   Thunar runs: `thunar-application.c` sets `G_APPLICATION_HANDLES_COMMAND_LINE`) and `thunar --quit`. The panel's
   `--quit` worked because xfce4-panel uses its own D-Bus method (no fds).

**Fix (GLib, `gtk3-wayland/patches/glib/0003-gapplication-send-stdin-only-over-a-connection-that-.patch`):**
attach stdin only when `g_dbus_connection_get_capabilities()` has `G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING`; the
primary already handles a `CommandLine` call without fds (its stdin stream is then NULL). Upstreamable as is (a
TCP bus has the same problem). **Host test** `xfce-wayland/hosttest/gapp-cmdline.sh` (+ `gapp_cmdline_test.c`, a
primary + a remote `--quit`) on the host's dbus-daemon: **negative control** — the host's stock GLib 2.88 over a
TCP bus (no fd passing, as Phoenix) fails exactly as the Pi did (`The connection is closed`, remote killed by its
own SIGTERM, the primary never gets the quit); the patched GLib over TCP: remote rc 0, `primary got remote quit=1
stdin=none`, primary exits; the patched GLib over a UNIX bus: `stdin=passed` (fd passing still used where it
exists). **ALL PASS.** The kernel fix (fill `sun_family` + the bound path in `usocket_getsockname`/`getpeername`)
is a core change for a later build: then fd passing is negotiated and this patch is simply not exercised.

### 2. The clock showed UTC — GLib parses TZ itself, libphoenix does not

libphoenix has **no TZ support**: `tzset()` is a stub (`time/time.c`, `/* TODO - env parsing */`, always UTC) and
`localtime_r()` ignores `timezone` (the libtime host harness runs everything under `TZ=UTC`). But the panel's clock
and Thunar's dates use GLib's `GDateTime`/`GTimeZone`, and `g_time_zone_new_identifier()` parses a **POSIX TZ string
itself** (`rules_from_identifier()`, tried before any zoneinfo file), so no tzdata is needed. Host check with GLib
2.88 (`TZ='CET-1CEST,M3.5.0,M10.5.0/3'`, no `TZDIR`): the m7h screenshot's instant (UTC 18:53) → `20:53 CEST
(+0200)`; January → `CET (+0100)`; the switch on the last Sunday of March/October at 02:00/03:00 local both right.
`/bin/xfce-session` exports that string (the lab is Europe/Warsaw; `TZ` knob). **Still UTC:** anything that asks
libc — `date`, bash's prompt `\t`, foot/mc times. Follow-up for libphoenix: parse POSIX TZ in `tzset()` and apply
it in `localtime_r()`/`mktime()` (with libtime host-harness cases).

### 3. Relink after build 19 (libphoenix `a41d8d5`: the printf `'` flag)

Thunar formats the byte count with `g_strdup_printf("%'" G_GUINT64_FORMAT, …)` (`thunar-file.c:2326`); GLib is
built with `USE_SYSTEM_PRINTF`, i.e. libphoenix's `vasprintf`. All XFCE programs were rebuilt against build 19's
sysroot `libphoenix.a` (gate: build 19 `Exported SHA256`, no `rebuild-rpi4b-fast.sh` running, the archive newer
than the fix). Only the C locale exists, whose `thousands_sep` is empty, so the status bar should now read
`… files: 15.4 GiB (<digits> bytes)` — the plain number, as glibc prints in the C locale (m7i row 8). **labwc-2, foot-2 and fuzzel-2
are not relinked:** no `%'` conversion in their sources.

### 4. `Failed to get system bus` (xfdesktop) — silenced

It is `set_accountsservice_user_bg()` mirroring the backdrop into AccountsService for a greeter; when
AccountsService is merely absent xfdesktop already logs at debug level. Patch **xfdesktop 0002 `desktop: a missing
system bus is not a warning`** logs the missing system bus the same way (no system bus is planned on Phoenix).

### 5. The one-command session

`/bin/xfce-session` (`pi/xfce-session`, bash, `#!/bin/bash` — libphoenix `execve()` runs shebang scripts):

- starts whichever of the three new-lane servers is missing (startx-drm's checks: `/dev/v3d-async`, `/dev/kms`,
  `shmsrv -s`), in order: `/bin/rpi4-v3d-async-low -r 1 -m serial -i`, `/bin/rpi4-kms-g7 -G -p 96 -C`,
  `/bin/shmsrv` (`V3DA_CMD`/`KMS_CMD`/`SHMSRV_CMD`, `NO_SERVERS=1`);
- runs `/bin/xfce-desktop-2.sh xfce input` (= `pi/xfce-desktop.sh` of this commit) with `LABWC=/bin/labwc-2`, the
  XFCE programs from **`/usr/lib/xfce-demo/bin/`** (their real names: `thunar`, `xfce4-panel`, `xfdesktop`,
  `xfce4-settings-manager`, `xfce4-appearance-settings`, `xfce4-appfinder`; first on `PATH`, so the stock
  `.desktop` files' bare `Exec=` find them), labwc's **`/etc/xdg/labwc-xfce-demo`**, `XDG_CONFIG_DIRS=/etc/xdg/
  xfce-demo:/etc/xdg`, `XDG_DATA_DIRS=/usr/share/xfce-demo:/usr/share`, settings under `/tmp/xfce-demo-home`
  (apart from m7f/m7h's `/tmp/xfce-home`), `TZ` as above;
- **`HOLD=0` (default): runs until Log Out**, then stops in order and returns to psh (the servers stay up).
  Knobs: `HOLD` (N = log out by itself after N s through the same path), **`RENDERER` `pixman` (default) |
  `gles2`**, `TZ`, `VERBOSE` (labwc: 0 default, 1 `-V`), `THUNAR_START`, `THUNAR_SECOND`.

**Log Out:** without a session manager on the bus the panel's actions plugin offers Log Out only if `loginctl` is
on `PATH`, and runs `loginctl terminate-session ''`. `/usr/lib/xfce-demo/bin/loginctl` (`pi/xfce-demo-loginctl`)
is a stand-in that only does that: it creates `$XFCE_LOGOUT_FLAG` (`/tmp/xdg/xfce-logout`), which the session
script polls (every 5 s); with no session script it sends SIGTERM to `$LABWC_PID` (= `labwc --exit`). The script's
stop sequence: `thunar --quit`, `xfce4-panel --quit`, `xfdesktop --quit` (now all over the bus), SIGTERM to labwc,
the bus. If labwc went first (its root menu's Exit), the `--quit`s are skipped (`XFCE quit skipped`). Remote
calls are bounded (20–30 s, `rc=124` if killed): a remote instance waits for its reply with no timeout.

**`xfce-desktop.sh` changes** (the m7f/m7h copy at `/bin/xfce-desktop.sh` is untouched): knobs `XFCE_BIN`,
`PANEL`, `XFDESKTOP` (the `--quit`s no longer hard-code `/bin/`), `XFCE_DATA_DIRS`, `XFCE_HOME`, `THUNAR_START`,
`THUNAR_SECOND`, `LOGOUT_CMD`; the hold loop ends on HOLD, logout request or labwc's exit (`XFCE session end
reason=hold|logout|labwc-exited`); `XFCE env PATH=… TZ=…` line; Thunar stopped by `--quit` (it saved nothing on
SIGTERM). Host checks: `hosttest/run.sh` ALL PASS unchanged; a dry run with stand-ins for labwc/Thunar/panel
(HOLD=10 + the loginctl stand-in + `THUNAR_SECOND`: `session end reason=logout`, `thunar exited rc=0 quit_rc=0`,
`quit panel_rc=0 xfdesktop_rc=0`, `labwc exited rc=0`, `dbus exited rc=0`; HOLD=0 with labwc killed:
`reason=labwc-exited`, `quit skipped`).

**Keys** (`conf/labwc-xfce-demo/rc.xml`, labwc's `<default />` kept): **Super** tapped alone (`onRelease`) →
xfce4-appfinder; **Super+Return** → foot-2; **Super+E** → Thunar; **Super+D** / **Alt+F2** → appfinder (full /
collapsed); **Super+Space** → fuzzel-2. Root menu (right click where xfdesktop does not take it): Run…, Terminal,
Files, Settings, Midnight Commander (foot-2), Reconfigure, **Log Out**, Exit.

**Panel** (`conf/xfce-demo/xfce4-panel.xml`, first in `XDG_CONFIG_DIRS`: xfconfd merges the system files in
reverse order, so it overrides m7h's property by property): applications menu | launchers **foot-2**, **Thunar**,
**Application Finder** | window buttons | spacer | clock (local time) | **Log Out** (actions plugin, only
`+logout`, with its confirmation dialog). **`.desktop` shadows** in `/usr/share/xfce-demo/applications/`:
`foot.desktop` (`Exec=/bin/foot-2`, icon `utilities-terminal`), `thunar.desktop` (the demo Thunar),
`mc.desktop`/`bash.desktop` (`/bin/foot-2 -e …`, `Terminal=false`: `Terminal=true` means `exo-open --launch
TerminalEmulator` in XFCE, which is not staged); `/etc/xdg/xfce-demo/fuzzel/fuzzel.ini`: `terminal=/bin/foot-2 -e`,
icons on. The m7c `/usr/share/applications/foot.desktop` and `/etc/xdg/fuzzel/fuzzel.ini` (still `/bin/foot`) are
unchanged; they are shadowed only inside the demo session.

**Wallpaper:** xfdesktop's compiled-in default backdrop is now the **dithered**
`backgrounds/phoenix/phoenix-gradient-dither-1920x1080.png` (`56beb330e638c0c9`, staged by the colour study,
[hdmi-colour.md](hdmi-colour.md): runs of 1.4 px instead of 11). The PNG and its generator,
`labwc-drm/conf/backgrounds/make-wallpaper-dither.py` (stdlib only: make-wallpaper.py's 8-bit gradient,
49-px box filter per row in float, ±0.25 noise + half-error feedback, `random.seed(1)`; a few seconds), are
committed next to `make-wallpaper.py`; the script's output is **pixel-identical** to the staged file.

### Build and artifacts

```
tools/gpu-lane/gtk3-wayland/build.sh --usr --out tools/gpu-lane/gtk3-wayland/build-out-usr-2     # GLib 0003
tools/gpu-lane/xfce-wayland/build.sh --gtk-out tools/gpu-lane/gtk3-wayland/build-out-usr-2 \
    --out tools/gpu-lane/xfce-wayland/build-out-m7i                                              # + xfdesktop 0002
tools/gpu-lane/xfce-wayland/hosttest/gapp-cmdline.sh tools/gpu-lane/gtk3-wayland/build-out-usr-2/src/glib
```

Build 19's sysroot `libphoenix.a` (`94a3e1e68567120e`, 21:27, after `a41d8d5`); the GTK stack of
`build-out-usr-2` (glib 0003 applied). Checks on the unstripped binaries: `nm -u` 0, no `PT_INTERP`, 0 X11 symbols
(build.sh); **GLib 0003 linked**: `g_application_impl_command_line` calls `g_dbus_connection_get_capabilities` in
thunar, xfdesktop, xfce4-panel, xfce4-appfinder (1 call each; m7h's thunar: 0); **printf `'` fix linked**:
libphoenix's `format_parse` in the new thunar compares with `0x27` (m7h's: no). (The XFCE tree was built on /tmp
because / was full; `build-out-m7i/` holds `bin/`, `stage-demo/`, both MANIFESTs and `build.log`.)

| program (`build-out-m7i/bin/`) | staged as | stripped | sha256 stripped (first 16) | unstripped |
|---|---|---|---|---|
| thunar | `/usr/lib/xfce-demo/bin/thunar` | 17 711 232 | **`113396c510607a95`** | `a5ba1c56a28750d8` |
| xfce4-panel | `/usr/lib/xfce-demo/bin/xfce4-panel` | 17 967 976 | **`b5d72aef28fe7f9a`** | `92c639ef4cda3049` |
| xfdesktop (+ 0002, dithered default backdrop) | `/usr/lib/xfce-demo/bin/xfdesktop` | 17 299 776 | **`2d68b9e10aec3410`** | `bbe4e34776c940d6` |
| xfce4-settings-manager | `/usr/lib/xfce-demo/bin/xfce4-settings-manager` | 17 070 392 | **`69ff9d3c0bfeb990`** | `203d31266a25a087` |
| xfce4-appearance-settings | `/usr/lib/xfce-demo/bin/xfce4-appearance-settings` | 17 149 680 | **`6f5c3b139a86ce51`** | `6029fbf572bee06f` |
| xfce4-appfinder | `/usr/lib/xfce-demo/bin/xfce4-appfinder` | 17 089 672 | **`afeb2edddd05b97a`** | `d7550cf5caad8cfa` |

**Staged 2026-09-27 21:55** (`stage-demo.MANIFEST`: 19 files, every path checked absent first, then `sudo -n install`
+ `sha256sum -c`: all verified). Nothing existing was touched: `/bin/thunar-wl`, `/bin/xfce4-panel`, `/bin/xfdesktop`,
`/bin/labwc-2`, `/bin/xfce-desktop.sh` and every m7c/m7h config are as before.

| staged file | sha256 (first 16) |
|---|---|
| `/bin/xfce-session` (`pi/xfce-session`) | `ca4a8ac1444ab1f0` |
| `/bin/xfce-desktop-2.sh` (`pi/xfce-desktop.sh`; re-staged 22:05: the second Thunar waits for the first one's bus name) | `8421b2c05ecf66c3` |
| `/usr/lib/xfce-demo/bin/loginctl` (`pi/xfce-demo-loginctl`) | `7256bdfacad1f3d8` |
| `/usr/lib/xfce-demo/bin/{thunar,xfce4-panel,xfdesktop,xfce4-settings-manager,xfce4-appearance-settings,xfce4-appfinder}` | the table above |
| `/etc/xdg/labwc-xfce-demo/{rc.xml,menu.xml,autostart,environment}` | `6c8021559a78bfc5`, `e51d55eaaa344c3c`, `dcf3bb44f35c3289`, `0cf86d7cb34e9923` |
| `/etc/xdg/xfce-demo/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml` | `3c71a4ae866a9ae0` |
| `/etc/xdg/xfce-demo/fuzzel/fuzzel.ini` | `e0b65762ba4efebd` |
| `/usr/share/xfce-demo/applications/{foot,thunar,mc,bash}.desktop` | `fd92cb94a7d3910e`, `bf6a28879e922022`, `94093c79f396eefe`, `414f2ce1a93542fb` |
| used, staged earlier: `/bin/labwc-2`, `/bin/foot-2`, `/bin/fuzzel-2`, the servers, the m7h data (icons, MIME, menus, xfconfd + its `.service`, schemas), `/usr/share/backgrounds/phoenix/phoenix-gradient-dither-1920x1080.png` `56beb330e638c0c9` | — |

Patches: gtk3-wayland `glib/0003-gapplication-send-stdin-only-over-a-connection-that-.patch` (`3d7c21d80134a3fe`),
xfce-wayland `xfdesktop/0002-desktop-a-missing-system-bus-is-not-a-warning.patch` (`20407717c2503e25`). The
stock-layout `stage/` of the same build is **not** staged.

### Cycle `m7i-xfce-demo` (after build 19; from a chain script: ≈ 11 min, longer than one Bash call)

**Question:** does the one-command demo session come up and go down on its own — servers started by the launcher,
the XFCE desktop with the demo panel, a second Thunar instance forwarded over the bus, the clock in local time, Log
Out through the panel's path, every component quitting cleanly — with labwc's pixman renderer, and then with its
GLES2 renderer on V3D (proven for labwc-2 by m7a2-labwc)?

```
./scripts/test-cycle-psh-interact.sh --label m7i-xfce-demo --idle-secs 60 --max-cmd-secs 420 \
    --hdmi-dense-on 'XFCE labwc socket=up' -- \
    "export HOLD=60" \
    "export VERBOSE=1" \
    "export THUNAR_SECOND=/usr" \
    "/bin/bash /bin/xfce-session" \
    "export RENDERER=gles2" \
    "/bin/bash /bin/xfce-session" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

Arm A = the first `xfce-session` (pixman), arm B = the second (gles2). The automated arms run it through
`/bin/bash` as every earlier cycle did; the bare `/bin/xfce-session` (libphoenix `execve()` follows `#!`, not yet
seen from psh on the UART) is bench item 1. Grade:
`grep -a -E '^XFCE|^XFCE-SESSION|Thunar|thunar|xfdesktop|xfce4-panel|GLES2|OpenGL|renderer|Gtk-|GLib-|^SHMSRV |^KMSTEST ' …m7i-xfce-demo.log`,
`./scripts/uart-summary.sh m7i-xfce-demo`. Allow ~1.3 % UART line corruption; EL0 dumps print twice.

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | A: `XFCE-SESSION start hold=60 renderer=pixman tz=CET-1CEST,M3.5.0,M10.5.0/3 labwc=/bin/labwc-2 conf=/etc/xdg/labwc-xfce-demo`, three `XFCE-SESSION server start:` lines, `XFCE-SESSION servers v3d-async=up kms=up shm=up`. B: no `server start` (all up) | the launcher brings the servers up | `servers … missing` + `done rc=1`: that server's own lines |
| 2 | `XFCE start session=xfce … labwc=/bin/labwc-2 thunar=/usr/lib/xfce-demo/bin/thunar missing=none`; `XFCE env PATH=/usr/lib/xfce-demo/bin:/bin:/usr/bin XDG_CONFIG_DIRS=/etc/xdg/xfce-demo:/etc/xdg XDG_DATA_DIRS=/usr/share/xfce-demo:/usr/share TZ=CET-1CEST,…` | staging complete | `missing=<paths>`: staging |
| 3 | bus/xfconfd/round trip as m7h rows 1 (`via=activation`), `labwc start conf=/etc/xdg/labwc-xfce-demo files=rc.xml,menu.xml,autostart,environment`, `socket=up`; labwc: `run session script /etc/xdg/labwc-xfce-demo/autostart`, no `exited with 127`, **B: `Creating GLES2 renderer`, `GL renderer: V3D 4.2…`** | as m7h; B as m7a2 | B falls back to pixman or labwc exits: m7a2's GLES2 rows; the rest of B still graded |
| 4 | `XFCE session up panel=registered`, `XFCE thunar start: /usr/lib/xfce-demo/bin/thunar /` | the panel within 60 s | `panel=missing`: `XFCE log xfce4-panel:` |
| 5 | **`XFCE thunar second instance dir=/usr rc=0 took_s=<1–15> waited_for_name_s=<0–30>`** (it starts only once the first Thunar owns `org.xfce.Thunar`) and `names=` still with one `org.xfce.Thunar` | **the GLib fix: the remote command line reaches the running Thunar** (a second window, `/usr`, appears on HDMI) | `rc=143` + `The connection is closed` in `XFCE log thunar-second:`: the binary lacks GLib 0003 (`strings -a /usr/lib/xfce-demo/bin/thunar` cannot show it: check the staged sha); `rc=124`: the primary did not answer |
| 6 | `XFCE hold … names=org.xfce.Panel,org.xfce.FileManager,org.xfce.Thunar,org.xfce.xfdesktop,org.xfce.Xfconf` ×6 | as m7h | — |
| 7 | **no** `xfdesktop-WARNING … Failed to get system bus` in `XFCE log xfdesktop:` | patch 0002 | the warning: the old xfdesktop ran (`XFDESKTOP`) |
| 8 | HDMI (dense from `labwc socket=up`, arm A and B): the **dithered** wallpaper (smooth, no 11-px bands); the panel: menu button, **three launcher icons** (terminal, Thunar, app finder), window buttons (two Thunar windows: `/` and `/usr`), **the clock in CEST = UART time + 2 h** (e.g. log 19:05 UTC → `Sun 27 Sep  21:05`), a **Log Out** icon at the right end (sensitive = `loginctl` found on PATH); Thunar's status bar **`… (NNNN bytes)`** with digits, no `%'lu` | the demo desktop | clock = UTC: `TZ` not in the panel's environment (row 2); Log Out greyed: `loginctl` not found (PATH); `%'lu`: an old Thunar |
| 9 | `XFCE hold over: /usr/lib/xfce-demo/bin/loginctl terminate-session rc=0`, **`XFCE session end reason=logout held=60s`** | HOLD ends through the Log Out button's own command | `reason=hold`: the flag was not created (`XFCE log logout-cmd:`) |
| 10 | stop: **`XFCE thunar exited rc=0 quit_rc=0`**, **`XFCE quit panel_rc=0 xfdesktop_rc=0 wait_s=<0–5> names=org.xfce.Xfconf`**, `XFCE labwc exited rc=0 … socket=gone`, `XFCE dbus exited rc=0 … socket=gone`, `XFCE saved channels=` incl. `xfce4-panel.xml,thunar.xml`, `XFCE done`, **`XFCE-SESSION done rc=0`** | **every program quits cleanly over the bus** (m7h: xfdesktop rc=143, Thunar TERM) | `xfdesktop_rc=143` + `The connection is closed`: GLib 0003 missing from that binary; `rc=124`: the primary did not act on it |
| 11 | arm B = rows 2–10 again with `renderer=gles2` | GPU composition works for the XFCE desktop | B only fails: note; the demo stays on pixman |
| 12 | `SHMSRV stats rc=0 live=0 bytes=0`, `KMSTEST stats … bos=0` | all released after two sessions | `live>0`: a client of one of the sessions survived |
| 13 | fault dumps | 0 kernel, 0 EL0 | addr2line on `tools/gpu-lane/xfce-wayland/build-out-m7i/bin/<prog>` |

**Bench-only checklist** (a person at the Pi with the USB keyboard + mouse; type **`/bin/xfce-session`** at psh,
default `HOLD=0`; otherwise **n/a**, not FAIL):

1. Type **`/bin/xfce-session`** (bare: the shebang path; if psh reports an exec error, `/bin/bash /bin/xfce-session`
   and note it). The desktop appears (wallpaper, panel, a Thunar window) within ~2 min; the clock shows local time.
2. **Applications menu** (panel, left) → System → **Foot**: a terminal opens; **type** `ls /` + Enter — output appears.
3. Tap **Super**: the application finder opens; type `thu`, Enter → Thunar (a new window of the running Thunar).
4. **Super+E** → another Thunar window; double click `usr`; **drag** a Thunar window by its title bar; resize it
   by an edge; click its button in the panel to minimise and again to raise.
5. **Super+Return** → foot; **Alt+F4** closes it. Right click on the wallpaper → xfdesktop's menu.
6. **Log Out** (panel, right end) → the confirmation dialog → *Log Out*: the desktop closes within ~15 s, the psh
   prompt returns (`XFCE-SESSION done rc=0` on the UART).
7. `/bin/xfce-session` again: the desktop comes back (the servers are still up); log out once more.

**Decides:** rows 1–10 of arm A = the demo is one command and exits cleanly; row 11 = whether the demo defaults to
`RENDERER=gles2`.

### Follow-ups (not in this step)

1. **Kernel:** `usocket_getsockname()` / `usocket_getpeername()` must fill in `sun_family` (+ the bound / peer
   path). Then dbus-daemon negotiates fd passing and GLib 0003 is no longer exercised on Phoenix (keep it: it is
   right for any bus without fds). A core change: build 20 at the earliest.
2. **libphoenix:** POSIX `TZ` parsing in `tzset()`, used by `localtime_r()`/`mktime()` (+ libtime host-harness
   cases), so libc programs agree with GLib's clock.
3. **Ports copies:** `xfce-wayland/build.sh` now names the framework port `xfce_wayland` (branch
   `feat/new-lane-wayland-ports`) whose patch files must stay identical: the two new patches (gtk3-wayland glib
   0003, xfdesktop 0002) and the dithered `-Ddefault-backdrop-filename` need copying there.
4. If `m7i` arm B passes: default `RENDERER=gles2` in `/bin/xfce-session`.

## Result — `m7i-xfce-demo` (chain65, build 20b, 2026-09-27 23:31): ✅ arm A (pixman) PASS — ✗ arm B (gles2) not on screen

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-233145-m7i-xfce-demo.log`; kernel `f20e96a0` (P11 fix), libphoenix `a41d8d5`.

**Arm A (pixman), every prediction row met:**
- `/bin/xfce-session` brought up all three servers (`XFCE-SESSION servers v3d-async=up kms=up shm=up`), XFCE came up.
- The second Thunar went over the bus: `second instance dir=/usr rc=0`. The `usr - Thunar` window is on HDMI
  (`artifacts/hdmi/20260927-233712-m7i-xfce-demo-tick.png`).
- Log Out through the `loginctl` stand-in stopped everything with rc=0: thunar, panel, xfdesktop, labwc, dbus;
  `XFCE-SESSION done rc=0`.
- 0 `Failed to get system bus`, 0 exceptions.
- **Local time on the panel clock**: `Sun 27 Sep 23:37`, matching the host.
- Thunar's status bar: **`2 files: 431.3 KiB (441685 bytes)`**. The printf `'` fix is visible where it was found.
- The root folder is clean after the export cleanup.

**Arm B (gles2):**
- The renderer came up: `Creating GLES2 renderer`, `GL renderer: V3D 4.2.14.0`, and the session ran and quit with rc=0.
- **But HDMI shows the text console for the whole arm**: the four `KMS import FAIL … why=foreign_kmsbuf (not supported)`
  lines, then labwc `view has no output, not centering`.
- There is no `KMS console handover disable` in arm B. `libseat` also logs `Failed to open device '/dev/kbd0': Device
  or resource busy` about once a second.
- Next step: rpi4-kms must accept a `/kmsbuf` buffer that another client allocated (labwc's GBM swapchain), or
  wlroots must allocate its scanout through the dumb-buffer path it already uses with pixman. **The demo default
  stays pixman.**

## Stage 6: Atril, the PDF viewer (`tools/gpu-lane/atril-wayland/`) — built, host-tested, staged, `m7j-atril` pre-registered

Owner request: an XFCE-compatible PDF viewer, windowed and full screen, on the Wayland desktop. **Atril** (MATE's
GTK 3 document viewer, Xubuntu's default) with the **Poppler** backend, static, on the gtk3-wayland `--usr` stack
(snapshotted into `build-out/gtk/`, as xfce-wayland does). Atril was practical: its only MATE library use is one
widget, and its X11 use is session management and two user-time calls. No fallback to Zathura was needed.
Poppler and Atril are GPL: the sources live only in `build-out/src/` (sha256-pinned tarballs); committed are
build.sh, the patches, `poppler-options.sh`, the sample generator, the session script, the `.desktop` entry, the host
test.

### Versions, configuration

| package | version | licence | build | on / off |
|---|---|---|---|---|
| Atril | **1.28.7** (latest 1.28.x, 2026-08-27; meson) | GPL-2.0+ | meson | **PDF backend only, built in** (patch 0004); off: ps/dvi/djvu/tiff/xps/comics/epub/pixbuf, caja extension, keyring (libsecret), D-Bus (`atrild` + `org.mate.atril.Daemon` activation), thumbnailer, previewer, introspection, help; `gtk_unix_print` found (GTK has it; no print backends) |
| Poppler | **26.09.0** (2026-09-03) | GPL-2/3 | cmake, C++23 | core + **poppler-glib** (cairo output); libjpeg (DCT), **openjpeg** (JPX), **lcms2** (ICC), libpng, fontconfig; off: Qt5/6, cpp wrapper, utils, NSS/GPGME, curl, tiff, boost, harfbuzz (subsetting needs harfbuzz-subset), introspection, tests — one list, `poppler-options.sh`, shared with the host test |
| openjpeg | 2.5.4 | BSD-2 | cmake | libopenjp2 only |
| lcms2 | 2.19.1 | MIT | meson | no utils, no GPL plugins |
| libxml2 | 2.15.4 (= labwc-drm's) | MIT | meson | Atril: XMP metadata, toolbar editor |

CMake cross build: `phoenix-aarch64.cmake` (Generic, phx-gcc/g++) with `CMAKE_FIND_ROOT_PATH` = one symlinked `/usr`
view of this DESTDIR + the GTK snapshot + the ports views, mode ONLY (CMake's own FindFreetype/Fontconfig/JPEG/PNG/ZLIB
see only target files), programs from the host. First C++ program of this lane: **no `hypotf` collision** at the link
(meson links with g++; nothing extra needed).

### Patches (`patches/<pkg>/`, `git format-patch`, applied with `git am`)

| patch | sha (first 16) | why |
|---|---|---|
| atril 0001 `build: X11, ICE/SM and mate-desktop are optional (Wayland-only builds)` | `b1053bfa4868f741` | meson required x11+ice+sm (EggSMClient XSMP) and mate-desktop unconditionally. New features `x11`, `mate_desktop` (auto); without x11 EggSMClient/EggDesktopFile come from the bundled `cut-n-paste/smclient` **without a backend** (the base client: no session saved), so the mate-submodules git subproject is not needed |
| atril 0002 `Include gdkx.h and use the X11 GDK API only with the X11 backend` | `49ddc96980bf6c3c` | a Wayland-only GTK installs no `gdk/gdkx.h`; 5 includes + the X11 user-time / screen-number calls under `GDK_WINDOWING_X11` |
| atril 0003 `shell: GtkImageMenuItem when built without libmate-desktop` | `2d7526b421b7af90` | MateImageMenuItem is the only libmate-desktop API used (3 menus); `ev-image-menu-item.h` maps to GTK 3's GtkImageMenuItem (its origin) |
| atril 0004 `libdocument: optionally link the document backends into the programs` | `4619d48db20a4bb8` | backends are GModule plugins; a static Phoenix program cannot load one. `-Dbuiltin_backends=true`: each backend a static library with `register_atril_backend` renamed `ev_builtin_<Module>_register`; a table generated from the `.atril-backend` files (`backend/ev-builtin-backends.py`: Module, Resident, TypeDescription, MimeType) is linked into atril/previewer/thumbnailer; the backends manager adds it before scanning the (now optional) backends directory; `ev_module_new_builtin()` calls the register function instead of `g_module_open()`. libdocument refers to the table weakly. Only the pdf backend is converted (configure stops for others) |
| atril 0005 `Optionally keep Atril's GSettings schema in its own directory` | `4039c82b283f55ac` | `g_settings_new("org.mate.Atril")` aborts on a missing schema, and the staged `/usr/share/glib-2.0/schemas/gschemas.compiled` is shared (GTK's). `-Dschemas_dir=/usr/share/atril/schemas`: the schema is installed there and atril **appends that directory to `GSETTINGS_SCHEMA_DIR`** at the top of `main()` — so it also works when launched from the panel menu or fuzzel with the session's `GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas` |
| poppler 0001 `cmake: accept fontconfig 2.14` | `b06a1874d1fbd91f` | Poppler asks for ≥ 2.15 but uses only old API (FcFontSort, FcPatternGet*, FcLangSet*…); the ports fontconfig is 2.14.2 and a second fontconfig in one static program is not an option |

### Build, checks, artifacts

```
tools/gpu-lane/atril-wayland/build.sh            # ≈ 8 min cold (Poppler ≈ 4); needs gtk3-wayland/build-out-usr
tools/gpu-lane/atril-wayland/hosttest/run.sh     # native build ≈ 5 min once, then seconds
```

`== program` fails the build on any miss: **`nm -u` 0, no `PT_INTERP`, 0 X11/SM/mate-desktop symbols**
(`XOpenDisplay`, `XInternAtom`, `xcb_connect`, `gdk_x11_display_get_type`, `gdk_x11_window_set_user_time`,
`SmcOpenConnection`, `IceOpenConnection`, `mate_image_menu_item_new`); present: `ev_builtin_backends`,
`ev_builtin_pdfdocument_register`, `ev_module_new_builtin`, `poppler_document_new_from_file`, `poppler_page_render`,
`CairoOutputDev::startPage`, `opj_decode`, `cmsCreateTransform`, `xmlXPathNewContext`, `gdk_wayland_display_get_type`,
`gtk_image_menu_item_new_with_label`, `ev_view_presentation_new`, `egg_sm_client_get`, `ev_resource_data` (the UI
definitions and CSS are a GResource in the binary); strings `pdfdocument`, `application/pdf`, `PDF Documents`,
`/usr/share/atril/schemas`, `org.mate.Atril`. Build-host path strings: 27 (libstdc++'s own `__FILE__`s from the
toolchain build, the 4 of the GTK stack, GTK's inkscape comments). Rebuilt on build 20b's sysroot (00:00): the
binary is **byte-identical** (reproducible).

| artifact (`build-out/`) | size | sha256 (first 16) | staged as |
|---|---|---|---|
| `bin/atril-stripped` (text 21 334 686 / data 105 372 / bss 83 716; unstripped `bin/atril` `6f862497b5195ba6`) | 21 445 696 | **`aae0497d157cc744`** | `/bin/atril-wl` |
| `data/schemas/gschemas.compiled` (`org.mate.Atril` + `.Default`, `--strict`) | 1 582 | `fa39e50577e8391e` | `/usr/share/atril/schemas/gschemas.compiled` |
| `data/sample.pdf` (`tools/make-sample-pdf.py`, ours BSD-3; deterministic) | 178 085 | `c67ed461b430cc6d` | `/usr/share/doc/phoenix/sample.pdf` |
| `conf/atril.desktop` (`Exec=/bin/atril-wl %U`, `Categories=GTK;Office;Viewer;` → the panel menu's Office, fuzzel; icon by absolute path) | — | `5c3a7bb469b10ce8` | `/usr/share/applications/atril.desktop` |
| `pi/xfce-desktop-atril.sh` | — | `c92b788cde3aaf16` | `/bin/xfce-desktop-atril.sh` |
| `hand-open.png` + 40 icon PNGs (Atril's action icons, its private search path; the 16/22/24/48 app icon) | — | `stage.MANIFEST` | `/usr/share/atril/…` |

**The sample document** (3 A4 pages, DejaVu fonts embedded as subsets by cairo, so the Pi needs no font of its own):
p. 1 title "Atril on Phoenix-RTOS", a paragraph in Sans/Serif/Mono, a **red rectangle, a green circle, a blue
triangle** and an orange Bezier stroke, a key help line; p. 2 "An embedded image": a 256×256 RGB field (red grows
to the right, green downwards) and an 8×8 checkerboard; p. 3 "A table": a 10×10 multiplication table on a shaded
grid. Footer "Page N of 3 - Atril on Phoenix-RTOS sample document".

**Host test** (`hosttest/run.sh`, **ALL PASS**): openjpeg, lcms2, libxml2 and Poppler built natively from the same
tarballs and patches with the same `POPPLER_OPTS` (+ utils) — Poppler's summary identical to the Pi's (cairo, glib,
libjpeg, libpng, openjpeg2, lcms2 yes; boost/tiff/nss/gpg/curl no). (1) pdfinfo: 3 pages, the title; pdftocairo: 3
PNGs. (2) `render_test.c` through **poppler-glib as Atril's backend calls it**: open, 3 pages, title, page-1 text,
and the colour at the six points `make-sample-pdf.py --points` lists — all within ±24 (rectangle `d02020`, circle
`20a040`, triangle `2040c0`, image corners `090980`/`f9f980`, table cell `e8f0ff`). (3) **Atril itself** with the
Pi's patches and options on the host's GTK: its `atril-thumbnailer` renders `sample.pdf` through the **built-in
backend table** — no backends directory exists — to a 400×566 PNG whose red-rectangle pixel is `d02020`;
negative control: a text file is refused. (The host has no `libgailutil-3`; Atril's meson asks for `gail-3.0` but
uses none of it: an empty stand-in `.pc`.)

### Staging (done 2026-09-28 00:00; new names only — all 46 paths checked absent, then `sudo -n install -D` + `sha256sum -c`: all verified)

```
X=tools/gpu-lane/atril-wayland/build-out; EXPORT=/srv/phoenix-rpi4-nfs-gcc16
while read -r sum path; do [ -e "$EXPORT/$path" ] && echo "EXISTS $path"; done < $X/stage.MANIFEST   # none
while read -r sum path; do m=644; [ -x "$X/stage/$path" ] && m=755
  sudo -n install -D -m $m "$X/stage/$path" "$EXPORT/$path"; done < $X/stage.MANIFEST
(cd $EXPORT && sha256sum -c --quiet $OLDPWD/$X/stage.MANIFEST)
```

Nothing existing was touched (the shared schemas, the hicolor theme and its cache, `/bin/xfce-desktop.sh`,
`/bin/xfce-session`, every labwc/XFCE config). Reused: `/bin/labwc-2`, the m7h XFCE programs and data
(`/bin/xfce4-panel`, `/bin/xfdesktop`, `/etc/xdg/labwc-xfce/`, xfconfd + `.service`, icons, MIME), the servers.

### Session (`pi/xfce-desktop-atril.sh` = the m7h `xfce-desktop.sh` of `61e5423eb`/`62c15b58d` with Atril as the client)

`/bin/bash /bin/xfce-desktop-atril.sh xfce input`: bus, xfconfd by activation, labwc **`/bin/labwc-2`** with
`/etc/xdg/labwc-xfce` (the XFCE autostart: xfdesktop + xfce4-panel), then — once the panel is on the bus — Atril
once per mode of `ATRIL_MODES` (default `window fullscreen presentation`), each `HOLD` s (default 40) with a
heartbeat every 10 s, then SIGTERM: `atril-wl DOC`, `atril-wl --fullscreen DOC`, `atril-wl --presentation DOC`.
Knobs `ATRIL`, `ATRIL_DOC` (default `/usr/share/doc/phoenix/sample.pdf`), `ATRIL_MODES`, `ATRIL_ARGS`, `HOLD`,
`RENDERER` (pixman: GLES2 labwc does not reach HDMI yet, m7i arm B), `LABWC`. Session `atril` = labwc without the
XFCE autostart. Stop as m7h (`xfce4-panel --quit`, `xfdesktop --quit`, labwc, bus). Atril's stderr reaches the UART
(started by the script, not by labwc).

Runtime design notes: `GSETTINGS_BACKEND=memory` (the session's) + the schema appended by patch 0005; GIO local
files only (no gvfs: Atril's per-document metadata is off without it — no error); MIME by
`/usr/share/mime/mime.cache` (content type `application/pdf` → the built-in backend); rendering on Atril's job
threads (Poppler + cairo image surfaces), the page shown through GTK's wl_shm buffers (shmsrv).

### Cycle `m7j-atril` (after build 20b; ≈ 8–9 min — from a chain script if it would exceed one 10-min Bash call)

**Question:** does a static C++ document viewer run on Phoenix — Poppler rendering a PDF (embedded fonts, vector
shapes, a raster image) in Atril's GTK 3 window under labwc in the XFCE session, then **full screen** and in
**presentation mode** — and stop cleanly?

```
./scripts/test-cycle-psh-interact.sh --label m7j-atril --idle-secs 60 --max-cmd-secs 480 \
    --hdmi-dense-on 'XFCE atril start' -- \
    "/bin/rpi4-v3d-async-low -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96 -C" \
    "/bin/shmsrv -v" \
    "/bin/bash /bin/xfce-desktop-atril.sh xfce input" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats"
```

Wall clock ≈ boot 60–150 s + ~10 s bus + ≤ 30 s labwc + ≤ 60 s panel + 3 × (≤ 15 s start + 40 s + TERM) + ≤ 30 s
stop. Grade:
`grep -a -E '^XFCE |atril|Atril|[Pp]oppler|Gtk-|Gdk-|GLib-|GLib-GIO-|GLib-GObject-|Fontconfig|^SHMSRV |^KMSTEST ' …m7j-atril.log`,
`./scripts/uart-summary.sh m7j-atril`. Allow ~1.3 % UART line corruption; EL0 dumps print twice. **bench** rows need
a person with the USB keyboard/mouse (otherwise **n/a**, not FAIL).

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `XFCE start session=xfce renderer=pixman … labwc=/bin/labwc-2 atril=/bin/atril-wl doc=/usr/share/doc/phoenix/sample.pdf modes=window,fullscreen,presentation missing=none` | once | `missing=<paths>`: staging |
| 2 | bus, `xfconfd via=activation`, `xfconf set_rc=0 get_rc=0`, `labwc … socket=up`, the panel on the bus (`XFCE waiting for the panel … org.xfce.Panel`) | as m7h rows 1–4 | m7h's rows decide |
| 3 | `XFCE atril start mode=window: /bin/atril-wl /usr/share/doc/phoenix/sample.pdf`, `XFCE atril pid=…`; allowed: GTK's m7e rows 4–5 lines (locale fallback, xkbcommon include path, `Using the built-in XKB keymap`), `Gtk-WARNING … Could not find signal handler` (none expected: Atril uses no GtkBuilder), icon lookups (`Could not load a pixbuf` / missing `view-page-*`, `zoom-fit-*`, `object-rotate-*`: Atril's own action icons are in `/usr/share/atril/icons/hicolor/*/actions`, which the staged hicolor `index.theme` does not list — cosmetic) | the 21.4 MB exec starts within ~5 s | nothing and `atril=exited` at the first heartbeat: exec/crash — EL0 dump: `aarch64-phoenix-addr2line -f -C -e tools/gpu-lane/atril-wayland/build-out/bin/atril <pc>`; **`Settings schema 'org.mate.Atril' is not installed`** + abort: patch 0005 / `/usr/share/atril/schemas/gschemas.compiled` |
| 4 | **no** `Unable to open document` / `File type … is not supported` / `Error opening backend` / `Cannot load backend` | the built-in pdf backend (patch 0004) found by content type `application/pdf` | "not supported": the MIME cache (`/usr/share/mime/mime.cache`) or the table — record the exact text |
| 5 | `XFCE hold mode=window … labwc=running atril=running names=…Panel…` ×4 | runs the whole hold | `atril=exited` early: its last stderr lines |
| 6 | `SHMSRV create` + `truncate …` for the window's buffers (≈ 1–2 × window size, e.g. ≤ 8 MiB caps) | GTK's wl_shm pools | `SHMSRV FAIL alloc`: contiguous memory (E1) |
| 7 | **HDMI, window** (dense from `XFCE atril start`): the XFCE panel and wallpaper; an **Atril window** (labwc title bar "Atril on Phoenix-RTOS - sample document" or `sample.pdf`) with its menu bar (File Edit View Go Bookmarks Help), a toolbar (page number `1` / `of 3`, zoom), **page 1 rendered**: the bold title "Atril on Phoenix-RTOS", three lines of text, the red rectangle / green circle / blue triangle and the orange curve, crisp text; possibly the thumbnail sidebar | **Poppler renders on Phoenix** | grey page area with a spinner forever: the render job thread (note the last Poppler/GLib line); text missing but shapes present: font loading (embedded TrueType through FreeType — `Syntax Error`/`Couldn't find a font` lines); the window missing: rows 3/6 |
| 8 | `XFCE atril exited mode=window rc=143` (SIGTERM, no handler) | clean kill | `rc=134`/`139`: a crash at exit — EL0 dump |
| 9 | `XFCE atril start mode=fullscreen: /bin/atril-wl --fullscreen …`, heartbeats `atril=running` | — | as rows 3–5 |
| 10 | **HDMI, fullscreen:** **no labwc title bar, the panel covered**, page 1 filling the 1920×1080 output (Atril's fullscreen: the page centred, grey around, and Atril's small fullscreen toolbar at the top) | labwc honours `xdg_toplevel.set_fullscreen` for a GTK window | the window stays decorated/windowed: labwc ignored the request (record); the panel above the page: labwc's layer order for fullscreen views (note, not an Atril failure) |
| 11 | `XFCE atril start mode=presentation: /bin/atril-wl --presentation …` | — | as rows 3–5 |
| 12 | **HDMI, presentation:** a **black** full screen with **one page** centred and scaled to the height (page 1: the shapes large), no toolbar/menus | Atril's presentation mode (EvViewPresentation) | a white/grey window instead: presentation mode not entered (note) |
| 13 | stop: `XFCE atril exited mode=presentation rc=143`, `XFCE quit panel_rc=0 …` (xfdesktop may be 143 as in m7h), `XFCE labwc exited rc=0 … socket=gone`, `XFCE dbus exited rc=0 … socket=gone`, `XFCE done` | clean shutdown | m7h row 12's alternatives |
| 14 | `SHMSRV stats rc=0 live=0 bytes=0`, `KMSTEST stats … bos=0` | all released (three Atril processes came and went) | `live>0`: a pool outlived Atril |
| 15 | fault dumps | 0 kernel, 0 EL0 | EL0 in atril: addr2line (row 3). Atril renders on GLib job threads (`g_thread_new`, stack size 0 → libphoenix's aarch64 default **256 KiB**, `PTHREAD_STACK_DEFAULT`), and Poppler's parser/`Gfx` recurse: a data abort in a non-main thread with `far` a few KB below that thread's stack = **stack size, not a Poppler bug** |
| 16 | **bench** (window arm): **Page Down** / **Space** → page 2 (the colour field + checkerboard; the page box reads `2`), again → page 3 (the table); **Page Up** / **BackSpace** back; **Ctrl+Home** / **Ctrl+End** first/last page | Atril's navigation keys | nothing: keyboard focus (labwc-2 keyboard rows of m7b2) |
| 17 | **bench:** **Ctrl++ / Ctrl+−** zoom; **F11** toggles full screen (as row 10) and back; **F5** starts the presentation (as row 12), **Right/Left** or **Page Down/Up** move pages there, **Esc** leaves it | fullscreen/presentation from inside a running window | — |
| 18 | **bench:** the panel's Applications menu → **Office → Atril Document Viewer** starts `/bin/atril-wl` (an empty window; File → Open… shows GTK's file chooser, open `/usr/share/doc/phoenix/sample.pdf`); fuzzel (`Super+Space` in the demo session) lists "Atril Document Viewer" | the `.desktop` entry; patch 0005 without the script's environment | an abort on the schema: patch 0005 not in the binary (`strings -a /bin/atril-wl \| grep /usr/share/atril/schemas`) |

**Coverage before the cycle:** the host test ran libdocument, the built-in backend table and Poppler's rendering
(the thumbnailer path) — but **Atril's shell (`main()`, EggSMClient without a backend, the GtkImageMenuItem menus,
patch 0005's `GSETTINGS_SCHEMA_DIR` append, the stock icons, EvView/presentation) was compiled, never executed**,
and this is the lane's first C++ program with exceptions and `std::mutex` on the target. `m7j` is its first run.

**Decides:** rows 3–7 = a PDF viewer runs on Phoenix-RTOS (Poppler + GTK 3 on Wayland); rows 9–12 = full-screen
and presentation rendering; 13–15 = clean exit, no leaks.

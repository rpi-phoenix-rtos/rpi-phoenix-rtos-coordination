# Desktop applications as framework ports (2026-09-30)

The owner's rule is that every executable on the image comes from the framework build. Three
groups of demo programs still existed only as hand-built, hand-staged tools under
`tools/gpu-lane/`: the windowed games (M8), the video players (M10) and the Atril PDF viewer (M7
m7j, MIGRATION.md §7.0 row 19). This pass turns all three into phoenix-rtos-ports recipes that the
default image installs, and adds XFCE applications-menu entries for them.

Everything is on branches, pushed to `publish`, **not merged**. Nothing was built through the
framework and no Pi cycle ran: the checks were static (§5).

| repo | branch | head |
|---|---|---|
| phoenix-rtos-ports | `feat/desktop-apps-ports` (merges `feat/desktop-apps-atril` and `feat/desktop-apps-video`) | `7e1c4c5` |
| phoenix-rtos-project | `feat/desktop-apps-ports` | `eed5d7f` |
| coordination | `feat/desktop-apps-ports` | this document + the two sync-check scripts |

## 1. Design

**Existing ports are extended with USE flags and no recipe is duplicated.** The one exception is
a new port where no recipe existed before (`video_player`, `atril_wayland`).

| Port | Change | Tools source |
|---|---|---|
| `mesa_drm` | new USE **`waylandgl`**: a sixth meson build, `waylandgl/` (`-Dplatforms=wayland -Dopengl=true`), with `link-gl.txt` + `link-gles.txt` | `mesa-drm/build.sh --wayland --opengl` (`build-out-wayland-gl`) |
| `sdl2_kmsdrm` | new USE **`wayland`**: a third SDL build (patches 0001–0011 + `patches/wayland/0101–0103`) into `wayland/`, plus `wayland/link-inputs.txt` (the tools format) and `share/gamewl/`. New USE **`rootfs`** (requires `wayland`): the session helpers | `sdl2-wl/build.sh` steps 1–3; `sdl2-wl/pi`, `conf/labwc-xfce-m8` |
| `quakespasm_drm`, `yquake2_drm`, `quake3_drm`, `supertuxkart_drm` | new USE **`wayland`**: the `-wl` clone built next to the `-drm` one; with `rootfs`, an XFCE menu entry | `sdl2-wl/build.sh` step 4, `build-quake{2,3}-wl.sh`, `build-stk-wl.sh`, `gamewl/relink-sdl-gl-game-wl.sh` |
| `video_player` (new) | anchored on the ffmpeg 6.1 tarball (the `ffmpeg` port is left untouched); USE `rootfs wayland gtk demo` | `video-player/build-ffplay.sh` (both SDLs), `gtk-video/build.sh`, `gen-clips.sh`, `pi/video-play2` |
| `atril_wayland` (new) | the gtk3_wayland/xfce_wayland shape: libxml2, lcms2, openjpeg, Poppler and Atril, each guarded by a `.built` stamp, with a **`p_relink`** that drops the stamps | `atril-wayland/build.sh` |

Choices made:

* **`xfce_wayland` and `gtk3_wayland` are not edited.** `port_manager` hashes a port's whole
  directory (`_recipe_digest`) and cleans the port **and every transitive dependent** when the
  hash changes. Adding one `.desktop` file to `xfce_wayland` would therefore rebuild XFCE, and
  adding one to `gtk3_wayland` would rebuild GTK (~5 GB) and XFCE. Instead, each application's
  port installs its own `/usr/share/applications/*.desktop`. The XFCE session reads that
  directory (`XFCE_DATA_DIRS=/usr/share/xfce-demo:/usr/share`).
* **The session helpers live in `sdl2_kmsdrm[rootfs]`.** These are `/bin/game-window.sh` and its
  autostart/quit scripts, plus `/etc/xdg/labwc-xfce-m8/`. Every windowed game already depends on
  that port. The labwc configuration is rewritten from the tools session's hand-staged names
  (`/bin/foot-2`, `/usr/lib/xfce-demo/bin/thunar`, …) to the image's names. The `sed` list and
  the fail-if-left check are the same ones `xfce_wayland` applies to `labwc-xfce-demo`.
* **Wayland client stack.** libwayland-client/-egl/-cursor, `libwlphx-compat` and the libffi view
  come from the **`wayland`** port, the one Mesa's wayland platform is built against. libxkbcommon
  and `<linux/input.h>` come from **`wayland_phoenix`**, each through a private view. The tools
  build linked labwc-drm's prefix instead.
* **The `os_create_anonymous_file` clash is resolved on the other side.** The tools renamed the
  symbol in private copies of the Mesa archives, which meant rebuilding the thin archives,
  including a copy of libgallium. The port renames it in a private copy of `libwayland-cursor.a`
  instead (`wlcursor_os_create_anonymous_file`): the copy is small and needs no thin-archive
  handling. The two implementations do the same thing (memfd_create over shmsrv), and each
  caller still binds to its own.
* **`liblwphx-compat.a` is dropped from the group.** It was in the tools group, but the link maps
  of all four `-wl` binaries show that none of them pulls a member from it.
* **Relink.** The `-wl` relinks of yquake2 and quake3 skip their control relink, because the
  `-drm` clone runs it just before on the same objects. The inverse control reads the game port's
  `prog/<engine>`: the tools read `_fs/root/usr/bin/<engine>`, which the default image no longer
  has.

## 2. What the default image gets

| Path | From | Notes |
|---|---|---|
| `/usr/bin/quakespasm-wl` | `quakespasm_drm[wayland]` | no launcher: `game-window.sh` passes `-window -width -height` |
| `/usr/bin/yquake2-wl`, `/usr/bin/quake2-wl` | `yquake2_drm[wayland]` | the launcher is the shipped `quake2-launcher.c` with its exec target rewritten |
| `/usr/bin/quake3e-wl`, `/usr/bin/quake3-wl` | `quake3_drm[wayland]` | the same, for quake3 |
| `/usr/bin/supertuxkart-wl`, `/bin/stk-wl` | `supertuxkart_drm[wayland]` | 3-line launcher rewrite, as `stk-drm` |
| `/bin/game-window.sh`, `/bin/game-window-autostart.sh`, `/bin/game-window-quit.sh`, `/etc/xdg/labwc-xfce-m8/{rc.xml,menu.xml,autostart,environment}` | `sdl2_kmsdrm[rootfs]` | games session: `export CONF_DIR=/etc/xdg/labwc-xfce-m8`, then `/bin/bash /bin/xfce-session` |
| `/usr/bin/ffplay-drm`, `/usr/bin/ffplay-wl`, `/bin/video-play` | `video_player[rootfs,wayland]` | `video-play` is the tools `video-play2`, rewritten to the final names (fail-if-left check) |
| `/usr/bin/gtk-video` | `video_player[gtk]` | links GTK + libav itself |
| `/usr/share/m10/` (4 clips, ~61 MB, + `labwc-xfce-m10/`) | `video_player[demo]` | clips are generated at build time by the **host** ffmpeg (x264, x265, vpx-vp9, opus, aac, all checked); the m10 cycles' paths |
| `/usr/bin/atril`, `/usr/share/atril/…` (schema, icons), `/usr/share/doc/phoenix/sample.pdf` | `atril_wayland[rootfs]` | Atril's own schema directory (patch 0005); GTK's `gschemas.compiled` is not touched |
| `/bin/xfce-desktop-atril.sh` | `atril_wayland[rootfs]` | the m7j test session |

The XFCE menu entries (`/usr/share/applications/`):

| File | Name | Exec | Icon | Menu |
|---|---|---|---|---|
| `quakespasm-window.desktop` | Quake (window) | `/bin/bash /bin/game-window.sh quakespasm` | `applications-games` | Games |
| `quake2-window.desktop` | Quake II (window) | `… game-window.sh quake2` | ″ | Games |
| `quake3-window.desktop` | Quake III Arena (window) | `… game-window.sh quake3` | ″ | Games |
| `stk-window.desktop` | SuperTuxKart (window) | `… game-window.sh stk` | ″ | Games |
| `gtk-video.desktop` | (tools file) | `/usr/bin/gtk-video %f` | `video-x-generic` | Multimedia |
| `video-demo.desktop` | Video Demo | `/bin/bash /bin/video-play /usr/share/m10/m10-h264-720p30-aac.mp4` | `video-x-generic` | Multimedia |
| `atril.desktop` | Atril Document Viewer | `/usr/bin/atril %U` | `/usr/share/atril/icons/hicolor/48x48/apps/atril.png` | Office |

All the icon names were checked against the staged PNG Adwaita theme.

`ports.yaml` (default image only): the four game ports `use: [rootfs, wayland]`, a new entry
`sdl2_kmsdrm` `use: [rootfs, wayland]`, `video_player` `use: [rootfs, wayland, gtk, demo]` and
`atril_wayland` `use: [rootfs]`.

## 3. First-build risks (for the coordinator's image build)

1. **Rebuild cascade: the largest cost.** The `mesa_drm` recipe changed, so its digest changed,
   which **cleans every port that depends on it**: Mesa itself (now six meson builds), then
   `sdl2_kmsdrm`, the five `*_drm` games, `kmscube_drm`, `vkcube_drm`, `xorg_server_drm` and
   `labwc_desktop`. GTK and XFCE are not affected. This takes hours and several GB. Check
   `df -h` first. The alternative, if that is unacceptable, is a standalone `sdl2_wayland` port
   with its own Mesa build: no cascade, but it duplicates about 150 lines of `mesa_drm`.
2. **First real run of every new code path**:
   * the `waylandgl` meson configuration;
   * the SDL-wl cmake configuration against the ports' pkg-config views;
   * the four `-wl` links and all their proofs;
   * the whole of `video_player`: FFmpeg configure, the fftools objects outside a programs
     build, three links;
   * the whole of `atril_wayland`: its first CMake packages on a symlink snapshot of the GTK
     tree, and the host pycairo + DejaVu fonts needed for the sample PDF.
3. **xkbcommon version.** The port links 1.7.0 (wayland_phoenix); the Pi-proven `-wl` binaries
   linked 1.13.2. SDL's use of the library is basic, and GTK/XFCE already parse labwc's keymap
   with 1.7.0, but keyboard input in a windowed game is the row to watch.
4. **The libwayland-cursor rename** (§1) changes the binary layout compared with the proven
   builds. The behaviour should be the same, but that has only been reasoned about, not tested.
5. **The demo clips depend on the host.** They differ byte for byte from the tools' clips, and a
   build host without those encoders fails the port (on purpose). Encoding 1080p x264/x265 takes
   minutes.
6. **`video_player[demo]` depends on `xfce_wayland`** (it copies its staged labwc configuration),
   so every XFCE rebuild also rebuilds the player.
7. **Rootfs growth**: about 18 MB for `quakespasm-wl` and a similar amount for each other `-wl`
   engine, plus 61 MB of clips, plus FFmpeg ×3, plus Atril/Poppler.
8. **The gate scripts do not know the new programs yet.** `check-gpu-stack-image.sh` and
   `check-rootfs-complete.sh` were not changed (other passes edit them), so a missing `-wl`
   program would not fail those checks. The new ports' own `b_die` checks are the only guard.

## 4. Transitional code (TD register, for the owner to record)

* `TODO(TD-26)` gains two sites: `sdl2_kmsdrm/port.def.sh` (`_sdl2_kmsdrm_stage_session`: the
  `labwc-xfce-m8` name and the "M8 " log prefix) and `video_player/port.def.sh` (the m10 labwc
  configuration's `/usr/lib/xfce-demo` names). `/bin/xfce-desktop-atril.sh` fits TD-26 too.
* `game-window.sh` is copied verbatim from the tools, so it keeps its `simple-egl` case. That case
  runs the hand-staged `/bin/weston-simple-egl-low`, which the image does not have, so it prints
  `FAIL … not staged`. Its comments also still name `/bin/xfce-session-2`. Fix both in the tools
  file and the port copy together (the sync check compares them).

## 5. Validation done (static only)

* `bash -n` on every changed or new recipe and on `relink-sdl-gl-game-wl.subr`. shellcheck is not
  installed on this host.
* `port_manager.py validate` on the ports branch: all ports load (96 on the games-only commit,
  98 with the two merged branches).
* **Dependency resolution**: `scripts/build-port.sh --dry --yaml <branch ports.yaml>` in a scratch
  buildroot (`make-scratch-buildroot.sh`, deleted afterwards), with the ports tree set to the
  branch.
  * Default mode: 77 ports, which is master's 75 plus `atril_wayland` and `video_player`;
    `mesa_drm` gets `+waylandgl`, `sdl2_kmsdrm` gets `+wayland +rootfs` and the four games get
    `+wayland`.
  * `RPI4B_GPU_LEGACY=1`: the same 58-port set as master's.
* **SDL patches**: 0001–0011 + overlay + 0101–0103 apply cleanly to the SDL2 2.30.12 tarball (a
  dry apply in a scratch directory).
* **Harness run of `_sdl2_kmsdrm_wayland` + `_sdl2_kmsdrm_stage_session`** with host-built stub
  archives and a stubbed cmake:
  * the `link-inputs.txt` it writes has the tools format (gallium, sdl, 14 mesa-gl, 14 mesa-es,
    9 tail, 6 flags; the bridge only in mesa-gl, libGLESv2 only in mesa-es);
  * the cursor archive's symbol is renamed;
  * the staged tree has 3 scripts and 4 configs with the names rewritten, and nothing is left
    that the fail-if-left check would catch;
  * `gamewl_desktop_entry` writes the expected `.desktop` file.
* The forks' own checks:
  * Atril: its 6 patches `git am` cleanly onto the tarballs;
  * video: the ffplay patch applies, and its staging snippet was run in a temp tree;
  * every vendored file `cmp`-identical to its tools file.
* **Sync checks with the new mappings** (4 for sdl2-wl and 8 for video-player in
  `check-gpu-lane-ports-sync.sh`, 6 for Atril in `check-wayland-ports-sync.sh`), run on the
  branch: 184 files in 57 mappings and 168 files in 53 mappings, all identical.

**Not done:** a framework build of any of this, a Pi cycle, and the `.claude/settings.json`
allowlist (nothing new needs one).

## 6. Pre-registered checks after the build

1. The ports log shows `mesa_drm: … waylandgl`, `sdl2_kmsdrm: wayland: 45 link items`, and for
   each game a `[<game>-wl] done:` line with `undefined symbols (nm -u): 0` and no
   `verification failed`.
2. The rootfs has every path in §2, including the 7 `.desktop` files.
   `grep -c foot-2 etc/xdg/labwc-xfce-m8/*` is 0.
3. Pi, XFCE session: the applications menu shows Games (4 entries), Multimedia (gtk-video, Video
   Demo) and Office (Atril). Each game opens a 1280×720 decorated window and prints
   `M8 game=<g> start …` and `<g>-wl: windowed GPU game …`. Atril opens `sample.pdf`.

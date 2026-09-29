# Desktop applications as framework ports (2026-09-30)

The owner's rule is that every executable on the image comes from the framework build. Three
groups of demo programs still existed only as hand-built, hand-staged tools under
`tools/gpu-lane/`: the windowed games (M8), the video players (M10) and the Atril PDF viewer
(M7 m7j, MIGRATION.md §7.0 row 19). This pass turns them into phoenix-rtos-ports recipes that
the default image installs, and gives each an XFCE applications-menu entry.

The design follows two owner directions of 2026-09-30:

* **One program per game and one per video player.** There are no separate windowed and full
  screen binaries.
* **Only the best current version ships.** No intermediate or test variants are installed, and no
  installed file names a hand-staged program (`*-2`, `*-low`, `video-play2`, `foot-2`, …).

Everything is on branches, pushed to `publish`, **not merged**. Nothing was built through the
framework and no Pi cycle ran; the checks were static (§5). The image build uses the
**integration branches** (§0), which merge this work with the P3 removal
([P3-removal.md](P3-removal.md)) and WiFi in the image
([2026-09-30-wifi-in-image.md](../misc/2026-09-30-wifi-in-image.md)).

## 0. Integration: `integration/finalize`

Each repo gets one branch from its current master/main (coordination: `main`). They are pushed to
`publish` and not merged; the SHAs are in the coordination commit that adds this section.

| Repo | Merged | Conflicts |
|---|---|---|
| devices | `gpu/p3-remove` (first GPU stack deleted), `wifi/in-image` (rpi4-wifi + `wifi` components) | none; `_targets/Makefile.aarch64a72-generic` auto-merged: no knob, no `rpi4-v3d`/`rpi4-fb`, `DEFAULT_COMPONENTS += rpi4-wifi wifi` |
| lwip | `wifi/in-image` (fast-forward) | — |
| ports | `gpu/p3-games-link` (includes `gpu/p3-remove`), then `feat/desktop-apps-ports` | `sdl2_kmsdrm/gamedrm/relink-sdl-gl-game.subr`, `yquake2_drm`, `supertuxkart_drm`: reconciled as the design below |
| project | `gpu/p3-remove`, `wifi/in-image`, `feat/desktop-apps-ports` | `ports.yaml` (the games' comment block); the desktop apps' entries lose their `RPI4B_GPU_LEGACY` condition, which P3 removed |
| coordination | `gpu/p3-remove`, `wifi/in-image`, `feat/desktop-apps-ports` | none (the scripts both P3 and WiFi changed auto-merged); then the image gates are extended with the new programs (§0.2) |

### 0.1 The games: one design for P3 and the dual-mode programs

* **The engine ports compile the engine and link nothing; the `*_drm` ports link it.** This is
  P3's TD-25 decision. `yquake2` and `quake3` compile against `sdl2_kmsdrm`'s headers and publish
  their link inputs in `port-sources/<port>/engine-link.sh` (`ENGINE_OBJS`,
  `ENGINE_LINK_FLAGS`, `ENGINE_LINK_TAIL`). `supertuxkart` publishes its CMake tree and a
  `link.txt` that names `sdl2_kmsdrm`'s `libSDL2.a`. `quakespasm_drm` compiles its engine itself
  (its own tarball), as before.
* **Each `*_drm` port links the engine ONCE, dual-mode.**
  * The objects come from the engine port.
  * The group is `sdl2_kmsdrm`'s `link-inputs.txt`: `libSDL2.a` with the KMSDRM and Wayland
    drivers; Mesa's GL build with EGL on GBM and on Wayland (`mesa-gl`, or `mesa-es` for
    quake2/STK); the Wayland client stack, libdrm, compat and zlib (`tail`); and its `flag`
    lines (`--wrap=mmap/ioctl/close/write`).
  * This happens in `relink-sdl-gl-game.subr` (yquake2_drm, quake3_drm), which is P3's
    `engine-link.sh` reader plus the `link-inputs.txt` group, the Wayland proofs and
    `game_desktop_entry`. `supertuxkart_drm` runs P3's `link.txt` plus the same group.
    `quakespasm_drm` uses the same group.
* There are no `-wl` programs and no control relink (P3 dropped it with the old stack). The names
  are the P1/P3 ones: `/usr/bin/{quakespasm,yquake2,quake3e,supertuxkart}-drm`, the launchers,
  and the plain names (TD-26).
* **vkQuake** stays P3's `vkquake_drm` with SDL's vulkan variant, KMSDRM only.
* **The tools scripts are superseded, not fixed.** P3 notes that
  `tools/gpu-lane/sdl2-wl/{build.sh,build-*-wl.sh,gamewl/relink-sdl-gl-game-wl.sh}` and
  `video-player/build-ffplay.sh` break at P3: they read the deleted ports and the old build.log
  line. The ports now build what they built, so they are deletion candidates (P3 §7). Only their
  patches, hooks and sources stay as sync-check masters.

### 0.2 Image gates

`check-rootfs-complete.sh` fails when any of these is missing:

* every GL game engine and ffplay, `/bin/video-play`, `/usr/bin/gtk-video`, `/usr/bin/atril`
  (+ its schema and `sample.pdf`);
* `/bin/game-window{,-autostart,-quit}.sh` and the `labwc-xfce-games` / `labwc-xfce-video`
  configs;
* the four demo clips and the seven menu entries;
* WiFi: `/sbin/rpi4-wifi`, `/bin/wifi`, `/etc/wifi.conf.example`, and the three BCM43455 firmware
  files + `LICENCE.cypress`, `GPL-2.0`, `WHENCE`. These are **now required**: an offline build
  without the firmware cache fails on purpose;
* the Wayland desktop and the GPU smoke tests, which were optional before;
* the `KMS/DRM Video Driver` and `SDL Wayland video driver` strings in each dual-mode engine and
  ffplay.

`check-gpu-stack-image.sh` adds a check "2b" covering:

* the same files, and each menu entry's `Exec` program;
* the three dual-mode strings (the two above plus `EGL_KHR_platform_wayland`);
* `etc/wifi.conf` absent;
* `rpi4-wifi` in `loader.disk`.

Its must-be-absent list (check 3) is P3's plus the superseded and hand-staged names: the `-wl`
game clones, `ffplay-{drm,wl}[2]`, `video-play2`, `atril-wl`, `xfce-desktop-atril.sh`,
`{foot,labwc,fuzzel,xfce-session}-2`, `xfce-desktop-2.sh`, `rpi4-v3d-async-low`,
`rpi4-kms-g{7,8,9}`, `weston-simple-egl-low`, `etc/xdg/labwc-xfce-m8`, `usr/share/m10`,
`Xorg-drm-noshim` and `v3dmemprobe`.

**Inverse control** (today's P1 rootfs + loader, read-only): `check-gpu-stack-image.sh` FAILs 45
checks. They are exactly the new programs and files, the missing dual-mode strings, and the two
stale P1 files `bin/Xorg-drm-noshim` and `bin/v3dmemprobe` in the persistent `_fs` tree.
`check-rootfs-complete.sh` reports INCOMPLETE with 39. Every check on the P1 stack's own files
still passes.

| repo | branch | head |
|---|---|---|
| phoenix-rtos-ports | `feat/desktop-apps-ports` (includes the Atril and video work) | `6693114` |
| phoenix-rtos-project | `feat/desktop-apps-ports` | `6b702ad` |
| coordination | `feat/desktop-apps-ports` | this document + the two sync-check scripts |

### 0.3 The build of the integration branches

**Validated statically:**
* `bash -n` on every changed recipe, subr and script, and `port_manager validate`: 94 ports.
* `build-port.sh --dry --yaml` of the rendered `ports.yaml` (scratch buildroot) resolves
  **75 ports**: master's 75 − `sdl2` − `xorg_server` + `atril_wayland` + `video_player`.
* `diff-boot-variants.py --order rpi4-vcmbox,posixsrv,rpi4-v3d-async,rpi4-kms,shmsrv,psh`, for
  sd/nfsroot/netboot × `RPI4_LOG_TO_FILE` 0/1: order OK and no duplicates.
  `rpi4-wifi` is absent from netboot, as on `wifi/in-image` (its `/` is a RAM dummyfs).
* Both sync checks are identical.
* The changed C files (`rpi4-wifi.c`, `wifi.c`, `libvcmbox.c`, lwip `wifi43455.c`) compile to
  `/dev/null` under the real flags (`-Werror`), with the command lines recovered by `make -n` from
  a scratch copy (nothing written to `.buildroot`).

**Pre-build cleanup** (P3 §8.3, plus the deleted ports' state files and the stale `_fs` files
found by the inverse control):

```
B=.buildroot/_build/aarch64a72-generic-rpi4b
F=.buildroot/_fs/aarch64a72-generic-rpi4b/root
rm -rf $B/include/SDL2 $B/lib/libSDL2.a $B/lib/libSDL2main.a $B/lib/pkgconfig/sdl2.pc $B/lib/libmd.a $B/include/sha1.h \
       $B/port-sources/{sdl2,quakespasm,vkquake,xorg_server}-* $B/.port_state/{sdl2,quakespasm,vkquake,xorg_server}-*.json
rm -rf $F/bin/Xorg-drm-noshim $F/bin/v3dmemprobe $F/bin/fbprobe $F/bin/hevc-play $F/.mesa-shader-cache
```

**Build** (every sibling on `integration/finalize`, coordination repo on its `integration/finalize`):

```
./scripts/rebuild-rpi4b-fast.sh --scope core --with-ports --with-showcase   # "[wifi-fw] cache verified" + "staged"
./scripts/check-rootfs-complete.sh .buildroot/_fs/aarch64a72-generic-rpi4b/root
./scripts/check-gpu-stack-image.sh
./scripts/make-pristine-nfs-export.sh && ./scripts/restore-export-data.sh
```

**Rebuilt by this build** (recipe digests): Mesa (all builds) and every dependent of it
(`sdl2_kmsdrm`, the engine providers `yquake2`/`quake3`/`supertuxkart` — STK's first CMake
reconfigure against the dual-driver SDL, P3 risk 1 —, the five `*_drm` games, `kmscube_drm`,
`vkcube_drm`, `libepoxy`, `xorg_server_drm`, `labwc_desktop`). New: `video_player` and
`atril_wayland`. GTK and XFCE are not rebuilt. It takes hours and several GB; check `df -h` first.

## 1. Design: one binary, the mode decided at run time

SDL 2.30 with both the **Wayland** and the **KMSDRM** video drivers tries Wayland first. When no
compositor socket exists it falls through to KMSDRM; `SDL_VIDEODRIVER` can force either one.
Mesa's EGL can have the GBM (drm) and the Wayland platforms in the same build. The tools builds
already relied on this: `sdl2-wl/build-out/quakespasm-wl` and `stk-wl/supertuxkart-wl` contain
both drivers and both EGL platforms. So each game is **one ELF**:

* From psh there is no socket, so it runs full screen on KMS, as the P1 image does today.
* Inside the desktop, `/bin/game-window.sh` starts the same program with
  `SDL_VIDEODRIVER=wayland` and the engine's windowed arguments, and it opens as a decorated
  window.

The same applies to ffplay: `/bin/video-play` sets `SDL_VIDEODRIVER` and `-fs` according to
whether a compositor socket exists.

| Port | Change | Why this shape |
|---|---|---|
| `mesa_drm` | The **opengl build (`gl/`) gets the EGL wayland platform next to GBM** (`-Dplatforms=wayland`, `opengl? ( wayland )`). Its lists `link-gl.txt` and `link-gles.txt` gain `libwayland_drm.a`. There is no new USE flag. | Only SDL programs use `gl/`: `sdl2_kmsdrm`, the four GL games and `video_player`. The other builds are unchanged: `gles/` (kmscube), `wayland/` (labwc, weston), `x11/` (Xorg-drm) and `vulkan/`. So adding the platform to the one GL build changes no other consumer. |
| `sdl2_kmsdrm` | The one `libSDL2.a` has **Wayland + KMSDRM**: `patches/wayland/0101–0103` are applied to the main source, after the vulkan copy. It writes **`link-inputs.txt`**, the full link group including the Wayland client stack. The vulkan variant (vkQuake) keeps `SDL_WAYLAND=OFF`. USE `rootfs` installs the game launcher and the games session. | This is the tools' `sdl2-wl/build.sh` (steps 1–3). vkQuake stays KMSDRM-only: a window on Wayland would need the V3DV Wayland WSI, which the static ICD does not have. |
| `quakespasm_drm`, `yquake2_drm`, `quake3_drm`, `supertuxkart_drm` | The existing `-drm` engine is now linked with `link-inputs.txt`; after integration, from the engine port's objects (§0.1). The proofs gain Wayland symbols and strings. Each port also installs an XFCE menu entry. | The names stay the same (`/usr/bin/quakespasm-drm`, `quake2-drm`, `quake3-drm`, `/bin/stk-drm`), so the plain commands (`/usr/bin/quakespasm`, `quake2`, `quake3`, `/bin/stk`) and `/bin/game-res` keep working unchanged. |
| `video_player` (new) | One `/usr/bin/ffplay` with both drivers; `/bin/video-play`; `gtk-video` (GTK/Wayland only). USE `rootfs gtk demo`. | It is anchored on the ffmpeg 6.1 tarball; the `ffmpeg` port is untouched. |
| `atril_wayland` (new) | Atril + Poppler on the gtk3_wayland stack. Every package is behind a `.built` stamp, so the port has a **`p_relink`** that drops the stamps. The m7j test session is not shipped. | It follows the gtk3_wayland / xfce_wayland shape. |

Other decisions:

* **`xfce_wayland` and `gtk3_wayland` are not edited.** `port_manager` hashes a port's whole
  directory and rebuilds the port and **every port that depends on it** when the hash changes.
  Each application installs its own `/usr/share/applications/*.desktop` instead; the XFCE
  session reads `/usr/share` (`XFCE_DATA_DIRS`).
* **Session files are the ports' own.** These are `game-window.sh` and its autostart/quit scripts,
  `labwc-xfce-games/`, `video-play` and `labwc-xfce-video/`. They are derived from the tools files
  with the image's program names, and they drop the `simple-egl` case (a hand-staged binary). Their
  log prefix is `GAME-WINDOW` and their knobs are `GAME_LIST` / `GAME_LIST_DELAY`. At staging a
  check fails the build if an installed file still names a hand-staged program or path. Only the
  unchanged copies (patches, hooks, gtk-video.c, …) are sync-mapped to tools.
* **Wayland client stack.** libwayland-client/-egl/-cursor, `wlphx-compat` and libffi come from
  the `wayland` port, which is the one Mesa's wayland platform is built on. libxkbcommon 1.7.0 and
  `<linux/input.h>` come from `wayland_phoenix` through private views.
* **The `os_create_anonymous_file` clash.** libwayland-cursor and Mesa both define this symbol.
  The build renames it in a private copy of `libwayland-cursor.a`; the tools renamed Mesa's copy.
  The two implementations do the same thing. `liblwphx-compat.a` is not in the link group: the
  `-wl` link maps show zero members of it are used.
* **Hooks.** The games keep `gamedrm_hooks.c`: the image gates grep its `<app>: new GPU lane`
  banner, and its `flipstat` counter is unchanged. Which driver actually ran is visible in two
  places: SDL's VIDEO debug lines (KMSDRM prints its `KMSDRM_*` init steps) and
  `GAME-WINDOW … driver=wayland`.

## 2. What the default image gets

| Path | From |
|---|---|
| `/usr/bin/quakespasm-drm` (+ `/usr/bin/quakespasm`, `/bin/qs-drm`), `/usr/bin/yquake2-drm` + `quake2-drm` (+ `quake2`), `/usr/bin/quake3e-drm` + `quake3-drm` (+ `quake3`), `/usr/bin/supertuxkart-drm` + `/bin/stk-drm` (+ `/bin/stk`): the same paths as P1; each binary now also has the Wayland driver | the four game ports |
| `/bin/game-window.sh <game>`, `/bin/game-window-autostart.sh`, `/bin/game-window-quit.sh`, `/etc/xdg/labwc-xfce-games/{rc.xml,menu.xml,autostart,environment}` | `sdl2_kmsdrm[rootfs]` |
| `/usr/bin/ffplay`, `/bin/video-play`, `/usr/bin/gtk-video` | `video_player[rootfs,gtk]` |
| `/usr/share/video-demo/{h264-720p30-aac.mp4, h264-1080p30-aac.mp4, hevc-720p30-aac.mp4, vp9-360p-opus.webm}` (~61 MB, generated at build time by the host ffmpeg), `/etc/xdg/labwc-xfce-video/` | `video_player[demo]` |
| `/usr/bin/atril`, `/usr/share/atril/…`, `/usr/share/doc/phoenix/sample.pdf` | `atril_wayland[rootfs]` |

The windowed commands run by the menu entries (defaults 1280×720, overridden with `GAME_W` /
`GAME_H`):

| Game | Windowed command |
|---|---|
| quakespasm | `/usr/bin/quakespasm-drm -window -width W -height H` (the engine itself: the launcher's full-screen `-width` would win) |
| quake2 | `/usr/bin/quake2 +set vid_fullscreen 0 +set r_mode -1 +set r_customwidth W +set r_customheight H` |
| quake3 | `/usr/bin/quake3 +set r_fullscreen 0 +set r_mode -1 +set r_customWidth W +set r_customHeight H +map q3dm1` |
| stk | `/bin/stk --windowed --screensize=WxH` |

XFCE menu entries (`/usr/share/applications/`):

| Menu | Entries (Exec) |
|---|---|
| Games | `quakespasm.desktop` "Quake", `quake2.desktop` "Quake II", `quake3.desktop` "Quake III Arena", `stk.desktop` "SuperTuxKart" (all `/bin/bash /bin/game-window.sh <game>`, `Icon=applications-games`) |
| Multimedia | `gtk-video.desktop` (`/usr/bin/gtk-video %f`), `video-demo.desktop` "Video Demo" (`/bin/bash /bin/video-play /usr/share/video-demo/h264-720p30-aac.mp4`) |
| Office | `atril.desktop` (`/usr/bin/atril %U`) |

The games session starts the games of `GAME_LIST` by itself:
`export CONF_DIR=/etc/xdg/labwc-xfce-games`, then `/bin/bash /bin/xfce-session`. The video
session works the same way: `CONF_DIR=/etc/xdg/labwc-xfce-video`, with the knobs `VIDEO_CLIP`,
`VIDEO_DELAY` and `VIDEO_PLAYER`.

**Product option for the coordinator:** STK full screen at 1280×720 scaled by rpi4-kms runs at
about 22 fps, against about 12.4 at 1080p. It is available today as `/bin/game-res stk 1280x720`
(M9). Making it `/bin/stk`'s default would be a launcher change (`stk-launcher.c`, a tools copy),
and is not done here.

## 3. First-build risks

1. **Rebuild cascade.** The recipes of `mesa_drm` and `sdl2_kmsdrm` changed, so both are rebuilt
   together with every port that depends on them: Mesa (all five builds), SDL, the five `*_drm`
   games, `kmscube_drm`, `vkcube_drm`, `libepoxy`, `xorg_server_drm` and `labwc_desktop`. GTK and
   XFCE are not affected. This takes hours and several GB; check `df -h` first (about 20 GB free
   at the time of writing).
2. **The P1-proven full-screen binaries change.** They now also link libEGL's wayland platform,
   the Wayland client libraries, and the compat layer's `--wrap=close/write` (plus `--wrap=ioctl`
   for quakespasm, which only had `--wrap=mmap`). The layout and the `close()`/`write()` path
   differ from the P1 gate binaries, which is why check (a) compares fps against the P1 medians.
3. **First real run** of: the Wayland-platform GL Mesa configure, the dual-driver SDL configure
   (the vulkan variant now also resolves egl.pc through the Wayland pkg-config view), the whole of
   `video_player`, and the whole of `atril_wayland` (CMake packages on a GTK symlink snapshot;
   the sample PDF needs host pycairo and the DejaVu fonts).
4. **xkbcommon 1.7.0 vs 1.13.2.** The proven `-wl` binaries linked 1.13.2. Keyboard input in the
   game windows is the row to watch.
5. **The libwayland-cursor rename** differs from the tools build. It was reasoned about, not
   measured.
6. **The demo clips depend on the host.** The build needs host ffmpeg with the x264, x265,
   vpx-vp9, opus and aac encoders; the recipe checks for them, and encoding takes minutes.
7. **The gate scripts are not updated.** `check-gpu-stack-image.sh` and `check-rootfs-complete.sh`
   do not list `/usr/bin/ffplay`, `/bin/video-play`, `/bin/game-window.sh`, `/usr/bin/atril` or the
   `.desktop` files. Add them before the next image gate.

## 4. Pre-registered Pi checks (for the coordinator)

**(a) Full screen from psh (KMSDRM), fps against the P1 gate medians.** Run each command at the
psh prompt, with no desktop running:

| Game | Command | P1 median fps | Pass |
|---|---|---|---|
| qspasm | `/usr/bin/quakespasm` | 43.8 | ±1 vsync step |
| q3 | `/usr/bin/quake3 +map q3dm1` | 58.8 | ±1 vsync step |
| q2 | `/usr/bin/quake2` | 59.8 | ±1 vsync step |
| stk | `/bin/stk --track=hacienda --numkarts=4 --profile-laps=2` | ≈ 12.4 | ±1 vsync step |
| vkq | `/usr/bin/vkquake` | ≈ 44 | unchanged binary set (control) |

For every row:
* the banner `<app>: new GPU lane` and SDL's `KMSDRM_*` VIDEO debug lines appear;
* no line says SDL is on Wayland (there is no socket);
* for stk, also run `/bin/game-res stk 1280x720` and expect about 22 fps (the M9 path still
  works);
* the video player: `/bin/video-play /usr/share/video-demo/h264-720p30-aac.mp4` gives
  `VIDEO-PLAY start mode=drm … video=KMSDRM fs=1`, then `ffplay-stat` lines, then
  `VIDEO-PLAY done rc=0`.

**(b) Windowed inside XFCE.** Set `export CONF_DIR=/etc/xdg/labwc-xfce-games`,
`export GAME_LIST=quakespasm:60,quake2:60,quake3:60,stk:90` and `export HOLD=420`, then run
`/bin/bash /bin/xfce-session`.

* Each game prints `GAME-WINDOW game=<g> start … driver=wayland`, its banner and `flipstat` lines,
  and no `KMSDRM_*` init lines.
* HDMI: a decorated 1280×720 window at (636,40) next to Thunar and foot; the panel and the
  wallpaper stay up.
* From the menu, by hand: Games opens the four entries, Multimedia plays gtk-video and Video Demo
  (`mode=wl`), and Office opens Atril on `sample.pdf`.
* The video session: `CONF_DIR=/etc/xdg/labwc-xfce-video` plays the clip in a window.

**(c) Quit paths.**

* Windowed: each `GAME_SECS` timeout sends SIGTERM and must print
  `GAME-WINDOW … exited rc=0 (clean exit)`.
* Session end: when `HOLD` expires with `LOGOUT_CMD=/bin/game-window-quit.sh`, the output must
  be `GAME-WINDOW quit: SIGTERM …` then `gone after <N>s result=rc=0`, then
  `XFCE-SESSION done rc=0`.
* Full screen from psh: `kill -TERM <pid>` of each game (from a second console or via
  `GAMEDRM_EXIT_SECS`) must return to a responsive psh prompt, and console handover must print
  `KMS srv console handover enable rc=0`.
* All cycles: 0 kernel faults and 0 EL0 faults.

## 5. Validation done (static only)

* `bash -n` on every recipe, on `relink-sdl-gl-game.subr`, on the three game-window scripts and
  on `video-play`. shellcheck is not installed on this host. `xmllint` passes on the four labwc
  XML files.
* `port_manager.py validate` of the ports branch: 98 ports.
* **`build-port.sh --dry --yaml`** of the branch `ports.yaml` in a scratch buildroot (deleted
  afterwards):
  * Default: 77 ports, which is master's 75 plus `atril_wayland` and `video_player`. `mesa_drm`
    is `+opengl +wayland +x11 +vulkan`; `sdl2_kmsdrm` is `+rootfs +vulkan`. `xfce_wayland` is no
    longer anyone's dependency.
  * `RPI4B_GPU_LEGACY=1`: the same 58-port set as master.
* The SDL patch set (0001–0011, the overlay, 0101–0103) applies cleanly to the 2.30.12 tarball.
* **A harness run of `sdl2_kmsdrm`'s real `p_build`**, with stub archives and a stubbed cmake:
  * `link-inputs.txt` has 45 items (gallium, sdl, 14 mesa-gl, 14 mesa-es, 9 tail, 6 flags). The
    tail archives are matched with `-ef`, so a `//` in the path is harmless, and `libz` appears
    exactly once.
  * The cursor symbol is renamed.
  * Session staging installs 3 scripts and 4 configs, and the leftover-name check passes.
  * `game_desktop_entry` writes the expected `.desktop` file.
* Sync checks with the updated mappings, run against the branch: 174 files / 52 mappings for the
  gpu-lane check and 167 files / 52 mappings for the wayland check, all identical.

**Not done:** a framework build, a Pi cycle, updating the image gate scripts (§3 item 7), and the
STK 1280×720 default (§2).

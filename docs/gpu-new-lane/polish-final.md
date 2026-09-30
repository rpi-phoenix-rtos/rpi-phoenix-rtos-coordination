# Final polish: one name per program, the showcase knobs (2026-09-30)

The owner's rules of 2026-09-29/30 are: ship only the best current version of everything, drop
every old, legacy, temporary or testing piece, and give each program one binary and one name.
This pass applies them to what the integration build (`integration/finalize`) still carried:

* launcher copies under a second name;
* session wrappers;
* server paths of hand-staged binaries;
* "new GPU lane" wording.

It also adds the knobs the showcase recording needs
([SHOWCASE-VIDEO-PLAN.md](../SHOWCASE-VIDEO-PLAN.md) Q1–Q8 on the docs branch).

Everything is on branches `polish/final`, pushed to `publish` and **not merged**. The checks
were static only: no build and no Pi cycle (§6). This file lists the user-visible effects for the
docs.

| Repo | Branch | Commits (on) |
|---|---|---|
| phoenix-rtos-ports | `polish/final` | `d7e6e90` functional, `c5384ab` wording (on master `7962adc`) |
| phoenix-rtos-project | `polish/final` | `b1b4751` `ports.yaml` comments only (on master `e23280f`) |
| coordination | `polish/final` | `b9c4a58eb` gates, tools, bootstrap; `9e6287ed8` banner token; this file (on main `409857325`) |

**Merge them in pairs:** ports `d7e6e90` goes with coord `b9c4a58eb`, and ports `c5384ab` goes with
coord `9e6287ed8`. Both are required by the wording rule. The second pair is separate only so its
rebuild cost can be scheduled (§7). Each pair on its own keeps the sync checks and the gates
consistent.

## 1. What a user sees

### 1.1 Command names

Each program now has exactly one name:

| Program | Before | Now |
|---|---|---|
| QuakeSpasm | `/usr/bin/quakespasm` + copy `/bin/qs-drm` | `/usr/bin/quakespasm` |
| Quake II | `/usr/bin/quake2` + copy `/usr/bin/quake2-drm` | `/usr/bin/quake2` |
| Quake III | `/usr/bin/quake3` + copy `/usr/bin/quake3-drm` | `/usr/bin/quake3` |
| vkQuake | `/usr/bin/vkquake` + copy `/bin/vkq-drm` | `/usr/bin/vkquake` |
| SuperTuxKart | `/bin/stk` + copy `/bin/stk-drm` | `/bin/stk` |
| X desktop | `/bin/startx-drm` + generated wrappers `/bin/startx`, `/bin/startx_gpu` | `/bin/startx` (the script itself) |
| XFCE session | wrapper `/bin/xfce-session` → `/usr/lib/xfce-demo/xfce-session` | `/bin/xfce-session` (the script itself) |
| Thunar | `/bin/thunar-wl` | `/bin/thunar` |
| gdbus | `/bin/gdbus-wl` | `/bin/gdbus` |
| Vulkan smoke test | `/bin/vkcube-drm` | `/bin/vkcube` |

The engines keep their `*-drm` names: `/usr/bin/{quakespasm,yquake2,quake3e,vkquake,supertuxkart}-drm`.
The launchers start them, so users never type these names. The plain engine names are the deleted
first stack's, and `check-gpu-stack-image.sh` check 3 asserts they are absent.

### 1.2 Behaviour changes

* **Bare `startx`** still runs the `action` showcase desktop. `HOLD` now defaults to `0`, so the
  desktop stays up until Window Maker's Exit, as the wrapper behaved. The recordings and the gate
  set `HOLD` explicitly.
* **The servers start at boot, and only there.** `startx`, `xfce-session` and `video-play` (drm
  mode) no longer start servers themselves:
  * They used to start them from `/bin/rpi4-v3d-async-low` and `/bin/rpi4-kms-g7`, which are not
    on the image.
  * Now they check `/dev/v3d-async`, `/dev/kms` and `/shm`. If one is missing they stop with
    `... done rc=1 (a GPU server is missing: they start at boot, see the boot log)`.
  * `startx --servers`, `V3DA_CMD`, `KMS_CMD`, `SHMSRV_CMD` and `NO_SERVERS` are gone.
* **`xfce-session`'s defaults are the image's.** The renderer is `gles2`, the programs are
  `/bin/labwc`, `/bin/xfce-desktop.sh`, `/bin/thunar`, `/bin/xfce4-panel` and `/bin/xfdesktop`,
  and the configuration is `CONF_DIR=/etc/xdg/labwc-xfce-demo`. Its header lists the three
  session configurations (demo, games, video).
* The games session's labwc rules (`/etc/xdg/labwc-xfce-games/rc.xml`) now also place the video
  players (ffplay, gtk-video) at (636,40). One `CONF_DIR` therefore serves a mixed scene.

### 1.3 New commands and knobs

These are the answers to Q1–Q8. psh's `export` cannot give a value with spaces (`psh/pshapp/env.c`
splits at the first `=` and passes quotes through), so none of the knobs needs a space.

**`XFCE_AUTOSTART`** (new `/bin/xfce-autostart.sh`) opens programs on the XFCE desktop without
keyboard or mouse, in order, once the panel is up:

```
export XFCE_AUTOSTART=<item>[=<arg>][:<secs>],...   [XFCE_AUTOSTART_DELAY=5] [XFCE_AUTOSTART_GAP=3]
/bin/bash /bin/xfce-session
```

* An item with `:<secs>` runs for that long, is closed, and then the next item starts.
* An item without `:<secs>` stays open, and the next item starts `XFCE_AUTOSTART_GAP` s later.
* At the session's stop (Log Out or `HOLD`), everything still open is closed first.
* Its UART lines start with `XFCE-AUTOSTART ` (`open`, `closed`, `skip`, `stop`, `done`).
* In the games session, `GAME_LIST=none` (new) starts no game from the session's own
  autostart, so `XFCE_AUTOSTART` alone decides what opens.

| Item | Opens |
|---|---|
| `atril[=<pdf>]`, `atril-fs`, `atril-pres` | Atril on `/usr/share/doc/phoenix/sample.pdf` (or `<pdf>`): windowed, `--fullscreen`, `--presentation` |
| `video[=<clip>]` | `/bin/video-play` in a window (default `VIDEO_CLIP`, else `h264-720p30-aac.mp4`). With `:<secs>` it plays that long (ffplay `-t`). `FFPLAY_AUTOKEYS` and the other knobs pass through |
| `gtk-video[=<clip>]` | `/usr/bin/gtk-video --autoexit` |
| `quakespasm`, `quake2`, `quake2-demo`, `quake3`, `stk`, `stk-race` (also `qs`, `q2`, `q3`) | the game in a window (`/bin/game-window.sh`). `:<secs>` = `GAME_SECS`, the game's own clean quit |
| `foot`, `mc`, `thunar[=<dir>]`, `appfinder` | a terminal, Midnight Commander in a terminal, a Thunar window (a second one if Thunar runs), the application finder |
| `sleep:<secs>` | a pause |
| `/<path>[=<arg>]` | any program, with at most one argument |

**Presets**, all one word:

| Command | Does | Answers |
|---|---|---|
| `game-window.sh stk-race` (also `GAME_LIST=stk-race:150`, `XFCE_AUTOSTART=stk-race:150`) | an AI race in a window | Q2 |
| `stk race` (full screen, from psh) | an AI race full screen | Q2 |
| `game-window.sh quake2-demo` | Quake II playing the pak's recorded demo in a window | Q8 |
| `quake2 +demomap q2demo1.dm2` (full screen) | Quake II playing the pak's recorded demo | Q8 |
| `vkquake +playdemo demo1` | vkQuake playing a recorded demo | Q5 |
| `vkquake +timedemo demo1` | vkQuake benchmark | Q5 |
| `startx browse [url]` | Window Maker + Dillo (1780×980 at (40,40)) on `<url>`, or on Dillo's start page | Q6 |
| `game-res stk 1280x720 race`, `game-res vkq 1280x720 +playdemo demo1` | the same in a scaled mode | — |

How the presets are built:

* The stk launcher expands the word `race` to `--track=hacienda --numkarts=4 --profile-laps=2`.
  In STK's profile mode, four AI karts race two laps and then STK exits.
* `q2demo1.dm2` is in the Quake II demo pak (`demos/q2demo1.dm2`). Quake II's launcher leaves out
  its `+map demo1` when the caller gives `+map`, `+demomap` or `+gamemap`.
* The Quake shareware pak has `demo1.dem`–`demo3.dem`. The vkquake launcher leaves out its
  `+map start` when the caller gives `+map`, `+playdemo` or `+timedemo`. Bare `vkquake` (the
  gate's row) is unchanged.

## 2. Answers to SHOWCASE-VIDEO-PLAN Q1–Q8

**Q1 (scripted input inside XFCE).** No input-injection tool was added. `XFCE_AUTOSTART` removes
the need for input in every scene except opening the menus on camera:

* Atril, the windowed games, the video player, Thunar, foot and the application finder all open
  by themselves.
* The Applications menu and its submenus still need a person at the Pi, or a future tool. The
  panel's own `xfce4-panel --plugin-event=applicationsmenu:popup` was considered. Its handler
  first takes a seat grab on a `GtkInvisible` (`applicationsmenu.c`
  `applications_menu_plugin_remote_event`), which is unlikely to succeed on Wayland, so it is not
  offered.
* Recommendation: the owner opens the menus by hand in one short clip (S3). Everything else runs
  unattended.

**Q2 (STK race in a window).** Yes: `GAME_LIST=stk-race:150` in the games session, or
`XFCE_AUTOSTART=stk-race:150`. Full screen: `stk race`.

**Q3 (Atril knob).** Yes: `XFCE_AUTOSTART=atril` (or `atril-fs`, `atril-pres`, `atril=<pdf>`).
With `:<secs>` it closes by itself.

**Q4 (does `FFPLAY_AUTOKEYS` reach ffplay?).** Yes, by environment inheritance:

* the chain is psh `export` → `/bin/bash /bin/xfce-session` → `xfce-desktop.sh` → labwc → the
  autostart `sh` (or `xfce-autostart.sh`) → `video-play` → ffplay;
* no step clears the environment: `xfce-desktop.sh` only exports more variables, and labwc's
  `environment` file only adds fixed ones;
* ffplay reads the variable with `getenv` (video_player patch 0001);
* the games session's `GAME_LIST` crossed the same path on hardware (M8);
* `video-play` prints the value it saw (`VIDEO-PLAY start … autokeys=…`), so the recording's UART
  log confirms it.

**Q5 (vkQuake demo).** Yes: `vkquake +playdemo demo1` (the launcher now leaves out `+map start`).
Nothing needs to be staged in `id1/`. Not run on hardware.

**Q6 (Dillo on X).** Yes: `startx browse https://example.com`, after `ntpclient -s pool.ntp.org`
for TLS and with the NAT gateway up. The image stages no `dillorc`, so a bare `startx browse` opens
Dillo's own start page. Dillo under Xorg (modesetting + glamor) has not been run.

**Q7 (which image for the reel).** Record on the **nfsroot** (netboot NFS-root) image:

* Restore the lab export with `scripts/restore-export-data.sh`. It brings back `/etc/wifi.conf`, so
  WiFi joins at boot.
* On the SD image, WiFi joins only after one `wifi connect <ssid> <psk>` typed at psh. That writes
  `/etc/wifi.conf` to the card; later boots join by themselves.
* The `netboot` variant (RAM `/`) does not start `rpi4-wifi` at all.

**Q8 (Quake II demo).** Yes: `quake2 +demomap q2demo1.dm2`, or `game-window.sh quake2-demo` in a
window. Not run on hardware.

### 2.1 The scenes with the new knobs (for the plan's §2)

* **S3 + S5 + S6 in one session**, with the games configuration, which places the game and the
  players at the right of Thunar and foot:

  ```
  export CONF_DIR=/etc/xdg/labwc-xfce-games GAME_LIST=none XFCE_AUTOSTART=atril:30,quake3:90,video=/usr/share/video-demo/h264-720p30-aac.mp4 FFPLAY_AUTOKEYS=12:fs,24:fs HOLD=240
  /bin/bash /bin/xfce-session
  ```

  * The games session's own autostart still runs `game-window-autostart.sh`, whose default
    `GAME_LIST` is `quakespasm`. `GAME_LIST=none` (new) makes it start no game, so the scene's
    programs come from `XFCE_AUTOSTART` alone.
  * Without the games configuration, drop `CONF_DIR`: labwc then places the windows itself.
* **S4:** `export CONF_DIR=/etc/xdg/labwc-xfce-games GAME_LIST=stk-race:150 GAME_LIST_DELAY=20 HOLD=200 LOGOUT_CMD=/bin/game-window-quit.sh`,
  then `/bin/bash /bin/xfce-session`.
* **S7:** `stk race` (or `game-res stk 1280x720 race`), `quake2 +demomap q2demo1.dm2` and
  `vkquake +playdemo demo1`.
* **S9:** `ntpclient -s pool.ntp.org`, then `export HOLD=120`, then
  `/bin/bash /bin/startx browse https://example.com`.

## 3. Rename decisions

| Candidate | Decision | Why |
|---|---|---|
| launcher copies `qs-drm`, `quake2-drm`, `quake3-drm`, `vkq-drm`, `stk-drm` | **dropped**; the launchers are installed under the plain names only | They were byte-copies, two names for one program. The gate already ran the plain names. `game-res` now runs the plain names too |
| `startx-drm`, `startx_gpu`, the `startx` wrapper | **one `/bin/startx`**, the script itself (source `xorg_server_drm/glue/pi/startx`) | The wrappers only set `HOLD=0`, which is now the script's default |
| `/bin/xfce-session` wrapper → `/usr/lib/xfce-demo/xfce-session` | **the script itself at `/bin/xfce-session`** | The wrapper only overrode the defaults, which now carry the image's values |
| `thunar-wl`, `gdbus-wl` | **`thunar`, `gdbus`** | Typing `thunar` in foot found nothing. No other port installs either name. xfdesktop's `-Dfile-manager-fallback` follows |
| `vkcube-drm` | **`vkcube`** | Smoke test users type. Only the port's install line and two gate lists named it |
| engines `*-drm` | kept | The launchers exec them and nobody types them. The plain engine names (`yquake2`, `quake3e`, `supertuxkart`) are check 3's "first stack" names |
| `Xorg-drm`, `/etc/X11/xorg-drm.conf`, `/var/log/Xorg-drm.1.log`, `eglx11-demo-x` | kept | `startx` starts them and users do not. A rename would touch the port's self-checks, the log path and `rebuild-rpi4b-fast.sh`'s marker for no user gain. It is a candidate if the owner wants `Xorg` |
| servers `rpi4-v3d-async` (`/dev/v3d-async`), `rpi4-kms` | kept | They are in the boot config (plo aliases), libdrm-phoenix's device name, the sessions' prechecks and the gates' ready lines |
| `/usr/lib/xfce-demo`, `/etc/xdg/labwc-xfce-demo`, `/etc/xdg/xfce-demo`, `/usr/share/xfce-demo` | kept, **TD-26 narrowed to exactly this** | Not typed by users. Renaming them moves the session's config tree and removes `xfce_wayland`'s sed (TD-26 lists the steps) |
| ports `*_drm`, `sdl2_kmsdrm`, `mesa_drm`, `libdrm_phoenix`, `xorg_server_drm` | kept | Internal. Every `depends=`, `ports.yaml`, `PORT_DEP_*`, `versioned-ports/` and sync-map path would change, which means a full rebuild for nothing a user sees |
| internal tokens `phoenix-newlane/newlane.subr`, `~/.phoenix-distfiles/newlane`, `nl_*`, `docs/gpu-new-lane/` | kept | Renaming the distfiles cache re-downloads every tarball; the others are API names across 15 recipes |

## 4. Wording

* **Runtime banners** of the engines and phxvk: `"<app>: new GPU lane --"` became
  `"<app>: Phoenix-RTOS GPU stack --"`. The SDL games' banner now also names both video drivers:
  `SDL 2.30.12 (KMSDRM + Wayland)`. The gates grep `<app>: Phoenix-RTOS GPU stack`:
  * `check-gpu-stack-image.sh` check 2;
  * `verify-sd-image-contents.sh`;
  * the ports' own string checks (`relink-sdl-gl-game.subr`, `supertuxkart_drm`, `vkquake_drm`,
    `vkcube_drm`).
* **Other runtime strings:**
  * Xorg's `builder_string` is `Phoenix-RTOS (Xorg-drm)`;
  * libxcvt's `.pc` Description is neutral;
  * the installed `/etc/X11/xorg-drm.conf` header and the labwc `rc.xml` headers have no "new GPU
    lane".
* **`desc=`** of every GPU and desktop port no longer contains "new GPU lane".
* **Residue:** 28 comment lines in the ports (outside patches and the `newlane` identifiers) still
  say "new GPU lane" or "new lane", for example provenance notes such as "the new lane's Xorg
  driver phxhid". There is no string literal left. The gpu-lane tools copies of `gamewl` are
  scaffolding (P3 §7).

## 5. Bootstrap host packages (A6)

`scripts/bootstrap-linux-host.sh` Tier 1.5 now installs every host tool a recipe checks for. Each
package has a comment citing that check:

| Need | Packages |
|---|---|
| meson ≥ 1.4 (Mesa 26.2) | apt `meson` when its candidate is new enough (26.04: 1.10.x); otherwise `uv tool install "meson>=1.4"` (24.04) |
| Mesa's codegen | `python3-yaml`, `python3-packaging` |
| wayland-scanner **exactly 1.24.0** | `libwayland-bin`. It comes from the host because the wayland port uses `-Dscanner=false`. 26.04 ships 1.24.0 |
| GLib codegen, schemas, icon cache, MIME database | `libglib2.0-dev-bin`, `libglib2.0-bin`, `libgtk-3-bin`, `gtk-update-icon-cache`, `shared-mime-info`, `libxml2-utils` |
| `pngify-icon-theme.py` | `python3-gi`, `gir1.2-gdkpixbuf-2.0`, `librsvg2-common`. The SVG pixbuf loader is only a Recommends, so it must be named |
| `make-sample-pdf.py`, the desktop fonts | `python3-cairo`, `fonts-dejavu-core`, `fontconfig` |
| X core fonts, wlroots' PnP ids | `xfonts-utils`, `hwdata` |
| video_player's demo clips | `ffmpeg`, moved up from the lab tier. Ubuntu's build has libx264, libx265, libvpx-vp9, libopus and aac, all five of which `video_player/port.def.sh` checks for |
| game data | `unzip`, `lhasa`, `7zip`; `p7zip-full` where no `7z`/`7za` exists (24.04) |
| WiFi firmware fetch | curl or wget (already there) |

* The Dockerfile is unchanged. Bootstrap installs the list, and 26.04's apt satisfies every pin
  (`apt-get -s install` resolves all names on this host).
* On a 24.04 host, the uv-installed meson lands in `~/.local/bin`. Bootstrap exports that on its
  own `PATH`, but a Dockerfile `RUN` step after it would not have it; 26.04 (the Dockerfile's
  default) uses apt's meson and is unaffected.
* A 24.04 host cannot build `--with-ports` from apt: wayland-scanner is 1.22, the host python is
  3.12 (the python port needs a 3.14 host python), and GLib is 2.80. Bootstrap warns about the
  first two.

## 6. Validation (static; no build, no Pi cycle)

* `bash -n`: every changed recipe, subr and script. `xmllint --noout`: the 10 changed labwc XML
  files. `py_compile`: `grade-x-desktop-video.py`.
* `port_manager.py validate` of the ports branch: 94 ports.
* `build-port.sh --dry --yaml` of the branch `ports.yaml` in a scratch buildroot (deleted
  afterwards): **75 ports, the same set and USE flags as master's**.
* `user.plo.yaml` is untouched.
* Both sync checks against the branch: gpu-lane 170 files / 50 mappings, wayland 167 / 52, all
  identical. The tools masters mirror the ports' changed files, and `startx-drm` was renamed
  `startx` in both places.
* C files: `stk-launcher.c`, `quake2-launcher.c`, `vkq-drm-launcher.c` and `game-res.c` compile
  with the cross gcc 16 under `-O2 -static -Wall -Wextra -Werror` and the image sysroot. The four
  banner files (`gamedrm_hooks.c`, `stkdrm_hooks.c`, `vkqdrm_hooks.c`, `phxvk_loader.c`) compile
  the same way against the buildroot's SDL and Mesa headers.
* Host tests:
  * the stk launcher's `race` expansion;
  * the vkquake and quake2 launchers' argument sets (`execv` stubbed);
  * `game-res` `GAME_RES_DRYRUN`;
  * `xfce-autostart.sh` with host programs: a timed item, a pause, a background item, an unknown
    item, a missing program, and the SIGTERM stop;
  * the launcher-rewrite guards of `relink-sdl-gl-game.subr` and `supertuxkart_drm`, simulated:
    1 and 3 lines, as required.
* **Inverse control:** the new `check-gpu-stack-image.sh` runs read-only on today's staged rootfs
  (mid-build). Its FAILs are exactly the new names and files, the five old banners, the 11 retired
  names still present in the persistent `_fs` tree, and the desktop apps that the running build had
  not staged yet.

**Not run on hardware:** everything above. The new paths without any HW run are:

* `xfce-autostart.sh`;
* the presets `stk-race`, `quake2-demo`, `vkquake +playdemo`;
* `startx browse` (Dillo on Xorg);
* the servers precheck replacing the start fallbacks;
* `/bin/thunar` as xfdesktop's file-manager fallback.

## 7. Build impact

* **Pair 1** (ports `d7e6e90` + coord `b9c4a58eb`) changes these recipe digests: `xorg_server_drm`,
  `xfce_wayland`, `labwc_desktop`, `sdl2_kmsdrm` (its `games/` and `gamedrm/` files),
  `quakespasm_drm`, `yquake2_drm`, `quake3_drm`, `vkquake_drm`, `supertuxkart_drm`, `vkcube_drm`
  and `video_player`.
  * port_manager cleans each of them and their dependents. Through `sdl2_kmsdrm` that includes
    the engine providers `yquake2`, `quake3` and `supertuxkart`, so STK's full compile runs again,
    plus SDL itself.
  * `video_player` compiles FFmpeg itself (it has no `ffmpeg` dependency), so FFmpeg is
    recompiled too.
  * Mesa, GTK, Poppler and Weston are **not** rebuilt.
  * `game-res` is rebuilt by `build-rootfs-helpers.sh` (coord).
* **Pair 2** (ports `c5384ab` + coord `9e6287ed8`, wording) adds `mesa_drm`, `libdrm_phoenix`,
  `wayland`, `wayland_phoenix`, `libxshmfence_phoenix`, `gtk3_wayland`, `atril_wayland`, `dbus`,
  `weston`, `kmscube_drm` and `libepoxy`.
  * Their closure is effectively the whole GPU and desktop ports stage: Mesa, GLib/GTK, Poppler
    and Atril, and XFCE again.
  * This is the whole cost of the neutral banner. The pair can be merged after pair 1's build, and
    pair 1 is consistent without it.
* **Core:** no core repo changed, so `--scope auto` with the ports is enough. Build with
  `--with-ports --with-showcase` as the integration build did.
* **Before the build**, delete the retired names from the persistent staging tree. Nothing removes
  them, and `check-gpu-stack-image.sh` check 3 fails on each:

  ```
  R=.buildroot/_fs/aarch64a72-generic-rpi4b/root
  rm -f $R/bin/{qs-drm,vkq-drm,stk-drm,startx-drm,startx_gpu,thunar-wl,gdbus-wl,vkcube-drm} \
        $R/usr/bin/{quake2-drm,quake3-drm} $R/usr/lib/xfce-demo/xfce-session
  ```

  `integration/final2` also removes the M7 test pieces (§8). Delete those too:

  ```
  rm -f  $R/bin/{tinywl,labwc-desktop.sh,labwc-desktop-m7c.sh,m7b-colors.sh,dbus-m7f.sh,dbus-m7m.sh,weston-gtk3.sh} \
         $R/etc/dbus-1/session-phoenix-external.conf $R/root/curses_smoke.py
  rm -rf $R/etc/xdg/labwc-m7c
  ```

  The same names are on the live NFS export until a pristine export replaces it.

## 8. Found, not done (for the coordinator / owner)

* ✅ (fixed on main, `9bb075e6f`: the image build stages the fonts into the overlay) **The SD image probably had no DejaVu fonts.** The rootfs overlay has no `usr/share/fonts`, and
  `scripts/stage-desktop-fonts.sh` runs only from `sync-netboot-tree.sh`, yet every desktop
  configuration names DejaVu. Check `usr/share/fonts` in the SD image, and stage the fonts in the
  image build.
* `.claude/settings.json` has dead allowlist entries `./scripts/run-showcase-gate.sh` (deleted
  here) and `./scripts/syntax-check-v3d.sh` (P3). `.claude/skills/rpi4-run/SKILL.md` shows a
  `startx_gpu deskapps` example. These are the owner's permission and skill files and were left
  alone.
* `scripts/make-demo-reel.sh`'s header and `segments=()` describe the old clips, including
  `startx_gpu action`; the showcase plan replaces them.
* `scripts/fetch-quake-data.sh:75` still suggests `p7zip-full`, which 26.04 does not have.
  `BUILD.md` quotes the bootstrap package list verbatim and is now out of date (✅ rewritten on
  `integration/final2`).
* `bin/labwc-desktop.sh` and `/etc/xdg/labwc-m7c` were the M7 milestones' test launcher and
  configuration. **Removed on `integration/final2`**, with tinywl, `m7b-colors.sh`,
  `dbus-m7f.sh`/`dbus-m7m.sh` (and the EXTERNAL-auth bus configuration only they used),
  `weston-gtk3.sh` and `/root/curses_smoke.py`; `check-gpu-stack-image.sh` check 3 asserts
  their absence.
* **Docs** (P4 branch): replace these references:
  * `startx-drm`, `startx_gpu`, `--servers` → `startx`;
  * `vkq-drm`, `stk-drm`, `qs-drm` → the plain names;
  * `vkcube-drm` → `vkcube`;
  * `thunar-wl` → `thunar`;
  * `GAME_ARGS` with spaces → the presets;
  * the S4, S5, S6, S7 and S9 "not scriptable" notes → §2.1 above;
  * the USER-GUIDE TODOs about Dillo and `browse` → `startx browse`.

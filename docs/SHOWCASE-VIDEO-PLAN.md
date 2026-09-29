# Showcase video — re-recording plan

This is the scene list for re-recording the Phoenix-RTOS Raspberry Pi 4 showcase video on the
current image, which has one GPU stack. The previous reel was cut in September 2026 on the
graphics stack that has since been replaced, so every clip in `scripts/make-demo-reel.sh`'s
`segments=()` table is superseded. What each scene shows is described in the
[User Guide](USER-GUIDE.md).

Each scene below gives:

- the exact psh commands, which are the recipes' own commands;
- how long the scene runs and how long to record;
- the capture command;
- whether any part of it cannot be scripted. Those parts are listed as questions for the
  coordinator in [§4](#4-questions-for-the-coordinator).

## 1. How capture works

**Record one clip per scene** with `scripts/record-showcase-clip.sh`:

```
./scripts/record-showcase-clip.sh <label> <secs> "<psh command>" ["<psh command>" ...]
```

- It starts `scripts/record-hdmi.sh --label <label> --secs <secs>` and, in parallel, a
  `test-cycle-psh-interact.sh` cycle with the periodic HDMI snapshots off
  (`RPI4B_HDMI_INTERVAL=0`). The capture card has only one opener, so both cannot hold it.
- The cycle runs with `--wait-secs 220 --inter-cmd-secs 8`.
- **The recording starts before the Pi is powered on**, so every clip contains the whole boot
  (about 60–75 s to `(psh)%` on netboot). `<secs>` must cover the boot plus the scene.
- The output is `artifacts/hdmi-video/<ts>-<label>.mp4`. The logs are
  `artifacts/hdmi-video/<label>.reclog` and `<label>.cyclog`, and the UART log is under the
  cycle label `rec-<label>`.
- **Pacing.** By default, `idle-secs` is `secs + 30` and `max-cmd-secs` is `secs + 60`. That
  suits a single long command. With several commands, set `REC_IDLE_SECS` (around 12) so later
  commands are sent before the clip ends. Otherwise the script warns that they will run after
  the recording.
- **One Pi cycle at a time.** Each clip is one power cycle. Never run two recordings at once,
  and never start one while another cycle or bench holds the Pi.
- **Bash `timeout`** for the call must be at least `(secs + 80) * 1000` ms. For a clip longer
  than about 8 minutes, split the scene.

**Commands while a desktop runs.** psh runs one foreground command at a time.
`/bin/bash /bin/xfce-session` and `/bin/bash /bin/startx` hold the prompt until the session
ends, so any later command in the same clip runs after the desktop has closed. **Everything
that happens inside a desktop must be set up with `export` before the session starts**, using
the knobs below. That is also why each desktop scene sets `HOLD`: the session then ends by
itself, the same way as the Log Out button.

**Assemble the reel** with `scripts/make-demo-reel.sh`:

- It has **no options** apart from an optional output path. The cut list is its `segments=()`
  table, in the form `"<clip basename>|<start s>|<length s>|<label>"`.
- Replace the whole table with the new clips, in the scene order below.
- Measure each `<start>` on the capture rather than by eye. Extract frames with
  `ffmpeg -ss <t> -i <clip>.mp4 -frames:v 1 /tmp/f.png`, and skip the boot and the grabber's
  first ~30 s.
- Update the header comments, which describe the old clips and old frame rates.
- Then check the reel with `scripts/verify-demo-reel.py <reel.mp4>`. It fails a
  segment that is dead, frozen, or cut past the end of its run. Only segments whose label
  starts with `Shell`, `Dillo` or `Boot` may be static, so name static segments that way.

**The lab setup for all clips:**

- the netboot NFS-root image of the merged tree;
- the NFS export restored with `scripts/restore-export-data.sh`, so the lab `/etc/wifi.conf` is
  present and WiFi joins at boot;
- the host AP up (`scripts/radio-ap-up.sh`) for the WiFi scene;
- the host NAT gateway (`scripts/pi-internet-nat.sh`) for the HTTPS scene;
- a USB keyboard and mouse attached.

## 2. Scene list

The durations are the target length on the reel. The record time is the `<secs>` argument.

### S1 — Boot (reel ~15 s)

| | |
|---|---|
| Shows | The kernel log on HDMI: kernel → drivers → the three graphics servers (`V3DA srv ready`, `KMS srv ready`, `SHMSRV srv ready`) → lwIP DHCP → NFS root → `(psh)%` |
| Commands | none needed. Cut it from any clip, e.g. S2's. |
| Label | `Boot — …` (static) |

### S2 — Shell and networking: Ethernet DHCP and WiFi (reel ~25 s)

```
REC_IDLE_SECS=10 ./scripts/record-showcase-clip.sh shell-net 160 \
    "uname -a" "ifconfig" "/bin/wifi status" "ping -c 3 10.43.0.1" "python3 -V"
```

- `ifconfig` shows the Ethernet lease (`en1`) and the WiFi lease (`wl2`).
- `wifi status` shows `joined=1` and the address. Its reference output is the W1 table in
  `docs/misc/2026-09-30-wifi-in-image.md`.
- `ping` goes over the AP's subnet.
- **Optional, a live WiFi join on camera, without typing the PSK:** use the W2 sequence of the
  same doc. Run `mv /etc/wifi.conf /etc/wifi.conf.off`, then `wifi status`,
  `mv /etc/wifi.conf.off /etc/wifi.conf`, and `wifi status` twice. The console shows `leaving`,
  then `joining` / `joined` / `dhcp_start`. **Check that the file is back** before the clip
  ends, and never run `wifi disconnect` on the lab export.
- Label: `Shell — …` (static).

### S3 — The XFCE desktop on Wayland with a windowed Quake III (reel ~40 s)

```
REC_MAX_CMD_SECS=420 ./scripts/record-showcase-clip.sh xfce-q3 330 \
    "export CONF_DIR=/etc/xdg/labwc-xfce-games" \
    "export GAME_LIST=quake3:120" \
    "export GAME_LIST_DELAY=20" \
    "export HOLD=200" \
    "export LOGOUT_CMD=/bin/game-window-quit.sh" \
    "/bin/bash /bin/xfce-session"
```

- **On screen:** the wallpaper and the panel (the Applications menu button, the launchers, the
  task list, the clock, Log Out), Thunar on `/`, and a foot terminal. About 20 s later, Quake
  III Arena appears in a decorated 1280×720 window at (636,40) next to them: q3dm1, the bot
  deathmatch with the orbiting camera, ~90 fps.
- **Grading lines:**
  - `GAME-WINDOW game=quake3 start … driver=wayland` and its `flipstat` lines;
  - no `KMSDRM_*` lines;
  - `XFCE-SESSION done rc=0`.
- Record `secs` = ~75 s boot + ~52 s session start + 20 s delay + 120 s game. Keep the
  `export` count low: each costs `--inter-cmd-secs` 8 s. The five exports above add about 40 s
  before the session command.
- **Not scriptable here:** opening the Applications menu on camera (Q1).
- Label: `XFCE 4.20 on Wayland — …`.

### S4 — SuperTuxKart in a window (reel ~25 s)

Same as S3 with `GAME_LIST=stk:150` and the label `xfce-stk`:

- The SuperTuxKart main menu appears in a window, at ~90 fps.
- **An AI race in a window** needs the race arguments: `GAME_ARGS="--windowed
  --screensize=1280x720 --track=hacienda --numkarts=4 --profile-laps=2"`. `GAME_ARGS`
  replaces the per-game defaults, one word per argument.
- psh passes the quotes through, so that `export` will not work as written (Q2). If it cannot
  be done, show the menu, which animates by itself.

### S5 — The PDF reader (reel ~15 s)

- **Not scriptable with the image as built (Q3).** No session knob starts Atril, and Office →
  Atril needs a mouse on the menu.
- By hand, in any desktop session: **Office → Atril Document Viewer**, or run
  `atril /usr/share/doc/phoenix/sample.pdf` in foot. Then show the full-screen and presentation
  modes (`atril --fullscreen …`, `atril --presentation …`).
- Label: `Atril — …`. A still page fails `verify-demo-reel.py`'s motion check unless
  `Atril` is added to its `STATIC_OK` list, or the scene pages through the document.

### S6 — The video player, windowed then full screen (reel ~30 s)

```
REC_MAX_CMD_SECS=360 ./scripts/record-showcase-clip.sh xfce-video 280 \
    "export CONF_DIR=/etc/xdg/labwc-xfce-video" \
    "export VIDEO_DELAY=30" \
    "export FFPLAY_AUTOKEYS=12:fs,24:fs" \
    "export HOLD=140" \
    "/bin/bash /bin/xfce-session"
```

- **On screen:** the desktop, then after 30 s the H.264 720p demo clip plays in a window next
  to Thunar. At +12 s ffplay goes full screen and at +24 s it returns to the window. The clip
  ends by itself (`-autoexit`).
- `VIDEO_CLIP=/usr/share/video-demo/hevc-720p30-aac.mp4` shows HEVC instead.
- `VIDEO_PLAYER=gtk` shows gtk-video, but its full screen is CPU-bound, so keep ffplay for this
  scene.
- **Grading lines:** `VIDEO-PLAY start mode=wl … autokeys=12:fs,24:fs`, the `ffplay-stat`
  lines (`fs=0` → `fs=1 win=1920x1080` → `fs=0`), and `VIDEO-PLAY done rc=0`.
- **Check:** that `FFPLAY_AUTOKEYS` reaches ffplay through labwc's autostart. The games
  session passes its knobs the same way (Q4).
- Label: `Video — …`.

### S7 — Full-screen games at their best settings (reel ~20 s each)

One clip per game. Every game starts at the psh prompt, with no desktop running:

| Clip label | Command | Record secs | Shows |
|---|---|---|---|
| `fs-q3` | `quake3 +map q3dm1` | 240 | Quake III bot deathmatch, orbiting third-person camera, ~59 fps |
| `fs-q2` | `quake2` | 200 | Quake II playing `demo1`, 60 fps |
| `fs-qs` | `quakespasm` | 200 | QuakeSpasm attract-demo loop, ~44 fps |
| `fs-vkq` | `vkquake` | 240 | vkQuake on Vulkan, the start map, ~43 fps. The camera is static unless a demo plays (Q5). |
| `fs-stk` | `game-res stk 1280x720 --track=hacienda --numkarts=4 --profile-laps=2` | 330 | SuperTuxKart: a 4-kart AI race, 720p scaled to the screen, ~22 fps |

```
./scripts/record-showcase-clip.sh fs-q3 240 "quake3 +map q3dm1"
```

- Grade each clip by its `<name> flipstat … fps` lines, not by the game's own counter.
- SuperTuxKart needs the longest window: in the P1 gate it was still racing when a 240 s
  capture closed.
- Labels: `Quake III Arena — …`, and so on.

### S8 — X11: Xorg + glamor with Window Maker (reel ~25 s)

```
REC_IDLE_SECS=12 REC_MAX_CMD_SECS=330 ./scripts/record-showcase-clip.sh x11 320 \
    "export HOLD=200" "/bin/bash /bin/startx action"
```

- **On screen:** Window Maker (dock and clip), the spinning "Phoenix V3D GL" window (EGL on X11
  through DRI3/Present, 60 fps), an xterm with Conway's Life in Python, xclock, xbill, and an
  xterm with `top`. Everything animates by itself.
- **Grading lines:** `XDRM start mode=action`, `XDRM done rc=0 reason=hold-done`, and 0 faults.
- `scripts/grade-x-desktop-video.py` grades the recording for the known X artefacts.
- Label: `X11 — …`.

### S9 — Dillo loading a live HTTPS page (reel ~13 s)

- **Not scriptable with the image as built (Q6).** `startx` has only the `action` and `wmaker`
  modes, and Dillo would have to be typed into an xterm.
- By hand: `ntpclient -s pool.ntp.org` at psh first, then `/bin/bash /bin/startx wmaker` →
  right-click → XTerm → `dillo https://example.com`. This needs the NAT gateway.
- Label: `Dillo — …` (static).

**Reel order:** S1 boot → S2 shell and networking → S3 XFCE with Quake III → S4 SuperTuxKart
windowed → S5 Atril → S6 video windowed then full screen → S9 Dillo → S8 X11 → S7 full-screen
games (Quake III, Quake II, QuakeSpasm, vkQuake, SuperTuxKart). That is about 5 minutes.

## 3. Before recording

1. The first showcase gate of the merged image must have passed:
   `scripts/run-showcase-gate-drm.sh`, plus the pre-registered checks of
   [desktop-apps-ports.md §4](gpu-new-lane/desktop-apps-ports.md) (a)–(c) and WiFi cycle W1. The
   recordings are not a test.
2. Warm-up is not needed: there is no shader disk cache. For the same reason, the first frame of
   every game takes as long in every clip. Budget vkQuake's shader compile.
3. Run GPU clips with the AP down (`scripts/radio-ap-down.sh`), except S2. A joined WiFi netif
   polls, and the bench conditions of the fps numbers were measured without it.

## 4. Questions for the coordinator

| # | Scene | Question |
|---|---|---|
| Q1 | S3, S4, S5 | **Scripted input inside XFCE.** Nothing in the tree drives the pointer or keyboard of a Wayland session (no `wtype`/`ydotool`-style tool, and no libinput injection). So opening the Applications menu, the Games / Multimedia / Office submenus and a desktop right-click cannot be recorded unattended. Either the owner performs them at the Pi's keyboard and mouse during a recording, or a small input-injection tool is needed. Which? |
| Q2 | S4 | `GAME_ARGS` with spaces cannot be set from psh, which passes quotes literally. Should the games session get a `stk-race` game name (or a `GAME_ARGS_<game>` file) so an AI race can run in a window? |
| Q3 | S5 | No session knob opens Atril. Should the image get an `ATRIL_FILE=<pdf>` knob in the session autostart (like `VIDEO_CLIP`), or should this scene be recorded by hand? |
| Q4 | S6 | Is `FFPLAY_AUTOKEYS` passed from psh through `xfce-session` and labwc's autostart to ffplay? The games session relies on the same inheritance for `GAME_LIST`, but the video autostart has not been run with it. |
| Q5 | S7 | vkQuake's launcher always adds `+map start`, which is a static view. Does `vkquake +playdemo demo1` (appended after `+map start`) play a demo on the current build? It has not been tried. If it does not, is a demo config staged in `id1/` acceptable? |
| Q6 | S9 | `startx` lost the old launcher's `browse [url]` mode. Should `startx-drm` get a `browse` mode (Window Maker + Dillo on a URL) for this scene? Dillo has also not yet been run on the Xorg desktop. |
| Q7 | all | Is the reel still recorded on the netboot NFS root, or on the SD image? On the SD image, WiFi joins only after a `wifi connect` typed by hand, because the image ships no `/etc/wifi.conf`. |

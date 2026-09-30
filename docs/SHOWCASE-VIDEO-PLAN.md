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
- whether any part of it cannot be scripted. Only one part cannot: opening the menus on
  camera ([§4](#4-what-is-not-scripted-and-what-has-not-run-on-the-pi)).

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
- **Pacing.** Each command ends after `idle-secs` of UART silence or after `max-cmd-secs`
  (`scripts/psh-interact.py`), and the same two values apply to every command. By default they
  are `secs + 30` and `secs + 60`, which suits a single long command.
  - **An `export` prints nothing**, so it always waits the full `idle-secs`. Put all the
    variables of a scene in **one** `export` command: psh's `export` takes several
    `NAME=value` words (`psh/pshapp/env.c`).
  - `xfce-session` and `startx` print a heartbeat every 10 s while `HOLD` runs, so
    `REC_IDLE_SECS=60` is safe for them. This is the setting of the M8 windowed-game cycles that
    passed. It is also short enough that the one `export` costs about 70 s.
  - For a scene of several short commands (S2), use `REC_IDLE_SECS=10`.
  - The script warns when `idle-secs` × the command count exceeds the recording.
- **One Pi cycle at a time.** Each clip is one power cycle. Never run two recordings at once,
  and never start one while another cycle or bench holds the Pi.
- **Bash `timeout`** for the call must be at least `(secs + 80) * 1000` ms. For a clip longer
  than about 8 minutes, split the scene.

**Commands while a desktop runs.** psh runs one foreground command at a time.
`/bin/bash /bin/xfce-session` and `/bin/bash /bin/startx` hold the prompt until the session
ends, so any later command in the same clip runs after the desktop has closed. **Everything
that happens inside a desktop is set up with `export` before the session starts**:

- `XFCE_AUTOSTART` opens programs on the XFCE desktop in order, each for a set time
  ([User Guide §5](USER-GUIDE.md#open-programs-by-themselves)). Its UART lines start with
  `XFCE-AUTOSTART ` (`open`, `closed`, `stop`, `done`).
- `GAME_LIST` (the games session) runs games in windows; `GAME_LIST=none` runs none, so
  `XFCE_AUTOSTART` alone decides what opens.
- `FFPLAY_AUTOKEYS` presses ffplay's keys at set times.
- `HOLD` ends each session by itself, the same way as the Log Out button.

psh's `export` cannot give a value with spaces (it passes quotes through), and none of these
knobs needs one.

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

- the netboot NFS-root image (`--variant nfsroot`) of the final tree. On the SD image WiFi
  joins only after one `wifi connect` typed at psh, and the `netboot` variant does not start
  WiFi at all;
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
REC_IDLE_SECS=10 ./scripts/record-showcase-clip.sh shell-net 200 \
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

### S3 — The XFCE desktop on Wayland: Atril, a windowed Quake III, a video (reel ~50 s)

One session with the games configuration, which places the game and the players at the right
of Thunar and foot:

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=330 ./scripts/record-showcase-clip.sh xfce-apps 420 \
    "export CONF_DIR=/etc/xdg/labwc-xfce-games GAME_LIST=none XFCE_AUTOSTART=atril:30,quake3:90,video=/usr/share/video-demo/h264-720p30-aac.mp4 FFPLAY_AUTOKEYS=12:fs,24:fs HOLD=240" \
    "/bin/bash /bin/xfce-session"
```

- **On screen:** the wallpaper and the panel (the Applications menu button, the launchers, the
  task list, the clock, Log Out), Thunar on `/` and a foot terminal. Then, one after another:
  Atril on the sample PDF (30 s); Quake III Arena in a decorated 1280×720 window at (636,40),
  q3dm1 with the bot deathmatch and the orbiting camera, ~70 fps (90 s); the H.264 720p clip in
  a window, full screen at +12 s and back in the window at +24 s.
- **Grading lines:**
  - `XFCE-AUTOSTART open atril`, `… closed`, `open quake3`, `open video`;
  - `GAME-WINDOW game=quake3 start … driver=wayland` and its `flipstat` lines, no `KMSDRM_*`
    lines;
  - `VIDEO-PLAY start mode=wl … autokeys=12:fs,24:fs` and the `ffplay-stat` lines (`fs=0` →
    `fs=1 win=1920x1080` → `fs=0`);
  - `XFCE-SESSION done rc=0`.
- Record `secs` = ~75 s boot + ~70 s for the `export` + ~20 s session start + the 240 s `HOLD`
  + teardown ≈ 420 s. The Bash `timeout` must be at least 500 000 ms.
- Label: `XFCE 4.20 on Wayland — …`. Cut Atril's part as its own segment (`Atril — …`) if it
  pages too little for `verify-demo-reel.py`'s motion check; `atril-pres` (a presentation)
  moves more.

### S4 — SuperTuxKart racing in a window (reel ~25 s)

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=330 ./scripts/record-showcase-clip.sh xfce-stk 430 \
    "export CONF_DIR=/etc/xdg/labwc-xfce-games GAME_LIST=stk-race:150 GAME_LIST_DELAY=20 HOLD=200 LOGOUT_CMD=/bin/game-window-quit.sh" \
    "/bin/bash /bin/xfce-session"
```

- **On screen:** the desktop, then about 20 s later SuperTuxKart in a window: four AI karts race
  two laps on hacienda, and the game exits by itself.
- **Grading lines:** `GAME-WINDOW game=stk-race start … driver=wayland`, its `flipstat` lines,
  `XFCE-SESSION done rc=0`.
- Label: `SuperTuxKart — …`.

### S5 — The menus (reel ~15 s, by hand)

Opening the Applications menu and its submenus (Games, Multimedia, Office) needs a person at the
Pi's mouse: nothing on the image injects pointer input into a Wayland session.

- Record one short clip with the owner at the keyboard and mouse:
  `REC_IDLE_SECS=60 ./scripts/record-showcase-clip.sh xfce-menu 300 "export HOLD=120" "/bin/bash /bin/xfce-session"`.
- Open **Applications → Games**, **Multimedia** and **Office**, and right-click the wallpaper.
- Label: `XFCE 4.20 on Wayland — the menus`.

### S6 — The video player, windowed then full screen (reel ~30 s)

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=260 ./scripts/record-showcase-clip.sh xfce-video 340 \
    "export CONF_DIR=/etc/xdg/labwc-xfce-video VIDEO_DELAY=30 FFPLAY_AUTOKEYS=12:fs,24:fs HOLD=140" \
    "/bin/bash /bin/xfce-session"
```

- **On screen:** the desktop, then after 30 s the H.264 720p demo clip plays in a window next
  to Thunar, at 30 fps. At +12 s ffplay goes full screen and at +24 s it returns to the window.
  The clip ends by itself (`-autoexit`).
- `VIDEO_CLIP=/usr/share/video-demo/hevc-720p30-aac.mp4` shows HEVC instead.
- `VIDEO_PLAYER=gtk` shows gtk-video, but its full screen is CPU-bound, so keep ffplay for this
  scene.
- **Grading lines:** `VIDEO-PLAY start mode=wl … autokeys=12:fs,24:fs` (it prints the value it
  received, so the log shows that `FFPLAY_AUTOKEYS` reached it), the `ffplay-stat` lines
  (`fs=0` → `fs=1 win=1920x1080` → `fs=0`), and `VIDEO-PLAY done rc=0`.
- Skip this scene if S3 already shows the video well enough.
- Label: `Video — …`.

### S7 — Full-screen games at their best settings (reel ~20 s each)

One clip per game. Every game starts at the psh prompt, with no desktop running:

| Clip label | Command | Record secs | Shows |
|---|---|---|---|
| `fs-q3` | `quake3 +map q3dm1` | 240 | Quake III bot deathmatch, orbiting third-person camera, ~59 fps |
| `fs-q2` | `quake2 +demomap q2demo1.dm2` | 200 | Quake II playing the demo pak's recorded demo, 60 fps |
| `fs-qs` | `quakespasm` | 200 | QuakeSpasm attract-demo loop, ~44 fps |
| `fs-vkq` | `vkquake +playdemo demo1` | 240 | vkQuake on Vulkan playing a recorded demo, ≈ 39–42 fps |
| `fs-stk` | `game-res stk 1280x720 race` | 330 | SuperTuxKart: a 4-kart AI race, 720p scaled to the screen, ~22 fps |

```
./scripts/record-showcase-clip.sh fs-q3 240 "quake3 +map q3dm1"
```

- Grade each clip by its `<name> flipstat … fps` lines, not by the game's own counter.
- SuperTuxKart needs the longest window: in the gate it was still racing when a 240 s capture
  closed. `race` ends the game by itself after two laps.
- Labels: `Quake III Arena — …`, and so on.

### S8 — X11: Xorg + glamor with Window Maker (reel ~25 s)

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=300 ./scripts/record-showcase-clip.sh x11 420 \
    "export HOLD=200" "/bin/bash /bin/startx action"
```

- **On screen:** Window Maker (dock and clip), the spinning "Phoenix V3D GL" window (EGL on X11
  through DRI3/Present, 60 fps), an xterm with Conway's Life in Python, xclock, xbill, and an
  xterm with `top`. Everything animates by itself.
- **Grading lines:** `XDRM start mode=action`, `XDRM done rc=0 reason=hold-done`, and 0 faults.
- `scripts/grade-x-desktop-video.py` grades the recording for the known X artefacts.
- Label: `X11 — …`.

### S9 — Dillo loading a live HTTPS page (reel ~13 s)

```
REC_IDLE_SECS=30 REC_MAX_CMD_SECS=240 ./scripts/record-showcase-clip.sh dillo 330 \
    "ntpclient -s pool.ntp.org" "export HOLD=120" "/bin/bash /bin/startx browse https://example.com"
```

- **On screen:** Window Maker with Dillo (1780×980 at (40,40)) loading the page over HTTPS.
- Needs the NAT gateway (`scripts/pi-internet-nat.sh`); the clock must be set for the
  certificate check, hence `ntpclient` first.
- **Grading lines:** `XDRM start mode=browse`, `XDRM done rc=0 reason=hold-done`.
- Label: `Dillo — …` (static).

**Reel order:** S1 boot → S2 shell and networking → S3 XFCE with Atril, Quake III and a video →
S5 the menus → S4 SuperTuxKart windowed → S6 video windowed then full screen (optional) → S9
Dillo → S8 X11 → S7 full-screen games (Quake III, Quake II, QuakeSpasm, vkQuake,
SuperTuxKart). That is about 5 minutes.

## 3. Before recording

1. The image of the final tree must have passed its gate first: `scripts/run-showcase-gate.sh`
   (the previous final image passed 7/7 with WiFi joined, WiFi W1 and the windowed games,
   MIGRATION §7s). The recordings are not a test.
2. Warm-up is not needed: there is no shader disk cache. For the same reason, the first frame of
   every game takes as long in every clip. Budget vkQuake's shader compile.
3. Run GPU clips with the AP down (`scripts/radio-ap-down.sh`), except S2. A joined WiFi netif
   polls: vkQuake measured 38.7 fps with WiFi joined against 42.2 before.

## 4. What is not scripted, and what has not run on the Pi

- **The menus (S5)** need a person at the mouse. No input-injection tool was added: the panel's
  `--plugin-event=applicationsmenu:popup` first takes a seat grab that is unlikely to succeed on
  Wayland.
- **Not yet run on the Pi** (host-tested only, polish-final.md §6): `XFCE_AUTOSTART` (S3),
  `GAME_LIST=none` and `stk-race` (S3, S4), `quake2 +demomap q2demo1.dm2` and
  `vkquake +playdemo demo1` (S7), `startx browse` (S9). A rehearsal clip of each scene before
  the real recording is cheap; grade it by the lines listed with the scene.

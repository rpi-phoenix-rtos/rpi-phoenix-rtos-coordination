# Phoenix-RTOS on the Raspberry Pi 4 — User Guide

This guide is for someone with a Raspberry Pi 4 running the Phoenix-RTOS image. It covers
booting the image, the XFCE desktop on Wayland, the X11 desktop, the games, the video
player, the PDF reader, networking (Ethernet and WiFi), audio and input. It ends with the
[showcase](#9-the-showcase--the-best-setup-in-one-sitting): one recommended order for showing
the whole system.

To build the image, see [BUILD.md](BUILD.md) for an SD card, or
[TUTORIAL-NETBOOT.md](../TUTORIAL-NETBOOT.md) to boot over the network. For open bugs, see
[KNOWN-ISSUES.md](KNOWN-ISSUES.md).

**Contents**

1. [What you need](#1-what-you-need)
2. [Booting](#2-booting)
3. [The shell](#3-the-shell)
4. [The graphics stack](#4-the-graphics-stack)
5. [The XFCE desktop (Wayland)](#5-the-xfce-desktop-wayland)
6. [Games](#6-games)
7. [Video, PDF, X11 and the other applications](#7-video-pdf-x11-and-the-other-applications)
8. [Networking, audio and input](#8-networking-audio-and-input)
9. [The showcase — the best setup in one sitting](#9-the-showcase--the-best-setup-in-one-sitting)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. What you need

- A **Raspberry Pi 4 Model B with 4 GB RAM**. It is the only board the port is validated on.
  The 2 GB and 8 GB boards are known to be mis-mapped (KNOWN-ISSUES P1), so do not use them.
- A **1920×1080 HDMI display** on the micro-HDMI port **nearest the USB-C jack**.
- A **USB keyboard** and a **USB mouse**. Plug them in before power-on.
- **Wired Ethernet** with a DHCP server on the network. WiFi also works (see
  [§8.2](#82-wifi)).
- Optional: a 3.3 V USB-serial adapter on GPIO 14/15 (115200 8N1). The boot log and the shell
  appear there too.
- Optional: headphones or powered speakers on the 3.5 mm jack.

## 2. Booting

There are two ways to boot. Both give the same programs and the same commands.

| | SD card | Network (netboot + NFS root) |
|---|---|---|
| What you build | `./scripts/rebuild-rpi4b-fast.sh --variant sd --with-showcase --with-ports` | `./scripts/rebuild-rpi4b-fast.sh --variant nfsroot --with-showcase --with-ports` |
| Where `/` lives | the ext2 partition of the card | an NFS export on your build host |
| Setup | flash the card ([BUILD.md](BUILD.md) steps 4–5) | a host with DHCP + TFTP + NFS ([TUTORIAL-NETBOOT.md](../TUTORIAL-NETBOOT.md)) |
| WiFi | yes | yes |
| Good for | a stand-alone Pi, demos | development: rebuild and reboot in minutes |

The Docker build in the [README](../README.md#build-with-docker-reproducible-any-host-os)
produces the same SD image without a Linux build host.

After power-on:

1. The kernel log scrolls on the HDMI screen. After about 50 seconds the `(psh)%` prompt
   appears.
2. During boot, the system starts the drivers, the network (DHCP on Ethernet), the WiFi
   daemon and the three graphics servers (see [§4](#4-the-graphics-stack)). Their ready lines
   (`V3DA srv ready`, `KMS srv ready`, `SHMSRV srv ready`) can print just after the first
   prompt. This is normal.
3. Type at the prompt with the USB keyboard.

## 3. The shell

The prompt is **psh**, Phoenix's own shell. It is simpler than a Unix shell:

- **No pipes (`|`), no redirection (`>`), no `;` or `&&`, and no background `&`.** Run one
  command per line.
- **Quotes are passed through literally.** `echo 'a b'` prints `'a b'`.
- **`export NAME=value`** sets an environment variable for the programs you start later. Many
  launchers below read such variables.
- `PATH` is `/bin:/usr/bin:/sbin:/usr/sbin`, so programs can be run by name (`quakespasm`,
  `wifi`, `python3`).
- **Shell scripts run through bash:** `/bin/bash /bin/xfce-session`. This is the tested way to
  start the desktop, X11 and the video player. Every command in this guide uses it where it
  applies.
  <!-- TODO(coordinator): starting a `#!` script directly from psh (`/bin/xfce-session`,
  `startx`, `video-play`) depends on libphoenix execve's `#!` support, which has never been
  checked from psh on the UART (MIGRATION §7.3). If a bench row confirms it, the guide can
  drop the `/bin/bash` prefix. -->

For a full interactive shell, run `bash`. GNU bash 5.2 supports pipes, loops and command
substitution. `exit` returns to psh.

Useful psh commands: `ls`, `cat`, `ps`, `top`, `kill <pid>`, `mem`, `df`, `uname -a`,
`dmesg`, `ifconfig`, `ping`, `nslookup`, `wget`, `ntpclient`, `reboot`.

## 4. The graphics stack

The Pi 4 has one graphics stack. It has the same layers as a Linux desktop:

| Layer | What it is |
|---|---|
| **`rpi4-v3d-async`** (`/dev/v3d-async`) | The render server. It owns the V3D 4.2 GPU and runs the render jobs of every client asynchronously, with fences and sync objects. |
| **`rpi4-kms`** (`/dev/kms`) | The display server. It drives the HDMI output through the firmware's display planes, with atomic page flips and vblank events. Full-screen games can ask it for lower resolutions, which the display hardware scales to 1080p. |
| **`shmsrv`** (`/shm`) | Shared memory for Wayland and X clients (`shm_open`, `memfd_create`). |
| **libdrm** | The standard libdrm API, with a Phoenix backend that talks to the two servers above. |
| **Mesa 26.2** | GBM, EGL, OpenGL ES 3.1, desktop OpenGL and **Vulkan (V3DV)** on the V3D. |
| **SDL 2.30** | KMSDRM (full screen) and **Wayland** (a window on the desktop) video drivers. |
| **Xorg 21.1** | The `modesetting` driver with **glamor** GPU acceleration, and DRI3/Present for GL windows. |
| **labwc 0.20** | The Wayland compositor. It composites on the GPU (GLES2). |
| **XFCE 4.20** | The desktop: panel, wallpaper, Thunar, settings, application finder. |
| **GTK 3.24** | The toolkit for the XFCE programs, Atril and the GTK video player. |

The three servers start at boot, so every program in this guide works right after the prompt
appears.

**One binary per program.** Each game and the video player is a single program that works in
two modes:

- Started from psh (no desktop running), it opens **full screen** on KMS.
- Started inside the desktop, it opens **in a window**.

---

## 5. The XFCE desktop (Wayland)

### Start it

```
/bin/bash /bin/xfce-session
```

After about 50 seconds the XFCE 4.20 desktop appears: the wallpaper, the panel at the top and
a Thunar window. The session runs until you log out. Then it stops cleanly and returns to
`(psh)%`. The graphics servers stay up, so a second `xfce-session` starts faster.

<!-- TODO(coordinator): the ~50 s start time is from the gate's `session up panel=registered
t=52` (MIGRATION §7r, P1 image). Re-read it on the merged image. -->

### What is on it

- **The panel**, from left to right: the **Applications menu**, launchers for the **terminal
  (foot)**, the **file manager (Thunar)** and the **application finder**, the **task list** (one
  button per window), the **clock** (local time) and the **Log Out** button.
- **Thunar**, the file manager. It opens on `/` when the session starts.
- **foot**, the terminal. It runs bash, with the same programs as psh.
- **The Applications menu**:

  | Menu | Entries |
  |---|---|
  | **Games** | Quake, Quake II, Quake III Arena, SuperTuxKart. Each opens in a 1280×720 window (see [§6.2](#62-in-a-window-on-the-desktop)). |
  | **Multimedia** | Video Player (gtk-video), Video Demo (plays a demo clip in a window) |
  | **Office** | Atril Document Viewer |
  | **System / Accessories** | Foot, Bash, Midnight Commander, File Manager, Settings Manager, Appearance, Application Finder, Run Program |

  <!-- TODO(coordinator): the category names of the Foot/Bash/mc/Thunar/Settings entries come
  from their .desktop files (garcon's xfce-applications.menu). Check the exact submenu names on
  the HDMI screen. -->

### Keyboard shortcuts

| Keys | Action |
|---|---|
| Super (tap) | application finder |
| Super+Return | a new terminal (foot) |
| Super+E | a new file-manager window (Thunar) |
| Super+D, Alt+F2 | application finder |
| Super+Space | fuzzel launcher |
| right click on the wallpaper | the desktop menu |

The keyboard layout is US.

### Log out

Click **Log Out** at the right end of the panel. labwc's root menu has Log Out too. The
desktop closes and psh comes back.

### Options

Set these with `export NAME=value` before starting the session:

| Variable | Default | Effect |
|---|---|---|
| `HOLD` | `0` | `0` = run until Log Out. `N` = log out by itself after N seconds (for unattended demos). |
| `RENDERER` | `gles2` | labwc's renderer: `gles2` composites on the GPU, `pixman` on the CPU |
| `TZ` | `CET-1CEST,M3.5.0,M10.5.0/3` | the time zone of the panel clock and file dates (POSIX TZ string) |
| `THUNAR_START` | `1` | `0` = no Thunar window at start |
| `CONF_DIR` | the default desktop | `/etc/xdg/labwc-xfce-games`: the games session ([§6.2](#62-in-a-window-on-the-desktop)). `/etc/xdg/labwc-xfce-video`: the video session ([§7.1](#71-video)). |

---

## 6. Games

Five 3D games are on the image. All of them render on the V3D GPU, and all their data is
included:

- Quake (the shareware episode)
- Quake II (the demo)
- Quake III Arena (the demo, plus QVMs built from ioquake3, so no retail files or CD key are
  needed)
- vkQuake (Quake on Vulkan)
- SuperTuxKart 1.4

### 6.1 Full screen from psh

Run these at the `(psh)%` prompt, with no desktop running:

| Game | Command | Measured on the Pi |
|---|---|---|
| **Quake** (QuakeSpasm, OpenGL) | `quakespasm` | ~44 fps at 1920×1080 |
| **Quake II** (yQuake2, OpenGL) | `quake2` | 60 fps (vsync), starts on the first level of the demo |
| **Quake III Arena** (quake3e, OpenGL) | `quake3 +map q3dm1` | ~59 fps, a bot deathmatch with an orbiting camera |
| **vkQuake** (Quake on Vulkan) | `vkquake` | ~42–44 fps, the start map |
| **SuperTuxKart 1.4** (OpenGL ES 3) | `stk` | ~12 fps at 1920×1080 (~22 fps at 1280×720, see [§6.3](#63-lower-resolution-for-more-fps)) |

The fps figures come from the image gate of 2026-09-29 (MIGRATION §7r). They are measured at
the page flip, not read from the game's own counter.

<!-- TODO(coordinator): these medians are from the P1 image. The merged image links every GL
game dual-mode (KMSDRM + Wayland). Re-read them from its first showcase gate
(`run-showcase-gate-drm.sh`), and update the table if they moved. -->

- **Quake II and Quake III** first copy their data into a RAM disk (`/tmp`), then start. The
  first start takes a few seconds longer.
- **SuperTuxKart:** `stk` opens the main menu and you drive. For a race with no input, run
  `stk --track=hacienda --numkarts=4 --profile-laps=2` (four AI karts, two laps). To skip the
  start screen and drive yourself, run `stk -N --track=olivermath`.
- **Quitting:**
  - Quake, vkQuake, Quake II: press `` ` `` to open the console and type `quit`, or use Esc →
    Quit.
  - Quake III: Esc → Exit.
  - SuperTuxKart: Esc → Quit.

  The display returns to the psh console.
- **Shaders are compiled at every start.** The image has no on-disk shader cache, so each game
  compiles its shaders on the GPU before its first frame. vkQuake takes the longest: expect
  some seconds of black screen before the menu.
  <!-- TODO(coordinator): measure vkquake-drm's `first present N ms after start` on the merged
  image and put the number here. mesa_drm is configured -Dshader-cache=disabled. -->

### 6.2 In a window on the desktop

Inside XFCE, open **Applications → Games** and pick a game. It opens in a **1280×720 window**
next to your other windows, with a title bar, keyboard and mouse. The same games run from a
foot terminal:

```
/bin/bash /bin/game-window.sh quake3
```

The game names are `quakespasm`, `quake2`, `quake3` and `stk`. Only one game window runs at a
time. For another size, run `export GAME_W=1600` and `export GAME_H=900` first (in foot:
`GAME_W=1600 GAME_H=900 /bin/bash /bin/game-window.sh stk`).

Measured in a window on the XFCE desktop (M8, 2026-09-28):

- Quake II: 60 fps
- Quake III: ~90 fps
- QuakeSpasm: 45–67 fps
- SuperTuxKart: ~90 fps on its menu

**vkQuake runs full screen only.** It has no desktop entry: a Vulkan window on Wayland needs
Vulkan's Wayland surface support, which this build does not have.

**The games session** starts a list of games in windows by itself, one after another. It is
useful for an unattended demo:

```
export CONF_DIR=/etc/xdg/labwc-xfce-games
export GAME_LIST=quake3:60,stk:90
/bin/bash /bin/xfce-session
```

Each item of `GAME_LIST` is `<game>:<seconds>`. The default is `quakespasm`, which runs until
the session ends. The first game starts `GAME_LIST_DELAY` seconds (default 15) after the
desktop. The session also opens a foot terminal.

### 6.3 Lower resolution for more fps

`game-res` starts a game full screen in a lower mode. The display hardware scales the picture
to the full screen:

```
game-res stk 1280x720 --track=hacienda --numkarts=4 --profile-laps=2
```

- **Game names:** `stk`, `qs` (Quake), `q2`, `q3` and `vkq`. Any arguments after the size go to
  the game.
- **Modes:** 1920×1080, 1600×900, 1440×1080, 1280×720, 1024×768, 960×540, 800×600 and
  640×480. A 4:3 mode is shown centred with black bars.
- **When to use it:** SuperTuxKart is limited by the GPU. At **1280×720 it runs at ~22 fps**,
  against ~12 at 1080p (M9: 22.3 vs 11.9). Below 720p the CPU becomes the limit (960×540:
  ~24 fps). The Quakes already run at or near 60 fps at 1080p.
- `export GAME_RES=1280x720` sets a default size for later `game-res` commands.

---

## 7. Video, PDF, X11 and the other applications

### 7.1 Video

The player is **ffplay** (FFmpeg 6.1) with SDL. Start it with the `video-play` launcher, which
picks the mode for you:

```
/bin/bash /bin/video-play /usr/share/video-demo/h264-720p30-aac.mp4
```

- **From psh**, the video plays full screen and returns to the prompt at the end.
- **From a desktop terminal**, it plays in a window.
- In the desktop menu, **Multimedia → Video Demo** plays the same clip in a window.

**Keys:**

| Key | Action |
|---|---|
| space or p | pause |
| ← / → | seek back / forward 10 s |
| ↓ / ↑ | seek back / forward 60 s |
| f | toggle full screen |
| 9 / 0 | volume down / up |
| m | mute |
| q or Esc | quit |

A double click toggles full screen. Resizing the window scales the video.

**The demo clips** in `/usr/share/video-demo/`:

| File | Content | Measured on the Pi |
|---|---|---|
| `h264-720p30-aac.mp4` | H.264 1280×720 30 fps + AAC, 45 s | 30 fps, in a window and full screen |
| `h264-1080p30-aac.mp4` | H.264 1920×1080 30 fps + AAC, 30 s | may drop frames (the CPU decodes it) |
| `hevc-720p30-aac.mp4` | HEVC (H.265) 1280×720 30 fps + AAC, 30 s | 30 fps, decoded on the CPU |
| `vp9-360p-opus.webm` | VP9 640×360 + Opus, 20 s | real time |

**Supported formats:**

- Video: H.264, HEVC (H.265), VP8, VP9, MPEG-4 Part 2, MJPEG, raw video.
- Audio: AAC, MP3, Opus, Vorbis, FLAC, PCM.
- Containers: MP4/MOV, Matroska/WebM, MPEG-TS, AVI, Ogg, WAV, and raw H.264/HEVC streams.

The player decodes on the CPU, with 4 threads.

**Options** (set with `export`):

- `VIDEO_MODE=wl|drm` forces the window or full-screen mode.
- `FS=1` starts full screen.
- `LOOP=0` loops forever.
- `AUTOEXIT=0` stays on the last frame until you press q.

**gtk-video** is a small GTK 3 player with a toolbar: open, play/pause, stop, a seek bar and
full screen. Open it from **Multimedia → Video Player**, or from foot:

```
gtk-video /usr/share/video-demo/h264-720p30-aac.mp4
```

It plays 720p at 26–30 fps in a window. Its full screen is CPU-bound (14–17 fps), so use
`video-play` to show a video full screen.

**The video session** plays a clip in a window on the desktop by itself, after a delay:

```
export CONF_DIR=/etc/xdg/labwc-xfce-video
export VIDEO_CLIP=/usr/share/video-demo/hevc-720p30-aac.mp4
/bin/bash /bin/xfce-session
```

`VIDEO_DELAY` sets the delay in seconds (default 45). `VIDEO_PLAYER=gtk` uses gtk-video
instead of ffplay.

### 7.2 PDF — Atril

**Atril** is MATE's document viewer, with Poppler. Open it from **Office → Atril Document
Viewer**, or from foot:

```
atril /usr/share/doc/phoenix/sample.pdf
```

`/usr/share/doc/phoenix/sample.pdf` is a sample document. `atril --fullscreen <file>` opens a
document full screen, and `atril --presentation <file>` opens it as a slide show. Atril reads
PDF only: the other document backends are not built.

### 7.3 X11 — Xorg and Window Maker

X11 runs Xorg with the modesetting driver and glamor acceleration on the V3D, and Window Maker
as the window manager:

```
/bin/bash /bin/startx
```

This starts the **showcase desktop**, which runs by itself with no input:

- Window Maker with its dock and clip
- a spinning OpenGL window ("Phoenix V3D GL", EGL on X11 through DRI3/Present, 60 fps)
- an xterm running Conway's Life in Python
- xclock
- xbill
- an xterm running `top`

To start Window Maker alone, run `/bin/bash /bin/startx wmaker`. Right-click the desktop for
Window Maker's applications menu.

**Leaving X:** exit Window Maker from its root menu (right-click → Exit). X shuts down and psh
comes back. For an unattended demo, `export HOLD=200` first: the desktop then closes by itself
after 200 seconds.

<!-- TODO(coordinator): check on the merged image that Window Maker's stock root menu (the
windowmaker port stages /etc/WindowMaker/WMRootMenu) opens an xterm under Xorg. -->

### 7.4 Web browsing — Dillo

Dillo is a small graphical browser for X11. It supports HTTPS with CA-verified TLS 1.2. In an
X session, open an xterm and run:

```
dillo https://example.com
```

- HTTPS needs a correct clock. The Pi has no battery-backed clock, so set it first from psh
  with `ntpclient -s pool.ntp.org`. psh also runs `ntpclient` once when it starts.
- Browsing the internet needs a default gateway and DNS from DHCP (see [§8.1](#81-ethernet)).

<!-- TODO(coordinator): Dillo on the X desktop has not been run on the Xorg (modesetting +
glamor) server. Its last HW run was under the X server this stack replaced. startx has no
`browse` mode, so Dillo needs an xterm from the root menu (§7.3 TODO). -->

### 7.5 Command-line programs

The image also ships a Unix userland. For the full list, see the
[README](../README.md#userland-cli-tools-and-languages):

- `bash`, GNU coreutils and BusyBox
- `python3` (CPython 3.14), `micropython`, `lua`
- `sqlite3`, `jq`, `redis-server`
- `curl`, `wget`
- `nano`, `vi`, `mc`

Terminal programs look right with `export TERM=vt100` at the console. The desktop's foot
terminal sets its own `TERM`.

---

## 8. Networking, audio and input

### 8.1 Ethernet

The gigabit Ethernet port gets an address by DHCP during boot.

- `ifconfig` shows the interfaces and their addresses.
- `ping <host>`, `nslookup <name>` and `wget <url>` work from psh. `curl https://…` works from
  bash or psh.
- **Clock:** `ntpclient -s pool.ntp.org` sets the clock over NTP. A server in `/etc/ntp.conf`
  (a `server=<host>` line) is used when `-s` is not given.
- **NFS root:** with the network boot, `/` itself is served over NFS (~30 MB/s read, ~20 MB/s
  write on gigabit).

<!-- TODO(coordinator): internet access has only been checked through the lab's host NAT
gateway (scripts/pi-internet-nat.sh + dnsmasq options 3/6). Behind an ordinary home router
it should work the same way (DHCP gives the gateway and DNS), but that has not been run. -->

### 8.2 WiFi

The WiFi daemon (`rpi4-wifi`) starts at boot with the SD and NFS-root images. Manage WiFi with
the `wifi` command:

```
wifi scan                          list the access points in range
wifi connect <ssid> <passphrase>   join a WPA2-PSK network and wait for the DHCP lease
wifi status                        the wanted network, the radio state and the address
wifi disconnect                    leave the network and forget it
```

- **It persists.** `wifi connect` saves the network in `/etc/wifi.conf` (readable by root
  only), and the Pi rejoins it after every reboot until you run `wifi disconnect`. Without that
  file, the image joins nothing.
- **To configure it by hand**, copy `/etc/wifi.conf.example` to `/etc/wifi.conf` and fill in
  the `ssid=` and `psk=` lines. The WiFi interface re-reads the file every few seconds, so no
  reboot is needed.
- **Limits:**
  - An SSID with a space, and a 64-digit hex key, are not supported yet.
  - The passphrase must be 8–63 characters.
  - A passphrase with spaces works only in the file, because psh passes quotes literally.
- **Routing:** Ethernet keeps the default route while it has an address. WiFi takes over when
  Ethernet has none (for example, no cable).
- **Speed:** ~3.6 MB/s out and ~3.3 MB/s in. Ethernet is faster (~20–30 MB/s).
- **Firmware and licence:** WiFi needs the Broadcom/Cypress BCM43455 firmware. The build
  downloads it from linux-firmware (tag `20260810`, sha256-pinned) and installs it under
  `/lib/firmware/brcm/`, next to its licences (`/lib/firmware/LICENSES/LICENCE.cypress`,
  `GPL-2.0`) and a `WHENCE` file. The Cypress licence allows redistribution in binary form for
  use with Cypress chips, which includes the BCM43455 in the Pi 4. The firmware is not in this
  repository.

### 8.3 Other network services

| Program | What it does |
|---|---|
| `dbclient user@host` | SSH client (Dropbear). `scp` copies files over SSH. |
| `/usr/sbin/dropbear` | SSH server |
| `/usr/sbin/lighttpd -f /etc/lighttpd.conf` | web server |
| `redis-server --port 6379` | Redis 7.2 over TCP |

<!-- TODO(coordinator): the SSH server needs host keys and a way to log in (root password or
authorized_keys). This guide does not document that, because no HW run of the dropbear server
on the current image was found. Verify `dropbear -R -F -E` and the login path, or drop the
row. Same for the lighttpd docroot. -->

### 8.4 Audio

The 3.5 mm jack is driven by the Pi's PWM audio (`/dev/audio0`, 44.1 kHz stereo). The games
(through SDL), ffplay and gtk-video play sound through it. HDMI audio is not supported.

### 8.5 Keyboard and mouse

USB keyboards and mice work everywhere: at the psh console, in the games, in X11 and on the
Wayland desktop (`/dev/kbd0`, `/dev/mouse0`). Plug them in before power-on. The layout is US.

Bluetooth is not usable: the radio comes up, but there is no Bluetooth host stack.

---

## 9. The showcase — the best setup in one sitting

This is the recommended way to show the whole system, with the best settings for each part.

**Setup:**

- A 4 GB Pi 4 booting the SD image, built with `--with-showcase --with-ports`, or the NFS root.
- A 1080p HDMI screen.
- A USB keyboard and mouse.
- Ethernet with internet access. WiFi joined once beforehand with `wifi connect`, so it rejoins
  at boot.
- Speakers on the jack.

**Run, in this order:**

1. **Boot.** Show the kernel log on HDMI until `(psh)%` appears.
2. **The shell.** Run `uname -a`, `ifconfig` and `wifi status`: the Ethernet lease, and WiFi
   joined with its address.
3. **The XFCE desktop.** Run `/bin/bash /bin/xfce-session`. On the desktop:
   1. Show the panel and the Applications menu. Thunar is already open.
   2. Super+Return opens foot.
   3. **Games → Quake III Arena** runs in a window next to Thunar and foot. Close it, then open
      **Games → SuperTuxKart** the same way.
   4. **Office → Atril** opens the sample PDF.
   5. **Multimedia → Video Demo** plays a clip in a window. Press **f** for full screen, then
      **f** again to go back to the window.
   6. **Log Out.**
4. **Full-screen games.** Run each at the psh prompt, at its best setting:
   - `quake3 +map q3dm1` (~59 fps)
   - `quake2` (60 fps)
   - `quakespasm` (~44 fps)
   - `vkquake`: Vulkan (~43 fps)
   - `game-res stk 1280x720 --track=hacienda --numkarts=4 --profile-laps=2`: SuperTuxKart
     scaled from 720p, ~22 fps, an AI race with no input needed
5. **Video full screen.** Run
   `/bin/bash /bin/video-play /usr/share/video-demo/hevc-720p30-aac.mp4`: HEVC decoded on the
   CPU, in real time.
6. **X11.** Run `/bin/bash /bin/startx`: Xorg with glamor, Window Maker, the GL window, Life,
   xclock and xbill, all animating by themselves. Exit through Window Maker's menu.
7. **The web** (optional). In `startx wmaker`, open an xterm, run
   `ntpclient -s pool.ntp.org` from psh first, then `dillo https://example.com`.

**Tips:**

- Start each game once before an audience arrives, so you know how long its first frame takes
  (see [§6.1](#61-full-screen-from-psh)).
- If a program prints nothing for a minute, it is probably still loading. Check the serial
  console before you assume it hung.

---

## 10. Troubleshooting

| Symptom | Try this |
|---|---|
| Nothing on the screen | Use the micro-HDMI port nearest the USB-C jack and wait 60 s. Check the serial console. |
| The Pi does not boot from the card | The EEPROM boot order may be set to network first ([BUILD.md](BUILD.md#step-5--boot-the-pi)). |
| `xfce-session` prints `a server is missing` | A graphics server did not start at boot. Look for `V3DA srv ready`, `KMS srv ready` and `SHMSRV srv ready` in the boot log. |
| `game-window.sh` says `no Wayland socket` | Start the desktop first, or run the game from psh for full screen. |
| `game-window.sh` says `another game is running` | Close the running game window first. Only one windowed game runs at a time. |
| No network | Check the cable and that the LAN has a DHCP server, then run `ifconfig`. |
| `wifi: cannot open /dev/wifi` | The WiFi daemon is not running. It does not start on the `netboot` variant. |
| `wifi connect` fails | Check the passphrase (8–63 characters) and that the SSID has no space. `wifi status` shows the state. |
| HTTPS certificate errors | The clock is wrong: run `ntpclient -s pool.ntp.org`. |
| Keyboard or mouse do nothing | Plug them in before power-on. |

For everything else, see [KNOWN-ISSUES.md](KNOWN-ISSUES.md). How the stack was built and
measured is recorded in [docs/gpu-new-lane/](gpu-new-lane/PLAN.md) (engineering history).

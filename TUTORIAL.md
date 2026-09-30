# Phoenix-RTOS on Raspberry Pi 4 — First-Time User Tutorial

A single, self-contained quick-start: build the full system image, flash an SD
card and boot your Pi. The [User Guide](docs/USER-GUIDE.md) then shows everything in
the distribution: the XFCE desktop on Wayland, five 3-D games (full screen or in a
window), video, a PDF reader, an X11 desktop, WiFi, a web browser, scripting
languages and more.

> **Hardware tested:** This has only been validated on a **Raspberry Pi 4
> Model B with 4 GB RAM**. Other Pi 4 variants (1/2/8 GB) and other boards are
> **untested** — the device-tree parser is currently specialized for the 4 GB
> Pi 4B. Use a 4 GB Pi 4B for a known-good experience.

---

## 1. What you'll need

**Hardware**
- Raspberry Pi 4 Model B, **4 GB** (see note above).
- A microSD card, **4 GB or larger**.
  The image (`rpi4b-sd-2part.img`) is 1.6 GB (1 570 404 352 bytes, 2026-09-30 build).
- USB-C power supply for the Pi.
- A display on **micro-HDMI** (use the HDMI port **nearest the USB-C** jack) + a
  micro-HDMI→HDMI cable.
- A **USB keyboard** and a **USB mouse** (the desktops and games use both).
- A **wired Ethernet** cable. WiFi works too: see the
  [User Guide](docs/USER-GUIDE.md#82-wifi).
- *Optional but handy:* a USB-to-serial (UART) adapter on GPIO pins
  8 (GND) / 10 (RXD→Pi TXD, GPIO14) / (Pi RXD, GPIO15) at **115200 8N1** to watch
  the boot log.

**Build host**
- Any Linux/macOS/Windows machine with **Docker** installed and internet access.
- ~35 GB free disk and a reasonably fast CPU: a full clean build takes
  **several hours** (the cross-compiler toolchain, Mesa, GTK and XFCE are the long
  poles).

You do **not** need a Raspberry Pi to build — only to run the result.

---

## 2. Build the image (Docker — recommended, universal)

The whole system builds from the public `rpi-phoenix-rtos` GitHub org in one
command. Docker fetches the `Dockerfile`, clones every repository, builds the
cross toolchain, compiles the kernel + drivers + all applications, downloads the
freely-redistributable game data (Quake **shareware**, the Quake II and Quake III
**demos**, and the SuperTuxKart 1.4 assets) and the WiFi firmware, and produces a
ready-to-flash 2-partition SD image.

```bash
# Build the full showcase image (SD variant, all apps, with Quake data).
docker build --no-cache --pull -t phoenix-rpi \
  https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile
```

That's it — the defaults already select the SD-card variant **with** the full
application showcase and Quake game data. When it finishes you'll see
`Successfully built …`.

**Copy the image out of the container** into a local `./out/` directory:

```bash
mkdir -p out
docker run --rm -v "$PWD/out:/out" phoenix-rpi
# -> out/rpi4b-sd-2part.img  (+ its sha256 is printed)
ls -lh out/rpi4b-sd-2part.img
```

Notes:
- If any external download fails (e.g. a flaky mirror), the build **stops with a
  clear `ERROR`** rather than producing an incomplete image — fix your
  connectivity (or override a URL) and re-run. It will never silently ship a
  half-baked system.
- Game data: the build stages the official Quake **shareware** `pak0.pak`, the
  Quake II and Quake III **demo** paks and the SuperTuxKart 1.4 asset roots
  (all freely redistributable, each from a pinned URL) via
  `scripts/stage-game-data.sh`. To build a Quake engine *without* its game data,
  add `--build-arg PAK0_URL=` (or `PAK0Q2_URL=` / `PAK0Q3_URL=`). To use your own
  data, pass e.g. `--build-arg PAK0_URL=<url-to-pak0.pak>`.
- **Quake III needs no retail content and no retail CD key** — the image ships
  the free demo pak plus a `pak1.pk3` of QVMs built from ioquake3 and a
  format-valid `q3key`.
- Prefer building natively on Linux instead of Docker? See
  [docs/BUILD.md](docs/BUILD.md) (`scripts/bootstrap-linux-host.sh` then
  `scripts/rebuild-rpi4b-fast.sh --variant sd --with-showcase --with-ports`).
- Want to boot over the network (DHCP + TFTP + NFS, no SD card) for a fast
  edit-rebuild-run loop? See [TUTORIAL-NETBOOT.md](TUTORIAL-NETBOOT.md).

---

## 3. Flash the SD card

> ⚠️ **`dd` writes raw to a whole disk — pick the wrong device and you erase it.**
> Identify your SD card carefully.

1. Insert the microSD card into your host (via a reader).
2. Find its device node:
   ```bash
   lsblk -o NAME,SIZE,TRAN,RM,MODEL   # look for your card: removable (RM=1), usb/mmc, matching size
   ```
   It will be something like `/dev/sdX` (Linux) or `/dev/diskN` (macOS). **Do not
   pick your system disk** (e.g. an `nvme…` device holding `/`).
3. Unmount any auto-mounted partitions of that device first.
4. Write the image (replace `/dev/sdX` with your card):
   ```bash
   sudo dd if=out/rpi4b-sd-2part.img of=/dev/sdX bs=4M conv=fsync status=progress
   sync
   ```

The image contains two partitions: a **64 MB FAT** boot partition (`PHOENIXPI`)
and an **ext2** root filesystem sized to its contents. Eject the card when `dd`
finishes.

---

## 4. Boot the Pi

1. Put the flashed card in the Pi.
2. Connect the **micro-HDMI** display (port nearest USB-C), the **USB keyboard**
   (and **mouse** if you'll try X11), and the **Ethernet** cable.
3. Apply power.

The kernel log scrolls on the HDMI screen, and after about 50 seconds you land at
the **`(psh)%` shell prompt** on the console. Type with the USB keyboard. Wired
Ethernet is brought up automatically (DHCP) during boot, and the graphics servers
and the WiFi daemon start too.

If you attached a serial adapter, the same log/console is available on UART at
115200 8N1.

---

## 5. Using the system

You're at `psh`, Phoenix's shell. It runs one command per line: it has no pipes,
no redirection and no `;` (run `bash` for a full shell). Familiar basics work: `ls`,
`cat`, `cd`, `ps`, `top`, `df`, `mem`, `uname -a`, `dmesg`, `cat /etc/passwd`, etc.

Check your network address:
```bash
ifconfig            # shows the DHCP-assigned IP on the genet interface
ping 8.8.8.8
```

---

## 6. What to try

The **[User Guide](docs/USER-GUIDE.md)** describes everything on the image and how to
start it. The short version:

```bash
/bin/bash /bin/xfce-session       # the XFCE desktop on Wayland (Log Out returns here)
quakespasm                        # Quake, full screen    (also: quake2, quake3 +map q3dm1, vkquake)
stk                               # SuperTuxKart 1.4
game-res stk 1280x720             # ... at 1280x720 scaled to the screen: about twice the fps
/bin/bash /bin/video-play /usr/share/video-demo/h264-720p30-aac.mp4
/bin/bash /bin/startx             # X11: Xorg + glamor, Window Maker
wifi connect <ssid> <passphrase>  # join a WPA2 network (remembered across reboots)
python3                           # CPython 3.14 (also: lua, micropython, sqlite3, jq, bash)
```

On the desktop, the Applications menu has the games (in windows), the video players and
the Atril PDF viewer. For the best order to show all of it, see the
[showcase](docs/USER-GUIDE.md#9-the-showcase--the-best-setup-in-one-sitting).

---

## 7. Known limitations (before you file a bug)

- **Only the 4 GB Pi 4B is validated.** Do not use a 2 GB or 8 GB board.
- **Bluetooth is driver-level only**: no host stack, so no pairing, profiles or audio.
- **WiFi** is ~3.5 MB/s each way. Use Ethernet for large transfers.
- **vkQuake runs full screen only** (no desktop window), and it shows a black screen for
  about 75 seconds at every start while it compiles its Vulkan pipelines. It has not hung.
- I²C/SPI/general-purpose PWM, camera and DSI are not implemented.

The full, precise list lives in [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).

---

## 8. Troubleshooting

| Symptom | Try this |
|---|---|
| Nothing on screen | Use the micro-HDMI port **nearest USB-C**; give it 30 s; try the UART console. |
| No network | Check the Ethernet cable/switch; `ifconfig` for an IP; DHCP must be available on the LAN. |
| The desktop says a server is missing | A graphics server did not start at boot: look for `V3DA srv ready`, `KMS srv ready`, `SHMSRV srv ready` in the boot log. |
| Keyboard/mouse dead | Plug them into the Pi's USB before power-on. |
| Build stopped with `ERROR` | An external download failed — fix connectivity or override the URL, then re-run `docker build`. |

---

Enjoy exploring a real microkernel RTOS booting a full GPU-accelerated
graphical stack on the Raspberry Pi 4. Feedback and issues welcome.

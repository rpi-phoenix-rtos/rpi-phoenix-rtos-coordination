# Phoenix-RTOS on Raspberry Pi 4 — First-Time User Tutorial

A single, self-contained quick-start: build the full system image, flash an SD
card, boot your Pi, and try out everything in the "distribution" — GLQuake, an
X11 desktop with window managers, a file manager, a web browser, editors,
scripting languages, benchmarks and more.

> **Hardware tested:** This has only been validated on a **Raspberry Pi 4
> Model B with 4 GB RAM**. Other Pi 4 variants (1/2/8 GB) and other boards are
> **untested** — the device-tree parser is currently specialized for the 4 GB
> Pi 4B. Use a 4 GB Pi 4B for a known-good experience.

---

## 1. What you'll need

**Hardware**
- Raspberry Pi 4 Model B, **4 GB** (see note above).
- A microSD card, **≥ 1 GB** (the image is ~840 MB; any modern card works).
- USB-C power supply for the Pi.
- A display on **micro-HDMI** (use the HDMI port **nearest the USB-C** jack) + a
  micro-HDMI→HDMI cable.
- A **USB keyboard** (for the console and Quake), and a **USB mouse** (for X11).
- A **wired Ethernet** cable (wireless networking is not usable yet — WiFi is
  control-plane-only; see §7).
- *Optional but handy:* a USB-to-serial (UART) adapter on GPIO pins
  8 (GND) / 10 (RXD→Pi TXD, GPIO14) / (Pi RXD, GPIO15) at **115200 8N1** to watch
  the boot log.

**Build host**
- Any Linux/macOS/Windows machine with **Docker** installed and internet access.
- ~15 GB free disk and a reasonably fast CPU: a full clean build takes roughly
  **1.5–2 hours** (most of it is building the cross-compiler toolchain once).

You do **not** need a Raspberry Pi to build — only to run the result.

---

## 2. Build the image (Docker — recommended, universal)

The whole system builds from the public `rpi-phoenix-rtos` GitHub org in one
command. Docker fetches the `Dockerfile`, clones every repository, builds the
cross toolchain, compiles the kernel + drivers + all applications, downloads the
freely-redistributable Quake **shareware** data, and produces a ready-to-flash
2-partition SD image.

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
- Quake data: the build uses the official Quake **shareware** `pak0.pak`
  (freely redistributable). To build the engine *without* game data, add
  `--build-arg PAK0_URL=`. To use your own data, pass `--build-arg
  PAK0_URL=<url-to-pak0.pak>`.
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
and a **768 MB ext2** root filesystem. Eject the card when `dd` finishes.

---

## 4. Boot the Pi

1. Put the flashed card in the Pi.
2. Connect the **micro-HDMI** display (port nearest USB-C), the **USB keyboard**
   (and **mouse** if you'll try X11), and the **Ethernet** cable.
3. Apply power.

Within ~20–30 seconds the kernel log scrolls on the HDMI screen and you land at a
**shell prompt** on the console. Type with the USB keyboard. Wired Ethernet is
brought up automatically (DHCP) during boot.

If you attached a serial adapter, the same log/console is available on UART at
115200 8N1.

---

## 5. Using the system

You're at a BusyBox/`psh` shell. Everything below is typed at that prompt (or
inside an `xterm` once you start X11). Familiar basics work: `ls`, `cat`, `cd`,
`ps`, `top`, `df`, `free`/`mem`, `uname -a`, `dmesg`, `cat /etc/passwd`, etc.

Check your network address:
```bash
ifconfig            # shows the DHCP-assigned IP on the genet interface
ping 8.8.8.8
```

---

## 6. The showcase — what to try and how to start it

### 🎮 GLQuake (hardware-accelerated 3-D on the V3D GPU)

The headline demo: id Software's Quake, rendered on the Pi's V3D GPU via a ported
Mesa driver, output over HDMI.

```bash
rpi4-quake
```

- It renders on the HDMI screen; control it with the **USB keyboard** (arrows /
  `Ctrl` to fire, `` ` `` for the console, `Esc` for the menu).
- The **shareware** episode and its demo loops play out of the box (the data is
  in `/usr/share/quake/id1/pak0.pak`).
- *Caveats:* single-player and demos work; **multiplayer hangs at LOADING**, and
  a few small pickup **items**/**torch flames** show a minor cosmetic geometry
  glitch — gameplay is unaffected.

### 🎮 Quake II and Quake III

Two more id engines run on the V3D GPU via SDL2. Their launchers **stage the game
data into a `/tmp` RAM disk first**, then run — so the first launch copies assets
(a few seconds) and then plays from RAM:

```bash
quake2                # yQuake2 (OpenGL ref_gl1) — plays its demo in textured 3D
quake3                # quake3e — renders gameplay on the GPU
```

- *Quake III:* `q3dm1` renders fully lit and correct. Some larger maps (e.g.
  `q3dm7`) show **black lightmap sectors** — a known V3D GL-path issue under
  investigation; gameplay otherwise renders.
- *Quake II:* renders its demo in textured 3D. (It used to show a black screen on
  slow NFS asset loads; the RAM-staging launcher fixed that.)

### 🎮 vkQuake (Vulkan / V3DV)

```bash
rpi4-vkquake          # Quake rendered through the Vulkan (V3DV) driver
```

*Caveat:* vkQuake renders the menu and textured 3D through the GPU, but currently
**hangs after the main menu and does not respond to the keyboard/mouse** — it is
a known open issue, not yet a playable path. Use `rpi4-quake` (GLQuake) to
actually play.

### 🖥️ X11 desktop and applications

X11 runs on a kdrive/fbdev server (`Xphoenix`) drawing to the framebuffer. Launch
it with `startx <client>`, which starts the server and the given client together.
A **USB mouse** is required to interact.

**Full window-manager desktop — WindowMaker:**
```bash
startx wmaker
```
WindowMaker (the NeXTSTEP-style WM) comes up; **right-click the root window** for
the applications menu, and use its Dock/menus to open programs.

**Lightweight desktop — twm + a demo client:**
```bash
startx desktop        # twm window manager + xeyes (draggable, decorated)
```

**Run a single X application directly** (server + that app, no WM):
```bash
startx xterm          # an X terminal (run other programs from here)
startx xcalc          # scientific calculator
startx xclock         # analog/digital clock
startx xedit          # text editor (Athena)
startx xlogo          # the X logo
startx dillo          # web browser (see below)
```

> If the X server ever exits uncleanly and won't restart, remove the stale lock:
> `rm -f /tmp/.X0-lock`, then relaunch.

### 🌐 Dillo — graphical web browser

A small GTK-free graphical browser (X11 app):
```bash
startx dillo
# or, from within an X session / xterm:
dillo https://example.com
```
Dillo **browses the live HTTPS internet** (CA-verified TLSv1.2) as well as plain
HTTP. *Note:* live internet access in this port goes through a **host-side NAT
gateway** (`scripts/pi-internet-nat.sh`) — the Pi reaches the internet through
the dev host, a lab/netboot setup rather than the Pi routing on its own. On a
plain LAN without that gateway you can still browse hosts reachable directly.

### 📂 Midnight Commander — text-mode file manager

```bash
mc
```
The classic two-pane file manager, in the console (or inside an `xterm`). Full
skins and syntax highlighting are bundled.

### 📝 Text editors

```bash
nano /etc/profile     # friendly modeless editor
vi   /etc/profile     # busybox vi
```
(`xedit` above is the graphical X11 editor.)

### 🐍 Scripting languages

```bash
python3               # CPython 3.14 REPL    (Ctrl-D to exit)
python3 -c 'import sqlite3, json, math; print(math.pi)'

micropython           # MicroPython 3 REPL   (Ctrl-D to exit)
micropython -c 'print(sum(range(100)))'

lua                   # Lua 5.4.7 REPL
lua -e 'print("hello from Lua on Phoenix-RTOS")'
luac                  # the Lua compiler
```

`python3` is a static **CPython 3.14** with `sqlite3`, `zlib`, `_ssl`/HTTPS,
`_decimal`, `ctypes`, and `.so` C-extension loading via `dlopen`.

### 🧰 CLI tools & data stores

The image (built `--with-ports`) ships a real command-line ecosystem, all
HW-verified:

```bash
# GNU coreutils 9.5 — 104 tools
ls -l /etc
wc -l /etc/passwd
sha256sum /etc/profile
seq 1 5

# jq — JSON processor (the '|' here is jq's own filter syntax, not a shell pipe)
jq -n '[1,2,3] | add'

# SQLite 3 — full SQL, in-memory or on-disk
sqlite3 :memory: 'select 2+2;'

# Redis 7.2 — in-memory data store; start the server on the Pi, then connect
# to it over TCP with redis-cli from another machine (find the Pi's IP with ifconfig)
redis-server --port 6379

# curl — HTTP/HTTPS client (mbedTLS); reaches the internet via the host NAT gateway
curl https://example.com

# GNU bash 5.2 — runs non-interactively; see the caveat below
bash -c 'echo "hello from bash"'
```

> **bash caveat:** interactive `bash` self-exits on EOF when there is no
> interactive tty at the console (`getty`/pts wiring is still pending), so it is
> not yet a usable interactive login shell — run scripts with `bash -c '…'` or use
> `psh` interactively. Non-interactive use is unaffected.

### 🎯 Small games & graphics toys

```bash
startx xbill          # "stop the bills" game  (see caveat)
startx xeyes          # eyes that track the mouse
voxeldemo             # voxel-terrain demo
rotrectangle          # spinning-rectangle GPU demo
```
*Caveat:* `xbill` may currently exit silently on some runs; the other X apps
render fine.

### ⚙️ Benchmarks / stress

```bash
coremark              # CPU benchmark
cpuburn               # CPU stress
perf                  # simple perf counters
```

### 🔌 Networking & services (wired Ethernet)

Client tools:
```bash
wget http://example.com/file      # download over HTTP
nc / nslookup / ntpclient / ping  # netcat, DNS, NTP, ping
dbclient user@host                # Dropbear SSH client
scp file user@host:/path          # SCP over SSH
openssl version                   # OpenSSL toolkit
```

Servers you can start:
```bash
dropbear                          # SSH server — then log in from another machine
lighttpd -f /etc/lighttpd.conf    # web server (serves its configured docroot)
```
Use `ifconfig` to find the Pi's IP so you can reach it from your workstation.

### 🧰 Hardware / system info

The port ships userspace drivers (started at boot) that expose devices you can
read from the shell:
```bash
cat /dev/thermal                    # SoC temperature (rpi4-thermal driver)
cat /dev/hwrng | hexdump -C | head  # hardware RNG (rpi4-hwrng driver)
cat /dev/gpio                       # GPIO register snapshot (rpi4-gpio driver)
```
Other bundled drivers include the framebuffer (`/dev/fb0`), audio
(`/dev/audio0`, PWM on the 3.5 mm jack), the SD card (`/dev/mmcblk0`) and USB
(keyboard `/dev/kbd0` + mouse `/dev/mouse0`).

---

## 7. Known limitations (before you file a bug)

This is a bring-up port; the base system (boot → shell, 4-core SMP, Ethernet, SD,
USB HID, HDMI, GPU) is solid, but some showcase edges are rough. Highlights:

- **Wi-Fi: not usable for networking yet.** The control-plane works (associates
  to WPA2-PSK + completes the 4-way key handshake) but the data-plane doesn't
  carry traffic yet — **use wired Ethernet**.
- **Bluetooth: driver-level only.** `/dev/hci0` comes up and an HCI Inquiry
  completes, but there is no host stack — no pairing, profiles, or audio.
- **GLQuake:** multiplayer hangs; minor cosmetic model glitch on some items.
- **Quake III:** some larger maps (e.g. `q3dm7`) show black lightmap sectors;
  `q3dm1` is fully lit.
- **vkQuake:** renders the menu but hangs after it and takes no keyboard/mouse
  input — use `rpi4-quake` (GLQuake) to actually play.
- **bash:** self-exits on EOF without an interactive tty; use `psh` interactively.
- **xbill:** may exit silently on some runs.
- **Only the 4 GB Pi 4B is validated.** Other RAM sizes/boards are untested.
- USB mass storage, I²C/SPI/PWM general-purpose, camera and DSI are not
  implemented.

The full, precise list lives in [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).

---

## 8. Troubleshooting

| Symptom | Try this |
|---|---|
| Nothing on screen | Use the micro-HDMI port **nearest USB-C**; give it 30 s; try the UART console. |
| No network | Check the Ethernet cable/switch; `ifconfig` for an IP; DHCP must be available on the LAN. |
| X server won't start | `rm -f /tmp/.X0-lock` and relaunch `startx …`. |
| Keyboard/mouse dead in X | Ensure they're plugged into the Pi's USB before boot; X reads `/dev/kbd0` and `/dev/mouse0`. |
| Build stopped with `ERROR` | An external download failed — fix connectivity or override the URL, then re-run `docker build`. |

---

Enjoy exploring a real microkernel RTOS booting a full GPU-accelerated
graphical stack on the Raspberry Pi 4. Feedback and issues welcome.

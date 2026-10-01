# Phoenix-RTOS on the Raspberry Pi 4

This is a from-scratch port of [Phoenix-RTOS](https://phoenix-rtos.com/), a small microkernel
operating system based on message passing, to the **Raspberry Pi 4 Model B / BCM2711**
(Cortex-A72, AArch64).

The port started from a system that did not boot. It now:

- boots to an interactive shell and drives the real hardware;
- serves its root filesystem from an SD card or over NFS;
- runs a full graphical userland on the V3D GPU, with a Linux-style graphics stack:
  - a **render server**, a **KMS display server**, **libdrm**, and **Mesa 26.2** (GBM, EGL,
    OpenGL ES 3.1, OpenGL and Vulkan);
  - **SDL 2**, with full-screen KMSDRM and Wayland windows;
  - **Xorg** with glamor acceleration;
  - the **XFCE 4.20 desktop on the labwc Wayland compositor**.

Programs on the image:

- The XFCE desktop: panel, Thunar file manager, foot terminal and application menu.
- An X11 desktop with Window Maker.
- Five 3D games, full screen or in a window on the desktop: Quake (QuakeSpasm), Quake II,
  Quake III Arena, vkQuake (Vulkan) and SuperTuxKart 1.4.
- A video player, including HEVC.
- The Atril PDF viewer.
- The Dillo web browser.
- WiFi and Ethernet networking, and a Unix command line with bash, Python 3.14 and more.

> This repository is the **coordination repo**: docs, build scripts and integration
> manifests. The Phoenix-RTOS source lives in sibling repositories cloned under `sources/`
> (see [Repository layout](#repository-layout)).

> 📖 **Using the system? Read the [User Guide](docs/USER-GUIDE.md).** It covers booting, the
> XFCE desktop, the games (full screen, windowed and scaled), video, PDF, X11, WiFi and
> networking. It ends with a showcase: one recommended way to show the whole system.
>
> 🚀 **First time here?** [**TUTORIAL.md**](TUTORIAL.md) walks you through building the image,
> flashing an SD card and booting the Pi. *(Tested on a Raspberry Pi 4 Model B with 4 GB RAM.)*
>
> 🌐 **Want a network dev setup?** [**TUTORIAL-NETBOOT.md**](TUTORIAL-NETBOOT.md) shows how to
> build from source and boot the Pi entirely over the network (DHCP + TFTP + NFS root, no SD
> card). This gives a fast edit-rebuild-run loop.

> ⚙️ **Toolchain: GCC 16.2.0 + binutils 2.47.** The port builds with an up-to-date
> aarch64-phoenix cross-toolchain. `bootstrap-linux-host.sh` builds it, and the whole system
> is built with it and verified on the hardware. The authoritative release gate is a Docker
> `--no-cache` clean build from the org Dockerfile, which produces `BUILD_RC=0` from a blank
> OS. Details are in the [gcc-16 release plan](docs/done/gcc16-release-plan.md).

## Quick start

On a fresh Ubuntu x86_64 machine, from an empty directory:

```bash
git clone https://github.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination.git ~/phoenix-rpi
cd ~/phoenix-rpi
./scripts/bootstrap-linux-host.sh            # installs deps, clones sources, builds the toolchain
./scripts/rebuild-rpi4b-fast.sh --variant sd --with-showcase --with-ports
                                             # builds artifacts/rpi4b/rpi4b-sd-2part.img
```

Flash the resulting image to a microSD card and boot it on a Pi 4. The full
walkthrough — prerequisites, timings, flashing, and first-boot expectations —
is in **[docs/BUILD.md](docs/BUILD.md)**; what to do once it boots is in the
**[User Guide](docs/USER-GUIDE.md)**.

## Build with Docker (reproducible, any host OS)

The whole build is packaged as a **single, self-contained Dockerfile**. It works on
any machine with a Docker CLI (Linux/macOS/Windows) regardless of host OS or
installed packages — the entire toolchain runs inside a container we fully control.
Nothing is copied from the host: every source tree, Ubuntu package, font, and the
freely-downloadable game data (Quake I shareware, the Quake II and Quake III
demos, and the SuperTuxKart 1.4 assets) and the WiFi firmware are fetched over the
network at build time (each from a pinned URL) and baked into the image
*you* build — this repo distributes only the build scripts, never a built image.

### Before you start

| | |
|---|---|
| **Docker** | a CLI with **BuildKit/buildx**. Docker Desktop bundles it; on a minimal Linux `docker.io` install run `sudo apt-get install docker-buildx`. |
| **Disk** | **~90 GB free** during the Docker build (it peaked at 84 GB on 2026-09-30: a GCC cross-toolchain, then the whole OS with Mesa, GTK, XFCE and the games, inside the image layers). The script reclaims it when the build ends. |
| **Time** | **1 h 42 min** for the full image on an 8-core / 16-thread AMD Ryzen 7 laptop with 32 GB RAM (measured 2026-09-30: `build-sd-in-docker.sh`, `--no-cache`, from an empty container, including the GCC cross-toolchain). |
| **Network** | the build clones from GitHub, downloads the port tarballs, the game data and the WiFi firmware. |

> **macOS / colima:** give the VM enough room up front — the default is too small
> and the build dies deep in the toolchain stage:
> ```bash
> colima start --cpu 8 --memory 16 --disk 100
> ```

### Copy-paste: build a bootable SD image

Pick **one** of these. Each is a complete recipe: build, then export the image.
Recipe 2 is the one to use.

**1. Without the showcase stage.** This leaves out the small helper programs that the
showcase stage adds (`ram-stage-play`, which the Quake II and Quake III launchers need, and
`game-res`). Every program on the image is a framework port, and the SD build runs the
whole ports stage either way, so this recipe is **not** a smaller or much faster build.
<!-- TODO(coordinator): recipe 1 no longer gives a "base system" (ports.yaml has no showcase
gating). Either gate the ports stage on --with-ports / --with-showcase again, or delete this
recipe and the Dockerfile's BUILD_FLAGS="" note. -->

```bash
mkdir -p out
docker build -t phoenix-rpi \
  --build-arg BUILD_FLAGS="" \
  https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile
docker run --rm -v "$PWD/out":/out phoenix-rpi
# -> ./out/rpi4b-sd-2part.img
```

**2. The showcase image (recommended)** — everything above **plus** the whole
graphics stack (Mesa, SDL, Xorg, labwc), the **XFCE desktop on Wayland**, the X11
desktop with Window Maker, **all five games** (Quake, Quake II, Quake III,
vkQuake, SuperTuxKart), the video player, Atril, Dillo, WiFi and the ported
command-line apps (`bash`, `python3`, coreutils, …). This is the default if you
pass no `BUILD_FLAGS` at all:

```bash
mkdir -p out
docker build -t phoenix-rpi \
  --build-arg BUILD_FLAGS="--with-showcase --with-ports" \
  https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile
docker run --rm -v "$PWD/out":/out phoenix-rpi
```

There is no third recipe and nothing to swap by hand: recipe 2 is the complete
system, with every program in its one current version.

Then flash `./out/rpi4b-sd-2part.img` exactly as in
[docs/BUILD.md](docs/BUILD.md) (macOS/Linux `dd`, or Raspberry Pi Imager's
"Use custom" option), put the card in the Pi, power on, and continue with the
[User Guide](docs/USER-GUIDE.md).

### What ends up on the card?

**Everything, in one version each.** Every program is built by the ports framework (the
project's `ports.yaml`) and installed **into the root filesystem**. Nothing is swapped by
hand, and nothing is bundled into the boot blob: `loader.disk` holds only the kernel, the
drivers and the servers.

Each game and the video player is **one program** with both of SDL's video drivers. From the
psh prompt it runs full screen on KMS. Inside the desktop it opens in a window.

| Program | Command at the psh prompt | On the desktop |
|---|---|---|
| **Quake** (QuakeSpasm, OpenGL) | `quakespasm` | Games → Quake |
| **Quake II** (yQuake2, OpenGL) | `quake2` | Games → Quake II |
| **Quake III Arena** (quake3e, OpenGL) | `quake3 +map q3dm1` | Games → Quake III Arena |
| **vkQuake** (Quake on Vulkan / V3DV) | `vkquake` | full screen only |
| **SuperTuxKart 1.4** (OpenGL ES 3) | `stk` | Games → SuperTuxKart |
| any game, in a lower mode scaled to the screen | `game-res stk 1280x720 race` | — |
| **XFCE 4.20** desktop on labwc (Wayland) | `/bin/bash /bin/xfce-session` | — |
| **X11** desktop (Xorg + glamor, Window Maker) | `/bin/bash /bin/startx` | — |
| **Video** (ffplay) | `/bin/bash /bin/video-play <file>` | Multimedia → Video Demo, Video Player |
| **PDF** (Atril) | — | Office → Atril |
| **WiFi** | `wifi connect <ssid> <passphrase>` | — |

The [User Guide](docs/USER-GUIDE.md) explains each of them.

Measured on the Pi at 1920×1080, at the page flip, in the final image's gate of 2026-09-30
with WiFi joined ([MIGRATION §7t](docs/gpu-new-lane/MIGRATION.md)):

| Program | fps |
|---|---|
| Quake II | 59.8 |
| Quake III | 59.8 |
| QuakeSpasm | 44.0 |
| vkQuake | 41.8 (42.8 with no WiFi network joined); ~75 s to the first frame, see below |
| SuperTuxKart | 12.6 (~22 at 1280×720 through `game-res`) |
| the X11 desktop's GL window | 60.0 |

In a 1280×720 window on the XFCE desktop: QuakeSpasm 56 fps, Quake III 70–74, SuperTuxKart
18.6. Video plays 720p at 30 fps, in a window and full screen. The XFCE desktop is up 12–20 s
after `xfce-session` starts.

**vkQuake compiles its Vulkan pipelines for about 75 seconds before its first frame**, at every
start (no shader cache yet, [KNOWN-ISSUES](docs/KNOWN-ISSUES.md) G4). The black screen until
then is not a hang.

**Quake III needs no retail content and no retail CD key.** Besides the free demo
`pak0.pk3` it needs two more files, both staged by `scripts/stage-game-data.sh` from
`assets/quake3-qvm/`:

- a `pak1.pk3` holding three QVMs we built from **ioquake3**. The demo's 1999 QVMs report UI
  API 3, while quake3e requires 6.
- a `q3key` file. Only its **format** is checked.

`tools/quake3-vm/build-quake3-vms.sh` builds all three QVMs from ioquake3 at a pinned commit.
The output was verified byte-wise against the shipped pak: two files are identical, and the
third differs only in its embedded `__DATE__`. See
[`assets/quake3-qvm/README.md`](assets/quake3-qvm/README.md).

The game data for all five games is staged into the rootfs overlay by
`scripts/stage-game-data.sh`, under `/usr/share/{quake,quake2,quake3,supertuxkart}`. The
Docker build calls the same script.

### Other build knobs

See the header of [`Dockerfile`](Dockerfile). The useful ones: `UBUNTU_TAG` (base
LTS, default `26.04` — the validated build host), `PAK0_URL` / `PAK0Q2_URL` /
`PAK0Q3_URL` (Quake game-data URLs, each defaulting to a verified upstream
demo/shareware mirror; set one to `""` to build that engine without bundled data),
`STK_ASSETS_URL` / `STK_ASSETS_SHA256` (the pinned SuperTuxKart 1.4 asset
package), `BUILD_VARIANT`
(`sd` / `nfsroot` / `netboot`), and `BUILD_FLAGS` (above; `--with-tests` adds the
`/bin/test-*` suites, the demos and the GPU and hardware diagnostics, which the release image
leaves out — see [docs/BUILD.md](docs/BUILD.md#what-the-image-contains)).

### If the build fails

Docker hides most of the compiler output by default, which makes a failure look
like a bare `collect2: error: ld returned 1 exit status`. Re-run with plain
progress and keep the log:

```bash
docker build --progress=plain --no-cache -t phoenix-rpi \
  --build-arg BUILD_FLAGS="--with-showcase --with-ports" \
  https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile \
  2>&1 | tee docker-build.log
```

The port build scripts print the failing command and the compiler/linker output in
full, so `docker-build.log` will contain the actual cause. Common causes: the VM ran
out of disk (see colima above), or a transient network failure while cloning — a
plain re-run resumes from the last cached layer (omit `--no-cache`).

> **Building from a local checkout** (your own edits, nothing pushed): run
> `./scripts/build-sd-in-docker.sh` — it serves this repo and the sibling/external
> repos (committed state) over a local git+http server, points the container at
> them, and exports to `./docker-out/`.

## Capabilities

Status of the Pi 4 hardware/software stack. `✅` works on hardware and is
validated; `🟡` usable but with a known gap; `⏸` deferred to human-attended
work; `⛔` blocked on external dependencies; `⬜` not started.

| Subsystem | Status | Notes |
|---|---|---|
| CPU / EL2→EL1 / MMU + caches | ✅ | Boots to userspace; caches ON, all Normal RAM WB-cacheable |
| SMP (4 cores) | ✅ | 4-core scheduling; global run-queue + per-core timer preemption. Load distributes across all cores (HW-verified: a 6-thread `cpuburn` saturates cpu1–3 at 100% while cpu0 runs `top`) |
| Generic timer, GIC-400 interrupts | ✅ | Scheduler ticks; GENET/USB/SD IRQs live |
| PL011 UART console | ✅ | Primary serial console + klog mirror |
| HDMI framebuffer console (fbcon) | ✅ | klog + psh on HDMI, FreeBSD `teken` VT engine |
| HDMI display (`rpi4-kms`, `/dev/kms`) | ✅ | KMS display server on the firmware's display planes: atomic page flips at 60.00 fps with vblank events, dumb buffers, and scaled lower modes (1600×900 … 640×480 shown full screen by the display hardware). Console handover to and from the fbcon |
| GPU (V3D 4.2) — render server (`rpi4-v3d-async`, `/dev/v3d-async`) | ✅ | Owns the GPU and runs every client's jobs asynchronously, with fences and sync objects. Clients share buffers with the display server without copies (kernel `memExport`) |
| GPU — OpenGL / OpenGL ES 3.1 | ✅ | **Mesa 26.2** (gallium `v3d`) with GBM and EGL (drm, Wayland, X11 platforms) on a Phoenix libdrm backend. SuperTuxKart at 12.6 fps at 1080p, which is Raspberry Pi OS parity on this board |
| GPU — Vulkan (V3DV) | ✅ | Mesa's `v3dv` with `VK_KHR_display`: **vkQuake at 42 fps** (~75 s pipeline compile before the first frame) |
| SDL 2.30 | ✅ | KMSDRM (full screen) + Wayland (windowed) video drivers in one library, Phoenix HID input and audio. Frame pacing fixed so Quake II runs at a vsynced 60 fps |
| X11 (Xorg 21.1 + modesetting + glamor) | ✅ | GPU-accelerated X with DRI3/Present: a GL window at 60 fps (vsync). Window Maker, xterm, xclock, xbill. `startx` runs the showcase desktop |
| Wayland desktop (labwc 0.20 + XFCE 4.20 + GTK 3.24) | ✅ | labwc composites on the GPU (GLES2). XFCE panel, desktop, Thunar, settings and application finder; the foot terminal; games and video in windows; the Atril PDF viewer. `xfce-session` starts it and Log Out returns to the shell |
| Video playback | ✅ | ffplay (FFmpeg 6.1) full screen or windowed: H.264 and HEVC 720p at 30 fps, VP9, AAC/Opus/MP3 audio (CPU decode, 4 threads). The BCM2711 `rpivid` HEVC block has been driven bit-exact by a stand-alone experiment (`tools/hevc-decode/`); it is not wired into the player |
| GENET gigabit Ethernet + lwIP | ✅ | IRQ-driven, ~0.9 ms ping RTT, autonomous DHCP |
| USB host (PCIe → VL805 xHCI) | ✅ | Enumerates reliably from cold boot |
| USB HID (keyboard + mouse) | ✅ | `/dev/kbd0`, `/dev/mouse0`; live keys reach psh and apps |
| SD card (EMMC2 SDHCI) | ✅ | `/dev/mmcblk0`, MBR partitions; UHS-I DDR50, multi-block, **ADMA2 scatter-gather for reads and writes** (the engine Linux uses on this SoC), 0 corruption |
| ext2 persistent root | ✅ | Mounts as `/`, binaries exec from the card |
| NFS root | ✅ | `/` served over NFS (`takeover` design); over gigabit ~30 MB/s read / ~20 MB/s write (bit-exact, 0 faults) |
| SoC thermal + throttle | ✅ | `/dev/thermal`, `/dev/throttled` via VideoCore mailbox |
| Hardware RNG (RNG200) | ✅ | `/dev/hwrng`; also backs `/dev/urandom` |
| GPIO observer | 🟡 | `/dev/gpio` read-only snapshot; outputs attended |
| Audio (PWM, 3.5 mm jack) | 🟡 | `/dev/audio0` streaming DMA; SDL audio driver (the games, ffplay) and gtk-video play through it; no audible sign-off on headphones yet |
| posixsrv / psh userland | ✅ | pipes, ptys, `/dev/{null,zero,urandom,full}`, AF_UNIX |
| WiFi (BCM43455 SDIO) | ✅ | **In the image** since 2026-09-30: the `rpi4-wifi` daemon starts at boot (SD and NFS-root images). `wifi connect <ssid> <psk>` joins a WPA2 network and waits for the DHCP lease; the network is saved in `/etc/wifi.conf` and rejoined after a reboot. `wifi status`, `wifi scan` and `wifi disconnect` also work. A lost association is rejoined. WiFi takes the default route only when Ethernet has no address. Throughput is ~3.6 MB/s TX / 3.3 MB/s RX, so **prefer wired Ethernet** (~20–30 MB/s) for bulk transfers. The BCM43455 firmware comes from linux-firmware, pinned and sha256-checked, and is installed with its licence files. On the final image a saved network is joined at boot with no command typed: firmware, join, DHCP lease and ping all pass (cycle W1, [docs/misc/2026-09-30-wifi-in-image.md](docs/misc/2026-09-30-wifi-in-image.md)) |
| Bluetooth (BCM43455) | 🟡 | **Driver-level bring-up works** — `/dev/hci0` up, firmware patchram loads (323/323), a real BD_ADDR is read, and an HCI Inquiry completes. **No host Bluetooth stack** — no pairing, profiles, or audio yet |
| USB mass-storage (USB 3 / SuperSpeed) | ✅ | **A USB 3 stick mounts and is read/written as a real filesystem.** Enumerates at SuperSpeed (`maxpkt=1024 burst=4`) through the xHCI driver; `/dev/umass0`, `/dev/umass1` per MBR partition, mounted via libext2. Measured with `/usr/bin/dd`'s own rate: raw read **59.1 MB/s**, raw write **18.2** (1 GiB; 39.9 for 256 MiB, the stick's SLC cache), ext2 read **47.1**. Metadata write amplification was **21.0x → 1.00x** (libcache now writes back only the dirty part of a cache line). ⓘ Getting here took **eleven libext2 defects** — all silent, all passing by return code, several data-losing (a hole read back the superblock; unmount deleted the files it had cached; on any block size above 1 KiB the allocator reserved bit N and handed out block N+1). `e2fsck -fn` on a full 1 GiB read-back of the device is now **completely clean** across create / write / delete / umount / remount, verified on **both** 4 KiB and 1 KiB block sizes. Hot-plug removal is implemented but the physical insert/remove cycle is not yet exercised |
| I²C/SPI/PWM, camera (CSI-2) | ⬜ | Not started |

The authoritative, per-peripheral matrix (with evidence and remaining work) is
[docs/pi4-hardware-support-matrix.md](docs/pi4-hardware-support-matrix.md).
Open bugs and known limitations are in
[docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).


## Userland: CLI tools and languages

Beyond the base system, a substantial ports ecosystem runs on the hardware
(built into the image with `--with-ports`; all HW-verified). The ports are
compiled optimised (`-O2`); until 2026-09-30 most autotools ports were silently
built at `-O0`, and fixing that halved `xz` compression time:

| Component | Notes |
|---|---|
| GNU **coreutils 9.5** | the full tool set (~105 programs) builds + installs; core tools HW-verified bit-exact (`ls`, `cat`, `wc`, `sha256sum`, `seq`, `stat`, `stty`, …) |
| GNU **bash 5.2** | runs; see caveat below |
| **CPython 3.14** | static `python3` with `sqlite3`, `zlib`/`bz2`/`lzma` compression (full `tarfile`), `_ssl`/HTTPS (TLS 1.3, CA-verified), `hashlib` incl. `blake2`, `_decimal`, `ctypes`, `curses` (TUI via the ncurses port), and `.so` C-extension `dlopen`. Built in since 2026-09-30 but not yet exercised on the Pi: XML (`pyexpat`, `xml.etree`), C `asyncio`, `termios`, `cProfile`, `syslog`, and computed gotos in the eval loop |
| **OpenSSL 3.5.9** (LTS) | the `openssl` command and the TLS library behind Python's `ssl`/`hashlib`, lighttpd and wpa_supplicant; built `-O2` with the 64-bit NIST-curve code |
| **Dropbear 2026.94** | SSH server and client (`dropbear`, `dbclient`, `scp`): ed25519 keys, curve25519 and post-quantum (mlkem768x25519, sntrup761x25519) key exchange; password login from a current OpenSSH client |
| **lighttpd 1.4.79** | web server; serves `/usr/www` on port 80 as shipped (HTTPS once a certificate is installed) |
| **Redis 7.2.16** | in-memory data store, served over lwIP TCP (7.2.16 carries the CVE-2025-49844 fix) |
| **SQLite 3** | full SQL incl. the math functions, in-memory + on-disk file VFS |
| **jq** | JSON processor, incl. the `test`/`match`/`sub`/`gsub`/`splits`/`scan` **regex builtins** (Oniguruma) |
| **Lua 5.4.7** | interpreter + `luac` compiler |
| GNU **grep 3.11**, **sed 4.10**, **tar 1.35**, **gzip 1.15**, **xz 5.4.7** | the only copies of these tools in an image built since 2026-09-30 (BusyBox no longer builds its own) |
| **BusyBox**, **curl** (mbedTLS) | shell utilities (incl. `awk`, `vi`, `find`, `diff`, `bzip2`) + HTTP(S), FTP(S) and FILE client (with gzip/deflate decoding; the other protocols are not built) |

> **bash:** GNU bash 5.2 now runs as a **full interactive shell** at the console.
> The earlier "self-exits on EOF at the prompt" bug was a libphoenix `select()`
> bug — a NULL (infinite) timeout returned `0` immediately instead of blocking, so
> readline's input wait saw EOF — and is fixed. Pipes, loops, variables,
> conditionals, and command substitution all work interactively (HW-verified).

## Running the showcase

Boot the image to the `(psh)%` prompt, with an **HDMI display**, a **USB keyboard** and a
**USB mouse** attached. Then:

```
/bin/bash /bin/xfce-session       # the XFCE desktop on Wayland; Log Out returns to psh
quake3 +map q3dm1                 # Quake III Arena, full screen (also: quakespasm, quake2, vkquake)
game-res stk 1280x720 race        # SuperTuxKart, an AI race, 720p scaled to the screen (~22 fps)
vkquake +playdemo demo1           # vkQuake on Vulkan playing a recorded demo (~75 s to the first frame)
/bin/bash /bin/video-play /usr/share/video-demo/hevc-720p30-aac.mp4
                                  # video, full screen (in a window when run on the desktop)
/bin/bash /bin/startx             # X11: Xorg + glamor, Window Maker and the animated showcase desktop
wifi connect <ssid> <passphrase>  # join a WPA2 network; it is rejoined after every reboot
```

On the desktop, the **Games** menu opens each GL game in a window, **Multimedia** has the
video players, and **Office** has the Atril PDF viewer.

The [User Guide](docs/USER-GUIDE.md) describes all of this in detail, and its
[showcase section](docs/USER-GUIDE.md#9-the-showcase--the-best-setup-in-one-sitting) gives
the best order and settings for showing the whole system. The plan for re-recording the
showcase video is [docs/SHOWCASE-VIDEO-PLAN.md](docs/SHOWCASE-VIDEO-PLAN.md).

## Repository layout

```
phoenix-rpi/                     this coordination repo — docs, scripts, manifests
├── scripts/                     bootstrap, build, flash, and lab-rig helpers
├── manifests/                   pinned integration states for reproducible builds
├── docs/                        documentation (see links below)
├── tools/                       small helper programs staged into the image
│                                (ram-stage-play, game-res, …), probes and
│                                experiments; every shipped application is a
│                                framework port under sources/phoenix-rtos-ports/
├── sources/                     Phoenix-RTOS sibling repos (cloned by bootstrap)
│   ├── phoenix-rtos-kernel/
│   ├── phoenix-rtos-devices/
│   ├── phoenix-rtos-lwip/
│   ├── plo/                     the bootloader
│   └── ...                      (16 repos total)
└── external/                    optional clones: the game forks and research sources
```

The sibling repos under `sources/` are separate git repositories, not
submodules. Each has `origin` pointing at the phoenix-rtos upstream and `fork`
pointing at the `rpi-phoenix-rtos/*` work fork — see [CONTRIBUTING.md](CONTRIBUTING.md).

## Documentation

- **★ [docs/USER-GUIDE.md](docs/USER-GUIDE.md)** — **how to use the system**: booting,
  the XFCE desktop, the games, video, PDF, X11, WiFi and networking, and the showcase.
- **★ [docs/PHOENIX-RTOS-RPI4-CHANGES.md](docs/PHOENIX-RTOS-RPI4-CHANGES.md)** —
  **what this fork actually changed in Phoenix-RTOS.** The single clearest
  outline of the work: the defects this port found in *upstream's* own code, the
  platform gaps every future port will hit, and then a per-repository account of
  the kernel/bootloader, libc, driver, networking, filesystem, port and
  build-system changes — with the hardware measurements behind the performance
  claims and an honest statement of scope and open defects. Start here if you
  want to know what was done rather than how to build it.
- **[docs/BUILD.md](docs/BUILD.md)** — build a bootable SD image from an empty
  directory, and flash + boot it (Tier 1: no special hardware).
- **[docs/HARDWARE.md](docs/HARDWARE.md)** — the optional author's test lab
  (serial console, HDMI capture, netboot, smart-plug power). Not required to
  build or flash.
- **[docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md)** — open bugs, known
  limitations, and transitional shortcuts.
- **[docs/SHOWCASE-VIDEO-PLAN.md](docs/SHOWCASE-VIDEO-PLAN.md)** — the scene list and
  capture commands for the showcase video.
- **[docs/gpu-new-lane/PLAN.md](docs/gpu-new-lane/PLAN.md)** — the engineering history
  of the GPU stack (render server, display server, libdrm, Mesa GBM/EGL, SDL, Xorg,
  Wayland, XFCE): milestones, pre-registered experiments and their results.
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — the fork/branch model and how to send
  changes upstream to Phoenix-RTOS.

### Developing with agents

Much of this port was built with AI coding agents. The agent-facing rules and
session conventions live in [AGENTS.md](AGENTS.md) and
[CLAUDE.md](CLAUDE.md) — these are workflow documents for contributors using
agents, not required reading to build or use the port.

## License

Phoenix-RTOS and its components carry their own licenses (predominantly
BSD/MIT-style). The ports carry the licenses of their upstream projects (Mesa,
SDL, QuakeSpasm, yQuake2, quake3e, vkQuake, SuperTuxKart, X.org, GTK, XFCE,
Atril, FFmpeg, etc.). In particular the Quake framework ports
(`sources/phoenix-rtos-ports/{quakespasm_drm,yquake2,yquake2_drm,quake3,quake3_drm,vkquake_drm}/`)
— recipe, glue and patches — are **GPL-2.0-or-later**, and the SuperTuxKart ports are
**GPL-3.0-or-later** (derivatives of those GPL engines); they are showcase ports kept
separate from the BSD core. The WiFi firmware is not in this repository: the build fetches it
from linux-firmware and installs it with its licence (`LICENCE.cypress`). See
[LICENSING.md](LICENSING.md) for the full breakdown. The game data is **not
included in this repo**: the build fetches the freely-redistributable Quake
shareware/demo paks and the SuperTuxKart assets from pinned URLs into the image
*you* build, subject to their upstream terms.

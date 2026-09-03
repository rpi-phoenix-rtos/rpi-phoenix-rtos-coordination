# Phoenix-RTOS on the Raspberry Pi 4

A from-scratch port of [Phoenix-RTOS](https://phoenix-rtos.com/) — a small
microkernel, message-passing operating system — to the **Raspberry Pi 4
Model B / BCM2711** (Cortex-A72, AArch64).

The port was taken from "does not boot" to a system that boots to an
interactive shell, drives the real hardware, serves its root filesystem from
an SD card or over NFS, and runs a graphical userland: an X11 desktop with
Window Maker, a web browser, and **four id-Software engines rendering on the V3D
GPU** — GLQuake, Quake II and Quake III through OpenGL/GLES, and vkQuake through
Vulkan/V3DV. All four ship on the SD image, along with a **modern 3D game,
SuperTuxKart 1.4**, which renders on the same GPU via OpenGL ES (an in-game race
was verified on hardware in August 2026, close to frame-for-frame with the same
game on a desktop AMD GPU; what is confirmed on the current clean image is in
[Which games end up on the card?](#which-games-end-up-on-the-card)).

> This repository is the **coordination repo** — docs, build scripts, and
> integration manifests. The Phoenix-RTOS source lives in sibling repositories
> cloned under `sources/` (see [Repository layout](#repository-layout)).

> ⚙️ **Toolchain — GCC 16.2.0 + binutils 2.47 (the default working setup).** The port
> builds with an up-to-date **GCC 16.2.0** aarch64-phoenix cross-toolchain and the latest
> **binutils 2.47** — a big jump from the previous GCC 14.2.0 / binutils 2.43. This is the
> **default toolchain** (a fresh `bootstrap-linux-host.sh` builds it; the previous gcc-14
> is kept as a rollback). The **entire system is gcc-16-built and HW-verified**: the kernel,
> drivers, libc, lwip and NFS boot to a shell and serve the NFS root on real Pi 4 hardware,
> and a full `--with-ports` image (coreutils, bash, jq, Python, busybox, curl, …) builds on
> gcc-16 and boots + runs on the Pi with 0 faults. (A Docker-reproducible gcc-16 release build
> is the remaining nice-to-have.) Details in the
> [gcc-16 release plan](docs/done/gcc16-release-plan.md).

> 🚀 **First time here?** [**TUTORIAL.md**](TUTORIAL.md) is a single,
> self-contained walkthrough: build the image, flash an SD card, boot the Pi,
> and launch everything in the distribution — the five game engines, the X11
> desktop with Window Maker, `dillo`, Python, Lua, and more.
> *(Tested on a Raspberry Pi 4 Model B with 4 GB RAM.)*
>
> 🌐 **Want a network dev setup?** [**TUTORIAL-NETBOOT.md**](TUTORIAL-NETBOOT.md)
> shows how to build from source and boot the Pi entirely over the network
> (DHCP + TFTP + NFS root, no SD card) — the fast edit-rebuild-run loop.

## Quick start

On a fresh Ubuntu x86_64 machine, from an empty directory:

```bash
git clone https://github.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination.git ~/phoenix-rpi
cd ~/phoenix-rpi
./scripts/bootstrap-linux-host.sh            # installs deps, clones sources, builds the toolchain
./scripts/rebuild-rpi4b-fast.sh --variant sd # builds artifacts/rpi4b/rpi4b-sd-2part.img
```

Flash the resulting image to a microSD card and boot it on a Pi 4. The full
walkthrough — prerequisites, timings, flashing, and first-boot expectations —
is in **[docs/BUILD.md](docs/BUILD.md)**.

## Build with Docker (reproducible, any host OS)

The whole build is packaged as a **single, self-contained Dockerfile**. It works on
any machine with a Docker CLI (Linux/macOS/Windows) regardless of host OS or
installed packages — the entire toolchain runs inside a container we fully control.
Nothing is copied from the host: every source tree, Ubuntu package, font, and the
freely-downloadable game data (Quake I shareware, the Quake II and Quake III
demos, and the SuperTuxKart 1.4 assets) is fetched over the network at build time
(each from a pinned URL) and baked into the image
*you* build — this repo distributes only the build scripts, never a built image.

### Before you start

| | |
|---|---|
| **Docker** | a CLI with **BuildKit/buildx**. Docker Desktop bundles it; on a minimal Linux `docker.io` install run `sudo apt-get install docker-buildx`. |
| **Disk** | ~35 GB free for the build (it clones Mesa, builds a GCC cross-toolchain, then the whole OS). |
| **Time** | 40–90 min on a modern 8-core machine, most of it the toolchain. |
| **Network** | the build clones from GitHub/freedesktop and downloads the game data. |

> **macOS / colima:** give the VM enough room up front — the default is too small
> and the build dies deep in the toolchain stage:
> ```bash
> colima start --cpu 8 --memory 16 --disk 100
> ```

### Copy-paste: build a bootable SD image

Pick **one** of these. Each is a complete recipe: build, then export the image.

**1. Base system** — kernel, drivers, `psh`, networking. No games, fastest build.

```bash
mkdir -p out
docker build -t phoenix-rpi \
  --build-arg BUILD_FLAGS="" \
  https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile
docker run --rm -v "$PWD/out":/out phoenix-rpi
# -> ./out/rpi4b-sd-2part.img
```

**2. The showcase image (recommended)** — everything above **plus** the X11 desktop
(`wmaker`, `xterm`, `xclock`, `xcalc`), the ported command-line apps (`bash`,
`python3`, coreutils, …) and **all five game engines** (GLQuake, vkQuake,
Quake II, Quake III, SuperTuxKart) running on the V3D GPU. This is the default if
you pass no `BUILD_FLAGS` at all:

```bash
mkdir -p out
docker build -t phoenix-rpi \
  --build-arg BUILD_FLAGS="--with-showcase --with-ports" \
  https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile
docker run --rm -v "$PWD/out":/out phoenix-rpi
```

Recipe 2 already includes the V3DV Vulkan stack and vkQuake — the Vulkan path is
built by default, and `--with-vkquake` is now a no-op kept only for compatibility.
There is no third recipe and nothing to swap by hand.

Then flash `./out/rpi4b-sd-2part.img` exactly as in
[docs/BUILD.md](docs/BUILD.md) (macOS/Linux `dd`, or Raspberry Pi Imager's
"Use custom" option), put the card in the Pi, and power on.

### Which games end up on the card?

**All five engines.** Each is built by the ports framework (all five are registered
`if: true` in the project's `ports.yaml`) and installed **into the rootfs**, so
`--with-showcase` ships every one of them on the same card. Nothing is swapped by
hand, and no game lives in the boot blob: `loader.disk` is **4.5 MB and contains
zero game bytes** (it was ~22 MB back when GLQuake was bundled into it). Games are
launched from the rootfs; binaries of 18–38 MB exec fine from both the ext2 root
and the NFS root — `supertuxkart` is 38 MB.

| Engine | Binary on the card | How to run it |
|---|---|---|
| **GLQuake** (Quake I, OpenGL) | `/usr/bin/quakespasm` | `quakespasm` |
| **vkQuake** (Quake I, Vulkan) | `/usr/bin/vkquake` | `vkquake` |
| **Quake II** (yQuake2, gl3/GLES3) | `/usr/bin/yquake2` | `quake2` (launcher) |
| **Quake III** (quake3e) | `/usr/bin/quake3e` | `quake3 +map q3dm1` (launcher) |
| **SuperTuxKart 1.4** | `/usr/bin/supertuxkart` | `stk` (launcher) |

What has been verified on the hardware from the clean image, each with an HDMI
capture under `artifacts/hdmi/`:

- **GLQuake** — full-screen in-game (`20260903-032501-final-qs-tick.png`). It needs
  the `id1/config.cfg` the image ships: QuakeSpasm's SDL2 path defaults to
  800x600, which renders a small frame inside the 1080p scanout.
- **Quake II** — full textured 3D through the `quake2` launcher
  (`20260903-020858-relink-q2-tick.png`).
- **Quake III** — full 3D gameplay **on the free demo data**
  (`20260903-051855-q3-restore-tick.png`); see the note below.
- **vkQuake** — renders the start map on Vulkan/V3DV
  (`20260903-040557-vkq-rep2-tick.png`).
- **GPU-accelerated X11 desktop** (`startx_gpu deskapps`) — Window Maker plus an
  xterm with a live shell, `xclock` and `xcalc`
  (`20260903-053119-final-xgpu-tick.png`).
- **SuperTuxKart** — its GPU-drawn UI renders with **0 wedges and 0 faults**, but
  the 194 MB of assets served over NFS does not finish loading inside a ~5 minute
  window, so **in-game is not yet verified on the clean image**.

**Quake III needs no retail content and no retail CD key.** Besides the free demo
`pak0.pk3` it needs two more files, both staged by `scripts/stage-game-data.sh`
from `assets/quake3-qvm/`: a `pak1.pk3` holding three QVMs we built from
**ioquake3** (the demo's 1999 QVMs report UI API 3, while quake3e requires 6), and
a `q3key` file whose **format alone** is checked. Honest caveat: the QVM build
recipe is not yet in this repo, so that pak is *staged* rather than rebuilt from
source — see [`assets/quake3-qvm/README.md`](assets/quake3-qvm/README.md).

Game data for all five engines is staged into the rootfs overlay by
`scripts/stage-game-data.sh` (the Docker build calls the same script), under
`/usr/share/{quake,quake2,quake3,supertuxkart}`. The engine binaries are
**byte-identical across the build tree, the NFS export and the SD ext2 image** —
`scripts/compare-rootfs-binaries.sh` checks ten binaries and all ten match — so a
game verified over netboot is the same artifact that runs from the card.

### Other build knobs

See the header of [`Dockerfile`](Dockerfile). The useful ones: `UBUNTU_TAG` (base
LTS, default `26.04` — the validated build host), `PAK0_URL` / `PAK0Q2_URL` /
`PAK0Q3_URL` (Quake game-data URLs, each defaulting to a verified upstream
demo/shareware mirror; set one to `""` to build that engine without bundled data),
`STK_ASSETS_URL` / `STK_ASSETS_SHA256` (the pinned SuperTuxKart 1.4 asset
package), `BUILD_VARIANT`
(`sd` / `nfsroot` / `netboot`), and `BUILD_FLAGS` (above; `--with-tests` adds the
`/bin/test-*` suites).

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
| HDMI framebuffer device `/dev/fb0` | 🟡 | Byte read/write + a custom `RPI4FB_GETMODE` devctl work; the standard Linux `FBIOGET_VSCREENINFO`/`FSCREENINFO` ioctls and a true `mmap()` of the framebuffer are **not implemented**, and display ownership vs the fbcon console is not arbitrated — so a stock Linux fbdev app can't mmap the surface (the X server + GL/Quake use the byte-write + GETMODE path instead) |
| GENET gigabit Ethernet + lwIP | ✅ | IRQ-driven, ~0.9 ms ping RTT, autonomous DHCP |
| USB host (PCIe → VL805 xHCI) | ✅ | Enumerates reliably from cold boot |
| USB HID (keyboard + mouse) | ✅ | `/dev/kbd0`, `/dev/mouse0`; live keys reach psh and apps |
| SD card (EMMC2 SDHCI) | ✅ | `/dev/mmcblk0`, MBR partitions; UHS-I DDR50 + SDMA multi-block reads (~38 MB/s), PIO multi-block writes (~17 MB/s), 0 corruption |
| ext2 persistent root | ✅ | Mounts as `/`, binaries exec from the card |
| NFS root | ✅ | `/` served over NFS (`takeover` design); over gigabit ~30 MB/s read / ~20 MB/s write (bit-exact, 0 faults) |
| SoC thermal + throttle | ✅ | `/dev/thermal`, `/dev/throttled` via VideoCore mailbox |
| Hardware RNG (RNG200) | ✅ | `/dev/hwrng`; also backs `/dev/urandom` |
| GPIO observer | 🟡 | `/dev/gpio` read-only snapshot; outputs attended |
| GPU (V3D 4.2) — OpenGL | ✅ | Ported Mesa `v3d` Gallium + GL → **GLQuake ~40 fps @ 1080p** |
| GPU (V3D 4.2) — Vulkan (V3DV) | ✅ | Ported Mesa `v3dv` Vulkan driver on real V3D 4.2 — init, texture upload (no-WSI buffer→image copy), SPIR-V vertex/fragment/compute shaders and render passes all execute on the GPU (HW-validated); **vkQuake renders the start map** — re-verified on the clean SD image, `artifacts/hdmi/20260903-040557-vkq-rep2-tick.png`. (An intermittent V3D binner wedge on long GPU runs — not Vulkan-specific — is tracked separately.) Fork: [rpi-phoenix-rtos/vkQuake](https://github.com/rpi-phoenix-rtos/vkQuake), branch `phoenix-rpi4-port` |
| GPU concurrency (`v3d-server`) | ✅ | A userspace **`v3d-server` daemon** (`/dev/v3d-srv`, `/sbin/rpi4-v3d`) owns the single V3D and serializes GPU submits from multiple clients over a message port, so **an accelerated X desktop and a second GPU program can run at the same time**. HW-proven end-to-end: BO/compute/render/TFU submit bit-exact through the daemon, two concurrent compute clients serialized, and a glamor GPU-accelerated X desktop with a **live GPU-rendered window running concurrently** on one screen. Lifts the earlier single-GPU-process limit. Clients link `libv3d-client`; opt-in today (not the default boot). Details: [docs/misc/2026-08-22-concurrent-gpu-v3d-server-feasibility.md](docs/misc/2026-08-22-concurrent-gpu-v3d-server-feasibility.md) |
| Audio (PWM, 3.5 mm jack) | 🟡 | `/dev/audio0` streaming DMA; Quakespasm audio backend |
| X11 / windowing (kdrive) | ✅ | Xphoenix **fbdev DDX** (CPU shadow-blit — the default, always-on path) + kbd/mouse; WindowMaker/JWM/twm, xterm/xcalc/xedit/xeyes/xclock. Migrated to real `phoenix-rtos-ports` (the X server, xterm, WindowMaker and dillo build as framework ports). An **experimental glamor build** additionally runs GPU-accelerated 2D X on the V3D GPU — `startx_gpu deskapps` on the clean SD image brings up Window Maker with an xterm running a live shell plus `xclock` and `xcalc` (`artifacts/hdmi/20260903-053119-final-xgpu-tick.png`; known cosmetic issue: the root window paints black instead of mauve) — and, via the `v3d-server` daemon (row above), can now do so **concurrently with another GPU client** (accelerated desktop + a live GPU window at once), lifting the former single-GPU-process restriction. Modern modesetting/DRM remains a future goal |
| posixsrv / psh userland | ✅ | pipes, ptys, `/dev/{null,zero,urandom,full}`, AF_UNIX |
| WiFi (BCM43455 SDIO) | 🟡 | **Joins WPA2 + gets a DHCP IP lease over the air** — associates to a real WPA2-PSK AP, completes the 4-way handshake, and carries real traffic: a full DHCP exchange (DISCOVER→OFFER→REQUEST→ACK) binds an IP, confirmed by the AP's `DHCPACK` (`tools/wifi-probe jointxcnt`). Remaining: an lwip netif so arbitrary sockets use WiFi — until then **use wired Ethernet** for general networking |
| Bluetooth (BCM43455) | 🟡 | **Driver-level bring-up works** — `/dev/hci0` up, firmware patchram loads (323/323), a real BD_ADDR is read, and an HCI Inquiry completes. **No host Bluetooth stack** — no pairing, profiles, or audio yet |
| USB mass-storage, I²C/SPI/PWM, camera (CSI-2) | ⬜ | Not started |

The authoritative, per-peripheral matrix (with evidence and remaining work) is
[docs/pi4-hardware-support-matrix.md](docs/pi4-hardware-support-matrix.md).
Open bugs and known limitations are in
[docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).

## Userland: CLI tools and languages

Beyond the base system, a substantial ports ecosystem runs on the hardware
(built into the image with `--with-ports`; all HW-verified):

| Component | Notes |
|---|---|
| GNU **coreutils 9.5** | the full tool set (~105 programs) builds + installs; core tools HW-verified bit-exact (`ls`, `cat`, `wc`, `sha256sum`, `seq`, `stat`, `stty`, …) |
| GNU **bash 5.2** | runs; see caveat below |
| **CPython 3.14** | static `python3` with `sqlite3`, `zlib`/`bz2`/`lzma` compression (full `tarfile`), `_ssl`/HTTPS, `hashlib` incl. `blake2`, `_decimal`, `ctypes`, `curses` (TUI via the ncurses port), and `.so` C-extension `dlopen` |
| **Redis 7.2** | in-memory data store, served over lwIP TCP |
| **SQLite 3** | full SQL, in-memory + on-disk file VFS |
| **jq** | JSON processor, incl. the `test`/`match`/`sub`/`gsub`/`splits`/`scan` **regex builtins** (Oniguruma) |
| **Lua 5.4.7** | interpreter + `luac` compiler |
| **BusyBox**, **curl** (mbedTLS) | shell utilities (incl. `awk`, `vi`, `tar` with seamless gz/bz2/xz, `xzcat`/`unxz`) + HTTP/HTTPS client (with gzip/deflate decoding) |

> **bash:** GNU bash 5.2 now runs as a **full interactive shell** at the console.
> The earlier "self-exits on EOF at the prompt" bug was a libphoenix `select()`
> bug — a NULL (infinite) timeout returned `0` immediately instead of blocking, so
> readline's input wait saw EOF — and is fixed. Pipes, loops, variables,
> conditionals, and command substitution all work interactively (HW-verified).

## Running the showcase apps

Boot the image and log in to the `(psh)%` prompt, with an **HDMI display** and a
**USB keyboard** attached (plus a **USB mouse** for the X11 desktop). Then:

### GLQuake (Quake 1)

```
quakespasm
```

Renders the shareware episode in textured 3D on the V3D GPU (~40 fps @ 1080p);
verified full-screen in-game on the clean image
(`artifacts/hdmi/20260903-032501-final-qs-tick.png`). The shareware `pak0` is
baked into the image at `/usr/share/quake/id1/`, together with a `config.cfg`
that selects 1920x1080 — without it QuakeSpasm's SDL2 path defaults to 800x600
and renders a small frame inside the 1080p scanout. Open the in-game console with
`` ` `` and type `quit` to exit (or Esc → menu → Quit). GLQuake links the V3D
driver in-process, so no separate GPU daemon is needed.

### X11 desktop (twm / Window Maker)

The `startx` launcher starts the Xphoenix server plus a session in one command:

```
startx              # Window Maker desktop (the default session)
startx term         # twm + an xterm you can type in
startx desktop      # twm + xeyes
startx deskapps     # twm + xterm + xclock + xcalc + xeyes
```

For **GPU-accelerated** X (experimental glamor server on the V3D GPU, via the
`rpi4-v3d` daemon), use **`startx_gpu`** with the exact same modes — it
auto-starts the GPU daemon and renders the desktop on the GPU:

```
startx_gpu          # Window Maker, GPU-accelerated (glamor on V3D 4.2)
startx_gpu term     # twm + xterm, GPU-accelerated
```

Drive the desktop with the USB mouse + keyboard. Exit the window manager to tear
down X and return to `(psh)%`. If a crash ever leaves a stale lock, `rm -f
/tmp/.X0-lock` and relaunch.

`startx_gpu deskapps` was verified on the clean image — Window Maker with an
xterm running a live shell, plus `xclock` and `xcalc`
(`artifacts/hdmi/20260903-053119-final-xgpu-tick.png`). One known cosmetic issue:
the root window paints black instead of mauve.

### Midnight Commander and nano

Both build again as of 2026-09-03 and are staged into the image. Each had been
broken by a stale-artifact bug rather than a missing feature: `mc`'s build script
copied its own obsolete `mntent.h` stub over the shared sysroot header (hiding
the `hasmntopt` that libphoenix now implements), and `nano` 2.2.6 initializes a
`bool` from `NULL`, which GCC 14+ rejects. Both are terminal UIs and need `TERM`
set for correct rendering over the console (`TERM=vt100 mc`,
`TERM=vt100 nano /etc/profile`). Neither has been exercised interactively on the
hardware yet — they are built, linked and staged, not use-tested.

### Quake II, Quake III, vkQuake

All three ship on the image; run them from `psh`:

```
quake2                  # yQuake2, gl3/GLES3 renderer (launcher: RAM-stages assets)
quake3 +map q3dm1       # quake3e (launcher: RAM-stages assets)
vkquake                 # Quake I through Vulkan / V3DV
```

Verified on the clean image: **Quake II** renders full textured 3D
(`artifacts/hdmi/20260903-020858-relink-q2-tick.png`); **Quake III** renders full
3D gameplay **on the free demo data**
(`artifacts/hdmi/20260903-051855-q3-restore-tick.png`) — it needs no retail
content and no retail CD key, see the note in
[Which games end up on the card?](#which-games-end-up-on-the-card); **vkQuake**
renders the start map (`artifacts/hdmi/20260903-040557-vkq-rep2-tick.png`). The
Vulkan stack is built by default — `--with-vkquake` is a no-op kept for
compatibility.

### SuperTuxKart 1.4

A **modern 3D kart racer** — not a 1990s engine — running on the V3D GPU via
its SP renderer on **OpenGL ES 3.x**:

```
stk                                  # launch SuperTuxKart
stk -N --track=olivermath            # auto-race flags: drive a race with no input
```

SuperTuxKart is built by the `supertuxkart` framework port and **ships on the
image** (`/usr/bin/supertuxkart`, launched via `stk`); its two asset roots
(`data/` plus `stk-assets/`, 194 MB together) are staged into the rootfs by
`scripts/stage-game-data.sh`.

On the current clean image, what is verified is that STK's **GPU-drawn UI renders
with 0 wedges and 0 faults** — but the 194 MB of assets served over NFS does not
finish loading inside a ~5 minute window, so **in-game is not yet verified on the
clean image**. Booting from the SD card, where the assets are local, is the way to
test that.

Earlier, on the hand-staged export (2026-08-27), `stk` was HW-verified booting to
a clean main menu and driving a **fully-lit in-game 3D race** — kart, opponents,
textured track, lighting and HUD all on the GPU, 0 crashes — and its rendering
was checked frame-for-frame against the same SuperTuxKart 1.4 on a desktop AMD
GPU, matching closely (main-menu SSIM 0.991, in-race 0.873); see
[docs/done/2026-08-27-stk-visual-parity.md](docs/done/2026-08-27-stk-visual-parity.md).
The `-N` auto-race flags drive a race without any input, which is the simplest
way to see it in motion.

## Repository layout

```
phoenix-rpi/                     this coordination repo — docs, scripts, manifests
├── scripts/                     bootstrap, build, flash, and lab-rig helpers
├── manifests/                   pinned integration states for reproducible builds
├── docs/                        documentation (see links below)
├── tools/                       out-of-tree work: the GPU/Mesa stack, game
│                                launchers, probes, and superseded ad-hoc recipes
│                                (the shipped game engines build as framework
│                                ports under sources/phoenix-rtos-ports/)
├── sources/                     Phoenix-RTOS sibling repos (cloned by bootstrap)
│   ├── phoenix-rtos-kernel/
│   ├── phoenix-rtos-devices/
│   ├── phoenix-rtos-lwip/
│   ├── plo/                     the bootloader
│   └── ...                      (16 repos total)
└── external/                    build-required deps (mesa, quakespasm, vkquake)
```

The sibling repos under `sources/` are separate git repositories, not
submodules. Each has `origin` pointing at the phoenix-rtos upstream and `fork`
pointing at the `rpi-phoenix-rtos/*` work fork — see [CONTRIBUTING.md](CONTRIBUTING.md).

## Documentation

- **[docs/BUILD.md](docs/BUILD.md)** — build a bootable SD image from an empty
  directory, and flash + boot it (Tier 1: no special hardware).
- **[docs/HARDWARE.md](docs/HARDWARE.md)** — the optional author's test lab
  (serial console, HDMI capture, netboot, smart-plug power). Not required to
  build or flash.
- **[docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md)** — open bugs, known
  limitations, and transitional shortcuts.
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
QuakeSpasm, yQuake2, quake3e, vkQuake, SuperTuxKart, X.org, etc.). In particular
the four Quake framework ports under
`sources/phoenix-rtos-ports/{quakespasm,yquake2,quake3,vkquake}/` — recipe, glue
and patches — are **GPL-2.0-or-later** and the `supertuxkart` port is
**GPL-3.0-or-later** (derivative of those GPL engines); they are optional,
opt-in showcases kept separate from the BSD core. See
[LICENSING.md](LICENSING.md) for the full breakdown. The game data is **not
included in this repo**: the build fetches the freely-redistributable Quake
shareware/demo paks and the SuperTuxKart assets from pinned URLs into the image
*you* build, subject to their upstream terms.

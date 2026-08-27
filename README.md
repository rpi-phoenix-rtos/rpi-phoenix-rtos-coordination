# Phoenix-RTOS on the Raspberry Pi 4

A from-scratch port of [Phoenix-RTOS](https://phoenix-rtos.com/) — a small
microkernel, message-passing operating system — to the **Raspberry Pi 4
Model B / BCM2711** (Cortex-A72, AArch64).

The port was taken from "does not boot" to a system that boots to an
interactive shell, drives the real hardware, serves its root filesystem from
an SD card or over NFS, and runs a graphical userland: an X11 desktop with
Window Maker, a web browser, and **GLQuake rendering on the V3D GPU via
OpenGL** (a Vulkan/V3DV path drives the GPU too — vkQuake renders its full
textured 3D map on the GPU, though its input is not yet wired — see the
capabilities table).

> This repository is the **coordination repo** — docs, build scripts, and
> integration manifests. The Phoenix-RTOS source lives in sibling repositories
> cloned under `sources/` (see [Repository layout](#repository-layout)).

> ⚙️ **Toolchain modernization — GCC 16.2.0 + binutils 2.47.** The port has moved
> to an up-to-date **GCC 16.2.0** aarch64-phoenix cross-toolchain and the latest
> **binutils 2.47** — a big jump from the previous GCC 14.2.0 / binutils 2.43. This
> is now the **default toolchain** (a fresh `bootstrap-linux-host.sh` builds it),
> and the gcc-16 system (kernel, drivers, libc, lwip, NFS) **boots to a shell and
> serves its NFS root on real Pi 4 hardware**. A full gcc-16 rebuild of the ports +
> X11 stack and a **Docker-reproducible gcc-16-based release** are in final
> validation. Follow the roadmap in the
> [gcc-16 release plan](docs/inprogress/gcc16-release-plan.md).

> 🚀 **First time here?** [**TUTORIAL.md**](TUTORIAL.md) is a single,
> self-contained walkthrough: build the image, flash an SD card, boot the Pi,
> and launch everything in the distribution — GLQuake, the X11 desktop with
> Window Maker, `mc`, `dillo`, the editors, MicroPython, Lua, and more.
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

The whole build is also packaged as a **single, self-contained Dockerfile**. It
works on any machine with a Docker CLI (Linux/macOS/Windows) regardless of host
OS or installed packages — the entire toolchain runs inside a container we fully
control. Nothing is copied from the host: every source tree, Ubuntu package, font,
and the freely-downloadable id Software game data (Quake I shareware + the Quake II
and Quake III demos) is fetched over the network at build time and baked into the
image *you* build — this repo distributes only the build scripts, never a built image.

Requires a Docker CLI with **BuildKit/buildx** (Docker Desktop bundles it; on a
minimal Linux docker.io install run `sudo apt-get install docker-buildx`).

```bash
mkdir -p out
# 1. build the image (clones all sources, builds the cross toolchain, builds the SD image)
docker build -t phoenix-rpi \
  https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/Dockerfile
# 2. export the finished SD-card image to ./out/
docker run --rm -v "$PWD/out":/out phoenix-rpi
# -> ./out/rpi4b-sd-2part.img   (flash it exactly like the native build)
```

Useful `--build-arg`s (see the header of [`Dockerfile`](Dockerfile)): `UBUNTU_TAG`
(base LTS, default `26.04` — the validated build host), `PAK0_URL` / `PAK0Q2_URL` /
`PAK0Q3_URL` (Quake I/II/III game-data URLs, each defaulting to a verified upstream
demo/shareware mirror; set one to `""` to build that engine without bundled data),
`BUILD_VARIANT` (`sd`/`nfsroot`/`netboot`), `BUILD_FLAGS` (default
`--with-showcase --with-ports`; use `""` for a base image).

> **Building from a local checkout (before this port is on public GitHub):** the
> Dockerfile clones from `REPO_BASE` (default GitHub). To build the current tree
> without pushing, run `./scripts/build-sd-in-docker.sh` — it serves this repo (and
> the sibling/external repos, committed state) over a local git+http server and
> points the container at it, then exports to `./docker-out/`.

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
| GPU (V3D 4.2) — Vulkan (V3DV) | ✅ | Ported Mesa `v3dv` Vulkan driver on real V3D 4.2 — init, texture upload (no-WSI buffer→image copy), SPIR-V vertex/fragment/compute shaders and render passes all execute on the GPU (HW-validated); **vkQuake renders the full textured 3D start map**. The only remaining WIP is app-level: vkQuake's **keyboard/mouse input is not yet wired** (an intermittent V3D binner wedge on long GPU runs — not Vulkan-specific — is tracked separately). Fork: [rpi-phoenix-rtos/vkQuake](https://github.com/rpi-phoenix-rtos/vkQuake), branch `phoenix-rpi4-port` |
| GPU concurrency (`v3d-server`) | ✅ | A userspace **`v3d-server` daemon** (`/dev/v3d-srv`, `/sbin/rpi4-v3d`) owns the single V3D and serializes GPU submits from multiple clients over a message port, so **an accelerated X desktop and a second GPU program can run at the same time**. HW-proven end-to-end: BO/compute/render/TFU submit bit-exact through the daemon, two concurrent compute clients serialized, and a glamor GPU-accelerated X desktop with a **live GPU-rendered window running concurrently** on one screen. Lifts the earlier single-GPU-process limit. Clients link `libv3d-client`; opt-in today (not the default boot). Details: [docs/inprogress/2026-08-22-concurrent-gpu-v3d-server-feasibility.md](docs/inprogress/2026-08-22-concurrent-gpu-v3d-server-feasibility.md) |
| Audio (PWM, 3.5 mm jack) | 🟡 | `/dev/audio0` streaming DMA; Quakespasm audio backend |
| X11 / windowing (kdrive) | ✅ | Xphoenix **fbdev DDX** (CPU shadow-blit — the default, always-on path) + kbd/mouse; WindowMaker/JWM/twm, xterm/xcalc/xedit/xeyes/xclock, plus mc/nano. Migrated to real `phoenix-rtos-ports` (the X server, xterm, WindowMaker and dillo build as framework ports). An **experimental glamor build** additionally runs GPU-accelerated 2D X on the V3D GPU — and, via the `v3d-server` daemon (row above), can now do so **concurrently with another GPU client** (accelerated desktop + a live GPU window at once), lifting the former single-GPU-process restriction. Modern modesetting/DRM remains a future goal |
| posixsrv / psh userland | ✅ | pipes, ptys, `/dev/{null,zero,urandom,full}`, AF_UNIX |
| WiFi (BCM43455 SDIO) | 🟡 | **Control-plane works** — associates to a real WPA2-PSK AP and completes the 4-way key handshake. The **data-plane does not carry traffic yet** (under active debugging). Not usable for wireless networking — **use wired Ethernet** |
| Bluetooth (BCM43455) | 🟡 | **Driver-level bring-up works** — `/dev/hci0` up, firmware patchram loads (323/323), a real BD_ADDR is read, and an HCI Inquiry completes. **No host Bluetooth stack** — no pairing, profiles, or audio yet |
| USB mass-storage, I²C/SPI/PWM, camera (CSI-2) | ⬜ | Not started |

The authoritative, per-peripheral matrix (with evidence and remaining work) is
[docs/inprogress/pi4-hardware-support-matrix.md](docs/inprogress/pi4-hardware-support-matrix.md).
Open bugs and known limitations are in
[docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).

## Userland: CLI tools and languages

Beyond the base system, a substantial ports ecosystem runs on the hardware
(built into the image with `--with-ports`; all HW-verified):

| Component | Notes |
|---|---|
| GNU **coreutils 9.5** | ~102 tools built + HW-verified (`ls`, `cat`, `wc`, `sha256sum`, `seq`, …); a handful skipped that need OS facilities Phoenix lacks (`stat`, `stty`, `df`, …) |
| GNU **bash 5.2** | runs; see caveat below |
| **CPython 3.14** | static `python3` with `sqlite3`, `zlib`, `_ssl`/HTTPS, `_decimal`, `ctypes`, and `.so` C-extension `dlopen` |
| **Redis 7.2** | in-memory data store, served over lwIP TCP |
| **SQLite 3** | full SQL, in-memory + on-disk file VFS |
| **jq** | JSON processor |
| **Lua 5.4.7** | interpreter + `luac` compiler |
| **BusyBox**, **curl** (mbedTLS) | shell utilities + HTTP/HTTPS client |

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
rpi4-quake
```

Renders the shareware episode in textured 3D on the V3D GPU (~40 fps @ 1080p).
The shareware `pak0` is baked into the image at `/usr/share/quake/id1/`. Open the
in-game console with `` ` `` and type `quit` to exit (or Esc → menu → Quit).
GLQuake links the V3D driver in-process, so no separate GPU daemon is needed.

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

### Midnight Commander and nano

Both are terminal UIs, so set `TERM` for correct rendering over the console:

```
TERM=vt100 mc                   # file manager; quit with F10
TERM=vt100 nano /etc/profile    # editor; quit with Ctrl-X
```

### Quake II, Quake III, vkQuake

These build but are **not yet part of the default `--with-showcase` image**
(GLQuake is the bundled game). vkQuake additionally needs `--build-arg
BUILD_FLAGS="--with-showcase --with-vkquake"` and renders the menu and the full
textured 3D start map on the GPU; the remaining work-in-progress is **input
wiring** (keyboard/mouse not yet delivered to the game) — see
[docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md). Wiring Quake II/III into the
default image build is tracked as a follow-up.

## Repository layout

```
phoenix-rpi/                     this coordination repo — docs, scripts, manifests
├── scripts/                     bootstrap, build, flash, and lab-rig helpers
├── manifests/                   pinned integration states for reproducible builds
├── docs/                        documentation (see links below)
├── tools/                       out-of-tree ports (X11, GPU/Mesa, quake engines)
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
BSD/MIT-style). The out-of-tree ports under `tools/` and `external/` carry the
licenses of their upstream projects (Mesa, Quakespasm, vkQuake, X.org, etc.).
In particular the Quake/vkQuake platform glue under `tools/quakespasm-port/` and
`tools/vkquake-port/` is **GPL-2.0-or-later** (derivative of those GPL engines) —
optional, opt-in showcases kept separate from the BSD core. See
[LICENSING.md](LICENSING.md) for the full breakdown. The Quake game data is not
included and is subject to id Software's terms.

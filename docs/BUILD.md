# Building and flashing Phoenix-RTOS for the Raspberry Pi 4

This is the **Tier 1 "build & flash" path**: everything a newcomer needs to go
from an empty directory on a stock Ubuntu machine to a booting Raspberry Pi 4 —
with **no special hardware** (no serial adapter, HDMI capture, dedicated
netboot NIC, or smart plug). The optional lab rig those things enable is
documented separately in [HARDWARE.md](HARDWARE.md).

The end result is a bootable 2-partition microSD image
(`artifacts/rpi4b/rpi4b-sd-2part.img`): a FAT boot partition plus an ext2 root
filesystem.

## Prerequisites

- **Supported host:** Ubuntu x86_64 (24.04 or newer). The build runs directly
  on the host — no VM.
- **A Raspberry Pi 4 Model B** (any RAM tier; the port is validated on the
  4 GB model) and a **microSD card** (4 GB or larger).
- **A network connection during the build.** The build is not fully offline:
  the toolchain build and several userspace ports (`phoenix-rtos-ports`,
  X.org tarballs) download their sources at build time.
- **Disk space:** the bootstrap clones several GB of sources (Mesa alone is
  ~1 GB) and the toolchain + buildroot need a few GB more. Budget ~15 GB free.
- **`sudo` access** — the bootstrap installs apt packages and the `uv` Python
  tool.

### Host packages

The bootstrap script installs everything for you. If you prefer to install the
build-&-flash dependencies by hand first, this is the authoritative list
(it is also encoded in `scripts/bootstrap-linux-host.sh` as `APT_PACKAGES`
and quoted in [`manifests/release-pin.md`](../manifests/release-pin.md)):

```bash
sudo apt-get update && sudo apt-get install -y --no-install-recommends \
  build-essential bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev wget xz-utils \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu cmake pkg-config make \
  autoconf automake libtool libhidapi-dev device-tree-compiler \
  mtools dosfstools parted e2fsprogs \
  git git-lfs curl jq python3 python3-pip python3-venv
# uv (the Python venv tool) is not in apt:
curl -LsSf https://astral.sh/uv/install.sh | sh
```

(The extra packages `dnsmasq iproute2 tio picocom ffmpeg v4l-utils gh` are only
needed for the optional Tier-2 lab rig; the bootstrap installs them too, but
they are not required to build and flash.)

## Step 1 — Clone the coordination repo

```bash
git clone https://github.com/houp/phoenix-rpi.git ~/phoenix-rpi
cd ~/phoenix-rpi
```

Everything below is run from this directory.

> Clone into `~/phoenix-rpi` as shown. The bootstrap defaults to that location
> for the sources, toolchain, and firmware blobs (override with the
> `PROJECT_DIR` environment variable if you must use another path); cloning
> elsewhere without setting `PROJECT_DIR` would split the tree and the build
> would not find them.

## Step 2 — Bootstrap the environment

```bash
./scripts/bootstrap-linux-host.sh
```

This is idempotent — safe to re-run if anything fails partway. It:

1. Installs the host packages listed above (via `sudo apt-get`) plus `uv`.
2. Clones the 16 Phoenix-RTOS sibling repos into `sources/<repo>/`, each wired
   with `fork` = `github.com/houp/<repo>` and `origin` = the phoenix-rtos
   upstream.
3. Clones the build-required external deps (Mesa, Quakespasm, vkQuake) into
   `external/`, pinned to known-good commits.
4. Stages the Raspberry Pi firmware blobs (`start4.elf`, `fixup4.dat`, the
   `bcm2711-rpi-4-b.dtb` device tree, and overlays) from `raspberrypi/firmware`
   into `.bootblobs/`. The DTB is fetched ready-made — it is never compiled.
5. Builds the `aarch64-phoenix` cross-toolchain (gcc-14.2.0 + binutils-2.43)
   into `.toolchain/`.
6. Creates a Python venv at `.venv/` with `pyserial` and the build's Python
   dependencies.

> **Reproducible builds:** add `--pinned` to check every source repo out to the
> exact SHA recorded in [`manifests/release-pin.md`](../manifests/release-pin.md)
> before building. Without it, siblings track their `master` branch. See
> [CONTRIBUTING.md](../CONTRIBUTING.md).

**Time:** the toolchain build is the long pole — typically **20-60 minutes**
(about 9 minutes on the reference test VM; longer on slow machines). The clones
add several minutes depending on your connection. The toolchain build is
idempotent: if it is already present the bootstrap skips it.

## Step 3 — Build the SD image

```bash
./scripts/rebuild-rpi4b-fast.sh --variant sd
```

This one command builds the complete bootable 2-partition SD image from a cold
buildroot: it builds the core system, the userspace ports, and the project
image, populates the ext2 root filesystem, then assembles, exports, and
verifies the card image.

> Do **not** add a `--scope` flag to this command. The full SD stage list
> (which builds the ports and populates the ext2 root) is selected only under
> the default `auto` scope; an explicit `--scope` overrides it and can produce
> a root filesystem missing the userspace ports.

When it finishes it prints the exported path and its SHA-256. The image lands
at:

```
artifacts/rpi4b/rpi4b-sd-2part.img
```

## Step 4 — Flash the image to a microSD card

The image is a full disk image (partition table + both partitions), so it is
written to the raw card device, not to a partition.

> ⚠️ **Writing to the wrong device destroys that disk — including your system
> disk.** Identify the card carefully before writing.

### Option A — `dd` (command line)

Insert the card, then find its device node:

```bash
lsblk        # find the microSD, e.g. /dev/sdX (NOT /dev/sdX1) or /dev/mmcblk0
```

Unmount any auto-mounted partitions, then write (replace `/dev/sdX` with the
device you just identified):

```bash
sudo dd if=artifacts/rpi4b/rpi4b-sd-2part.img of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

### Option B — Raspberry Pi Imager

Open [Raspberry Pi Imager](https://www.raspberrypi.com/software/), choose
**"Use custom"** as the OS, select `artifacts/rpi4b/rpi4b-sd-2part.img`, pick
the microSD card, and write.

> On **macOS** the repo ships `scripts/write-sdimg.sh` (uses `diskutil`; pass a
> disk identifier via `RPI4B_SD_DEV`, e.g. `disk4`). On Ubuntu use `dd` or the
> Imager as above.

## Step 5 — Boot the Pi

1. Insert the flashed card and power on the Pi 4.
2. **EEPROM boot order:** the Pi 4 boots according to its EEPROM boot-order
   setting. The factory default tries SD first, so a freshly-flashed card
   normally boots straight away. If your Pi's EEPROM is set to network-boot
   first (as the author's lab units are), either remove the network cable so it
   falls through to SD, or reset the boot order to SD-first with the official
   [Raspberry Pi Imager → Bootloader / EEPROM configuration](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#raspberry-pi-boot-eeprom)
   utility.
3. **What to expect:** on an HDMI display you should see the framebuffer
   console come up (`fbcon: ok`) roughly 50 seconds after power-on, followed by
   the `(psh)% ` prompt about a second later. If you have a 3.3 V USB-serial
   adapter on GPIO 14/15 (115200 8N1), the same boot log and prompt appear
   there. Plug in a USB keyboard to type at the prompt.

## Showcase apps (X11, Quake, browsers)

The base SD image boots to a shell and drives the hardware, but it does **not**
include the graphical showcase applications (the X11 desktop with Window Maker
and xterm, the Dillo web browser, Midnight Commander, and GLQuake on the V3D
GPU). Those are built by a separate, opt-in orchestrator,
`scripts/build-showcase-apps.sh`, which compiles the out-of-tree ports under
`tools/` (the ported Mesa GPU/GL/Vulkan driver, the quake engines, the X11 lib
stack + apps, and the extra userland ports) against `external/mesa` and the
vendored tarballs in `tools/ports/src/`.

### Extra host dependencies

The showcase build needs a few packages beyond the base set. They are already
in `bootstrap-linux-host.sh`'s apt list (the "Showcase build deps" block):
`ninja-build python3-mako libdrm-dev glslang-tools gperf`. It also needs
`meson >= 1.4`, which is newer than Ubuntu 24.04's apt `meson` (1.3.x) — so the
orchestrator provisions a local `meson`/`ninja`/`mako` in a `uv` venv at
`/tmp/mesa-pyenv` automatically; nothing to install by hand.

`external/mesa` (and, for the quake engines, `external/quakespasm` /
`external/vkquake`) must be cloned — the bootstrap does this.

### One-command showcase SD image

Pass `--with-showcase` to the same SD build from Step 3:

```bash
./scripts/rebuild-rpi4b-fast.sh --variant sd --with-showcase
```

This runs the orchestrator in two phases around the normal build:

1. **Before `build.sh`** it builds the GPU/GL/Vulkan + Quake static archives
   into `tools/.gpu-libs/` (`libv3d-phoenix.a`, `libGL-phoenix.a`,
   `libquakespasm.a`, and — unless `--skip-vulkan` — `libv3dv-phoenix.a`,
   `libvkquake.a`). The in-tree `rpi4-quake` / `rpi4-vkquake` `_user`
   components link these and are bundled into `loader.disk` by the project
   stage. (The rebuild script passes `GPU_LIBS=<repo>/tools/.gpu-libs`
   explicitly so the archives are always found.)
2. **After the image is built, before the ext2 root is packed** it builds the
   port libraries (libiconv, libffi, ncurses, glib2, fltk), the X11 lib stack,
   and every app (xterm, xedit, xcalc, xclock, xlogo, xbill, Window Maker, the
   Xphoenix server, `dillo`, `mc`, `nano`) and stages their binaries + data
   files into the ext2 rootfs tree (`_fs/<target>/root`).

### Running the orchestrator standalone

You can also run it directly (e.g. to iterate on just the GPU libs):

```bash
./scripts/build-showcase-apps.sh --phase gpu     # GPU/Quake archives only
./scripts/build-showcase-apps.sh --phase stage   # X11/ports into the rootfs
./scripts/build-showcase-apps.sh                  # both (phase "all")
```

Useful flags: `--force` (rebuild archives even if fresh), `--skip-vulkan` (GL
only, no vkQuake — also skips the glslang requirement), `--skip-x11` (skip the
X11 lib stack + X apps + dillo), `--stage-dir DIR` (stage into an arbitrary
rootfs tree, e.g. an NFS export). It is idempotent (skips up-to-date archives)
and fail-loud (each step is gated on its expected output existing). The X11
apps and userland ports are treated as best-effort: a single app failing is
recorded and reported at the end rather than aborting the whole run, so you get
as many of the showcase apps as build cleanly.

> **GLQuake vs. vkQuake shaders.** GLQuake (`libquakespasm` → `rpi4-quake`) is
> pure GL and needs no shader compiler. vkQuake needs real SPIR-V shaders from
> `glslang`; if `glslangValidator` is missing the vkQuake archive still links
> but with non-rendering placeholder shaders (the orchestrator warns loudly).

## Troubleshooting

- **`aarch64-phoenix-gcc: not found` during the build.** The toolchain build
  in Step 2 didn't complete, or its `bin/` isn't on `PATH`. Re-run
  `./scripts/bootstrap-linux-host.sh` (it re-checks and rebuilds the toolchain
  if missing); the rebuild script expects it at
  `.toolchain/aarch64-phoenix/bin/`.

- **Stale-sysroot header/build errors** (a header or library that "should" be
  there is reported missing, or a stage fails on something a previous stage was
  supposed to produce). Incremental builds reuse cached objects and can carry a
  stale state across source changes. The fix is a clean rebuild: wipe the local
  buildroot (`.buildroot/`) and re-run the Step 3 command from clean. Do not
  work around it by adding `--scope full-clean` to the `--variant sd` command —
  that changes the stage list and can drop the ports/rootfs population; delete
  the buildroot and re-run the plain command instead.

- **Missing Pi 4 DTB.** The rebuild script auto-prepares the DTB from the
  firmware blobs staged by the bootstrap. If you see a DTB warning that stops
  the build, confirm `.bootblobs/bcm2711-rpi-4-b.dtb` exists (re-run the
  bootstrap if not).

- **Pi shows nothing / doesn't boot from the card.** Almost always the EEPROM
  boot order (see Step 5) or a bad flash. Re-verify with `lsblk` that you wrote
  to the whole card device (not a partition), re-flash, and confirm the card is
  seated.

- **A build stage fails intermittently on a parallel make.** A cold buildroot
  can expose build-order races that a warm sysroot hides. Re-running the build
  (or doing a clean rebuild as above) usually resolves it; report a reproducible
  case if it persists.

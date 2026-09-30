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

- **Supported host:** Ubuntu x86_64 (24.04 or newer; 26.04 LTS validated). The
  build runs directly on the host — no VM.
- **A Raspberry Pi 4 Model B with 4 GB RAM** (the only board the port is
  validated on; the 2 GB and 8 GB boards are mis-mapped, KNOWN-ISSUES P1) and a
  **microSD card** (4 GB or larger).
- **A network connection during the build.** The build is not fully offline:
  the toolchain build and several userspace ports (`phoenix-rtos-ports`,
  X.org tarballs) download their sources at build time.
- **Disk space:** the bootstrap clones the sources and builds the toolchain;
  the full image (Mesa, GTK, XFCE, the games and their data) needs tens of GB
  in the buildroot. Budget ~35 GB free.
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
git clone https://github.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination.git ~/phoenix-rpi
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
   with `fork` = `github.com/rpi-phoenix-rtos/<repo>`. By default `origin` also
   points at the org (`github.com/rpi-phoenix-rtos/<repo>`) so the published set
   is self-contained; override `PHOENIX_UPSTREAM_BASE=https://github.com/phoenix-rtos`
   to wire `origin` at the phoenix-rtos upstream instead.
3. Clones the four game-engine forks into `external/` (tracked by branch,
   development conveniences — the game ports build from their own pinned
   upstream tarballs, not from these clones). Mesa is not cloned: the
   `mesa_drm` port builds the Mesa 26.2.0 release tarball.
4. Stages the Raspberry Pi firmware blobs (`start4.elf`, `fixup4.dat`, the
   `bcm2711-rpi-4-b.dtb` device tree, and overlays) from `raspberrypi/firmware`
   into `.bootblobs/`. The DTB is fetched ready-made — it is never compiled.
5. Builds the `aarch64-phoenix` cross-toolchain (gcc-16.2.0 + binutils-2.47)
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

This is **the release build**:

```bash
./scripts/rebuild-rpi4b-fast.sh --variant sd --with-showcase --with-ports
```

Do not add `--with-tests` for a release image. The images that passed the Pi
gates were built with the same flags **plus `--with-tests`**, which only adds the
`phoenix-rtos-tests` programs (`/bin/test-*`, `/bin/test_*`) to the root
filesystem; the release image leaves them out.

This one command builds the complete bootable 2-partition SD image from a cold
buildroot: it builds the core system, every userspace port (the graphics
stack, the desktops, the games and the apps — see
[What the image contains](#what-the-image-contains)), and the project image,
stages the helper programs, populates the ext2 root filesystem, then
assembles, exports, and verifies the card image. Always pass
`--with-showcase`: without it the helper programs the game launchers need
(`ram-stage-play`, `game-res`) are not staged.

> Do **not** add a `--scope` flag to this command. The full SD stage list
> (which builds the ports and populates the ext2 root) is selected only under
> the default `auto` scope; an explicit `--scope` overrides it and can produce
> a root filesystem missing the userspace ports.

When it finishes it prints the exported path and its SHA-256. The image lands
at:

```
artifacts/rpi4b/rpi4b-sd-2part.img
```

### The same build in Docker (any host OS)

The [`Dockerfile`](../Dockerfile) runs Steps 1–3 inside an Ubuntu 26.04 container:
it clones the repos, runs `scripts/bootstrap-linux-host.sh` (the
[host packages](#host-packages), the
[extra host dependencies](#extra-host-dependencies) of the ports stage and the
cross-toolchain), then the release command above (its default
`BUILD_FLAGS` is `--with-showcase --with-ports`). The copy-paste recipe that
builds from the published repos is in the
[README](../README.md#build-with-docker-reproducible-any-host-os). To build your
**local checkout** (the committed state of this repo and the sibling repos) the
same way:

```bash
./scripts/build-sd-in-docker.sh    # -> ./docker-out/rpi4b-sd-2part.img (or pass an output directory)
```

It serves the repos to the container over a local git server and always
builds with `--no-cache`. A `--no-cache` Docker build is **the release gate**:
it proves that the image builds from a blank host with nothing but the
committed sources.

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

## What the image contains

Every program on the image is a **framework port**: a recipe in
`sources/phoenix-rtos-ports/<port>/port.def.sh`, listed in the project's
`_projects/aarch64a72-generic-rpi4b/ports.yaml`. The build's `ports` stage builds
all of them and installs them into the root filesystem. No program goes into
`loader.disk`. Each program has one port and one version:

| Area | Ports |
|---|---|
| Graphics stack | `libdrm_phoenix` (libdrm on the render and KMS servers), `mesa_drm` (Mesa 26.2: GBM, EGL, GLES, GL, Vulkan), `sdl2_kmsdrm` (SDL 2.30 with KMSDRM + Wayland), `libepoxy`; the smoke tests `kmscube_drm` / `vkcube_drm` and `drmprobe` are built only with `--with-tests` |
| X11 | `xorg_server_drm` (Xorg 21.1, modesetting + glamor, `startx`), `xorg_libs`, `xorg_fonts`, `xorg_apps`, `xterm`, `windowmaker`, `xbill`, `dillo` |
| Wayland desktop | `wayland_phoenix` (libwayland 1.24, wayland-protocols, libxkbcommon), `dbus`, `gtk3_wayland` (GTK 3.24), `labwc_desktop` (labwc, foot, fuzzel), `xfce_wayland` (XFCE 4.20, `xfce-session`) |
| Games | `quakespasm_drm`, `yquake2` + `yquake2_drm`, `quake3` + `quake3_drm`, `vkquake_drm`, `supertuxkart` + `supertuxkart_drm` (the engine port compiles, the `*_drm` port links one program with both SDL video drivers) |
| Applications | `video_player` (ffplay, `video-play`, gtk-video, demo clips), `atril_wayland` (Atril + Poppler), `python`, `bash`, `coreutils`, `busybox`, `curl`, `mc`, `nano`, `sqlite3`, `redis`, `lua`, … |

The three graphics servers (`rpi4-v3d-async`, `rpi4-kms`, `shmsrv`) and the WiFi
daemon (`rpi4-wifi`) are core components of `phoenix-rtos-devices` and start at
boot (`user.plo.yaml`).

`--with-showcase` adds one step after the ports: `scripts/build-showcase-apps.sh`
runs `scripts/build-rootfs-helpers.sh`. It builds the small static helpers that no
port produces into the rootfs tree (`_fs/<target>/root`):

- `ram-stage-play`: copies the Quake II and Quake III data into the `/tmp` RAM disk
  before the engine starts;
- `game-res`: starts a game in a lower full-screen mode;
- `pty-run`;
- with `--with-tests` only, a few diagnostics (`thermal-soak`, `mtstress`, `pwmwrite`,
  `pwmdma`, `armtrials`).

`--with-tests` is the one switch for test programs: it adds the `test` stage
(phoenix-rtos-tests and the `_user` demos `hello`, `hellocpp`, ...) and sets
`RPI4B_WITH_TESTS=1`, which `ports.yaml` and `build-rootfs-helpers.sh` read. The release
build does not pass it, so the image carries no test or diagnostic program.

`--with-ports` inserts the `ports` stage into the non-SD builds too. The SD build
always runs it.

### Extra host dependencies

The ports stage needs more host tools than the base system.
`scripts/bootstrap-linux-host.sh` installs all of them (its "Showcase build deps" block;
each package carries a comment naming the recipe check it satisfies), and the Dockerfile runs
the same script. The recipes check for them and stop with a clear message when one is
missing:

- **`meson` ≥ 1.4** for Mesa 26.2: apt's `meson` where it is new enough (Ubuntu 26.04),
  otherwise `uv tool install "meson>=1.4"` (Ubuntu 24.04, into `~/.local/bin`).
- **`wayland-scanner` 1.24.0** exactly (`libwayland-bin`; `wayland_phoenix`, `gtk3_wayland`,
  `labwc_desktop` and `xfce_wayland`).
- the GLib tools (`glib-compile-resources`, `gdbus-codegen`, `glib-mkenums`,
  `glib-genmarshal`, `glib-compile-schemas`), `gtk-update-icon-cache`, `shared-mime-info`,
  and `python3` with GObject introspection, GdkPixbuf and the SVG loader (`xfce_wayland`: the
  icon theme is rendered on the host).
- `python3` with **pycairo** and the **DejaVu** fonts (`atril_wayland` draws the sample PDF;
  the desktop fonts).
- **`ffmpeg`** with the libx264, libx265, libvpx-vp9, libopus and aac encoders
  (`video_player` generates the demo clips at build time).
- `unzip`, `lhasa` and `7z` for the game data.

Ubuntu 24.04 cannot build the ports stage from apt alone: its wayland-scanner (1.22) and
host Python (3.12; the python port needs 3.14) are too old, and bootstrap warns about both.
Ubuntu 26.04, the Dockerfile's default, has everything.

### Game data and WiFi firmware

The engines are only the binaries. Their data is staged separately by
**`scripts/stage-game-data.sh`**, which populates the project's
`rootfs-overlay` — the one staging path that reaches both the SD ext2 packer and
the netboot NFS export:

```bash
./scripts/stage-game-data.sh all        # or: q1 q2 q3 stk (idempotent; --force re-fetches)
```

It fetches the Quake I shareware pak and the Quake II / Quake III demos (each
from a pinned URL) and both SuperTuxKart 1.4 asset roots (`data/` +
`stk-assets/`), ~306 MB in total. For Quake III it additionally stages
`pak1.pk3` and `q3key` from `assets/quake3-qvm/` — see that directory's
`README.md`; **no retail content and no retail CD key are involved.** The Docker
build calls the same script with the same pins.

The **WiFi firmware** (BCM43455, from linux-firmware tag `20260810`, sha256-pinned)
is fetched by `scripts/fetch-wifi-firmware.sh`, which the rebuild script calls. It
is cached in `.firmware/`, so later builds work offline. It is staged under
`rootfs-overlay/lib/firmware/` together with its licence files, and is never
committed to git. Look for `[wifi-fw] cache verified` and `staged` in the build
output.

### Building a single port

`scripts/build-port.sh <port>` builds one port and its dependencies on their own,
through the real `port_manager`, to check a recipe. `--incremental` skips the clean
re-extract, and `--dry` resolves the dependencies without building. The image itself
is always built by `rebuild-rpi4b-fast.sh`.

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

- **The first build after a libphoenix change takes longer than usual.** Each
  port records the hash of the static libraries it links (the sysroot's
  `libphoenix.a` and the toolchain's `libgcc.a`/`libstdc++.a`/`libsupc++.a`).
  When those change, the port is **relinked** from its existing objects, not
  rebuilt, so a stale static binary cannot survive a C-library fix. Expect one
  "Link inputs changed … relinking" line per port. An unchanged libc relinks
  nothing. A port whose program cannot be recreated this way defines `p_relink`
  in its `port.def.sh`.

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

# Phoenix-RTOS on the Raspberry Pi 4

A from-scratch port of [Phoenix-RTOS](https://phoenix-rtos.com/) — a small
microkernel, message-passing operating system — to the **Raspberry Pi 4
Model B / BCM2711** (Cortex-A72, AArch64).

The port was taken from "does not boot" to a system that boots to an
interactive shell, drives the real hardware, serves its root filesystem from
an SD card or over NFS, and runs a graphical userland: an X11 desktop with
Window Maker, a web browser, and **GLQuake on the V3D GPU**.

> This repository is the **coordination repo** — docs, build scripts, and
> integration manifests. The Phoenix-RTOS source lives in sibling repositories
> cloned under `sources/` (see [Repository layout](#repository-layout)).

## Quick start

On a fresh Ubuntu x86_64 machine, from an empty directory:

```bash
git clone https://github.com/houp/phoenix-rpi.git ~/phoenix-rpi
cd ~/phoenix-rpi
./scripts/bootstrap-linux-host.sh            # installs deps, clones sources, builds the toolchain
./scripts/rebuild-rpi4b-fast.sh --variant sd # builds artifacts/rpi4b/rpi4b-sd-2part.img
```

Flash the resulting image to a microSD card and boot it on a Pi 4. The full
walkthrough — prerequisites, timings, flashing, and first-boot expectations —
is in **[docs/BUILD.md](docs/BUILD.md)**.

## Capabilities

Status of the Pi 4 hardware/software stack. `✅` works on hardware and is
validated; `🟡` usable but with a known gap; `⏸` deferred to human-attended
work; `⛔` blocked on external dependencies; `⬜` not started.

| Subsystem | Status | Notes |
|---|---|---|
| CPU / EL2→EL1 / MMU + caches | ✅ | Boots to userspace; caches ON, all Normal RAM WB-cacheable |
| SMP (4 cores) | 🟡 | 4 cores enumerated; scheduler is cpu0-only |
| Generic timer, GIC-400 interrupts | ✅ | Scheduler ticks; GENET/USB/SD IRQs live |
| PL011 UART console | ✅ | Primary serial console + klog mirror |
| HDMI framebuffer console (fbcon) | ✅ | klog + psh on HDMI, FreeBSD `teken` VT engine |
| HDMI framebuffer device `/dev/fb0` | 🟡 | Read/write + `GETMODE` devctl; mmap/display-ownership attended |
| GENET gigabit Ethernet + lwIP | ✅ | IRQ-driven, ~0.9 ms ping RTT, autonomous DHCP |
| USB host (PCIe → VL805 xHCI) | ✅ | Enumerates reliably from cold boot |
| USB HID (keyboard + mouse) | ✅ | `/dev/kbd0`, `/dev/mouse0`; live keys reach psh and apps |
| SD card (EMMC2 SDHCI) | ✅ | `/dev/mmcblk0`; PIO reads/writes, MBR partitions |
| ext2 persistent root | ✅ | Mounts as `/`, binaries exec from the card |
| NFS root | ✅ | `/` served over NFS (`takeover` design), ~8.5 MB/s |
| SoC thermal + throttle | ✅ | `/dev/thermal`, `/dev/throttled` via VideoCore mailbox |
| Hardware RNG (RNG200) | ✅ | `/dev/hwrng`; also backs `/dev/urandom` |
| GPIO observer | 🟡 | `/dev/gpio` read-only snapshot; outputs attended |
| GPU (V3D 4.2) — OpenGL | ✅ | Ported Mesa `v3d` Gallium + GL → **GLQuake ~40 fps @ 1080p** |
| GPU (V3D 4.2) — Vulkan (V3DV) | 🟡 | Full init + shaders on HW; vkQuake paused at texture-upload |
| Audio (PWM, 3.5 mm jack) | 🟡 | `/dev/audio0` streaming DMA; Quakespasm audio backend |
| X11 / windowing (kdrive) | ✅ | Xphoenix fbdev DDX + kbd/mouse; Window Maker, JWM, xterm |
| posixsrv / psh userland | ✅ | pipes, ptys, `/dev/{null,zero,urandom,full}`, AF_UNIX |
| WiFi (BCM43455 SDIO) | ⛔ | Firmware-execution gate not cleared; needs deeper HW visibility |
| Bluetooth, USB mass-storage, I²C/SPI/PWM, camera | ⬜ | Not started |

The authoritative, per-peripheral matrix (with evidence and remaining work) is
[docs/inprogress/pi4-hardware-support-matrix.md](docs/inprogress/pi4-hardware-support-matrix.md).
Open bugs and known limitations are in
[docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).

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
pointing at the `houp/*` work fork — see [CONTRIBUTING.md](CONTRIBUTING.md).

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
licenses of their upstream projects (Mesa, Quakespasm, vkQuake, X.org, etc.);
note that the Quake game data is not included and is subject to id Software's
terms.

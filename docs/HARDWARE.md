# The author's test lab (optional Tier-2 rig)

> **None of this is required to build or flash Phoenix-RTOS for the Pi 4.**
> If you just want a bootable SD image, follow [BUILD.md](BUILD.md) — that path
> needs no serial adapter, no HDMI capture, no dedicated NIC, and no smart plug.
> This document describes the **optional** lab rig the author uses for fast,
> repeatable, hands-off development: automated netboot, serial + HDMI capture,
> and remote power-cycling.

The lab automation is driven by the helper scripts under `scripts/`
(`netboot-server-up.sh`, `test-cycle-netboot.sh`, `capture-rpi4b-uart.sh`,
`pi_power_on.sh` / `pi_power_off.sh`, …). Every hardware handle is an
environment variable with a safe default, so a machine that lacks a given piece
of hardware simply skips it rather than failing.

## Configuration: `.env.local`

The scripts read machine-specific settings from a gitignored `.env.local`.
Start from the committed template:

```bash
cp .env.example .env.local
# edit .env.local for your machine
```

Every value has a built-in default in the scripts, so an unset variable never
breaks the portable build path — these settings only matter for the lab rig.
The variables:

| Variable | Purpose | Script default |
|---|---|---|
| `RPI4B_NETBOOT_IFACE` | Host NIC wired directly to the Pi's Ethernet | `eth1` |
| `RPI4B_PI_MAC` | Pi 4 Ethernet MAC (used to confirm the netboot DHCPACK) | — |
| `RPI4B_NETBOOT_TFTPROOT` | Where dnsmasq serves the bootfs from | under the buildroot |
| `RPI4B_NETBOOT_STATE_DIR` | dnsmasq lease/state directory | under `artifacts/` |
| `RPI4B_SERIAL_DEV` | USB-UART device for the serial console | autodetect (`/dev/serial/by-id/*`, then `ttyUSB*`/`ttyACM*`) |
| `RPI4B_HDMI_GRABBER` | V4L2 device of the USB HDMI grabber | `/dev/video4` |
| `RPI4B_SD_DEV` | SD-card target for flashing (macOS `write-sdimg.sh`) | none (must be set) |
| `MEROSS_PLUG_SCRIPT` | Script that toggles the Pi's smart plug on/off | skipped if unset/absent |

## Serial (UART) console

A 3.3 V USB-UART adapter wired to the Pi 4:

- GPIO 14 (TX) → adapter RX
- GPIO 15 (RX) → adapter TX
- GND → GND
- 115200 baud, 8N1

Watch it live with `tio -b 115200 /dev/ttyUSB0` (or your `RPI4B_SERIAL_DEV`).
The capture helper `scripts/capture-rpi4b-uart.sh` records the boot log to a
file and autodetects the device if `RPI4B_SERIAL_DEV` is unset.

## HDMI capture

A cheap UVC-compliant HDMI→USB grabber on the Pi's HDMI0, exposed as a V4L2
device (`RPI4B_HDMI_GRABBER`, default `/dev/video4`). Preview it with
`ffplay $RPI4B_HDMI_GRABBER`. The test-cycle scripts grab periodic HDMI
snapshots into `artifacts/hdmi/` so you can confirm the screen reached
`fbcon: ok` even when the UART is quiet.

## Dedicated netboot NIC + dnsmasq

For flash-free, fast iteration the author boots the Pi over Ethernet instead of
from an SD card. A **dedicated** USB-Ethernet adapter (not the host's main
uplink) is cabled directly to the Pi's ETH port and given a static
`10.42.0.1/24`. A user-space `dnsmasq` on that interface provides DHCP + TFTP,
serving the build output (`kernel8.img` / `loader.disk` /
`phoenix-armstub8-rpi4.bin` plus the firmware blobs) to the Pi's firmware
netboot.

Bring the netboot server up with `scripts/netboot-server-up.sh`; run a full
build → power-on → capture cycle with `scripts/test-cycle-netboot.sh`. Build for
the netboot rig with `--variant netboot` or `--variant nfsroot` (`nfsroot` is
the rebuild script's default variant):

```bash
./scripts/rebuild-rpi4b-fast.sh --variant netboot   # probe-only SD, root from network
./scripts/rebuild-rpi4b-fast.sh --variant nfsroot   # root filesystem served over NFS
```

The Pi's EEPROM must have network boot in its boot order for this to work (this
is why the author's units are network-boot-first — see the EEPROM note in
[BUILD.md](BUILD.md)).

## Smart-plug power automation

Hands-off test cycles power-cycle the Pi between runs. `pi_power_on.sh` /
`pi_power_off.sh` invoke the script named by `MEROSS_PLUG_SCRIPT` as
`<script> on` / `<script> off`. If `MEROSS_PLUG_SCRIPT` is unset and the
author's default path is absent, the power toggle is **skipped** (non-fatal), so
machines without a controllable plug still run builds and manual tests. Point
it at whatever smart plug or switchable hub you have (the author uses a Meross
plug; any on/off script works).

## Self-flash via netboot Linux

For unattended **SD-boot** testing without physically swapping cards, the author
netboots a small Linux on the Pi, `dd`s a freshly-built Phoenix image onto the
Pi's own `/dev/mmcblk0` from within that Linux, then reboots into the flashed
Phoenix SD image. This avoids the flash → shuttle-card → boot loop entirely.
It is a lab convenience layered on the netboot rig above, not part of the
build-&-flash path.

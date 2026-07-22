# Linux-host bootstrap — Phoenix-RTOS Raspberry Pi 4 dev environment

This guide gets the project running on a fresh **Ubuntu 24.04 (or newer)
x86-64** machine that is dedicated to Phoenix-RTOS Pi 4 bring-up. No VM
is required — everything runs on the host.

The script [`scripts/bootstrap-linux-host.sh`](../../scripts/bootstrap-linux-host.sh)
automates most of the setup. Read this guide first, then run the script.

## Hardware needed

| Device | Purpose | Notes |
|--------|---------|-------|
| Raspberry Pi 4 Model B (any RAM tier) | Target | All TD-12 fixes were verified on 4 GB |
| MicroSD card | Boot mode selector | Or use the Pi's EEPROM netboot bootmode |
| USB→UART adapter (3.3 V) | Serial console | Wired to Pi 4 pins 14 (TX) and 15 (RX) and GND. Pi 4 PL011 is at 115 200 8N1. |
| USB capture card (HDMI→USB UVC) | Watch HDMI output | Any cheap UVC-compliant grabber. Phoenix's fbcon is 1920×1080 (BCM2711 default). |
| USB-Ethernet adapter | Pi netboot link | A *dedicated* NIC, not the host's main internet uplink. Direct cable to Pi 4 ETH port. |
| USB keyboard | (Optional) USB-keyboard input verification | Any wired HID keyboard. Required for `/dev/kbd0` testing. |
| Smart power switch | Power-cycling the Pi | Optional but big quality-of-life improvement; the existing `pi_power_on.sh`/`pi_power_off.sh` scripts assume something MQTT/kasa-style. Adapt for whatever you have. |

## Network topology

```
Internet ──── host main NIC (eth0/wlan0)
                                          
           ┌─────────────────────────────┐
           │ Linux dev host (this machine)│
           │                              │
           │   USB-Ethernet adapter (eth1)│ ───── direct cable ───── Pi 4 ETH
           │                              │
           │   USB serial ──── UART ───── Pi 4 GPIO 14/15 + GND
           │                              │
           │   USB grabber ─── HDMI ───── Pi 4 HDMI0
           │                              │
           │   (optional) USB keyboard ── Pi 4 USB-A
           └─────────────────────────────┘
```

The dedicated NIC (`eth1` or similar) hosts dnsmasq on `10.42.0.1/24`. The
Pi gets a lease at `10.42.0.12` and TFTPs the boot files from this same NIC.

## What the bootstrap script does

1. Installs system packages:
   - Build: `build-essential`, `gcc-aarch64-linux-gnu`, `binutils-aarch64-linux-gnu`, `cmake`, `pkg-config`, `device-tree-compiler`, `mtools`, `dosfstools`, `parted`
   - Net: `dnsmasq` (run as user, *not* as the system DNS resolver), `iproute2`
   - Serial: `tio` (preferred) and `picocom` (fallback)
   - Video capture: `ffmpeg`, `v4l-utils` (for diagnosing the HDMI grabber)
   - Python: `python3`, `python3-pip`, `python3-venv`, `uv` (for the pyserial venv `psh-interact.py` uses)
   - Misc: `git`, `git-lfs`, `curl`, `jq`, `make`, `gh` (GitHub CLI, for issue/PR management)
2. Clones the project layout to `~/phoenix-rpi/`:
   - The coord repo at `~/phoenix-rpi/`
   - Each sibling at `~/phoenix-rpi/sources/<name>/`, with `fork` (rpi-phoenix-rtos/*) as default and `origin` pointing at upstream phoenix-rtos/*
3. Downloads the Raspberry Pi firmware blobs (`start4.elf`, `fixup4.dat`,
   `bcm2711-rpi-4-b.dtb`, `overlays/miniuart-bt.dtbo`) into a staging
   directory; the build script later copies these into the netboot tree
   alongside our own `kernel8.img` / `loader.disk` / `phoenix-armstub8-rpi4.bin`.
4. Creates a Python venv at `~/phoenix-rpi/.venv/` with `pyserial` installed
   for `scripts/psh-interact.py`.
5. Builds the AArch64 cross-toolchain prerequisites and verifies they're
   on `$PATH`.
6. Prints the next-step instructions including how to start dnsmasq, the
   expected serial device path on Linux (`/dev/ttyUSB0` typically), the
   HDMI grabber path (`/dev/video0` typically), and the netboot test
   procedure.

## Differences from the macOS host setup

The macOS host used a Lima VM (`phoenix-dev`) to run dnsmasq because socket_vmnet
was needed to bridge a USB-Ethernet adapter into a VM-private network. On Linux
this is straightforward:

- dnsmasq runs as a regular user (or via a systemd-user unit) directly on
  the dedicated `eth1` interface.
- No `limactl shell phoenix-dev`; just `sudo systemctl start dnsmasq` or a
  plain `dnsmasq -C /path/to/conf.conf` invocation.
- TFTP root is on the host filesystem; the build script writes to it
  directly, no shared-folder mount or `lima copy` step.
- Serial device path is `/dev/ttyUSB0` (or `/dev/ttyACM0` for some adapters)
  rather than `/dev/cu.usbserial-XXXXXX`.

## Manual steps after the bootstrap

1. **Set the dedicated NIC's static IP** — `eth1` must be `10.42.0.1/24`.
   The bootstrap installs a NetworkManager profile (or, if NetworkManager
   isn't in use, a netplan stanza) but you'll need to confirm it activates
   and that the kernel's network policy doesn't try to route default
   traffic through it.

2. **Test the dnsmasq config without the Pi** — run
   `sudo dnsmasq --no-daemon --conf-file=$PWD/.netboot/dnsmasq.conf` and
   confirm it binds to `10.42.0.1:67` (DHCP) and `:69` (TFTP). Quit with
   Ctrl-C; the persistent invocation goes through systemd.

3. **Test the serial port without the Pi** — `tio -b 115200 /dev/ttyUSB0`
   and confirm the prompt opens cleanly. Quit with `Ctrl-T Q`.

4. **Test the HDMI grabber without the Pi** — `ffplay /dev/video0` should
   show a black or blue framebuffer (Pi off) or whatever the last
   firmware screen was.

5. **Configure the smart power switch** — adapt the `pi_power_on.sh` and
   `pi_power_off.sh` scripts in the coord repo to control whatever
   IoT plug or USB-controllable hub you use. The macOS scripts use kasa
   smart plugs over local network; on Linux the same Python library works.

6. **Build and netboot** — `./scripts/rebuild-rpi4b-fast.sh --scope full-clean`
   then `./scripts/test-cycle-netboot.sh --label first-linux-boot`. Connect
   `tio` to the UART; expect the firmware boot, plo, kernel banner, then
   `fbcon: ok` and `(psh)% ` about 55-60 s after power-on.

## Where the current state stands (2026-05-19)

The image baseline is documented in
[`manifests/2026-05-19-td12-stable-plus-pm-sigint.md`](../../manifests/2026-05-19-td12-stable-plus-pm-sigint.md).
Read [`docs/knowledge/build-cache-and-cold-boot-races.md`](build-cache-and-cold-boot-races.md)
and [`docs/knowledge/console-architecture-and-fbcon.md`](console-architecture-and-fbcon.md)
for the why-things-are-the-way-they-are context.

Open work items as of bring-up to a new machine:

- **USB keyboard interactive verification** (Task #3 in the session task
  list): code chain is fully wired; need an interactive run with a USB
  keyboard plugged in to confirm `/dev/kbd0` materialises and keypresses
  reach psh. Runbook at [`docs/knowledge/interactive-verification-runbook.md`](interactive-verification-runbook.md).
- **fbcon prompt-indent rendering glitch** (Task #1): cosmetic; needs
  live instrumentation of `pl011_fbcon_putc` to capture the bytes
  flowing through it between command output and the next prompt.
- **General**: a number of TD-NN items in
  [`docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](../inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
  describe transitional Pi-4-bringup shortcuts. Several are now
  resolvable; the doc is the source of truth.

## Running the bootstrap

```bash
# Choose a destination directory
mkdir -p ~/phoenix-rpi
cd ~/phoenix-rpi

# Grab just the bootstrap script — the rest follows
curl -fsSL https://raw.githubusercontent.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination/main/scripts/bootstrap-linux-host.sh -o bootstrap-linux-host.sh
chmod +x bootstrap-linux-host.sh
./bootstrap-linux-host.sh
```

The script is idempotent — safe to re-run if anything fails partway.

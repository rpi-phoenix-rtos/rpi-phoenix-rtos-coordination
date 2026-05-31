---
name: phoenix-rpi-netboot
description: Use when booting/testing the Phoenix-RTOS Pi 4 on real hardware over network boot (the primary, human-free test loop) — starting/checking the host dnsmasq+TFTP server, running a power-cycle + UART capture, and analyzing UART logs and HDMI screen grabs. Also covers the occasional SD-card boot path.
---

# Phoenix RPi 4 Netboot (autonomous hardware test loop)

Network boot is the **primary** Pi 4 test method: it needs no human present and
no SD re-flashing — a rebuild updates the TFTP tree and the next power-cycle
serves it. SD-card boot is an occasional check for milestones (see last section).

## Topology (Linux host — NO VM)

```
Linux host ── USB NIC enx00e04c68013a (10.42.0.1/24) ──crossover cable── Pi 4 ETH
   dnsmasq (DHCP 10.42.0.10-20  +  TFTP)
   TFTP root = .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs
```

The Pi's VideoCore firmware does DHCP, gets the TFTP server (option 66 =
10.42.0.1), pulls `kernel8.img` (= plo `loader.disk`) + config/dtb/armstub, and
boots Phoenix. There is **no VM and no bridge** on this host — old script
comments mentioning Lima / `socket_vmnet` / `phoenix-dev` are macOS-era cruft.

Config is read from `.env.local` (gitignored): `RPI4B_NETBOOT_IFACE=enx00e04c68013a`,
`RPI4B_PI_MAC=dc:a6:32:3c:dd:f1`. Server state lives under `artifacts/netboot/`
(`dnsmasq.conf`, `dnsmasq.log`, `dnsmasq.leases`, `dnsmasq.pid`).

## ⚠️ Gotcha #1 — the SD card must be OUT of the Pi for clean netboot

If an SD card with a bootable image is in the Pi *and* netboot is configured, the
firmware does **boot-order contention**: tries one, aborts, reboots, tries the
other — producing a ~90× firmware-reboot storm (16k+ UART lines, Phoenix pushed
to the end of the capture window, "looks broken"). With the SD removed, netboot
is one clean firmware boot (~1 `RPi: BOOTLOADER` line, a few hundred UART lines).
Check reboot count: `grep -c "RPi: BOOTLOADER" <uartlog>` — should be **1**.

## The autonomous loop

```
./scripts/rebuild-rpi4b-fast.sh --scope core   # or project / full-clean; updates the TFTP bootfs
./scripts/netboot-server-up.sh                 # (re)start dnsmasq fresh; idempotent
./scripts/test-cycle-netboot.sh --capture-secs 200 --label myrun > /tmp/cycle.log 2>&1
./scripts/uart-summary.sh myrun                # stage table + faults
```

No re-flash between rebuilds — TFTP serves the fresh `rpi4b-bootfs` directly.

`test-cycle-netboot.sh` (default = netboot) does: start UART capture → power on →
wait for the firmware's DHCP → capture `--capture-secs` → power off (EXIT trap).
It runs `netboot-server-up.sh` itself unless `--skip-server-up`.

### Timeouts
- `--capture-secs` ≥ 180 to see lwip/userspace; 200–240 for sustained capture.
- Bash tool `timeout` ≥ `(capture_secs + 80) * 1000` ms; cap is 600000 (10 min).

### ⚠️ Gotcha #2 — exit 143 is BENIGN
The cycle almost always exits 143: the capture watchdog kills picocom at
`--capture-secs`, then the Meross-cloud power-off in the EXIT trap is slow. This
is NOT a boot failure. **Always redirect cycle stdout to a file and read it**
(`> /tmp/cycle.log 2>&1`) — never `| tail` (SIGTERM eats piped output). After a
143, also run `./scripts/pi_power_off.sh` to be sure the Pi is off.

## Server: start / check / stop

- **Start/restart (fresh):** `./scripts/netboot-server-up.sh` — sets the NIC IP,
  regenerates `dnsmasq.conf`, restarts dnsmasq, verifies the pid. Safe to re-run.
- **Stop:** `./scripts/netboot-server-down.sh` (do this before an SD-boot test so
  the firmware can't netboot and falls back to SD).
- **Check it's serving:**
  - `pgrep -af 'dnsmasq.*artifacts/netboot'` — our instance (distinct from the
    libvirt dnsmasq on virbr0).
  - `ip -br addr show enx00e04c68013a` — expect `10.42.0.1/24` (DOWN/NO-CARRIER
    is normal while the Pi is off; carrier comes up when the Pi powers on).
  - `tail artifacts/netboot/dnsmasq.log` — look for `DHCPDISCOVER`/`DHCPACK` from
    the Pi MAC and `sent .../kernel8.img`. "Early terminate" / "failed sending"
    / "file ... not found (recovery.elf, cmdline.txt, ...)" lines are **normal**
    Pi firmware probing, not errors.

The worker is `scripts/vm-netboot-server.sh {up|down|status}` (legacy name);
the `netboot-server-{up,down}.sh` wrappers set the Linux env and call it.

## Result analysis

- **UART (source of truth):** `./scripts/uart-summary.sh <label>` prints a stage
  table (firmware → plo → kernel → fbcon → psh → lwip) + fault count. Raw logs:
  `artifacts/rpi4b-uart/rpi4b-uart-<UTC-ts>-netboot-<label>.log`. List recent:
  `./scripts/uart-list.sh`.
- **HDMI grabs:** `artifacts/hdmi/<ts>-<label>-{tick,final}.png` (every ~25 s
  while powered). Useful when UART is quiet but the Pi is up.
- **⚠️ Gotcha #3 — HDMI fbcon ≈ psh's console.** A healthy boot shows only
  `Phoenix-RTOS HDMI console` / `fbcon: ok` / `(psh)%` on HDMI. The rich driver
  klog (pcie, xhci, **USB enum, `/dev/kbd0`**, genet) is userspace/serial output
  on **UART**, not fbcon. "3-line HDMI" is NORMAL — confirm health from UART.
- **⚠️ Gotcha #4 — timestamps.** UART log *filenames* are UTC (`date -u`);
  `dnsmasq.log` lines are host **local** time. Reconcile before correlating.
- A genuinely healthy USB boot shows on UART: `usb-hcd: post usb_devEnumerate ok`
  then `usbkbd: New /dev/kbd0 device created`, with `fault_pattern_matches: 0`.

## Power control
`./scripts/pi_power_on.sh` / `./scripts/pi_power_off.sh` (Meross smart plug via
cloud; calls can be slow but reliable). Always leave the Pi **off** between runs.

## Occasional SD-card boot (milestone validation)
The final product must boot from SD. To test it: write `artifacts/rpi4b/rpi4b-sd.img`
to the card, then `./scripts/netboot-server-down.sh` (force SD fallback) and
`./scripts/test-cycle-netboot.sh --sd-boot ...` (implies `--skip-server-up
--dhcp-wait-secs 0`; logs tagged `-sdboot-`). Requires a human to flash + insert
the card, so use it sparingly — netboot is the day-to-day loop.

## Related
- `skills/phoenix-rpi-hw-test` — broader build/test classification.
- memory `project_sdboot_clean_vs_netboot_artifacts` — the debugging story behind
  gotchas #1–#4.

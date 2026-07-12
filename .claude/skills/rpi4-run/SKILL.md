---
name: rpi4-run
description: >-
  Boot the current Phoenix-RTOS image on the real Raspberry Pi 4 and run
  commands / capture results. Use whenever you need to run something on the Pi:
  execute a binary at the psh prompt (e.g. a test), capture a boot log, or grab
  the screen — over SD boot or NFS/netboot root. This is the stable, repeatable
  Pi test-run recipe; follow it instead of re-reading scripts/*.sh each time.
---

# Running things on the Raspberry Pi 4

One physical Pi 4, driven over UART (`/dev/ttyUSB0`) with switched power and an
optional HDMI capture card. **Only one Pi cycle at a time** — the UART is
exclusive; a second concurrent cycle gets an empty log. Builds are parallelizable;
the boot/UART step is not.

Host facts (this machine): `/dev/sda` is **always** the SD card (safe to `dd`, no
identity check); `/dev/nvme0` is the system SSD. Toolchain + buildroot are under
the repo (`.toolchain/`, `.buildroot/`).

## Pick the scenario

| Goal | Boot mode | Card | Recipe |
|---|---|---|---|
| Run a binary/command at psh + capture its output | SD (self-contained) | in Pi | A |
| Same, over the network | netboot / nfsroot | **out** of Pi | B |
| Just capture a boot (no interaction) | either | — | C |
| Build a fresh image + get it onto the card | — | in host | D (then A) |

`sd` mounts a local ext2 root; `nfsroot` mounts the NFS export as `/`; `netboot`
uses a RAM root. **Card-in forces SD boot** (VideoCore firmware won't fall through
to network with a card present), so netboot/nfsroot need the card **removed**.

## A — Run commands over SD boot (most common)

Card is already in the Pi. `test-cycle-psh-interact.sh` power-cycles, waits for the
`(psh)%` prompt, sends each command, captures per-command UART output, then powers
off. Skip the netboot server for SD.

```
./scripts/test-cycle-psh-interact.sh --skip-server-up --label <label> -- \
  "<command 1>" "<command 2>" ...
```

- Each `"<command>"` is one psh line (quote the whole thing incl. its args).
- Defaults: `--wait-secs 150` (max wait for prompt), `--idle-secs 20` (capture
  window after each command), `--max-cmd-secs 120`. Raise `--idle-secs` for a
  long-running command; raise `--wait-secs` only if boot is unusually slow.
- Bash tool `timeout`: set ≥ `(wait_secs + n_cmds*(idle+3) + 120) * 1000` ms.
  For ~4 quick commands, `480000` (8 min) is safe.
- UART log: `artifacts/rpi4b-uart/rpi4b-uart-<ts>-<label>.log`. HDMI snapshots (if
  `/dev/video4` present): `artifacts/hdmi/` (periodic `-tick.png` + a `-final.png`).

Example (this is how the libc suite is run):
```
./scripts/test-cycle-psh-interact.sh --skip-server-up --label libc -- \
  "/bin/test-libc-string -v -g string_memmem" \
  "/bin/test-libc-misc -v -g langinfo"
```

## B — Run commands over netboot / nfsroot

Remove the card first. Do NOT pass `--skip-server-up` (dnsmasq/TFTP must be up):

```
./scripts/netboot-server-up.sh                    # if not already running
./scripts/test-cycle-psh-interact.sh --label <label> --inter-cmd-secs 8 -- "<cmd>" ...
```

Raise `--inter-cmd-secs` (e.g. 8) so a post-takeover command lands after the NFS
root has mounted.

## C — Capture a boot only (no commands)

```
./scripts/test-cycle-netboot.sh --sd-boot --capture-secs 180   # SD boot
./scripts/test-cycle-netboot.sh --capture-secs 240             # netboot (card out)
```

`--capture-secs` ≥ 180 to see user-space/lwip; set the Bash `timeout` to
`(capture_secs + 80) * 1000` ms. Then analyze the boot stages:

```
./scripts/uart-summary.sh <label|path>     # stage health table (psh prompt, lwip, faults)
./scripts/uart-list.sh                      # recent UART logs
```

## D — Build a fresh image + flash the SD card

Build the variant you need (add `--with-tests` to include `/bin/test-libc-*`,
`--with-ports` for busybox etc., `--with-showcase` for GPU/X apps):

```
./scripts/rebuild-rpi4b-fast.sh --variant sd --with-tests     # -> artifacts/rpi4b/rpi4b-sd-2part.img
```

Flash to the card (in the host reader). No device check needed — `/dev/sda` is the card:

```
udisksctl unmount -b /dev/sda1 2>/dev/null || true
sudo dd if=artifacts/rpi4b/rpi4b-sd-2part.img of=/dev/sda bs=4M conv=fsync status=progress
sync
```

Optional integrity check (cheap, catches a bad write): read back N=`ceil(size/4M)`
blocks and compare SHA to the source image. Then tell the user to move the card to
the Pi (or, if you flashed it, it's ready — proceed to recipe A).

Notes:
- `rpi4b-sd-2part.img` is the real bootable card (FAT boot + ext2 root).
  `rpi4b-sd.img` is FAT-boot-only (no rootfs) — don't flash that for SD boot.
- `--scope`: `auto` (default) is fine after a normal edit; use `--scope core`
  after a committed kernel/devices/usb/lwip/libphoenix change (stale-core hazard),
  `full-clean` from cold.

## Reading results

```
log=$(ls -t artifacts/rpi4b-uart/rpi4b-uart-*-<label>.log | head -1)
grep -aE 'TEST\(|Tests .*Failures|^OK$|^FAIL|Exception|Data Abort|Fatal' "$log"
```

Use `grep -a` (logs have binary bytes). For Unity tests, look for the
`N Tests M Failures` summary + `OK`/`FAIL`; a failing assertion prints its
`file:line` + expected/actual. If a stage is missing (no psh prompt, no lwip),
the capture was too short — raise `--wait-secs`/`--capture-secs`, don't blame the
harness. "Slow boot"/"truncated" is usually a crash: grep the whole log for
`Exception`/`Data Abort` and check the last few HDMI frames (the final one is
often black from power-off).

## Gotchas

- One Pi cycle at a time (exclusive UART). Wait for a cycle to finish before the next.
- SD boot is fast (~30–60 s to psh); netboot adds DHCP/TFTP.
- The cycle powers the Pi OFF on exit (EXIT trap). A re-run with the card in needs
  no re-flash — just run recipe A again (power-cycle only).
- If you change these scripts to make a scenario easier, update this skill too.

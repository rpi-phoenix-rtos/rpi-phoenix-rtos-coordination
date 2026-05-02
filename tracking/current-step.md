# Current Implementation Step

## Step: Cleanup after first real Pi 4 psh prompt (TD-14)

**Status**: ACTIVE — Pi 4 reaches the UART shell prompt. The next task is
to reduce diagnostic backlog and validate interactive shell commands on real
hardware.

**Date**: 2026-05-02 night

**Manifest**: `manifests/2026-05-02-td14-uart-shell-prompt.md`

## What just changed

Sibling commits:
- kernel `60703368` — relative `proc_portLookup("devfs")` fix, direct
  stored OID for the `devfs` namespace, bounded TD-14 `proc_send("devfs")`
  timing probe.
- devices `63f1d438` — PL011 minimal stat/attr support plus direct
  `/dev/console` alias.
- devices `3ee4702` — `TIOCSPGRP` now stores the requested foreground
  process-group ID directly.
- libphoenix `3c76bba` — temporary `/dev/console` open trace plus a narrow
  fast path that skips the second `resolve_path()` walk for the direct console
  alias.
- utils `da2f541` — psh early probes use `debug()` and bracket tty open,
  `isatty`, `tcsetpgrp`, and first `readcmd`.

Validation:
- QEMU Pi 4 smoke reaches `(psh)% help`.
- Real Pi image SHA256:
  `d219efa27dd617ea171465f601742427ca1c96f3d505fb3979a1c7a27d0c520e`.
- Real Pi log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`.

## New known boundary

The first real Pi 4 UART prompt is reached:

```text
psh: tty ready
psh: tcsetpgrp
psh: tcsetpgrp done
psh: readcmd
(psh)%
```

## Next action

Run a cleanup-focused iteration:
- Strip or gate the highest-volume TD-04/TD-14 boot probes that are no longer
  needed for the prompt boundary.
- Keep the functional fixes: `devfs` direct OID, PL011 stat/attr support,
  `TIOCSPGRP` semantics, and the temporary direct console alias/fast path.
- Rebuild and run QEMU smoke.
- Run real Pi netboot long enough to verify `(psh)%`, then run an interactive
  UART smoke if the current helper supports sending commands.

Then run:

```bash
./scripts/rebuild-rpi4b-fast.sh
./scripts/qemu-shell-smoke.sh rpi4b
./scripts/test-cycle-netboot.sh --label td14-clean-prompt --capture-secs 240 --dhcp-wait-secs 90
python3 scripts/summarize-rpi4b-uart-log.py artifacts/rpi4b-uart/<latest>.log
```

## 2026-05-03 forward plan — toward HDMI text console + USB keyboard

The user's near-term milestone is **fully booted Phoenix-RTOS on Pi 4
with HDMI0 text console and USB keyboard input**. UART (psh)% is
verified. Remaining gates, in order:

### Gate 1 — clean reproducible boot

- Re-run real Pi netboot at the current main checkpoint
  (manifest `manifests/2026-05-02-td14-uart-shell-prompt.md`,
  image SHA `d219efa27dd6...`) twice in a row, ~20 min apart, to
  confirm `(psh)%` is reliably reached (not a one-off).
- If not reliable, investigate residual TD-14 races before going
  further.
- Strip the high-volume probes the previous agent noted:
  TD-13 spawn-cap log lines, TD-04 hack debug markers, TD-14
  per-syscall timing prints. Keep just enough to identify the
  pl011-tty boundary and the `(psh)%` event.

### Gate 2 — HDMI text console (HDMI0)

The wiring already exists end-to-end:

```
plo video.c (mailbox firmware framebuffer fetch)
  -> syspage_graphmodeSet(width, height, bpp, pitch, framebuffer)
  -> kernel pctl_graphmode handler in hal/aarch64/generic/generic.c
     reads hal_syspage->hs.graphmode and returns it
  -> pl011-tty/pl011-tty.c::pl011_fbcon_init() does platformctl(),
     mmap(framebuffer, MAP_DEVICE|MAP_UNCACHED|MAP_PHYSMEM),
     clears rows, prints "Phoenix-RTOS HDMI console\r\n".
  -> pl011_thr's TX path calls pl011_fbcon_write() per character
     (line 633), so kernel klog mirrored to UART also appears on HDMI.
```

Suspected current failure mode: in the latest psh-prompt log
(`rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`),
there is no "Phoenix-RTOS HDMI console" string, so
`pl011_fbcon_init()` either returned `-ENOSYS` (graphmode not
populated by plo) or never ran. The previous agent's probe-strip
removed both branches' debug() prints, so we can't tell which.

Plan:
1. Add a single `pl011_writeRaw(uart, "fbcon: ok\r\n")` /
   `"fbcon: skip <err>\r\n"` after `pl011_fbcon_init()` returns —
   one cheap UART line, not a debug() syscall.
2. If "fbcon: skip" with an error, drop into `pctl_graphmode`'s
   syspage read and confirm width/height/bpp/pitch/framebuffer are
   sane. They might be zero (plo never set them) or corrupt
   (TD-04-class on the syspage handoff).
3. If plo never set them: probe plo's `video_init` (file
   `plo/hal/aarch64/generic/video.c`). The mailbox sequence at
   `tag_setphywh / tag_setdepth / tag_setpxlordr / tag_getfb /
   tag_getpitch` may be timing out on real Pi 4 mailbox.
4. If plo set them but kernel reads garbage, that's TD-04-class on
   the syspage `hs.graphmode` field — same fix pattern as
   `_hal_syspageCopied` (NC mapping or DCIVAC + DSB).

Once fbcon prints its banner, kernel klog should mirror to HDMI
automatically because pl011_thr's TX path is the same on both
devices.

### Gate 3 — PCIe + xHCI + HID for keyboard

The pieces are present but their first IPCs probably hang on Pi 4
(same TD-14 IPC slowness as pl011-tty had). Pipeline:

```
phoenix-rtos-devices/pcie/server/pcie.c  (pid 8)
  - probes BCM2711 PCIe host bridge at PCIE_BCM2711_HOST_BASE
  - enumerates VL805 USB host controller
phoenix-rtos-devices/usb/xhci/xhci.c  (pid 9, "usb")
  - mmaps XHCI_BCM2711_MMIO_BASE
  - resets, runs xhci_init, scans ports
phoenix-rtos-usb/usb/dev.c
  - enumerates USB devices, dispatches to class drivers
phoenix-rtos-usb/libusb/hid_client.c
  - HID class driver, parses report descriptors,
    publishes /dev/kbd0 (boot-protocol keyboard)
phoenix-rtos-devices/tty/pl011-tty pl011_kbdthr() (PL011_TTY_KBD_PATH=/dev/kbd0)
  - opens /dev/kbd0, read()s keystrokes, libtty_putchar() into pl011 tty
  - psh sees keystrokes as if typed on UART
```

In the latest log, `pcie` and `usb` are spawned but produce no
progress beyond `main: spawned`. We need to:
1. Add cheap UART markers in `pcie/server/pcie.c` and
   `usb/xhci/xhci.c` `main()` entry to confirm they reach init.
2. If they hang on first lookup() or namespace IPC, apply the
   same TD-14 mitigations the other agent applied to pl011-tty
   (devfs-direct OID lookup, fast path, retries).
3. Confirm HID server publishes `/dev/kbd0` on real Pi 4 with a
   physical USB keyboard plugged into HDMI0-side port.
4. Confirm pl011-tty kbdthr opens `/dev/kbd0` and feeds keys.

### Gate 4 — interactive UART smoke

Once Gates 1-3 are stable:

- `help`, `ps`, `ls /dev` via picocom send.
- Boot-time prompt + interactive command in <60 s end-to-end.

### Gate 5 — interactive HDMI + USB keyboard smoke

Combined Gate 2 + Gate 3 result. Visual confirmation: type on
USB keyboard, see characters echo on HDMI display.

## Risk register for the plan

- **TD-04-class slowness still active.** Each Pi 4 cycle is
  expensive (~5 min minimum). Aggressive testing budget: at most
  3 Pi cycles per session. Use QEMU + careful single-edit changes.
- **HDMI mailbox** is a separate TD-04-class candidate. If plo
  mailbox doesn't drain reliably, fbcon won't init.
- **PCIe + USB on Pi 4** has never been exercised by Phoenix-RTOS
  per the commit history; first-time bring-up may surface its
  own TD-NN class of issues.
- **HID server / /dev/kbd0** existence is assumed by pl011-tty
  but not verified to actually be created by the usb stack on
  Pi 4. May need additional wiring.

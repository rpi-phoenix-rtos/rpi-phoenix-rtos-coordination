# Interactive verification runbook

Image to test: `8b3fc0b049a82332ca497c361bf26cdde480723c46393d2f2250fdc9338cd1da`
Manifest: [manifests/2026-05-19-td12-stable-plus-pm-sigint.md](../../manifests/2026-05-19-td12-stable-plus-pm-sigint.md)

What's in this image vs the morning's interactive build:
- Same TD-12 boot speed bundle (pl011-tty batch fbcon, usb setvbuf, threads.c hal_consolePrint stripped, Pass 4 noise cleanup)
- + `pl011_thr` one-batch fix (TX no longer starves RX → keyboard input responsive while klog drains)
- + `sleep(1)` restored in psh_run (upstream behaviour, hides cold-boot race)
- + **devfs fast-path predicate restored** (kernel `c8a81d5e` — fixes the 21.6 s `td14: send` hang that started appearing in test cycles)
- + **psh `pm` respects SIGINT** (utils `b188911` — Ctrl-C now exits the monitor cleanly instead of locking the shell)

## Procedure

1. Plug USB keyboard into one of the Pi 4 USB-A ports.
2. Connect serial (CoolTerm @ 115200, 8N1) — `/dev/cu.usbserial-201310` on this Mac.
3. Power on:

   ```
   /Users/witoldbolt/phoenix-rpi/scripts/pi_power_on.sh
   ```

4. Wait for `fbcon: ok` then ~1 s for `(psh)% ` (the sleep(1) intentionally
   gives klog time to drain). Total wall time from power-on to prompt: about 55-60 s
   (40-45 s firmware + 10-15 s plo+kernel+userspace).

## Things to verify

| Test | Command | Expected | Why |
|------|---------|----------|-----|
| Interactive psh | `help`, `pwd`, `mem`, `ps`, `ls /` | All return promptly | psh + fbcon + libtty working |
| Input responsiveness | type a long string fast | All chars appear without lag | pl011_thr one-batch fix |
| pm interruptible | `pm` then Ctrl-C | Returns to prompt | Today's `b188911` fix |
| `/dev/console` accessible | `ls /dev/console` | Shows the file | Bind+lookup honoured |
| `/dev/kbd0` exists | `ls /dev/kbd0` | Shows the file | Means usbkbd matched + created the device |
| HID report stream | `cat /dev/kbd0` then press a USB-keyboard key | Bytes appear (then Ctrl-C to bail) | Keyboard → /dev/kbd0 path live |
| USB keyboard into psh | At plain `(psh)%`, press a key on USB keyboard | Char appears on the line | Full chain: USB → kbd0 → pl011_kbdthr → libtty RX → psh |

## If `/dev/kbd0` does not exist after boot

The most likely reasons, in priority order:

1. **xhci enumeration didn't complete.** Look for `usbkbd: New device:` in the UART log — if absent, xhci either didn't run or hit an error somewhere between `usb-hcd: pre ops->init` and the device probe.
   Diagnostic: at psh, run `ps` and confirm a process named `usb` is in the list. If `usb` exited, it crashed mid-init.
2. **The keyboard isn't HID class.** Some Bluetooth dongles, gaming keyboards with proprietary protocols, or USB hubs report different classes. Try a different (basic, wired) keyboard.
3. **The xhci VL805 firmware reset hasn't settled.** Phoenix's xhci-pcie waits 200 ms after `bcm2711NotifyXhciReset`; if a particular keyboard model needs more, enumeration silently fails. A reboot sometimes helps.
4. **devfs registered `kbd0` but it didn't show up in lookup.** Phoenix-RTOS bind+readdir has a known limitation where `ls /dev` returns empty even when devices are registered. `ls /dev/kbd0` (full path) DOES work because lookup honours the bind.

## If `cat /dev/kbd0` blocks but key presses produce no bytes

That means `/dev/kbd0` was created (usbkbd matched a device) but the HID interrupt-IN pipe isn't delivering reports. Most likely causes: missed pipe IRQ, wrong endpoint descriptor, or boot-protocol report descriptor that doesn't match what the keyboard actually sends.

Capture the boot log (so I can grep for any xhci/usbkbd errors) and let me know — that's a deeper investigation that I'll need real telemetry for.

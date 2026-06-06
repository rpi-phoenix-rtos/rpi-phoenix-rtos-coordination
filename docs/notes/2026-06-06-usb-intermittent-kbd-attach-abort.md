# Intermittent USB-daemon EL0 instruction-abort during keyboard HID attach (2026-06-06)

Surfaced by the USB-enumeration reliability bench (netboot, current HEAD). 4 of the
first 5 bench boots were fully clean (psh + kbd0 + mouse0, 0 faults); **boot B5 showed
4× `Exception #32: Instruction Abort (EL0)`** after enumeration. Enum itself SUCCEEDED on
B5 (hub + `/dev/mouse0` + `/dev/kbd0` all created) — the aborts are a *contained
userspace crash*, not an enum failure, and the boot still reached lwip/DHCP. So this is
an **intermittent (~1 in 5) userspace crash**, not a hard regression.

Log: `artifacts/rpi4b-uart/rpi4b-uart-20260606-134508-netboot-enum-bench-B5.log` (lines 388-434).

## Signature (decisive)

- `Exception #32: Instruction Abort (EL0)`, `esr=0x8200000f` (instruction fetch fault),
  two distinct faulting PCs, each repeated: **`pc=0x1d5b8`** and **`pc=0x21f0`**, with
  `pc == far == lr` each time → the process **branched/returned to that address** and the
  fetch faulted.
- `aarch64-linux-gnu-addr2line` on the unstripped `prog/usb` resolves **`0x1d5b8` and
  `0x21f0` to `??`** (NOT valid code in the usb binary), while a *valid* nearby value
  `x16=0x4009d0` resolves cleanly to **`usblibdrv_open` (usb.c:552)**. Runtime VAs match
  ELF VAs (no PIE offset), so `0x1d5b8`/`0x21f0` are genuinely **non-code addresses** →
  this is a **wild jump through a corrupted code pointer**, not a normal fault.
- Registers x12/x13 decode (LE ASCII) to **`usb-046d-c31c-if00`** — the Logitech
  keyboard's (`046d:c31c`) USB device-path string. So the crash is in the **keyboard
  device-naming / HID-attach path**, right after `/dev/kbd0` is created and around
  `xhci: interrupt-IN pipe ready`.
- Related prior symptom: an earlier SD-boot log showed `usb: /dev/usb-046d-c31c-if00 ->
  /dev/kbd0 symlink error: 2` (ENOENT) on the same symlink/naming path.

## Interpretation

The USB daemon (USB_INTERNAL_ONLY; usbkbd/usbmouse linked in) intermittently invokes a
**corrupted function pointer** while attaching/opening the keyboard HID interface
(`usblibdrv_open` and the surrounding device-node-naming + `usb_drvPipeOpen` flow). A
garbage code pointer / return address → branch to `0x1d5b8`/`0x21f0` (not code) →
instruction abort. This is the same *family* as the historical USB memory-corruption
issues ([[project_usb_freelist_121_state]], #121 UAF / DMA-pool free-list) — an
intermittent corruption of a driver/struct pointer in the host stack.

## Status / next steps (NOT fixed — needs attended, validated work)

- **Do not blind-fix overnight:** USB-daemon internals + intermittent (≈1/5) → a fix
  cannot be validated in a few boots ([[feedback_unattended_scoping]]). Documented for
  attended root-cause.
- **Characterize the rate** via the ongoing enum bench (B1-B4 clean, B5 = aborts).
- **Audit the HID-attach callback path** for the corruptible pointer: how the in-daemon
  usbkbd driver's `usb_driver_t`/ops are dispatched during attach, the device-name/symlink
  creation (`usb-VID-PID-ifNN -> /dev/kbdN`), and `usb_drvPipeOpen` (usb.c:558). Look for
  an uninitialized/freed callback or a struct overwritten by the DMA-pool corruption that
  #121 mitigated-but-did-not-root-cause.
- Ties to tasks #142/#143 (USB daemon productionization) and #121 (free-list corruption,
  root cause UNCONFIRMED).

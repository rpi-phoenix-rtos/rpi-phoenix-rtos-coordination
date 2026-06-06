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
- **Rate (measured):** **1 in 10** boots on current HEAD (enum bench B1-B7 + the 3-boot
  stability bench: only B5 showed the abort). USB enumeration itself was **10/10 reliable**
  (every boot created `/dev/kbd0` + `/dev/mouse0`) — so the FIX-14/#78 enum large-N gate is
  substantially satisfied; this abort is a separate, rare HID-attach-path corruption.
- **Audit the HID-attach callback path** for the corruptible pointer: how the in-daemon
  usbkbd driver's `usb_driver_t`/ops are dispatched during attach, the device-name/symlink
  creation (`usb-VID-PID-ifNN -> /dev/kbdN`), and `usb_drvPipeOpen` (usb.c:558). Look for
  an uninitialized/freed callback or a struct overwritten by the DMA-pool corruption that
  #121 mitigated-but-did-not-root-cause.
- Ties to tasks #142/#143 (USB daemon productionization) and #121 (free-list corruption,
  root cause UNCONFIRMED).

## Candidate corrupted pointers (static audit, 2026-06-06)

The wild jump (`lr`→non-code) is a branch/return through a clobbered pointer. In the
kbd attach/open path the dispatched code pointers are:
- `drv->handlers.insertion` / `drv->handlers.completion` — the registered
  `usb_driver_t usbkbd_driver` (usbkbd.c:839) function pointers; invoked by the daemon
  per insertion/completion. usbkbd_handleInsertion DID run ("handleInsertion fired"), so
  the clobber happens during/after it (opening the interrupt-IN pipe).
- `hcd->ops->transferEnqueue` (usb.c:138) and `t->ops->urbAsyncCompleted`/`urbSyncCompleted`
  (usb.c:410/413) — HCD / transfer op tables.
- `drv->hostPriv` (usb.c:548, deref in usblibdrv_open) — if the priv pointer is clobbered.
- A stack return address smashed by a buffer overflow in device-name formatting
  (`usb-VID-PID-ifNN` → `/dev/usb-...`); LOWER probability — a fixed-size overflow would
  be deterministic (10/10), but the abort is 1/10, so heap/DMA-pool corruption hitting one
  of the pointers above (the #121 free-list family) is the more likely culprit.

Attended next step needs runtime: a guard/canary on `usbkbd_driver` + the hcd/transfer op
tables (detect when a function pointer leaves its expected value), or poison-on-free in the
usb mem pool to catch the #121 use-after-free at the moment of corruption.

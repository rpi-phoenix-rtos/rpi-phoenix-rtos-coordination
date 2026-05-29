# Known Hardware-Bounded Limits

> **⛔ THE TEST BOARD IS KNOWN-GOOD. "Broken hardware" is NEVER the cause of
> our bugs.** The specific Pi 4B we test on works correctly under Linux
> (Raspberry Pi OS) for USB (incl. USB-keyboard input), WiFi, Ethernet, and
> the rest, and USB keyboard input also works in the firmware boot menu. So
> every USB / WiFi / PCIe / DMA / peripheral failure under Phoenix is a
> **Phoenix software bug**, not flaky/defective silicon. Entries below are
> mainline-Linux/forum **context** only; they must **never** be cited as the
> explanation for *our* failures. This is a permanent project rule — see
> `AGENTS.md`. (The USB "silicon flakiness" conclusion was formally
> retracted on 2026-05-29.)

This document consolidates Pi 4 platform issues reported in mainline
Linux / other OSes. The point is to save future developers from
re-investigating the mainline *context* — NOT to license attributing our
own failures to hardware (see the banner above).

## Index

- [USB / VL805 — silicon angle investigated, NOT confirmed as our cause](#usb--vl805--silicon-angle-investigated-not-confirmed-as-our-cause)
- [WiFi / BCM43455 firmware execution gate](#wifi--bcm43455-firmware-execution-gate)
- [Eth / PHY INT_B not routed to GIC](#eth--phy-int_b-not-routed-to-gic)

---

## USB / VL805 — silicon angle investigated, NOT confirmed as our cause

> **Correction (2026-05-29).** This entry previously asserted that our
> USB failures were "silicon flakiness." A three-agent re-analysis of the
> raw logs (see `docs/notes/2026-05-29-usb-reanalysis.md`) does **not**
> support that conclusion, and our USB failure is most likely a *software*
> bug. The entry is kept here only to record the genuine — but separate —
> mainline-Linux silicon issue and why it is probably **not** our cause.

**The genuine mainline silicon issue (real, but likely not ours):** some
Pi 4 boards have a documented VL805 reliability problem; a few need a
hardware RC circuit (~10 ms delay between 3.3V and nPONRST). This is a
board-level repair, not a software change.

- [Linux #5060 — xHCI host not responding to stop endpoint](https://github.com/raspberrypi/linux/issues/5060)
- [CM4 VL805 firmware (RC-delay hardware fix)](https://forums.raspberrypi.com/viewtopic.php?t=380969)
- [u-boot patch: Load Raspberry Pi 4 VL805's firmware](https://patchwork.ozlabs.org/project/uboot/cover/20200629163725.13330-1-nsaenzjulienne@suse.de/)
- [USB dead on Pi4 — RPi forums #344412](https://forums.raspberrypi.com/viewtopic.php?t=344412)

**Why it is probably not our cause:** USB keyboard input always works on
this exact test board under Linux and under the Pi firmware boot menu.
Our two observed failure populations have software-shaped signatures:

- *Embedded PoC:* a **100% deterministic** failure — bridge up,
  controller running (`CRR=1`), but zero events ever DMA-written
  (`first event @idx -1`). Silicon link-flakiness cannot be deterministic.
- *`X` diag rig:* intermittent bridge-MMIO-dead (`pre USBSTS=0x00`/`0xdead`),
  but the runs were 3–6 min apart — inside the project's own documented
  rapid-cycle bridge-degradation window (`docs/status.md:267-280`), which
  is a software/cadence cause, not silicon.

The detailed evidence, the concrete `xhci.c` bugs found (non-spec event
ring, halt-per-command, recycled-DMA-page cache hazard, inert
`resettleOutboundWindow` in DRIVE_ONLY), and the corrected pass-rate
numbers (the old "0/21" was inflated by ~12 truncated captures) are in
`docs/notes/2026-05-29-usb-reanalysis.md`. USB is therefore an **active
software investigation**, not a hardware-bounded limit; it should not be
treated as closed/silicon. See also memory `usb-dma-write-loss`
(corrected) and `vl805-known-silicon-flakiness` (downgraded to SHAKY).

## WiFi / BCM43455 firmware execution gate

**Symptom on Phoenix:** after a textbook ARM-CR4 release (IoCtrl
pre=0x21 post=0x01) the firmware does not execute — HT_AVAIL is never
set in CHIPCLKCSR, F2 never readies, CARD_INTR stays 0, the SDIO-DEV
to-host mailbox stays at 0, and SOCRAM head is unchanged. Five
independent signals all say "fw not running."

**Status in mainline Linux:** identical symptoms (HT Avail timeout)
reported repeatedly. Software-level recovery — module reload, driver
unbind / bind — does not work; only a true PSU power cycle restores
operation.

- [OpenWrt #23069 — HT Avail timeout, PSU-cycle-only recovery](https://github.com/openwrt/openwrt/issues/23069)
- [Sporadic failure to load brcmfmac wifi firmware (Infineon)](https://community.infineon.com/t5/AIROC-Wi-Fi-MCUs/Sporadic-failure-to-load-brcmfmac-wifi-firmware/td-p/402183)
- [raspberrypi/linux #4145 — brcmfmac firmware regression](https://github.com/raspberrypi/linux/issues/4145)

**Documented root cause:** the Pi 4 `WL_REG_ON` GPIO path through the
expander cannot fully power-cycle the 43455 — some chip state survives
the toggle that only a true PSU drop clears.

**What Phoenix does today:** WiFi P3 (firmware load + CR4 release)
verified up to the point of releasing the CR4 with the correct
IoCtrl / rstvec / NVRAM trailer / cold-reset sequence. Parked here
because nothing software-side can clear the chip state that the
hardware `WL_REG_ON` toggle leaves behind.

See memory entry `bcm43455-chip-id` for the full investigation trail.

## Eth / PHY INT_B not routed to GIC

**Symptom:** the BCM54213PE PHY exposes an `INT_B` link-state
interrupt pin but the Pi 4 board doesn't route it to a GIC SPI.

**Status in mainline Linux:** Linux and U-Boot both MDIO-poll the PHY
for link state on this board — the interrupt is not used.

**What Phoenix does today:** TD-Eth-LinkIRQ tracks this. `genet_linkPollThread`
polls MDIO at 1 Hz (same cadence as Linux's `mii_link_poll`). Revisit
if a future Pi-4 board variant exposes the line.

---

## What's NOT in this document

- **TD items in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`** are
  Phoenix-internal shortcuts taken during bring-up. Those are fixable
  in software; this doc is only for issues Phoenix cannot fix.
- **Test-infrastructure quirks** (e.g. TD-Eth-DHCP being infra-gated)
  live in the TD doc, not here. Those *can* be unblocked by improving
  Phoenix's test tooling.

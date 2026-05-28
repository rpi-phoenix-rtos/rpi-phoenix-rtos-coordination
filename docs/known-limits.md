# Known Hardware-Bounded Limits

This document consolidates Pi 4 platform issues that affect mainline
Linux and other OSes equally — they are *not* Phoenix-specific bugs
and cannot be fixed in Phoenix software. The point is to save future
developers from re-investigating them.

## Index

- [USB / VL805 silicon flakiness](#usb--vl805-silicon-flakiness)
- [WiFi / BCM43455 firmware execution gate](#wifi--bcm43455-firmware-execution-gate)
- [Eth / PHY INT_B not routed to GIC](#eth--phy-int_b-not-routed-to-gic)

---

## USB / VL805 silicon flakiness

**Symptom on Phoenix:** the `X` diag rig has ~50 % pass rate across
fresh cold boots with no code change between trials. Failures show
`pre USBSTS=0x00` — bridge MMIO reads return 0 for arbitrary registers,
indicating the PCIe link / outbound translation is in a bad state.

**Status in mainline Linux:** identical-symptom failures reported at
mainline scale.

- [Linux #5060 — xHCI host not responding to stop endpoint](https://github.com/raspberrypi/linux/issues/5060)
- [CM4 VL805 firmware (RC-delay hardware fix)](https://forums.raspberrypi.com/viewtopic.php?t=380969)
- [u-boot patch: Load Raspberry Pi 4 VL805's firmware](https://patchwork.ozlabs.org/project/uboot/cover/20200629163725.13330-1-nsaenzjulienne@suse.de/)
- [USB dead on Pi4 — RPi forums #344412](https://forums.raspberrypi.com/viewtopic.php?t=344412)

**Documented fix where it exists:** some affected boards need a
hardware RC circuit creating ~10 ms delay between 3.3V and nPONRST.
This is a board-level repair, not a software change.

**What Phoenix does today:** `bcm2711_pcie_initVL805`'s DRIVE_ONLY
path waits for `PCIE_DL_ACTIVE | PHYLINKUP` for up to 10 s and returns
`-ENODEV` if the link doesn't come up, with MISC_STATUS printed for
diagnostics. The xhci HCD bails cleanly rather than flailing on dead
MMIO.

**Phoenix-specific bug separate from the silicon issue:** the embedded
USB host stack PoC fails 0 / 21 trials even when the link is up and
the controller is in the expected halted state. The rig's 50 % pass
rate is *silicon flakiness*; the PoC's 0 % is an *additional* bug.
See memory entries `usb-dma-write-loss` and `vl805-known-silicon-flakiness`
for the full investigation. Currently parked pending a fresh angle.

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

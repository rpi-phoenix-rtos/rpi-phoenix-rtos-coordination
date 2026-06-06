# USB Triage Resumption Strategy (2026-05-25)

USB on Pi 4 was parked 2026-05-24 at the statistical mode A/B/C wedge
(see `docs/knowledge/rpi4-os-development-guide.md` §"PCIe + USB"). The stated
reason: UART-only triage isn't producing new data and the wedge is
silent at the OS layer.

This doc captures the resumption strategy now that net-routed
observability is in place (lwip `b261265` and the subsequent
diag-udp suite, today 2026-05-25).

## What changed

The `diag-udp` UDP responder on port 9999 (running inside the
lwip-port, after Ethernet is up — which it always is post-fbcon)
gives us a cross-process, on-demand MMIO + mailbox probe channel.
We can read xHCI registers, query throttle bits, dump cmd-ring
DRAM contents from a side process, all without re-instrumenting
the failing `usb-hcd` process and without UART.

The single biggest implication: **the original parking decision
("nothing to chase because nothing's observable") is no longer
fully true**. Worth one more software-side iteration before
falling back to JTAG.

## Four hypotheses, ranked by likelihood + cheapness

### H1: TD-Pi4-FalseSharingPenalty applied to xHCI command ring

The most novel hypothesis, brought in by today's SMP Phase E
saturation experiment. We discovered that Phoenix userspace memory
on Pi 4 has cache-domain behavior where plain `volatile T*` writes
don't always reach cross-core or cross-domain consumers — fixing the
GENET saturation burner required cache-line padding + `_Alignas(64)`
slots per `TD-Pi4-FalseSharingPenalty` in
`docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.

The xHCI command ring sits in DRAM that the host writes via cached
pages and that the VL805 controller reads via PCIe bus master DMA.
If those memory views don't share an inner-shareable cache domain,
the controller sees stale memory (zero) while the host thinks the
TRB is written. **Mode-C symptom matches exactly**: doorbell rings,
CRCR_LO stays at 0 (controller didn't advance the ring because it
sees an empty TRB), event[0] stays at 0 (no command completion to
report).

**Test (via diag-udp)**: have lwip-port mmap the cmd-ring PA AFTER
usb-hcd has written a NOOP TRB but BEFORE the doorbell. If the
side process sees the TRB content, the data is in shareable memory
and the controller should see it too — H1 ruled out. If the side
process sees zeros or stale data, H1 is confirmed and the fix is
the same pattern that worked for the GENET burner.

If H1 fixes mode C, **it may also fix modes A and B** since the
bridge inbound DMA state could be similarly affected by domain
issues that aren't visible to UART-only diagnostics.

### H2: Pi 4 under-voltage / throttle correlation

Pi 4's PMIC has a documented community correlation between under-
voltage events and "statistical PCIe/USB weirdness". The thermal
probe added today (`lwip 36ef795`) already exposes the throttle
bitfield. The bits we care about:

| Bit | Meaning |
|-----|---------|
| 0 | Under-voltage NOW active |
| 16 | Under-voltage SINCE BOOT (sticky) |

**Test (via diag-udp)**: run multiple USB cycles, classify each
into mode A/B/C, query `GET_THROTTLED` post-fail. If mode B (HSE
on R/S=1) correlates with bit 0 or bit 16 set, the lab bench PSU
is marginal and the fix is hardware (known-good 5.1 V 3 A USB-C
PSU). This isn't a "Phoenix bug" — it's a board-level signal-
integrity issue that Linux users hit constantly.

### H3: Per-process bridge translation state, but for READS

Earlier USB work found that two userspace processes both `mmap`ing
the PCIe outbound window get incoherent views — the "USB merge"
finding documented in
`docs/knowledge/rpi4-os-development-guide.md` §"Worked example: the USB
merge". That was about WRITING (each process configures the bridge
differently).

The question for diag-udp is whether **reads-only** from a side
process work. If yes, diag-udp can do xHCI observability for any
future USB triage. If no, the inspector has to live inside
usb-hcd itself.

**Test**: simple — diag-udp 'x' command tries to mmap the xHCI
MMIO PA and read HCIVERSION + USBSTS + USBCMD. Compare to what
usb-hcd reports in its UART trace.

This experiment is **inherently valuable** regardless of which
hypothesis turns out to be the cause: it tells us whether
post-WiFi+post-BT cross-process MMIO inspection is viable in
general.

### H4: Silicon variability we can't fix in software

The original 2026-05-24 parking decision. Still possible. But the
three above are cheap to test and any of them being true is a
software-fixable problem; H4 alone is the case where JTAG is
needed.

## 10-cycle experiment plan

Budget: ~10 test cycles. Each ~3 min wall with the watchdog reboot
path (was ~6 min with Meross). Total ~30-45 min wall + analysis.

| Iter | Goal |
|------|------|
| A | Add 'x' (xHCI MMIO snapshot) to diag-udp. Validate side-process MMIO reads work (rules H3 in/out). |
| B | Same probe; also dump cmd-ring TRB content from side process AFTER usb-hcd init has run (H1 test). |
| C | Add 'V' (USB power + throttle). Baseline. |
| D-H | Run 5 usb-hcd attempts to completion or failure; query 'x'/'V'/'t' on each; classify mode A/B/C; record throttle correlation. |
| I-J | Based on D-H, implement the most-likely fix and re-test 2 cycles. |

## Decision tree at iteration's end

- **H1 confirmed** (cmd-ring TRB reads as zero from side process):
  patch usb-hcd to use C11 atomic stores + `__atomic_thread_fence(SEQ_CST)`
  before the doorbell. Re-test. If pings, commit + done.
- **H2 confirmed** (throttle bit 0/16 correlates with mode B):
  document hardware dependence in
  `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`. Swap PSU and re-test.
- **H3 disproved** (side reads return `0xdeaddead`): observability
  has to live inside usb-hcd. Document, move on to WiFi/BT.
- **Inconclusive**: H4 (silicon variability) becomes the leading
  hypothesis. Pin a manifest summarizing the failure mode +
  classification + throttle correlation, accept the JTAG block,
  move on to WiFi/BT.

## Constants worth keeping handy

- xHCI MMIO mapping size: per `usb/xhci/xhci.c`
  `XHCI_MAP_SIZE = 0x10000` — **likely too big** per
  `memory/project_bcm2711_pcie_64bit_bug.md` cause A. VL805 BAR0
  is only 4 KiB.
- xHCI MMIO PA: determined at runtime by usb-hcd via PCIe
  enumeration. Capture from a usb-hcd UART trace or compute by
  walking the indexed config like `bcm2711-pcie.c` does.
- VL805 PCIe BDF: `XHCI_BCM2711_PCIE_BUS:SLOT.FUNC` from
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`.
- PCIe outbound window: `0x600000000` base, 64 MiB.
- VC mailbox `GET_THROTTLED` tag `0x00030046`.
- VC mailbox `GET_POWER_STATE` tag `0x00020001`, device_id 3 = USB HCD.

## References

- `memory/project_bcm2711_pcie_64bit_bug.md` — full prior USB triage,
  causes A-D documented.
- `memory/project_phoenix_usb_patterns.md` — Pi 4 USB divergence audit.
- `memory/project_pi4_diag_udp_pattern.md` — the diag-udp infrastructure.
- `docs/knowledge/rpi4-os-development-guide.md` §"PCIe + USB" — narrative.
- `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` `TD-Pi4-FalseSharingPenalty`.

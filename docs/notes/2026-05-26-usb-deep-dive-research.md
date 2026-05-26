# USB deep-dive (2026-05-26): VL805 bring-up failure analysis

## Context

After ~2 weeks of accepted "H4 silicon variability" parking for the
Pi 4 USB stack, a fresh challenge surfaced:

- The Pi 4 firmware/bootloader **uses the same VL805 successfully**
  during boot (USB keyboard input for boot device selection works).
- **Linux on the same physical board has working USB.**

So the hardware is fine. Phoenix is doing something software-side
that the reference implementations (Linux, Circle, Pi firmware,
NetBSD) get right.

Three parallel agents investigated:
1. Linux drivers/usb/host/xhci-pci.c + drivers/pci/controller/pcie-brcmstb.c + raspberrypi/linux specifics
2. Bare-metal Pi 4 OS survey (Circle, USPi, Ultibo, NetBSD, FreeBSD)
3. Phoenix-side code review of xhci.c + bcm2711-pcie.c

## Key findings (ranked by evidence strength)

### USB-FIX-1: BUS_MASTER ordering INVERTED relative to every reference

Phoenix `bcm2711-pcie.c::scanFunc` enables `BUS_MASTER` BEFORE the
firmware mailbox notify. Every reference does the opposite:

- **Circle** `lib/usb/xhcidevice.cpp::CXHCIDevice::Initialize`:
  Mailbox notify, THEN `EnableDevice` (BAR + cmd = MEM | MASTER).
- **Linux** `drivers/usb/host/pci-quirks.c::quirk_usb_early_handoff`:
  `pci_enable_device()` (MEM only), mailbox notify, BAR allocation,
  then in `xhci_pci_probe`: `pci_set_master()`.
- **NetBSD bwfm**: same pattern, MEM before, MASTER after.

The mailbox handler touches bridge-side registers. If BUS_MASTER is
on, VL805 can issue inbound DMA reads against stale bridge
translations during that window, corrupting its internal DMA TLB.
Symptom: HSE on first R/S=1 (mode B in 10-cycle experiment).

Phoenix's own comment at `bcm2711-pcie.c:859-865` admitted the
empirical observation that putting MASTER after produced 0xdead
capability reads. Re-analysis: 0xdead came from the
bridge-translation invalidation bug (fixed via dev-0-only PCIe scan
+ keep-alive mmap), not from BUS_MASTER ordering. Capability reads
go through MEM_ENABLE (outbound). BUS_MASTER only governs inbound
DMA. So pre-vs-post BUS_MASTER doesn't affect cap reads.

**Status (2026-05-26)**: Applied as commit f8c7543. Did NOT fix
the wedge. The ordering change is correct per every reference but
isn't the load-bearing cause.

### USB-FIX-2: Unconditional mailbox notify (`raspberrypi/firmware#1617`)

Linux's `rpi_firmware_init_vl805()` reads `PCI_CONFIG[0x50]` (VL805
firmware version register). If non-zero, the EEPROM has already
loaded firmware and the mailbox call is **skipped**.

`raspberrypi/firmware#1617` documents repeated XHCI_RESET notifies
producing the exact symptom `can't setup: -110` -- our error code.

On Pi 4B 1.4+ (and Pi 400, CM4), the EEPROM ships VL805 firmware.
The mailbox call is then redundant and harmful -- it tears down a
working state and re-loads firmware against a bridge that's been
configured for the running system.

**Status (2026-05-26)**: Applied as commit f8c7543 -- skip mailbox
when fw_ver != 0. Did NOT fix the wedge either, in combination
with FIX-1.

### USB-FIX-3: Bridge re-settle missing after HCRST

Phoenix has `bcm2711_pcie_resettleOutboundWindow()` as a callable
helper but only invokes it once (post-mailbox in
`bcm2711_pcie_initVL805`). `xhci_reset()` does HCRST + 100 ms
settle but never re-invokes the outbound-window re-program.

If HCRST churns bridge state (analogous to the mailbox), then
DCBAAP / CRCR / ERSTBA writes that follow immediately can land on
a stale translation, producing the HSE we see.

The CRCR re-publishing before each doorbell is currently a
workaround for this -- symptom rather than root fix.

### USB-FIX-4: 64-bit MMIO pointer-write hazard (`raspberrypi/firmware#1424`)

BCM2711 bridge may not handle single 64-bit MMIO stores to AC64
xHCI pointers correctly. `DCBAAP`, `CRCR`, `ERSTBA`, `ERDP` must be
two 32-bit stores (LO then HI), never a single `str x0`-style
64-bit. If the compiler combines two 32-bit assignments into a
`stp` or `str` 64-bit instruction, the high word can be silently
corrupted -> HSE on first DMA fetch.

Worth a code audit in `xhci.c`.

### USB-FIX-5: VSR1 endian + PCIE_MISC_CTRL bits

Constants for `RCB_MPS_MODE` (bit 10), `RCB_64B_MODE` (bit 7),
`SCB_ACCESS_EN` (bit 12), `CFG_READ_UR_MODE` (bit 13),
`SCB0_SIZE_4G` (bits 31:27=17), `VSR1` endian-bar2 = 0 are all
defined in `bcm2711-pcie.c:367-374`. Need to verify they're
actually written by `pcie_cfgInitBcm2711` and the bridge state is
correct before any device-side access.

## Other (informational) findings

- **IMOD value mismatch**: Phoenix writes 4000 to IR_IMODI (~1 ms);
  Linux writes 160 (~40 us). Not load-bearing for cold-boot wedge.
- **MSI vs INTx**: Phoenix uses polling. xHCI doesn't need MSI to
  come up; HSE happens before any interrupt would fire.
- **HCCPARAMS1.AC64=1 for VL805**: confirmed. So pointers are 64-bit
  and the FIX-4 hazard applies.
- **Capprobe 0xdead poison handling**: retry-with-delay only. If
  poison persists, we should re-settle bridge, not just re-read.

## Next experiments (ranked)

1. **Apply FIX-3** (bridge re-settle after HCRST) and re-test.
   Smallest change with strong-but-not-overwhelming evidence;
   complements the workaround already in place via CRCR re-publish.

2. **Audit FIX-4** (64-bit pointer writes). Read `xhci.c` for every
   write to DCBAAP / CRCR / ERSTBA / ERDP / ERDP_LO / ERDP_HI.
   Confirm each is two 32-bit stores. If the compiler is emitting a
   single 64-bit, fix with `volatile` cast and explicit LO-then-HI
   writes.

3. **Audit FIX-5** (PCIE_MISC_CTRL bits). Trace through
   `pcie_cfgInitBcm2711` and confirm every bit per Linux
   `pcie-brcmstb.c` is set. Especially the SCB0_SIZE field for
   4GiB (4 GB lab board).

4. **Try lwip-port side-process bring-up with FIX-3** applied. Iteration
   L showed three variants all failed; if FIX-3 changes that, it's
   the answer.

5. **Side-channel: confirm fw_ver_pre value** via diag-udp probe
   command that reads VL805 PCI config[0x50] independently. Tells
   us whether our lab board's EEPROM loaded firmware or not, and
   thus whether FIX-2 had any effect.

## Sources (selected)

- raspberrypi/firmware issue #1617 -- repeat XHCI_RESET -> -110
  (https://github.com/raspberrypi/firmware/issues/1617)
- raspberrypi/firmware issue #1495 -- RC_BAR2 state assumption
  (https://github.com/raspberrypi/firmware/issues/1495)
- raspberrypi/firmware issue #1424 -- AC64 vs 32-bit-only bridge
  (https://github.com/raspberrypi/firmware/issues/1424)
- Linux commit 9935b4c -- usb_vl805_init introduction
  (https://github.com/raspberrypi/linux/commit/9935b4c7e360b4494b4cb6e3ce797238a1ab78bd)
- Linux commit fdb3db3 -- VL805 firmware loader v2
  (https://github.com/raspberrypi/linux/commit/fdb3db37f4ebe998f0d41ebfe3c8770f4cc4e97a)
- Circle xhcidevice.cpp
  (https://github.com/rsta2/circle/blob/master/lib/usb/xhcidevice.cpp)
- Circle bcmpciehostbridge.cpp
  (https://github.com/rsta2/circle/blob/master/lib/bcmpciehostbridge.cpp)
- linux-usb VL805 DMA read faults
  (https://www.spinics.net/lists/linux-usb/msg161380.html)
- raspberrypi forum 365719 -- VL805 firmware lost after PCI reset
  (https://forums.raspberrypi.com/viewtopic.php?t=365719)

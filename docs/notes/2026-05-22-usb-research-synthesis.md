# USB-on-Pi-4 research synthesis (2026-05-22)

5 parallel research agents (BSD, Linux, bare-metal, Phoenix-RTOS
cross-platform audit, web/forum/spec search) returned with the
breakthrough we'd been missing. The kernel-pmap rabbit hole was
the wrong layer. The actual cause set is in BCM2711 + VL805
silicon quirks that the major OSes ALREADY know about and work
around.

## Headline finding

**VL805 BAR0 is only 4 KiB.** Our `XHCI_MAP_SIZE = 0x10000` maps
**64 KiB**, so the trailing 60 KiB of the mapping points at unmapped
PCIe address space. The brcmstb PCIe root complex *aborts* on any
access there (not 0xFFFF — actually aborts); arm64 absorbs the
SError and returns the literal sentinel `0xdeaddead` to the load
target. Speculative reads, compiler-coalesced wider loads, or
out-of-order issue can all trip this from inside what we thought
was the "valid" first 4 KiB.

This explains the size-and-PROT-sensitive symptom we kept seeing.
A 4 KiB PROT_READ mapping fits the BAR exactly and never spills;
a 64 KiB PROT_RW mapping spills.

## The three orthogonal causes of `0xdeaddead`

1. **Cause A — mapping spills past BAR** (most likely for us).
   VL805 BAR0 = 4 KiB. Anything past it = bridge abort = 0xdead.
2. **Cause B — 64-bit MMIO read corrupts high half**
   ([raspberrypi/firmware#1424](https://github.com/raspberrypi/firmware/issues/1424)).
   VL805 advertises HCCPARAMS1.AC64 = 1 but the BCM2711 path
   returns garbage in the high 32 bits. Fix: always read xhci
   registers as two 32-bit loads, treat AC64 as 0.
3. **Cause C — outbound window not covering access**. Different
   from A: this is when WIN0_BASE_LIMIT math is wrong or
   pre-link-up. We've verified this is NOT our current cause
   (Diag #2 readback of WIN0 was correct).

## Cross-validated bring-up sequence (from Linux/BSD/Circle/U-Boot)

1. PCIe RC SW reset → assert PERST → clear SerDes IDDQ →
   MISC_CTRL burst=128B.
2. **Inbound RC_BAR2 = offset 0, size ≥ 3 GiB.**
   ([firmware#1495](https://github.com/raspberrypi/firmware/issues/1495):
   VPU's XHCI_RESET assumes bus==CPU 1:1; HI offset must be 0.)
3. Deassert PERST, sleep 100 ms per CEM spec.
4. Poll link-up (≤100 ms).
5. Outbound window — 1-MiB-granular math (we do this correctly).
6. Indexed-config enumeration, BAR programming with `MEM_TYPE_64`.
7. MEM-enable + bus-master in command reg.
8. **Mailbox `RPI_FIRMWARE_NOTIFY_XHCI_RESET`** (tag 0x00030058,
   channel 8) before any BAR read.
9. **No delay needed** — mailbox is blocking.
10. First MMIO read of HCIVERSION; standard xhci_reset.

Phoenix already does steps 1-8 correctly. The break is at step
10 because of Cause A (and possibly B).

## Other VL805 quirks we'll hit later

- **TRB overfetch** ([linux be18ca1](https://github.com/raspberrypi/linux/commit/be18ca1d4ca4cd6b85eabfe3645d3d11ad0939d3)):
  VL805 prefetches across 4 KiB boundaries even past Link TRB.
  Linux quirk = `XHCI_TRB_OVERFETCH`.
- **Link-TRB ep context bug**: hw-maintained endpoint context
  sticks at Link TRB address → spurious ring-expansion events.
- **Don't repeat NOTIFY_XHCI_RESET** ([firmware#1617](https://github.com/raspberrypi/firmware/issues/1617)).
- **ASPM L1 substates disabled** by Pi engineers' recommendation.

## License-safe reference code to port

- **FreeBSD** `sys/arm/broadcom/bcm2835/bcm2838_pci.c` + `bcm2838_xhci.c`
  — **ISC** license. ~960 lines total. Cleanest reference.
- **NetBSD** `sys/arch/arm/broadcom/bcm2838_pcie.c` + `.h`
  — **BSD-2-clause**. Register header uses `__SHIFTIN()` — cleaner
  bit math than our hand-rolled masking.
- **Circle** `lib/bcmpciehostbridge.cpp` + `lib/usb/xhcidevice.cpp`
  — MIT-like. Best for the integrated PCIe+xhci bring-up flow.
- Linux brcmstb-pcie / pci-quirks.c — GPL-2.0, **reference only**.

## Phoenix-RTOS-side cleanup (separate from the silicon fix)

From the cross-platform audit:

- Pi 4 USB diverges substantially from canonical ia32/imx6ull
  pattern. The comment claiming otherwise (`xhci.c:1992`) is
  factually wrong.
- `usb/xhci/bcm2711-pcie.c` is a fork of `pcie/server/pcie.c` —
  two copies will rot.
- `usb/xhci/xhci.c` uses `fprintf(stderr)` 73× instead of
  `log_error` from `phoenix-rtos-usb/usb/log.h`.
- Pi-4 development `debug()` calls landed in
  `phoenix-rtos-usb/usb/hcd.c:198-218` — shared upstream code.
- `XHCI_MAP_SIZE` is duplicated between `xhci.c` and
  `bcm2711-pcie.c` (which is also why this bug landed: the BSP
  carries its own copy of a wrong-by-16× constant).
- xhci has no IRQ handler — currently poll-only. Will limit
  performance once USB works.
- Long-term: kernel-side PCI scan (`platformctl(pctl_pci)`) like
  ia32 does is the real canonical pattern; would eliminate
  `getXhciMmio()`, the `cfgio.destroy` leak, and the per-process
  bridge-state workaround.

## Re-evaluated hypotheses

Of the three earlier ranked hypothesis sets, the most likely
truth is now:

- ✅ **BSD/web cause A** — VL805 BAR is 4 KiB, mapping spills.
  Verified by the silicon constraint, the abort behavior, and
  our size-sensitive symptom. **Test first.**
- ✅ **BSD/web cause B** — 64-bit-MMIO high-half corruption.
  Strong evidence (multiple OSes work around it). **Will need
  attention but probably not as the first fix.**
- ❌ Codex hyp #2 (cached-scratch_tt MMU walker visibility) —
  rejected on re-read of pmap.c (proper Inner Shareable
  Cached + dsb sy + tlbi). Validated by the failed dsb-barrier
  experiment (it made things WORSE).
- ❌ Codex hyp #3 (RW mapping racing VL805 reset state) —
  doesn't fit the WIN0 readback evidence.
- ❌ Kernel pmap aliasing — the bug is silicon-level, not
  software-aliasing.

## Plan for next iteration (when SMP work is done per user direction)

1. **Single-line change**: reduce `XHCI_MAP_SIZE` from `0x10000`
   to `0x1000`. Same for `BCM2711_PCIE_XHCI_MMIO_SIZE` in
   `bcm2711-pcie.c` (or unify both into `xhci.h`).
2. Rebuild and run 3-cycle stability test on hardware. Expected
   outcome: xhci_capProbe reads valid data; failure mode shifts
   beyond rc=-19/110 toward something later in init.
3. If still failing, audit disassembly of `xhci_read*` and
   add 32-bit-only 64-bit register access helpers (Cause B fix).
4. Long-term cleanup (separate PRs): unify `XHCI_MAP_SIZE`,
   replace `fprintf` with `log_error`, remove `debug()` from
   shared `hcd.c`, port FreeBSD/NetBSD reference code where it
   improves on our forked version.

Sources (full citations in
[`docs/notes/2026-05-21-pcie-bridge-ageing-codex.md`](2026-05-21-pcie-bridge-ageing-codex.md)
+ [`2026-05-22-kernel-pmap-investigation.md`](2026-05-22-kernel-pmap-investigation.md)
plus the agent reports in
`/tmp/claude-1000/.../tasks/`):
- raspberrypi/firmware#1424 (VL805 AC64 lie)
- raspberrypi/firmware#1495 (RC_BAR2 offset constraint)
- raspberrypi/firmware#1617 (don't repeat NOTIFY_XHCI_RESET)
- pftf/RPi4 issue #50 (BSD UEFI xHCI 0xdead reproduction)
- threedots.ovh BAR mapping article
- Pete Batard's MS xhci.sys reverse-engineering blog
- FreeBSD reviews D25068, D25261

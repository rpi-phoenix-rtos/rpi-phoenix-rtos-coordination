# USB Stage-4 Phase 2 — VL805 xHCI bring-up to HID keystrokes

Implementation plan for finishing Phoenix-RTOS USB host on Raspberry Pi 4
through to a working HID keyboard, then onward to mass-storage. Source of
truth for current state: `tracking/current-step.md` (lines 287-446) and the
xhci/pcie/usb working trees identified below.

## 1. State today (2026-05-06 baseline)

**Working** (verified on real Pi 4, image `acf01e80`, log
`artifacts/rpi4b-uart/rpi4b-uart-20260506-185130-netboot-stage4-phase2-cmd-pre-mailbox.log`):

- VL805 BAR0 program in
  `sources/phoenix-rtos-devices/pcie/server/pcie.c::scanFunc`
  (BAR0 read-back `0xf8000004` confirmed).
- `BME | MSE` always-set + reorder: cmd-register write happens
  *before* the `bcm2711NotifyXhciReset` mailbox notify and BAR
  programming (the previous write-after-reset order had the controller
  drop subsequent MMIO writes silently).
- `xhci_init`'s capProbe retry loop in
  `sources/phoenix-rtos-devices/usb/xhci/xhci.c:1873-1892` waits up to
  ~5 s for the pcie daemon to finish BAR programming.
- `xhci_allocCommandSpace` alignment fix (CMD_RING 4 KB; rationale
  comment lines 110-120 in `xhci.c`) — `va2pa` no longer disagrees
  with the controller's 64-byte / 64-K-no-cross requirement.
- `xhci_validateRuntime` passes:
  `caplen=20 ver=0100 p1=05000420 p2=fc000031 cc1=002841eb dboff=00000100
   rtsoff=00000200 pagesize=00000001 ctxSz=32 ac64=1` — 5 ports, 32 slots,
  4 interrupters, 32-byte contexts, AC64.
- EL2→EL1 drop landed on the kernel side; Phoenix reaches `(psh)%`
  on real Pi 4 with USB host-driver constructor logging
  `usb: Initializing driver as host-side: usbkbd` to klog. Banner
  + first two klog lines now also render on HDMI (Stage 4 phase 1h
  validated, screenshot `artifacts/hdmi/2026-05-06-stage4-phase1h-klog-on-hdmi.png`).

**Diagnostic instrumentation in tree, to be removed at phase 5**
(see `Section 5`):

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`:
  per-step `debug()` markers in `xhci_init` (lines 1858-1948),
  `validateRuntime` field-failure prints (lines 721-775),
  `allocCommandSpace` failure-branch debug + va/phys dump (791, 798,
  817, 822), `programCommandSpace` mismatch debug + write-readback
  dump (874, 882, 914, 919).
- `sources/phoenix-rtos-devices/pcie/server/pcie.c`:
  per-step `extern void debug(const char *)` blocks in
  `bcm2711NotifyXhciReset` (lines 227-228), `pcie_cfgInitBcm2711`
  (273-276, 470-473), `scanFunc` BAR-program block (685-688, 752-756,
  770-777), `pcie_scanBus` enumeration trace (803-808, 830-834), and
  `main` startup trail (916-961).
- `sources/phoenix-rtos-usb/usb/usb.c`: milestones (lines 429, 432,
  435, 438, 441).
- `sources/phoenix-rtos-usb/usb/hcd.c`: per-stage milestones
  (lines 198, 201, 206, 208, 211, 216).
- `sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c`: insertion +
  /dev/kbd0 markers (lines 677, 743).

**Latest cycle log path:** `artifacts/rpi4b-uart/rpi4b-uart-*-stage4-phase2-*.log`.

## 2. Remaining `xhci_init` steps to validate

Each step below is already wired into the cascade in `xhci.c` lines
1894-1948. Only `validateRuntime` and `allocCommandSpace` have been
confirmed passing on real silicon; the rest are merely *reached* in
code but their behavior on VL805 is unknown.

| Step | Function | What it does | Predicted UART signature on success | Risk |
|---|---|---|---|---|
| 2.1 | `xhci_initCommandRing` (831) | CPU-only: sets `cmdRingCount`, writes the LINK TRB at the last slot, validates back. | `xhci: pre/post initCommandRing` adjacent in log; no `fprintf`. | Low. Only fails if `XHCI_CMD_RING_SIZE / TRB_SIZE <= 1` (cannot, sizes are constants). |
| 2.2 | `xhci_programCommandSpace` (863) | Writes DCBAAP (lo/hi), CRCR (lo/hi with RCS=1), CONFIG.MaxSlotsEn; reads back; compares. | `xhci: pcs writeReadback dcbaa=… want=… crcr=… want=… cfg=…` matches; `pre/post programCommandSpace`. | Medium. The previous BME-was-clear bug surfaced here as `0xdeaddead` write-readback; with cmd-write-before-mailbox now landed, mismatch should not return. If it does, suspect (a) DCBAAP physical address ≥ 4 GiB while AC64 path mishandled, or (b) CRCR’s `CR_PTR_LO__MASK` clipping a legitimate ring-base bit. |
| 2.3 | `xhci_runStateSelftest` (928) | Writes `USBCMD.RS=1`, polls `USBSTS.HCH→0` (≤20 ms), then `RS=0` and waits for halt. | `pre/post runStateSelftest` straight through; no `run/halt transition timeout`. | Medium. The first time the controller is actually run. Failures here usually point at PAGESIZE / context-size / AC64 mismatch, or scratchpad buffers being required (HCSPARAMS2.MaxScratchpadBufs is non-zero on VL805 — check the cap dump; if non-zero, scratchpad alloc must run before RS, see Section 11). |
| 2.4 | `xhci_allocEventRing` (1035) | Allocates eventRing (4 KB, page-aligned) + ERST (64 B). CPU-only. | Currently silent (no debug markers — phase 1 will add them temporarily); next iter shows `pre/post allocEventRing`. | Low. Same allocator quirk as cmd ring; already mitigated. |
| 2.5 | `xhci_programEventRing` (1809) | Writes IR0 ERSTSZ/ERSTBA/ERDP, reads back. | `pre/post programEventRing`. | Low-medium. Same family as step 2.2; `ERDP_LO_EHB` must read back as 0. |
| 2.6 | `xhci_cmdNoopSelftest` (1117) → `xhci_cmdExec` (1123) | Drops a NO_OP TRB on the cmd ring, ENTERS run state, rings doorbell `xhci_dbWrite32(xhci, 0u, 0u)`, **polls** event ring for completion (1 ms × 100 ms timeout), then halts. *Polled, not interrupt-driven.* | `pre cmdNoopSelftest` → ~tens of ms of quiet → `post cmdNoopSelftest`. | High. This is the first end-to-end exercise of cmd-ring + event-ring + doorbell + run-state. Failure likely if (a) interrupter 0 IMAN.IE is not required for polling but ERDP-handshake misordered, (b) MSI/MSI-X capability needs explicit disable on VL805 because Phoenix never enables it, (c) polling on an event TRB whose write was never posted to coherent memory (caches-off so should be fine). |
| 2.7 | `xhci_cmdEnableSlot` (1197) | Issues ENABLE_SLOT, expects slotId 1..nslots back. | `pre cmdEnableSlot` → `post cmdEnableSlot` and `xhci->slotId` populated; visible later via dcbaa write. | Medium. Same machinery as 2.6; once 2.6 works this should too. |
| 2.8 | `xhci_allocSlotSpace` (1261) | CPU-only allocation of devCtx + inputCtx + ep0Ring; writes `dcbaa[slotId]=devCtxPhys`. | Silent today; add `pre/post` markers. | Low. |

Tactic for each iteration: enable per-step debug markers (already in
place for 2.1-2.3 and 2.7; add equivalent prints around 2.4, 2.5, 2.6,
2.8 for the next image), then narrow on whichever step prints "pre"
without a matching "post".

## 3. `usb_devEnumerate` — predicted xHCI-roothub failure modes

Once `xhci_init` returns 0, `hcd_init` calls `usb_devEnumerate` for the
roothub at `sources/phoenix-rtos-usb/usb/hcd.c:209` (already
instrumented). `usb_devEnumerate` lives at
`sources/phoenix-rtos-usb/usb/dev.c:624` and immediately scans
roothub ports through hub-class control transfers
(`sources/phoenix-rtos-usb/usb/hub.c:291`).

xHCI-specific risk surface:

- **Roothub descriptor synthesis.** The xHCI driver acts as a virtual
  hub. `xhci_getDesc` (xhci.c:2013) and `xhci_getPortStatus`
  (xhci.c:2057) must return shapes the generic `usb_devEnumerate` /
  `hub.c` parser accepts. A subtle bug class is mismatched
  `wPortStatus` bit semantics between USB 2.0 hub spec (what hub.c
  expects) and PORTSC bits (what xhci provides) — confirm
  `xhci_getPortStatus` covers PED, OCC, PRC, CCS conversion exactly.
- **Speed encoding.** `xhci_usbSpeedToPsi`/`xhci_ep0MaxPacket`
  currently only return useful values for full/low/high speed; the
  default/super-speed branch returns 0. A USB 3.0 keyboard plugged
  into a USB-3 port will report `xhci->portsc` PORT_SPEED 4 (super)
  and the ep0 maxpacket fallback of 0 will fail later. Document and
  punt to follow-on (Section 8).
- **Port-power assertion.** PORTSC.PP must be 1 before CCS becomes
  meaningful. `xhci_init` doesn't set PP today; `hub_init` /
  `usb_devEnumerate` may need a SET_FEATURE(PORT_POWER) per port,
  which the roothub driver should translate into `PORTSC.PP=1` plus
  the 20 ms `XHCI_PORT_POWER_GOOD_DELAY_US` wait.
- **Reset semantics.** `PORTSC.PR=1` followed by waiting for PRC=1
  (cleared as RW1C). The 100 ms `XHCI_PORT_RESET_TIMEOUT_MS`
  matches the spec; ensure the wait uses `xhci_portWaitBits`
  (xhci.c:464) and not a generic timer.

Phase-1 expectation: `usb-hcd: post usb_devEnumerate ok` lands in the
log without device entries (no keyboard yet). The keyboard insertion
fires asynchronously through `xhci_roothubStatusThread` (xhci.c:523),
which then re-runs the enumeration path in `usb_devEnumerate` for the
new device.

## 4. HID enumeration → keystrokes path

Trace, end-to-end, after a keyboard is plugged into a USB port:

1. **xHCI port-status interrupt or polling.** `xhci_roothubStatusThread`
   (xhci.c:523) detects PORTSC.CSC=1 → constructs a hub-status change
   notification and posts it into the usb daemon’s hub event queue.
2. **Hub event dispatch.** `usb` daemon's hub thread
   (`sources/phoenix-rtos-usb/usb/hub.c`) consumes the change, runs port
   reset via the xhci roothub ops, then calls
   `usb_devEnumerate` (dev.c:624) for the new device.
3. **Device enumeration.** GET_DESCRIPTOR(DEVICE) → ADDRESS_DEVICE
   (translates in xhci to the cmd-ring `ADDRESS_DEVICE` TRB —
   `xhci_cmdAddressDevice`, xhci.c:1220, with `setAddress=0` first
   then 1, and the input context shaped by
   `xhci_prepareAddressContext` xhci.c:1408) → GET_CONFIGURATION_DESCRIPTOR.
4. **Driver registration.** `usb` daemon walks each interface, matches
   `bInterfaceClass=0x03 / SubClass=0x01 / Protocol=0x01` against the
   registered driver list (usbkbd registered itself in its C
   constructor at startup, klog line `usb: Initializing driver as
   host-side: usbkbd`).
5. **`usbkbd_handleInsertion` fires** (usbkbd.c:671). Steps inside:
   open ctrl pipe; `usb_setConfiguration(drv, pipe, 1)`;
   open interrupt-IN pipe (xhci.c:1458 `xhci_initInterruptInPipe` is
   what backs this open); `usbkbd_setProtocol` →
   SET_PROTOCOL(boot, =0); `usbkbd_setIdle`; `create_dev(&oid,
   "/dev/kbd0")`. Marker `usbkbd: New /dev/kbd0 device created`.
6. **kbdthr opens it.** `pl011-tty` already opens `/dev/kbd0` and
   reads boot-protocol 8-byte HID reports
   (`pl011-tty.c:888-939` — confirmed in earlier session work). On
   each read, the report bytes are translated to ASCII and pushed
   into libtty.
7. **libtty → pl011_thr → UART + fbcon.** Phase 1h made
   `pl011_thr`'s drain non-sleeping when work is queued, so chars
   appear on UART promptly and (via the fbaddr mirror at
   `pl011-tty.c:870`) on HDMI.

End-to-end expected: type `a`; UART log shows `a`; HDMI displays `a`.

## 5. Diagnostic-cleanup plan (phase 5)

Per AGENTS.md "remove diagnostic-only code whose hypothesis was
confirmed". Per file:

- **`sources/phoenix-rtos-devices/usb/xhci/xhci.c`**
  - Remove the `debug()` cascade in `xhci_init` (lines 1858, 1861,
    1864, 1873, 1876, 1879, 1882, 1885, 1892, and every paired
    `pre/post` from 1895 through 1937) — keep the structural
    if-cascade itself.
  - Remove field-level `debug()` on validateRuntime failure paths
    (lines 721-775) — the corresponding `fprintf(stderr,…)` prints
    already exist and are sufficient for production.
  - Remove allocCommandSpace va/phys dump (lines 809-818) and the
    debug() prints at 791, 798, 822 (keep the fprintf companions).
  - Remove programCommandSpace write-readback dump (lines 905-915)
    and the debug() at 874, 882, 919.
- **`sources/phoenix-rtos-devices/pcie/server/pcie.c`**
  - Remove every `extern void debug(const char *s);` block + its
    `debug(m)` / `debug("...")` calls at lines 227-228, 273-276,
    470-473, 685-688, 752-756, 770-777, 803-808, 830-834, 916,
    945, 947, 953, 957, 959, 961.
  - Keep the BAR-programming and BME-set logic (those are the fix,
    not the diagnostic).
- **`sources/phoenix-rtos-usb/usb/usb.c`**: remove the four `debug()`
  calls at lines 429, 432, 435, 438, 441.
- **`sources/phoenix-rtos-usb/usb/hcd.c`**: remove the six `debug()`
  calls at lines 198, 201, 206, 208, 211, 216.
- **`sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c`**: remove the
  two `debug()` calls at 677 and 743 (the `fprintf(stdout, "usbkbd:
  New device: %s\n", …)` at line 742 is permanent user-facing output,
  keep it).

After cleanup: rebuild, real-Pi cycle, confirm keystrokes still flow,
snapshot a manifest at `manifests/2026-05-XX-stage4-phase2-cleanup.md`,
commit upstream-ready prefixes per `docs/code-quality-and-upstreaming.md`.

## 6. Phased delivery

- **Phase 1 — xhci_init completes.** Sequentially confirm steps 2.1
  through 2.8 from Section 2 by adding/keeping debug markers around
  any silent step. Goal: `usb-daemon: post-hcd-init` reaches UART.
  ETA: 1-2 cycles per step; 8-10 cycles total worst case.
- **Phase 2 — `usbkbd_handleInsertion` fires.** Plug a known-working
  USB keyboard into a Pi 4 USB-2 port, expect `usbkbd:
  handleInsertion fired` on UART. If the marker doesn't fire,
  failure is in `usb_devEnumerate` / hub.c interface-walk; narrow with
  the per-class print already in dev.c:428.
- **Phase 3 — keystroke on UART.** Type `a`; expect `a` echoed on
  UART through libtty. Failures here: HID interrupt-IN read returns
  short or zero (xhci_submitInterruptIn xhci.c:1550), or HID-to-ASCII
  table missing the key.
- **Phase 4 — keystroke on HDMI.** Same as phase 3 but via fbcon
  mirror. Phase 1h already proved the path; this is integration.
- **Phase 5 — cleanup + commit + snapshot.** Per Section 5.
- **Phase 6 (stretch) — USB mass-storage class driver.** New
  `sources/phoenix-rtos-devices/usb/usb-storage/` driver implementing
  USB BBB (Bulk-Only Transport) + a SCSI Read10/Write10/Inquiry/Test-
  Unit-Ready stack, exposed as `/dev/sdaN`. Depends on Phoenix's
  filesystem layer for any user-facing mount.

## 7. Test strategy

- **Manual today.** Plug a known-good USB-2 keyboard into one of the
  Pi 4's USB-2 ports (the two black ports). Avoid the blue USB-3
  ports until super-speed is supported. Run a netboot cycle, watch
  UART, type after `(psh)%` appears.
- **Future automation.** Two viable paths: (a) a USB-redirector
  device on the Pi 4 USB ports controlled from the host running the
  build loop — replays canned HID reports; (b) a small
  `usb-gadget` host emulator (Phoenix dev box pretending to be a
  keyboard via OTG) for in-loop input. Both are out-of-scope for the
  initial bring-up but should be tracked once Phase 5 closes.

## 8. Follow-on USB work

- **Mass-storage class driver** (Phase 6 above). Estimated
  2-3 dev-weeks: BBB framing + 10 SCSI commands + bdev shim.
- **USB-OTG / dual-role.** Pi 4’s USB-C port routes to the BCM2711
  built-in USB-OTG, not the VL805. Needs a separate `dwc_otg` host /
  device driver — entirely independent of the xhci work.
- **Hub support.** Pi 4 has 4 user-facing USB-A ports fronted by an
  internal hub. Phoenix's `phoenix-rtos-usb/usb/hub.c` already
  handles 2.0 hubs as a class. Validate that downstream-hub
  enumeration works once Phase 4 lands (plug a powered hub into the
  Pi, then a keyboard into the hub). Likely just exercises existing
  paths; no new code unless a quirk surfaces.

## 9. Inter-dependencies

- **Depends on** the pcie daemon being functional and BME/BAR
  programming for VL805 (landed; see Section 1).
- **Depends on** EL2→EL1 drop landed (Phoenix reaches `(psh)%`).
- **Independent of** Stage 1 cache-enable: USB at caches-off speed
  is functional, just slow. Allocator latency dominates; once caches
  land, every step in Section 2 measurably tightens.
- **Depends on** for Phase 6 mass-storage: Phoenix's filesystem
  layer (vfs + a FAT/ext driver) for anything beyond raw `/dev/sda`.

## 10. Effort estimate

| Phase | Effort | Calendar |
|---|---|---|
| 1 (xhci_init complete) | 1-3 days of /loop iterations | hours-to-days, hardware-iteration-bound |
| 2 (insertion fires) | 0.5 day | one cycle once Phase 1 lands |
| 3 (keystroke on UART) | 0.5-1 day | likely one cycle |
| 4 (keystroke on HDMI) | minutes | integration only |
| 5 (cleanup + snapshot) | 1 day | manual review-grade pass |
| 6 (mass-storage) | 2-3 dev-weeks | sequential after Phase 5 |

## 11. Open questions

- **Scratchpad buffers.** HCSPARAMS2 lower bits encode
  `MaxScratchpadBufs`. The cap dump
  (`p2=fc000031`) decodes to `MaxScratchpadBufs={hi:0xfc>>5=7,
  lo:0x31&0xf=...}` — confirm the exact decoding and, if
  `MaxScratchpadBufs > 0`, allocate the scratchpad buffer array
  before issuing RS in `xhci_runStateSelftest`. The driver currently
  doesn't.
- **Multi-port hub support.** The Pi 4's internal hub is upstream of
  every user-facing USB-A port. Confirm whether the firmware leaves
  the internal hub powered-up and addressable, or whether xhci must
  explicitly issue SET_HUB_DEPTH / hub-class control transfers.
- **OTG port.** Out of scope for VL805 work but worth a tracking
  entry: do we want OTG-as-host (so Pi 4 can host a keyboard via the
  USB-C port) or OTG-as-device (so Pi 4 can be addressed by another
  host) or dual-role?
- **USB 3.0 super-speed.** VL805 supports SS but Phoenix has neither
  the SS slot-context shape nor the SS port-reset state machine.
  Estimated 3-5 dev-days additional once HS path is solid.
- **Interrupt vs polling.** `xhci_cmdExec` polls; OK for cmd-ring
  but noisy for periodic interrupt-IN. Consider wiring the GIC
  interrupter to the xhci event ring once polling proves robust.
- **DMA coherency.** With caches off there's no coherency question.
  When Stage 1 cache-enable lands, every `va2pa` mapping used for
  DMA (DCBAA, cmd ring, event ring, ERST, devCtx, inputCtx, ep0Ring,
  transfer rings, HID report buffers) must either be Normal NC
  mapped or explicitly cleaned/invalidated around each transfer.
  Track separately under TD-DMA or similar.

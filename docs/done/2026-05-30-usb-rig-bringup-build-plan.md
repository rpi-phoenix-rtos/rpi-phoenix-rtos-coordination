# USB: build the real stack on the rig's PROVEN bring-up — staged plan

**2026-05-30.** User-chosen direction ("Build USB on the rig's path") after the
USB failure was characterized as an elusive, fully-eliminated divergence
between `xhci_init` (fails 0/N) and the diag rig (`diag_format_xhci_bringup`,
enumerates a hub end-to-end from the SAME worker context). Plan produced by a
Plan subagent that read all three layers. See
`docs/done/2026-05-30-usb-register-layer-exhausted.md` for why we got here.

## Reframing findings (correct earlier assumptions)

1. **Transcription is useless — call the rig's REAL code.** `xhci_init` already
   source-matches the rig: `xhci_programCommandSpace` order (xhci.c:1150-1164),
   `enterRunState` dsb+10ms settle + single `R/S|INTE|HSEE` (xhci.c:1291-1317),
   `cmdExec` CRCR re-publish before doorbell (xhci.c:1526-1545). It still fails.
   So what's proven is the rig's *execution*, not its text → make `xhci_init`
   CALL the rig's actual function.
2. **The first No-Op is a fair fight; only the transfer path diverges
   structurally.** Framework's per-command R/S toggle (`enterHaltedState` at
   xhci.c:645/1686/2203/2220/2304/2320) only bites command #2+, not the first
   No-Op. It's a transfer-stage risk note, not the bring-up bug.
3. **Framework event-ring consumption is fragile (the expected NEXT bug).**
   `xhci_cmdExec` walks the ring (1565-1587, OK) but the transfer paths
   `xhci_ep0ControlRead` (2184/2194), `xhci_submitInterruptIn` (2113), and the
   roothub thread (619) inspect ONLY `event[0]` — they will miss a completion
   sitting behind a Port-Status-Change event (the rig scans 256, diag-udp.c:
   1782). Fix when reached.
4. **Linkage:** `diag_format_xhci_bringup` is currently `static` (diag-udp.c:
   1478) — NOT exported (2b55c99 reverted/absent). diag-udp.c → lwip-port
   binary; xhci.c → `libusbxhci` → same final binary. Cross-module call needs a
   non-static export + shared header (`port/usb-embed/xhci-rig-handoff.h`) +
   include path in xhci/Makefile.
5. **Rig handoff blockers:** it `munmap`s all rings (diag-udp.c:2169-2179) and
   claims slot 1 via its own EnableSlot/AddrDev/GetDesc (1890-2165). The
   handoff variant must STOP after No-Op verification, keep allocations alive.

## Recommendation: hybrid → (A), gated stage-by-stage on hardware

Target architecture (A): the real phoenix-rtos-usb HCD framework
(`usb_devEnumerate`, hub.c/dev.c, usbkbd) on top of the rig's proven controller
bring-up. (B) hand-coding the whole stack in lwip-port is the per-layer fallback
only for whatever layer actually fails. De-risk by calling the rig FIRST, then
re-home into xhci.c for upstreamability AFTER it's proven.

## Stages (each independently testable; small commits)

- **Stage 1 (DECISIVE):** Factor the rig body (diag-udp.c:1478-1792, through the
  No-Op 256-TRB land check) into an exported `diag_xhci_rigBringupHandoff(
  xhci_rig_handoff_t *out)` that STOPS after No-Op (no EnableSlot/AddrDev, no
  munmap) and returns live MMIO/ring VAs+PAs + cmd/event ring positions +
  cycle states. **Retry loop lives here** (N≈8 full re-alloc+re-HCRST attempts;
  converts the rig's ~50%/attempt into a stable handoff). Behind env gate
  `XHCI_USE_RIG_BRINGUP`, `xhci_init` (xhci.c:2456-2519) skips its own
  map/reset/alloc/program/No-Op and instead calls the handoff, populates
  `xhci_t`, then resumes the framework at `xhci_cmdEnableSlot` (2510).
  Cmd-ring handoff: simplest = rig hands over a fresh clean cmd ring (CRCR
  re-published while CRR=0) so the framework's `cmdExec` (always TRB[0]) is
  consistent. **Go/no-go:** framework's EnableSlot completes (cc=1, slot>0) =>
  framework command/event path works on a rig-brought-up controller => (A)
  viable. Timeout => isolate to framework event consumption (apply Finding-3
  fix), re-test; else fall back to (B) for the transfer layer.
- **Stage 2:** let `usb_devEnumerate` drive EP0 control IN GET_DESCRIPTOR.
  Pre-emptively apply the Finding-3 event-scan fix to `xhci_ep0ControlRead`
  (2184-2199) + `xhci_ep0ControlWriteNoData` + roothub thread (619). Go =
  framework reads the 18-byte device descriptor (matches the rig's known-good
  VID=2109 PID=3431).
- **Stage 3:** hub enumeration + downstream port → reach the keyboard
  (SET_CONFIG/hub-class via `xhci_ep0ControlWriteNoData`, per-port reset +
  AddressDevice). Go = HID interface (class 3) discovered, usbkbd binds.
- **Stage 4 (GOAL):** HID boot protocol + interrupt-IN key reports
  (`xhci_initInterruptInPipe`/`submitInterruptIn`). Apply the event-scan fix;
  if periodic transfers stall, stop the per-transfer R/S toggle (leave
  controller RUNNING like the rig). Go = keypresses print decoded scancodes
  on UART.

## Re-home (post-Stage-2, upstreamability)

After (A) is proven, transcribe the rig sequence into an in-tree
`xhci_bringupLikeRig()` in xhci.c and drop the cross-module glue. If the
transcription then fails where the rig-CALL succeeded, that PROVES the
divergence is environmental (process/allocation/timing), not source — decisive
either way, and the rig-call glue stays as the shipping path.

## Test loop
`./scripts/rebuild-rpi4b-fast.sh` → `./scripts/test-cycle-netboot.sh
--capture-secs 60 --label <stage>` (generous capture + Bash timeout; harness
is functional — the paired capture booted + answered UDP 'X' at +84s).

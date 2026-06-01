# Pi 4 USB re-architecture plan (#129)

User decision 2026-06-01: move USB off the fragile lwip-embedded "rig" PoC into a
proper standalone xhci driver doing a NORMAL `xhci_init`. Plan from a scoping
subagent; this is the durable record.

## Key insight (the crux)
The framework USB stack (`phoenix-rtos-devices/usb/xhci`) is already complete and
the keyboard works end-to-end (#122) — **but only on a controller brought to
RUNNING by the lwip rig** (`diag_xhci_rigBringupHandoff`, adopted via
`XHCI_USE_RIG_BRINGUP`). The rig reaches RUNNING only intermittently (~12% e2e).

The framework's OWN bring-up was declared a "JTAG-gated inbound-DMA wall"
(`first event @idx -1`: controller RUNS but the first command/event DMA never
lands in DRAM) in `docs/notes/2026-05-30-usb-register-layer-exhausted.md`. BUT:
- that verdict predates the Stage event/command-ring rewrites (Bugs #1/#3/#4,
  commits ~834defb→8e9e563, 2026-05-30/31), and
- it was ALWAYS measured under `USB_HCD_PCIE_DRIVE_ONLY=1`, where **FIX-19**
  (`xhci.c` `xhci_enterRunState` RC_BAR2 re-settle right before R/S — the
  candidate fix for "runs but posts zero events") is **silently inert**, because
  FIX-19 depends on `bcm2711_pcie_lastCtx` which is only set on the
  NON-DRIVE_ONLY branch (`bcm2711-pcie.c:1495`, past the DRIVE_ONLY early return
  at `:1416`).

So the framework's own bring-up has **likely never been tested in the merged,
non-DRIVE_ONLY, non-rig config with FIX-19 live + the post-Stage engine.** That
config is the re-architecture target. Testing it is near-zero-diff.

## Migration steps (one variable each; verify with diag-udp 'k' / diag-kbd-probe.sh; netboot, SD out; multi-trial)
- **Step 0** — snapshot baseline; note the 'k' counter lives in the lwip binary,
  so plan a standalone verification story (psh reads /dev/kbd0, /dev/usb, xhci
  `@idx`/`rc` debug markers, HDMI) before relocating out of lwip.
- **Step 1 (DECISIVE, near-zero-diff, IN PROGRESS)** — run the EXISTING embedded
  USB in the merged config: disable `USB_HCD_PCIE_DRIVE_ONLY` + `XHCI_USE_RIG_BRINGUP`
  in `port/main.c` (done) and remove the `usb --bridge-only` boot daemon
  (`user.plo.yaml`, done) so lwip's USB does the full in-process bring-up with
  FIX-19 live. **Q: does `first event @idx` land (>=0) / does the kbd enumerate
  (insertions>0)?** Outcome A (lands) → wall obsolete, migration = plumbing →
  Step 3. Outcome B (still -1) → Step 2.
- **Step 2 (only if B)** — transcribe the rig's exact bring-up sequence
  (`diag-udp.c:1631-1707`) into an in-tree `xhci_bringupLikeRig()` in `xhci.c`,
  replacing reset+alloc+program+selftest. Diff candidates: MaxSlotsEn=8 during
  bring-up; ERSTSZ→ERDP→ERSTBA order; fused `R/S|INTE|HSEE` write; no halt/re-R/S
  before first No-Op.
- **Step 3** — relocate to the standalone `usb` daemon: `user.plo.yaml` plain
  `usb` line (no `--bridge-only`); drop the `LWIP_EMBED_USB` block in
  `port/Makefile:46-51` + the embed thread in `port/main.c:261-270`; delete
  `port/usb-embed/` + `xhci-rig-handoff.h`. Merging dissolves both split
  rationales (CRCR-stale + bridge-leak parking) since one long-lived daemon owns
  everything.
- **Step 4** — delete the rig branch (`xhci.c:2864-2962`), `--bridge-only`
  parking, the rig in `diag-udp.c` + its 'X' command, resolved TD markers, the
  'k' diag counters. Multi-trial pass-rate bench; verify SD-boot too.

## Step 1 RESULT (2026-06-01) — OUTCOME A: the wall is gone
Built a network-readable bring-up reporter instead of fighting the back-pressured
UART (advisor call): non-static diag globals in `xhci.c` (`xhci_diagEventsSeen`,
`xhci_diagFix19Rc`, `xhci_diagUsbsts`, `xhci_diagBringupRc`) externed by
`diag-udp.c`'s new **'U'** command; `scripts/diag-usbhcd-probe.sh` boots + reads
it. **Trial 1 (merged config, no rig, no DRIVE_ONLY):**
```
usbhcd: eventsSeen=9 fix19Rc=0 usbsts=0x00000010 bringupRc=0
```
- `eventsSeen=9` ⇒ inbound DMA writes land — the `@idx -1` wall is GONE.
- `fix19Rc=0` ⇒ FIX-19 armed (premise confirmed: it was inert under DRIVE_ONLY).
- `bringupRc=0` ⇒ `xhci_init` succeeded (controller RUNNING, first EnableSlot OK).
- `usbsts=0x10` ⇒ PCD set, HCH=0, HSE=0 (healthy running controller).
- BUT `insertions=0` ⇒ keyboard not yet enumerated — the remaining work is the
  downstream hub/multi-slot **enumeration** (#116-class), NOT the bring-up wall.

The "JTAG-gated inbound-DMA wall" verdict (`2026-05-30-usb-register-layer-exhausted.md`)
was an artifact of the untested config (DRIVE_ONLY ⇒ FIX-19 inert + pre-Stage
engine). **Next: confirm reproducibility (multi-trial, n≥4 — the rig was ~50%
flaky, is the clean path stable?), then debug enumeration, then Step 3 (relocate
out of lwip). The rig is now obsolete for bring-up.**

## Step 2 RESULT (2026-06-01) — FULL ENUMERATION, then a memory-corruption wall
Two fixes (committed) took the framework path from "controller up, 0 devices"
to the **entire tree enumerated**:
1. `keepRunning=1` for the Pi4 non-rig path (xhci.c, before cmdNoopSelftest):
   halt-per-command left the VL805 halted between commands → next transaction
   fails (Context State Error) + no Port-Status-Change events → enumeration never
   started. eventsSeen plateaued at exactly 9.
2. `xhci_enterRunState` made idempotent via a `running` flag: cmdExec called
   enterRunState every command; under keepRunning that re-ran FIX-19's RC_BAR2
   re-settle per command, churning inbound DMA → "transfer completion timeout" at
   the root-hub interrupt-IN. Skipping it when already running let it through.

Result: root hub → **VIA hub (2109:3431)** → **mouse /dev/mouse0** + **keyboard
046d:c31c /dev/kbd0**, pl011-tty opened the kbd bridge. No fault.

THEN: a flood of `mbox CORRUPT in tryfetch` — the lwIP TCP/IP **mailbox**
(port/mbox.c, SAME process as the embedded USB stack) is structurally clobbered
(`ring`/`sz`/`tail` overwritten with pointer-shaped garbage e.g. 0x435E88)
**immediately when /dev/kbd0 is opened and the first kbd interrupt-IN URB is
submitted** (slot=3 ep=3). The mouse pipe was set up too but nothing opened
/dev/mouse0 → no URB → no corruption. This is the long-standing #121 corruption,
now ~deterministic (when enum reaches the kbd) instead of intermittent. It kills
lwIP networking → Pi unreachable.

### Corruption hunt state (advisor-guided)
- Buffer-alloc hypothesis DISCONFIRMED: the async URB path (`usblibdrv_handleUrb`
  → `usb_transferAlloc`) DMAs into a `usb_alloc` pool buffer (`t->buffer`), not
  the plain-heap `dev->report[i]` (that's only the completion copy dest). Ring +
  buffer + dcbaa + evtRing all use `va2pa` on page-aligned `usb_alloc`/
  `usb_allocAligned` (MAP_CONTIGUOUS|MAP_UNCACHED) memory.
- Event-ring sizing is CONSISTENT (256 TRBs, ERST ringSegmentSize=256, ERSTSZ=1)
  — no static overrun.
- Aliasing (#26: USB pool physically overlaps lwIP heap) is the LEADING-to-be-
  ruled-out theory: advisor's first-principles — mmap(ANON)+calloc in one process
  can't share a physical page unless the kernel allocator is broken (and then
  everything would corrupt). Cross-boot PAs were merely "nearby" (null hypothesis).
- **Next (outcome-2): a TRB/ring programmed with a WRONG PA that lands on the mbox
  page**, OR a CPU write. The kbd is LOW-SPEED behind a TT (unlike the working
  hub) — its endpoint-context / transfer-ring / TT programming is the differ.

### OBSERVABILITY LESSON (cost me ~6 boots — do not repeat)
The UART is back-pressured; **multiple writers (printf=stdout, debug()=syscall,
fprintf=stderr) interleave mid-line and garble exactly the multi-value lines you
need.** `mbox NEW` survived only because it was single-writer printf at a quiet
moment. Rules: ONE writer per diagnostic line, single atomic line, leading `\n`;
and the corruption kills the network so diag-udp can't read post-corruption.
**Robust path for the next attempt:** either (a) network-readout on a SHALLOW
boot (no kbd → no corruption → network alive) via a diag-udp command that reports
USB-pool ranges + mbox PAs + a containment verdict computed in-code; or
(b) validate-DMA-PA-at-source: bound-check each PA against known USB pool ranges
*before* programming the TRB, and on a bad PA log it + skip the doorbell so the
write never happens and the network stays up to read the diagnostic.

Diagnostic instrumentation committed (marked DIAGNOSTIC #129, revert before
close): xhci DMAMAP dump, mbox PA prints + flood rate-limit, usb/mem USBPOOL log.

## Risks
- If Step 1 = B AND Step 2 transcription also fails → the bug is below the
  bring-up sequence (the live SError `esr=0xbf000002` / bridge-NACK lead,
  `2026-05-29-usb-reanalysis.md:146-202`), which is JTAG-gated. **Keep the rig
  path as the working fallback until the clean path is proven — do not delete
  it (Step 4) until Step 1/2 succeed.**
- Per-boot non-determinism is real → always multi-trial.
- Verification relocation (Step 0) is a real gap once USB leaves lwip.

## Key refs
`port/main.c:98,104,261-270`; `port/Makefile:46-51`; `usb/usb.c:505-554,430-497`;
`xhci.c:2804-3041` (init, rig + normal branches), `:1331-1380` (FIX-19),
`:1699-1802` (cmdExec), `:1619-1690` (eventAwait); `bcm2711-pcie.c:1416,1495,1529,1557`;
`diag-udp.c:1513,1631-1707,2418`; `user.plo.yaml:33`.
Docs: `2026-05-30-usb-rig-bringup-build-plan.md`, `-usb-register-layer-exhausted.md`,
`-linux-reference-inbound-dma-finding.md`, `2026-05-29-usb-reanalysis.md`.

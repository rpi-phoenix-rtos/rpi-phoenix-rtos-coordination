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

### CONTAINMENT TEST RESULT (2026-06-01) — NEGATIVE → not aliasing
Clean same-boot capture (both mbox NEW + USBPOOL via atomic debug()): deep-enum
boot fired the corruption, victim **MBOXPA pa=0x3353a50** (h=0 sz=0 — fields
ZEROED). USB pool = 48 chunks spanning **0x3294000..0x3311000 then 0x33d3000**.
**0x3353a50 is in the GAP — NOT inside any pool chunk.** So #26 aliasing is
RULED OUT (matches advisor's first-principles: mmap+calloc can't share a phys
page). Further narrowing:
- The kbd interrupt **data buffer** is `usb_alloc` (pool) — confirmed for the hub
  (buf=0x33d3260, in-pool). So the kbd 8-byte HID DMA targets the pool, NOT the
  victim → the corruption is **not** the kbd data-buffer DMA.
- `h=0 sz=0` (zeroed) is *consistent with* an 8-byte idle-HID write but the kbd
  data buffer is ruled out, so the zeroing source is elsewhere.
- Remaining suspects (kbd is LS-behind-TT, unlike the working hub): (1) CPU write
  / use-after-free of the mbox memory; (2) the kbd slot's **device/output
  context** PA in the DCBAA programmed wrong → controller writes context to the
  mbox; (3) event-ring or a transfer TRB with a stray PA. Need a DEEP-enum boot's
  kbd-slot DMAMAP (buf/ring/dcbaa) — but deep enum is currently <50% (the hub's
  downstream-connect reporting is itself flaky), which is the observability
  bottleneck. Per-submit DMAMAP is committed to catch the kbd slot when a deep
  boot lands. ALSO instrument the kbd slot's device-context PA (allocSlotSpace)
  and compare to 0x335xxxx.

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

## DIRECTION (2026-06-01, after ~6 corruption-hunt boots) — go to Step 3
Deep-enum boots are now <50% and 3 in a row came back shallow (hub only), so the
in-place kbd-PA capture is observability-starved. Combined with: corruption
ZEROES the mbox (h=0 sz=0), the kbd DATA buffer is ruled out (it's in-pool), and
aliasing is ruled out (containment negative) — the leading hypothesis is the
ORIGINAL #121 one stated in mbox.c: **a libc-heap overflow / use-after-free on
the USB side clobbers the adjacent lwIP mbox, because the embedded USB stack
SHARES the lwIP process heap** (LWIP_EMBED_USB). Two consequences:
1. This is precisely what **Step 3 (USB as a standalone daemon — the user's chosen
   #129 endgame)** isolates: a USB-side overflow would then hit USB's OWN heap
   (the usb_mem free-list detector at mem.c:171/182/245 catches it), lwIP/network
   survives → diag-udp observability is RESTORED, and the bug becomes debuggable
   in isolation instead of killing the only channel. The advisor's
   "isolation-relocates-to-silent" caveat is weaker here: usb_mem HAS a detector,
   and in-place is empirically NOT observable.
2. The alternative in-place probe (if staying embedded): instrument the LS-kbd
   slot's device-context PA in xhci_allocSlotSpace (DCBAA[slotId]) via debug() and
   catch it on a deep boot; plus add heap guard bytes around usbkbd allocations to
   localize an overflow. Both still gated on the flaky deep-enum path.

**CORRECTION (advisor, after Step-3 scoping): do NOT do Step 3 yet — identify the
writer first.** Step 3 changes TWO variables at once — the heap AND the driver
path (embedded `usblibdrv` → cross-process `procdriver`, which has NEVER run on
Pi4). A "fix" would be unattributable, and it discards the deterministic repro +
the mbox detector that names the victim. Worse, the write may not be DMA at all:
`h=0 sz=0` with `ring` rewritten is a STRUCTURED MULTI-FIELD CPU write
(use-after-free / init-on-reused memp allocation), not an 8-byte HID DMA (ruled
out). If it's CPU shared-heap corruption, Step 3 relocates it silently and
usb_mem never sees it (the victim is an lwIP memp struct, not a usb_mem chunk).

**Next = catch the writer in-place (keep embedded, keep repro+detector):**
- gdbstub watchpoint on the mbox VA/PA is the ideal (CLAUDE.md: prefer QEMU
  gdbstub). CAVEAT: QEMU rpi4b almost certainly doesn't emulate the VL805/USB +
  real keyboard, so the corruption won't reproduce there — verify before relying.
- Real-HW fallback: mprotect/guard the victim page and report the faulting PC —
  but the TCP/IP mbox is actively written by lwIP, so naive RO-protect floods on
  legit writes. Workable variants: (a) redzone/canary bytes around the memp pool
  block and check them in the detector — canary hit ⇒ sequential overflow (and
  direction); canary intact but struct corrupted ⇒ wild-write/UAF/DMA;
  (b) poison-on-free in lwIP memp + USB malloc to catch UAF;
  (c) a HW debug watchpoint (DBGWVR/DBGWCR) on the mbox PA if the kernel can set
  one — traps CPU writes (PC), DMA doesn't trap (proves controller).
- The enum-depth flakiness (deep <50%, hub-only otherwise) is likely the SAME
  single-priv interrupt-pipe limitation (usbkbd comment), and the corruption only
  fires on the deep path — possibly one bug. Step 3 wouldn't touch it.
- Step 3 remains the eventual architecture goal, but AFTER the writer is
  understood — then it's a justified refactor, not a blind bet.

## STATE 2026-06-01 (late) — CPU-not-DMA confirmed; network survives; enum-flakiness is the gate
Forensic results (memory dump at corruption + network probe):
- **CPU write, not DMA — confirmed.** A deep-enum boot dumped the victim mbox
  struct; it holds a FOREIGN object full of lwIP-VA pointers into the 0x501xxx
  memp region (self-referential list head + nearby nodes + a 0x42bfd0 ptr). A
  device DMA writes HID reports/TRBs, never lwIP VA pointers. So the memp slot
  was REUSED = use-after-free / stolen memory. The Step-3 "isolate DMA pool"
  rationale is dead for this bug.
- **`sys_mbox_free` (mbox.c:79) frees `mbox->ring` but does NOT null it or
  invalidate the struct** — a concrete UAF candidate: a freed mbox keeps stale
  fields and, once its memp slot is reused, a later poll reads garbage.
- **The network SURVIVES**: a probe boot was reachable at 240s (answered 'U'+'k'),
  eventsSeen=30 (hub enumerated + interrupt-polling), bringupRc=0. So the
  corruption (when it fires) is likely a SURVIVABLE lwIP glitch caught by the
  detector's recover-return, NOT a fatal network-killer (the earlier
  "unreachable" was the now-rate-limited flood + slow-boot timing).
- **The GATE is flaky DOWNSTREAM enumeration**: the hub ALWAYS comes up, but the
  kbd/mouse enumerate only ~50% (deep). The hub's single interrupt-URB
  intermittently misses the boot-time port-change → no downstream enum. This is
  the #124 single-priv/single-URB interrupt-pipe limitation, and per the advisor
  is plausibly the SAME bug class as the corruption (two faces). It blocks BOTH
  reliable enumeration AND a deterministic corruption repro.

**NEXT FOCUS = interrupt-pipe reliability (#124).** Make the hub's interrupt-IN
delivery reliable (faster in-completion resubmit, multi-URB per pipe, or a
one-time GET_PORT_STATUS sweep at hub init to catch already-connected devices
regardless of interrupt timing). That (a) makes kbd/mouse enumerate every boot
(the user's reliability goal), and (b) gives a deterministic corruption repro to
then fix the mbox UAF (candidate fix: `sys_mbox_free` set ring=NULL/sz=0 +
audit who polls a freed netconn mbox). Pending free-vs-steal verdict (MBOXFREED)
on the next DEEP boot confirms UAF-vs-heap-corruption. Symbol-resolve 0x42bfd0
via the lwip ELF (.buildroot/_build/.../prog/lwip) — needs a toolchain-nm wrapper
(not yet allowlisted).

## STATE 2026-06-01 (later) — found the CORE bug: halt-timeout infinite loop
Tried the hub initial-port-scan to fix flaky downstream enum. It drove deeper
enum (eventsSeen 9→96) but exposed two bugs (scan now DISABLED, committed):
1. **`hub_devConnected` never sets `hub->devs[port-1]=dev`** — device tracking
   broken; the scan dedup guard is inert; ports double-enumerate.
2. **Unbounded `xhci: halt transition timeout` loop (106014 lines = HANG /
   network death)** when downstream (LS kbd behind the VIA hub's TT) enum
   loops. **This is very likely THE root cause of the ~50% flakiness**: a boot
   whose kbd enum hits the loop hangs/goes shallow; one that doesn't succeeds
   (kbd binds — the idemp boot). Pre-scan shallow boots were reachable (not
   hung); the scan forced enum every boot → made the hang deterministic.

**NEXT FOCUS = fix the halt-timeout loop (the root of flakiness + hang).**
Concrete leads (no Step-3 needed):
- `xhci_cmdExec` timeout path (xhci.c ~1829) calls `xhci_enterHaltedState`
  UNCONDITIONALLY — wrong under `keepRunning=1` (controller is meant to run
  continuously; the VL805 won't halt cleanly → the timeout flood). Under
  keepRunning, on a command timeout do NOT halt; just return -ETIMEDOUT.
- Find + BOUND the caller that retries cmdExec ~unbounded (the 106k count). The
  flood dominated the log so the trigger context scrolled off; add a one-shot
  "enum attempt N for slot S" debug() or bound HUB_ENUM_RETRIES-style loops, and
  capture again (scan re-enabled as a deterministic forcing function).
- Fix `hub_devConnected` to record `hub->devs[port-1]=dev` on success (and clear
  on disconnect) so dedup works.
Then re-enable the hub scan (uncomment hub_notifyScan in hub_conf) → reliable
kbd/mouse enum every boot. The mbox UAF corruption likely also stems from the
looping enum churning device/mbox lifetimes — fixing the loop may resolve it.

## STATE 2026-06-01 (evening) — halt-loop fix landed but NOT the whole hang; scan still off
Committed two correctness fixes (reliability UNVALIDATED): (a) `xhci_enterHaltedState`
no-op under keepRunning (halt-timeout-loop fix); (b) `hub_devConnected` records
`hub->devs[]`. The hub initial-scan is DISABLED again because:
- The scan-triggered downstream enum FAILS to bind the kbd (`insertions=0`) and
  ~50% of boots hang, while the INTERRUPT-triggered enum of the same kbd
  SUCCEEDS (idemp boot bound /dev/kbd0). Same enum code ⇒ timing/state: the scan
  runs right after hub_conf, likely before the hub TT/ports settle.
- The enterHaltedState fix did NOT stop the hang ⇒ there is ANOTHER hang source
  (mbox UAF killing TCP/IP, or a second loop). 
- One scan-OFF verification boot was also unreachable — AMBIGUOUS (regression
  from the two fixes vs a slow-boot flake; boot timing is highly variable this
  session). NOT yet distinguished.

**NEXT (fresh context):**
1. **Disambiguate**: run a MULTI-boot (3–5) scan-off reachability bench. If
   reliably reachable ⇒ the fixes are safe + the prior unreachable was a slow-boot
   flake. If flaky-unreachable ⇒ a fix regressed; bisect (revert enterHaltedState
   first). This MUST be settled before more feature work.
2. **Reliability approach** (after #1): the hub-scan needs a settle delay before
   the downstream enum (let the TT settle), OR abandon the scan and fix the hub
   INTERRUPT-completion delivery (why the boot-time status-change is missed ~50%).
3. **The non-enterHaltedState hang source**: instrument/seek a second loop or the
   mbox UAF actually breaking TCP/IP on the hang boots.
The committed merged-config milestone (deterministic bring-up + full enum on a
good boot) remains the rollback baseline (manifest 2026-06-01-usb-merged-config).

## DISAMBIGUATION (2026-06-01 late) — NO regression; state is safe
2-boot scan-off bench: BOTH reachable (host up +0s), no hang, eventsSeen=8/30,
insertions=0 (both shallow — interrupt path didn't deliver the downstream connect
this run). So the committed correctness fixes (enterHaltedState-no-op-under-
keepRunning + hub devs[] tracking) are SAFE — the earlier scan-off unreachable
boot was a slow-boot flake or a deep-boot+corruption case, not a hard regression.

### Clean state summary (end of this session)
- USB bring-up: DETERMINISTIC (committed). Hub enum: RELIABLE.
- Downstream (kbd/mouse) enum: ~50% (interrupt path misses the boot-time
  status-change). Shallow boots are reachable + stable.
- mbox corruption: a CPU use-after-free (reused lwIP memp slot), NOT DMA; survivable
  (detector recovers). Fired on deep boots; may have contributed to deep-boot
  network death together with the now-fixed halt-loop.
- Halt-timeout infinite loop: FIXED (enterHaltedState no-op under keepRunning).

### Two remaining problems + leads (next sessions)
1. **Reliable downstream enum.** The hub initial-scan (committed, DISABLED) drives
   it but its enum runs too early (TT not settled) → fails/hangs. Options: add a
   settle delay before the scanAll enum; OR fix the hub interrupt-completion
   delivery so the boot-time status-change isn't missed (the root of the ~50%).
2. **UNTESTED: does a DEEP boot now SURVIVE** (reachable + kbd binds) with the
   halt-loop fix + scan-off? Both bench boots were shallow. Run several boots
   (or briefly re-enable the scan as a forcing function) to catch a deep one and
   check reachability + insertions. If it survives → remaining work is only the
   ~50% rate (#1). If it still dies → fix the mbox UAF (sys_mbox_free doesn't
   invalidate; a freed netconn mbox is still polled — lwIP lifetime).

Diagnostic cleanup owed (TD): the aliasing hypothesis was DISPROVED, so the
USBPOOL (usb/mem.c), DMAMAP (xhci.c), and mbox PA/dump prints can be removed;
keep the mbox CORRUPT detector + MBOXFREED for the ongoing UAF hunt.

## FALSIFIED HYPOTHESIS + METHOD RESET (2026-06-01, advisor)
**"Scan-enum fails where interrupt-enum succeeds" is NOT established — do not
build on it.** Evidence: exactly ONE boot ever bound /dev/kbd0 (the early "idemp"
boot) and it then corrupted the mbox and died. That is n=1 AND a failure, not a
working baseline. Every scan variant AND every recent scan-off boot showed
insertions=0. So the scan-vs-interrupt distinction that drove 4 scan variants is
likely noise. Don't build variant #5.

**The real blocker is METHOD, not knob choice.** ~6+ boots this session were
ambiguous because: deep enum ~50%, corruption intermittent, network dies on the
boots we most need to see, and UART truncates exactly at the kbd-enum stage. You
cannot tune out of that. Zero net convergence over the last several iterations.

**Required first step before ANY more enum code:** get ONE clean deep-boot trace
of the kbd-enum sequence (New device → AddressDevice → SET_CONFIG → HID setup →
bind, or where it dies). To fit it ungarbled in the capture, CUT TRACE VOLUME:
gate out the high-frequency spam (RC_BAR2 / per-command / roothub-poll debug())
so the low-volume kbd sequence survives (debug() lines survive when volume is
low — proven). One clean trace > ten counter-probes. Then decide approach from
what the trace SHOWS, not from a guessed discriminator.

## STEP 3 RESULT (2026-06-01, user-chosen) — builds + DETERMINISTIC repro, but reboot-loops
Standalone `usb` daemon + `usbkbd`/`usbmouse` processes: BUILDS clean (lwip links
without USB; the 3 binaries are bundled). Boot-test: the daemon brings up the
controller + enumerates EVERY boot (deterministic — the repro we lacked!), but
the **LS keyboard behind the VIA hub's TT fails its ep0 control transfers**
(`xhci: transfer completion timeout`, xhci.c:2676 in xhci_ep0ControlRead). The
failed enum is retried UNBOUNDED (~1200 timeouts/boot) and the system
REBOOT-LOOPS (~85 reboots in 240s; lwip never starts). So Step 3 is currently
WORSE than embedded (no network) BUT gives a deterministic kbd-enum-failure repro.

This is the SAME core bug across all configs: the LS-kbd-behind-TT control
transfers don't complete (the #116 TT/route/slot-context path). The standalone
daemon just exposes it every boot + the unbounded retry makes it catastrophic.

**NEXT (in order):**
1. **Bound the enum-failure retry** so a failed device doesn't re-enumerate
   forever → no reboot-loop → the usb daemon fails gracefully and lwip/network
   survives (proving Step 3's isolation benefit). Find the re-trigger loop:
   ~1200 control-timeouts/boot ≫ HUB_ENUM_RETRIES, so something re-drives enum
   (hub port-change not cleared on failure? roothub thread? usb daemon main?).
   Also: WHY does it reboot (user-process fault vs watchdog starvation vs kernel
   fault)? Capture an Exception/Data-Abort line or check the watchdog.
2. **Diagnose the LS-kbd ep0 control timeout** (the real enum fix): the kbd is
   low-speed behind the HS VIA hub's TT. Its slot context needs correct TT
   (hub slotId + port), route string, and speed. Capture the daemon's clean UART
   trace (now deterministic + volume-reduced) of the kbd AddressDevice + first
   GET_DESCRIPTOR to see where it dies. This is now diagnosable BECAUSE Step 3
   made it deterministic.
NB: Step 3 is committed (user's choice + the repro). The reboot-loop is netboot
(reload-able, no HW harm) but the Pi is unusable until #1. Embedded fallback is
revertible (uncomment the port/Makefile block + restore the plo lines).

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

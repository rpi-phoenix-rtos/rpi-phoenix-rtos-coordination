# USB re-analysis (2026-05-29) — the "silicon flakiness" conclusion does not hold

Three independent analysis agents re-examined the Pi 4 USB stack from
scratch: a code-correctness deep-read of the live driver, a cross-OS
behavioral comparison, and a forensic re-derivation of every headline
claim from the raw UART logs. This note records their findings. It
**supersedes** the "silicon flakiness" framing in the prior
`docs/status.md` (2026-05-28) entry and `docs/known-limits.md`.

The motivating doubt (from the project owner) was correct: USB keyboard
input *always* works on this exact board under Linux and under the Pi
firmware boot menu. A hardware-flakiness conclusion for our failures was
never well-supported, and all the Pi 4 USB code is AI-written with no
upstream Phoenix reference — so it can and does contain real bugs.

## The central correction: there are TWO distinct problems, not one

The docs merged two categorically different failure populations under a
single "flakiness" umbrella. They have different signatures and almost
certainly different causes.

### Problem A — PoC embedded-USB: deterministic event-DMA loss (software)
- Signature in every *decidable* PoC run: `xhci_reset enter USBSTS=0x01`
  → `post-HCRST=0x11` → controller running (`CRCR` shows `CRR=1`) →
  `cmdExec TIMEOUT` → `first event @idx -1` → `embedded usb_init() failed`.
- The bridge is **up** and the controller is **readable and running**;
  the failure is that **zero events are ever DMA-written** to the event
  ring. This is **100% reproducible**. A deterministic failure cannot be
  silicon link-flakiness.

### Problem B — `X` diag rig: intermittent bridge-MMIO-dead (cause unproven)
- Signature on rig failures: `pre USBSTS=0x00000000` or `0xdeaddead` —
  bridge MMIO reads return garbage, i.e. the PCIe link / outbound
  translation is dead *at probe time*.
- This is the *only* signature that even looks silicon-shaped, and even
  here the local evidence favors a **software/cadence** cause:
  - The project's own `docs/status.md:267-280` documents **rapid-cycle
    bridge degradation** needing a ≥30 min idle gap between boots. The
    five rig runs analyzed were **3–6 minutes apart** — squarely in that
    regime.
  - `docs/status.md:360-363` (CPU-side pmap bug) and memory
    `usb-dma-write-loss` (dev-1..31 config scan tearing down the
    outbound window) offer software explanations for the identical
    `0xdead`/MMIO-0 signature.
  - Linux drives this exact VL805 reliably; the firmware boot menu uses
    USB keyboard with zero problems.

The `pre USBSTS=0x00` signature that the docs attributed to the PoC
actually belongs to the **rig**. No decidable PoC failure exhibits it.

## Forensic corrections to the headline numbers

- **"PoC fails 0/21 trials" is inflated.** ~12 of the 21 captures were
  powered off / `--capture-secs` too short and **ended before
  `usb_init` printed any verdict** — they contain only the pre-bring-up
  `usb: Initializing driver as host-side: usbkbd` registration line.
  Those are *not evidence*. Honest statement: **0 successes among ~18
  decidable runs**, ~12 truncated. Counting truncated captures as
  failures is exactly the documented false-negative failure mode.
  - Truncated / re-run needed (`--capture-secs ≥ 180`): `trial-T1`,
    `trial-T5`, `usbinit-trial-B`, `drive-only-D3`, `no-bridge-map-B2`,
    `no-bridge-map-B5`, `scratch-contig-S2`, `max-slots-1-M1`,
    `max-slots-1-M2`, `max-slots-1v2-N1/N3/N4`.
- **The rig's 2/4 successes are genuine enumerations** — a real 18-byte
  Get-Descriptor — **but of the VIA companion USB2 hub (VID 2109 PID
  3431), not the K120 keyboard (046d:c31c).** "Rig works" demonstrates
  root-hub enumeration cross-process; it has **never** demonstrated a
  working keyboard.
- **The BRIDGE_ONLY/DRIVE_ONLY CRCR-stale split** may be a real fix but
  produced **no change in the terminal PoC outcome** — post-split runs
  still end in `first event @idx -1`.

## Concrete bugs in the live driver (`usb/xhci/xhci.c`, `bcm2711-pcie.c`)

Live path is `sources/phoenix-rtos-devices/usb/xhci/{xhci.c,bcm2711-pcie.c}`
built into `libusbxhci`. The standalone `pcie/server/pcie.c` is **dead
duplicate code** (daemon not spawned) — remove before public release.

- **Bug #1 (High) — event ring never advanced.** `eventCycleState` is
  set once (`xhci.c:1410`) and never toggled; `cmdExec`/roothub thread
  `memset` the ring and re-read index 0 every command, resetting ERDP to
  base. This treats a producer/consumer ring (xHCI 1.2 §4.9.4 / §5.5.2.3.3)
  as a single mailbox. Works only because the controller is halted
  between commands (Bug #4). Bites at command #2 (EnableSlot). The rig
  scans all 256 TRBs and tracks distinct `cmd_pa` params, which is why it
  can chain NOOP→EnableSlot→AddressDevice→GetDesc. **Fix:** real event
  consumer — track ERDP index + consumer cycle state, dequeue every TRB
  whose cycle bit == CCS, dispatch by type, flip CCS on wrap, write ERDP
  with EHB; stop memset-ing the ring.
- **Bug #2 (High; leading non-silicon cause of Problem B) — recycled
  DMA page cache hazard.** Rig `mmap`/`munmap`s its DMA pages per run;
  Phoenix recycles physical pages. Mapping is `MAP_UNCACHED`, but nothing
  cleans a dirty cache line a *previous cacheable owner* of that physical
  page may have left, and BCM2711 PCIe is non-coherent (the code clears
  NoSnoop in both RC and VL805 DCTL — `bcm2711-pcie.c:771,1116` — for
  exactly this reason). **Fix:** allocate DMA rings once at init, never
  free/recycle; `dsb` + cache-clean after the initial memset before first
  controller use. **Decisive read-only experiment:** in the rig,
  allocate once and reuse across invocations — if flakiness drops, the
  recycled-page hazard is confirmed.
- **Bug #4 (Medium) — stop/start controller around every command.**
  `cmdExec` calls `enterRunState` then `enterHaltedState` per command
  (`xhci.c:1506/1698`). No reference xHCI driver does this; the
  controller is meant to run continuously. Not the current 0% cause (the
  PoC dies at command #1, before the halt), but destabilizes enumeration
  once writes land. The rig sets R/S=1 exactly once.
- **Bug #3 (Medium) — `resettleOutboundWindow()` inert in production.**
  `bcm2711_pcie_lastCtx` is set only in the non-DRIVE_ONLY branch
  (`bcm2711-pcie.c:1498`), past the DRIVE_ONLY early return (`:1440`).
  `main.c:89` sets `USB_HCD_PCIE_DRIVE_ONLY=1`, so the PoC never sets
  `lastCtx` → FIX-3 (`xhci.c:881`) and FIX-19 (`xhci.c:1275`) silently
  return `-ENODEV` and do nothing. Comments call them load-bearing; they
  are dead. Also `main.c:46-55` comment ("we deliberately do NOT set
  USB_HCD_PCIE_DRIVE_ONLY") **contradicts** the live `main.c:89`.
- **Bug #5 (Low-Med) — missing pre-R/S settle.** Rig does `dsb sy` +
  `usleep(10000)` before R/S=1 (`diag-udp.c:1586-1587`); production has
  the `dsb` but no settle (`xhci.c:1309`). One of only two material
  divergences before the first doorbell. Easily benched.
- **Bug #6/#7 (Low/latent)** — scratchpad-count ignores the Hi field
  (VL805 reports Hi=0 so harmless); no explicit cache-clean between final
  ring memset and R/S=1 (moot under MAP_UNCACHED, couples to Bug #2).

## Disproven hypotheses (stop chasing)

- **VL805 firmware not loaded / not reloaded after reset:** disproven.
  Logs show `pcie: VL805 fw_ver @0x50 = 0x000138c0 (loaded, skip mailbox)`.
  The board's SPI-EEPROM firmware is loaded by Pi firmware at boot;
  Phoenix reads cfg[0x50], sees non-zero, and correctly skips the
  `NOTIFY_XHCI_RESET` mailbox. Matches Linux `rpi_firmware_init_vl805`.
- **Events undeliverable without MSI:** disproven. VL805-on-BCM2711 uses
  legacy INTA and Phoenix polls the event ring; xHCI posts events by
  DMA-writing a TRB regardless of interrupt delivery, and the failure is
  `USBSTS.HSE`/no-write which precedes any interrupt.

## NEW LEAD (2026-05-29 PM) — external-abort SError during PCIe/USB bring-up

While implementing the TD-10 SError handler, unmasking SError on real Pi 4
revealed a **live, continuous external-abort SError source in the BCM2711
PCIe / VL805 bring-up** that was previously invisible (SError had been
masked across all paths since 2026-04-30).

- Signature: `esr=0xbf000002` (EC=0x2F SError, IL=1, **IDS=1** → A72
  IMP-DEF syndrome, so EA/AET/DFSC are not architecturally meaningful),
  `far=0`, imprecise (charged to whatever EL0/EL1 code was running).
- **Isolation-proven causation:** with the `usb` daemon and the
  lwip-embedded USB thread both disabled, the boot runs 15+ s with full
  networking (GENET link up, DHCP) and **zero SErrors**. With USB enabled,
  the first SError fires exactly when `usb-hcd ops->init` begins, and the
  earlier kernel init (vm/proc/threads/dummyfs) is SError-free.

**Why this matters for the USB wall:** the PCIe/USB code is issuing a
memory access (MMIO or config) that the bridge or VL805 NACKs with
SLVERR/DECERR, surfacing as an imprecise SError. This is a *direct,
mechanistic* candidate for the long-unexplained "controller runs but
events never post / inbound DMA writes don't land": if the bridge returns
an error response on the controller's inbound write (or on a host MMIO/
config access in the bring-up path), that both loses the write AND raises
the SError. The entire prior investigation missed this because SError was
masked — every "DMA write silently lost" framing may actually be "DMA
write / access externally aborted, abort masked."

**Next experiment to localize it** (read-mostly, no JTAG): temporarily
unmask SError (revert NO_SERR) and add coarse markers around the phases of
`bcm2711_pcie_initVL805` and `xhci_init` (PERST/bridge-config / link-wait /
VL805-config / HCRST / ring-program / R/S). Because the SError is
imprecise it won't pin a single instruction, but bracketing which *phase*
the first SError follows narrows the aborting access. Then read back that
register's response / check the bridge MISC error-status to confirm
SLVERR vs DECERR. (See TD-10 and memory `pi4-serror-pcie-source`.)

### Localization attempt (2026-05-29 PM) — what we learned

Ran the phase-marker + SError-unmask experiment (diagnostics reverted
afterward; baseline `bcb64610` restored). Findings:

- **No synchronous aborts at all** (zero EC 0x20/0x21/0x24/0x25 across the
  whole boot). The error is genuinely a posted-write/fabric **async** abort
  charged imprecisely — so "the bridge-register *reads* caused it" is wrong
  attribution. The pre-Phoenix dump reads returned real values (`0x0...`),
  not `0xdead`, so the RC MMIO reads themselves succeed.
- With a **log-and-continue** handler (async + far=0 ⇒ `eret`-resume is
  safe; HDMI + UART both confirmed the boot reaches `(psh)%`), the SErrors
  are non-fatal: **17 SErrors in two bursts, both in USB bring-up** — (1)
  the boot daemon's `bcm2711_pcie_initVL805` bridge bring-up, (2) the
  lwip DRIVE_ONLY path right after `link UP`. **Continuing past them does
  NOT restore event-posting** (USB still `first event @idx -1`, rc=-110),
  so the SError and the lost write are correlated-but-not-trivially-one-event.
- **Marker-timing localization is unreliable here** (imprecise + multi-PE
  delivery; the UART output garbles under the multi-core SError storm,
  defeating clean register-value reads). This is the tooling wall until
  JTAG.

### Codex second opinion (2026-05-29) — ranked + the decisive next step

1. **Most likely: an illegal CPU→PCIe *outbound* access NACK'd by the RC.**
   Linux `pcie-brcmstb` treats this hardware as *aborting* (not returning
   all-ones) on bad PCIe accesses, and exposes RC error-report registers at
   **`0xfd500000 + 0x6004..0x6020`** that latch VALID, CFG-vs-MEM,
   read-vs-write, the offending address, and cause (timeout / abort / UR /
   disabled / bad-address). The SError is probably an outbound-traffic
   symptom (RC regs / PCI config / VL805 BAR MMIO), while the lost
   event-ring writes are *inbound* DMA — opposite directions, so not
   automatically the same transaction, but they can share one root cause
   (bad RC/SCB inbound setup).
2. **Config discrepancy vs Linux (concrete, testable):** Phoenix programs
   `RC_BAR2` = **4 GB identity** and `SCB0_SIZE_4G` (encoding 17). But the
   Pi 4 PCIe wrapper is limited to the **first 3 GB** (`dma-ranges` =
   `0xc0000000`); Linux builds the inbound viewport from `dma-ranges`
   (CPU addr 0, power-of-two) **and** sets `SCB*_SIZE` accordingly. A 4 GB
   aperture / wrong SCB-size on the 3 GB-limited model is a credible cause
   for inbound writes being dropped while reads work. (The event ring at
   ~51 MB is inside both apertures, so this is plausible-but-unproven.)

**Decisive next step (no JTAG): read the RC outbound error registers
cleanly.** A direct UART dump during bring-up is garbled by the SError
storm. Read them instead **over diag-udp** (UDP reply, no UART garble) once
the boot settles — the registers *latch* and persist. Add a diag-udp
command that maps `0xfd500000` and returns `0x6004..0x6020`. If `VALID=1`,
it gives the exact offending address + CFG/MEM + cause (→ fix outbound
addressing/sequencing). If it stays clear, pivot to the inbound-decode
angle: compare Phoenix's `MISC_CTRL` / `RC_BAR2` / `SCB*_SIZE` /
`dma-ranges`-derived model byte-for-byte against Linux's 3 GB model.
(Then JTAG, arriving in a few days, can confirm precisely.)

## RC-error-reg read attempt (2026-05-29 evening) — deferred to JTAG; two corrections

Tried to read the RC outbound error regs (`0x6004..0x6020`) to classify the
abort. The read was defeated **three** ways and is **deferred until JTAG**
(days away): (1) over diag-udp — blocked by a DHCP regression (below);
(2) via boot-daemon UART dump — the daemon's `debug()` output is severely
reordered/buffered relative to other cores and didn't reliably flush;
(3) the boot-daemon baseline read returned all-zero. A diag-udp `'P'`
command (lwip `b4217c0`) is committed for the JTAG-day / network-up read.

Two corrections recorded so a future session doesn't re-chase them:

- **`MISC_CTRL=0` / `HARD_DEBUG=0` in the pre-Phoenix dump is normal
  RESET-state, NOT a dead bridge.** It runs before `bcm2711PrepareHostBridge`
  deasserts SW_INIT/PERST. Across logs: `MISC_CTRL=0` appears ~110× vs the
  post-bring-up `0x88003480` ~9×; `MISC_STATUS=0xb0` (link up) confirms the
  bridge does come up. So the "bridge-MMIO-dead this boot" worry was a
  misread — there is less per-boot non-determinism than feared.
- **DHCP regression observed:** with the embedded USB host active, lwip's
  GENET DHCP looped DHCPDISCOVER→DHCPOFFER every 60 s with **no
  DHCPREQUEST/DHCPACK** — the netif never got its IP, so the Pi was
  unreachable by UDP. This **contradicts "TD-Eth-DHCP RESOLVED"** and may be
  the embedded-USB bring-up activity perturbing lwip's DHCP state machine.
  Worth a dedicated look (stability goal), tracked separately from USB.

## Recommended order of work (none requires JTAG)

1. **Read-only experiments first** (cheap, decisive, separate the
   confounds): (a) trigger the rig at the PoC's 10 s mark — or delay the
   PoC to several minutes — to separate boot-timing/fabric-warmup from
   code path; (b) allocate-rings-once vs munmap-per-run in the rig to test
   the recycled-page hazard (Bug #2).
2. **Then fix the timing/DMA variable** so *any* event lands.
3. **Then the targeted event/command-ring rewrite** (Bugs #1 + #4):
   continuously-running controller + a real event-ring consumer.

Fixing the ring engine alone will NOT move 0%→nonzero (PoC never reaches
command #2); fixing timing/DMA alone leaves a ring engine that breaks at
EnableSlot. Both are needed, in that order.

## Open / lowest-confidence

- Bug #2's exact mechanism is Medium confidence — the recycled-page
  hazard is the best-supported software explanation for Problem B's
  non-determinism, but the allocate-once experiment is needed to confirm
  it versus a pure fabric-warmup timing effect. The "concurrent GENET DMA
  keeps the SCB fabric draining" hypothesis (memory `usb-dma-write-loss`)
  is also **not ruled out** by the logs.
- The cross-OS agent's web tools were denied this session, so its
  upstream citations are from in-repo notes, not freshly fetched. The
  code-correctness and forensic findings are from live source and raw
  logs and stand on their own.

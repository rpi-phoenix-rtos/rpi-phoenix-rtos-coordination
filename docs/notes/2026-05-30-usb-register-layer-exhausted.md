# USB: register-layer value-diff EXHAUSTED — failure is sub-register (inbound DMA write path)

**2026-05-30.** Decisive zero-device-cost value-by-value diff of the working
diag rig vs the failing boot PoC, both in the **current same-process build**
(lwip-port). This closes the "is any register/ring field different?" question
that prior notes *claimed* answered but had only checked in the stale
cross-process era (Codex flagged those as thin/stale).

## The diff (rig success vs PoC failure)

Rig success (`artifacts/diag-udp/2026-05-27-092210-usb-enum-es3.txt`,
`...-093223-usb-enum-ad.txt`):
- `pre USBSTS=0x00000011` (HCH=1, PCD=1)
- `post USBCMD=0x0000000d USBSTS=0x00000010` (running, PCD=1)
- `evt[0]@idx0 = 01000000 00000000 01000000 00008801` → **type-34 Port Status
  Change**, port 1 (autonomous; needs NO inbound read).
- `evt[1]@idx1 = <cmdRingPA> 00000000 01000000 00008401` → **type-33 Command
  Completion**, parameter == CmdRng PA → No-Op completed.
- EnableSlot @idx2 cc=1, AddrDev cc=1. Full enumeration.

PoC failure (current composite build, `usb-rig-match*` trial logs):
- `pre USBSTS=0x00000011` — IDENTICAL.
- `post USBCMD=0x0000000d USBSTS=0x00000010` — IDENTICAL.
- ERSTSZ=1, cmd_ctrl=0x5c01, cycleState=1, internal pointer consistency
  validated (ERSTBA==erstPhys, ERDP==eventRingPhys, DCBAA[0]==scratchpad) —
  all as the rig.
- Event ring: **`first event @idx -1`** — ZERO events. No type-34, no type-33.

## What this proves

- **The register/ring layer is exhausted.** Every controller-visible field
  (USBCMD, USBSTS, ERSTSZ, ERDP, ERSTBA, CRCR/RCS, DCBAAP, cmd-TRB bytes,
  cycle state) matches the working rig, in the same process, same boot timing
  class. There is no missed register.
- **It is specifically a WRITE-PATH failure.** Both runs have a pending port
  change (PCD=1) at R/S=1. A type-34 Port Status Change event is posted by the
  controller **autonomously**, requiring no inbound read of any host
  structure. The rig gets it; the PoC does not. So "CRR=1 doesn't prove reads
  work" (Codex) is moot for the headline symptom: the controller's inbound
  *writes* to the PoC's ring pages do not land, full stop.
- **The only remaining variable is the physical pages.** Rig rings sit at
  ~0x335xxxx–0x336xxxx; PoC rings in the same ~51 MB region. Same SoC, same
  bridge config, same VL805, same process. The VL805's inbound writes reach
  the rig's pages and not the PoC's.

## Composite experiment result (this session)

Made the PoC byte-match the rig's bring-up (ERDP/ERSTBA written LO-then-HI;
+10 ms pre-R/S settle) — the only two code deltas a full rig-vs-PoC bring-up
diff found. Result: **2/2 clean trials still `@idx -1`** (plus harness thrash
from the 10-min Bash cap killing multi-trial benches). The half-order is in
fact provably inert for our low-memory rings (HI half = 0, so both orders
yield an identical final register value). Kept the edits only as
rig-matching hygiene + to correct a false "matches the rig" comment in
xhci.c; **they are NOT the fix.**

## Where this leaves USB (honest status)

This is the deep wall prior work documented after ~20 elimination iterations.
The failure is below the register layer: the controller's inbound DMA write
does not reach the PoC ring's physical pages, while it reaches the rig's, with
everything controller-visible identical. Remaining angles, all still SOFTWARE
(per Codex's "not silicon / not JTAG-gated" — the board is known-good):

1. **Same-boot paired-PA capture (decisive, in-reach):** in ONE boot, log the
   PoC's ring PA (it fails) AND the rig's ring PA via the 'X' command (it
   succeeds/fails), to confirm the controller writes one PA and not the other
   in the same boot → narrows the cause to page/allocation provenance vs
   process/timing/registers. (Needs the netboot harness healthy for DHCP+UDP.)
2. **Kernel-side page-provenance instrumentation:** log the physical frames
   the kernel hands the PoC's `usb_allocAligned` mmaps vs the rig's, and check
   for any cacheable kernel alias / SLC residency difference per
   allocation history (the user's "per-process kernel memory/spawn state"
   hint). This is kernel code, no JTAG.
3. **Allocation-order probe:** the PoC allocates its rings after
   `usb_memInit`/`hub_init` (a different allocation history than the rig's
   fresh diag-time mmaps), even though flags + per-structure fresh-mmap match.
   Test forcing the PoC's ring allocations to mirror the rig's exact
   alloc sequence/region.

Register-poking and blind statistical benches are NOT the path forward; the
next experiments are deterministic and target the page/DMA-write layer.
See memory `usb-dma-write-loss` and `pi4-serror-pcie-source`.

## Fresh-page event-ring retry (2026-05-30, page-provenance leaning REFUTED)

Implemented an in-`xhci_init` retry: on No-Op timeout, halt, re-allocate the
event ring with FRESH physical pages (leak the old ring so `mmap` can't hand
back the same frame — a free+realloc reuse would invalidate the test),
re-program ERST/ERDP, retry up to 8×. Hypothesis: if the failure is specific
to the PoC's event-ring *pages*, a fresh allocation should start receiving the
controller's inbound writes.

Result (two builds, free-realloc and leak-realloc): the retry **engaged**
(`try 1`/`try 2` debug lines captured) and **still failed** — final
`ops->init fail rc=-110`. Page-provenance is therefore **leaning refuted**
(fresh event-ring pages do not help), i.e. the failure is process/controller-
global, not specific to the original frames. CAVEAT: the netboot harness
degraded badly this session (DHCP catastrophically slow after repeated
timeout-killed cycles), so neither run captured a clean full `8/8 failed`
verdict — the captures died mid-retry (~try 2). The retry code was **reverted**
(unproven + leaks memory = not shippable); re-run with a healthy harness if
this angle is pursued.

## Same-boot paired-PA capture (2026-05-30) — page-provenance REFUTED; fabric-activity lead

User chose "finish the current thread cleanly." Did the decisive same-boot
paired capture: one boot where the boot driver's `usb_init` fails AND the 'X'
rig runs (artifact `artifacts/diag-udp/2026-05-30-102025-paired-rig.txt` +
boot log `...082018-netboot-paired-poc.log`):

| actor (same boot, same lwip-port process) | event-ring PA | result |
|---|---|---|
| boot driver `usb_init` (~46 s) | `0x032f4000` | `@idx -1`, writes LOST |
| 'X' rig (+84 s) | `0x032ee000` | type-34 + type-33 land, EnableSlot cc=1 |

The two event-ring PAs are **adjacent** (~24 KB apart, same ~51 MB region),
yet the controller DMA-writes the rig's ring and not the driver's, in the SAME
boot. Conclusions:
- **Page-provenance REFUTED** — it is NOT the PA value / physical frame
  (adjacent addresses, one works one doesn't). (Consistent with the
  fresh-page retry, which also didn't help; that retry is reverted — it also
  destabilised the boot, dying at ~46 s, so abandon the in-driver retry.)
- **Inbound writes are NOT globally dead** — the controller writes DDR fine in
  this boot (the rig's ring). So it's not a missing bus-master/window/config.
- The discriminator is the **bring-up CONTEXT**. The rig runs at +84 s *with
  GENET RX DMA active* (it is receiving the 'X' UDP packet over the net) and is
  effectively a later/second bring-up; the boot driver runs at ~46 s with the
  fabric comparatively idle (DHCP not yet bound, no sustained traffic).

**Leading hypothesis (revived, never cleanly tested):** BCM2711 SCB/SLC fabric
only drains VL805 inbound writes to DRAM when there is concurrent fabric/DMA
activity. NOTE the prior "ORDERING" test (spawn usb after GENET *link-up*) was
negative — but link-up ≠ *active RX/TX DMA*; the rig works specifically while a
packet is in flight. The clean test is to bring up xHCI **while sustained
GENET traffic is flowing** (e.g. defer `usb_init` until DHCP-bound + drive
continuous ping/UDP during the No-Op poll), not merely after link-up.

## Decision point (surfaced to user 2026-05-30)

After 5 negative USB iterations this session (inbound-offset, rig-match
composite, register value-diff, free-realloc + leak-realloc fresh-page retry),
the well-characterized state is: **register layer exhausted; sub-register
inbound-DMA-write failure; page-provenance leaning refuted; netboot harness
broken.** The remaining real software angle is a kernel-side pmap/page-state
(SLC-residency) instrumentation deep-dive — a major undertaking — and the test
harness needs repair before ANY further device testing (USB or WiFi). Pausing
to get the user's steer on the path forward.

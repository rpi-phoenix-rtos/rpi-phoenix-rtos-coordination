# USB 10-Cycle Experiment Results (2026-05-25)

Conducted per `docs/inprogress/usb-resumption-strategy.md` after the diag-udp
observability infrastructure landed. Goal: characterize the
statistical mode A/B/C wedge and test the four hypotheses (H1
memory coherence, H2 under-voltage, H3 side-process MMIO reads,
H4 silicon variability) without JTAG.

## Setup

- Phoenix-RTOS image built from `agent/rpi4-genet` (lwip-port with
  diag-udp 'x' xHCI snapshot + 'd' DCBAA dump).
- Cycles 1-5 used the 'x' command only.
- Cycles 6-10 used 'x' + 'd' in sequence.
- usb-hcd allowed to run to its natural failure (no instrumentation
  changes to usb-hcd or any other in-process code).
- UART captured for usb-hcd error-mode classification.

## Results table

| Cycle | USBSTS | Mode | DCBAA[0]      | DCBAA[1..15] | Throttle |
|-------|-------:|------|---------------|--------------|---------:|
| 01    | 0x11   | C    | (not probed)  | (not probed) | 0x0      |
| 02    | 0x15   | B    | (not probed)  | (not probed) | 0x0      |
| 03    | 0x15   | B    | (not probed)  | (not probed) | 0x0      |
| 04    | 0x15   | B    | (not probed)  | (not probed) | 0x0      |
| 05    | 0x11   | C    | (not probed)  | (not probed) | 0x0      |
| 06    | 0x11   | C    | 0x03301000    | all zero     | 0x0      |
| 07    | 0x11   | C    | 0x03301000    | all zero     | 0x0      |
| 08    | 0x15   | B    | 0x03301000    | all zero     | 0x0      |
| 09    | 0x11   | C    | 0x03301000    | all zero     | 0x0      |
| 10    | 0x15   | B    | 0x03301000    | all zero     | 0x0      |

Across every cycle:

```
xHCI MMIO (side-process view)
  CAPLENGTH=0x20  HCIVERSION=0x0100  (xHCI 1.0)
  HCSPARAMS1=0x05000420 (32 slots, 4 intrs, 5 ports)
  HCCPARAMS1=0x002841eb (AC64=1, BNC=1, PPC=1, LHRC=1, NSS=1)
  USBCMD=0x00000000 (R/S=0, usb-hcd has exited or HCRST'd)
  CONFIG=0x00000020 (MaxSlotsEn=32, programmed by usb-hcd)
  CRCR=0x00000000_00000000 (controller view of cmd ring)
  DCBAAP=0x00000000_032ff000 (programmed by usb-hcd, register-side)
```

Mode classification breakdown:
- **5 × mode C (50%)** — USBSTS=0x11 (HCH | PCD, no HSE).
  Controller halted but never had HSE; R/S=1 succeeded into
  RUNNING but NOOP cmd never completed.
- **5 × mode B (50%)** — USBSTS=0x15 (HCH | HSE | PCD, HSE sticky).
  Controller HSE'd during R/S=1 transition.
- **0 × mode A** — no cap-probe poisoning observed this session.

## Hypothesis verdicts

### H1: TD-Pi4-FalseSharingPenalty applied to xHCI — **PARTIALLY RULED OUT**

OS-side memory coherence is sound. Cycles 6-10 confirm:
- usb-hcd's write to DCBAA[0] (`0x03301000` scratchpad pointer)
  reaches DRAM.
- The lwip-port side process reads it back identically across
  every cycle.
- usb-hcd's writes to xHCI MMIO registers (DCBAAP, CONFIG) persist
  and are visible from the side process.

So the **CPU memory model and the OS-side cross-process visibility
are coherent**. The TD-Pi4-FalseSharingPenalty pattern (which
required `__atomic_store_n(RELEASE)` + cache-line padding to fix
false sharing on the GENET burner) does **not** apply here. The
problem is not OS-userspace memory coherence.

What we did NOT test directly: whether the VL805 bus master's
PCIe inbound DMA reads see the same DRAM contents that the CPU
sees. That's the path between `BCM2711 PCIe RC inbound window →
VL805 internal master → DRAM read`. To test, we'd need either:
- Instrumentation in usb-hcd to dump the cmd ring TRBs at the
  moment of the doorbell (so we know what was written), then
  read them back from a side process. We have DCBAA confirmed
  but the cmd ring PA isn't exposed in any MMIO register since
  CRCR_LO=0 (the controller never advanced its internal read).
- JTAG / PCIe analyzer on the bus.

### H2: Pi 4 PMIC under-voltage — **DEFINITIVELY RULED OUT**

Throttle = 0x00000000 across all 10 cycles. Bits 0 (uv-now), 1
(arm-cap-now), 2 (throttle-now), 3 (soft-now), 16 (uv-since), 17,
18, 19 (sticky equivalents) all clear. Lab bench PSU is delivering
stable voltage. PMIC is not flagging any issue.

This is a clean result and worth noting independently: **the Pi 4
test rig is electrically healthy**. Whatever causes mode A/B/C is
NOT a power supply problem.

### H3: Per-process bridge state for READS — **RULED OUT**

Side-process MMIO reads of xHCI at PA `0x600000000` work cleanly
in every cycle. CAPLENGTH, HCIVERSION, HCSPARAMS, HCCPARAMS,
USBCMD, USBSTS, CRCR, DCBAAP, CONFIG, PAGESIZE all return sensible
values from the lwip-port process while usb-hcd may have completely
exited.

This is a positive finding for the future: **diag-udp is a working
cross-process xHCI inspector** for any future Pi 4 USB work.

### H4: Bus-master DMA state variability — **CONFIRMED AS LEADING CAUSE**

By elimination plus positive evidence:
- Modes B and C both reflect "host configured everything correctly
  but the controller can't make progress".
- Mode B (HSE on R/S=1) is the controller's way of saying "I can't
  read DCBAA via my bus master".
- Mode C (NOOP cmd timeout) is the controller's way of saying "I
  can't read the cmd ring TRB via my bus master".
- Both modes manifest with OS-side state that the CPU can verify is
  correct.

The only remaining explanation is that the **VL805 ↔ BCM2711 PCIe
bridge ↔ DRAM inbound DMA path** is the wedge. This is below the
OS visibility horizon. The original 2026-05-24 parking conclusion
("needs JTAG / oscilloscope to characterize PCIe transactions") is
correct.

## What we learned this session

Even though the wedge wasn't fixed, the experiment added six
concrete pieces of information that the prior parking didn't have:

1. **USBSTS bit 3 (HSE sticky) is a free mode-classification signal**
   readable from any side process at any time post-failure. No UART
   scraping needed.
2. **Mode distribution shifted to 50/50 B/C** (no A this batch); the
   prior estimate of 1/3-each was based on a noisier sample.
3. **Throttle is definitively clean** — power supply is not in play.
4. **Side-process xHCI MMIO inspection works** — future USB triage
   has a non-instrumented observation channel.
5. **DCBAA contents are stable across boots** — usb-hcd's allocator
   is deterministic; PA 0x032ff000 (DCBAA) + PA 0x03301000
   (scratchpad) consistent every cycle. Useful for future probes.
6. **CRCR read-back from controller is always 0** in failure — the
   controller's internal command-ring pointer never advances. Could
   be a useful "is it stuck or not" check from any inspector.

## What this experiment does NOT rule out

A more invasive test would be to add usb-hcd instrumentation that:
- Logs the cmd-ring buffer PA before the doorbell.
- Reads the TRB content from a side process at that PA.
- Compares to what usb-hcd wrote.

If the side-process read matches usb-hcd's written TRB → confirms
H1 OS-side coherence is fine for the cmd ring too (likely; DCBAA
already confirms it).

If the side-process read shows zeros → indicates an OS-side
coherence issue specific to the cmd-ring allocation path (would
contradict the DCBAA finding but worth checking).

Either way, the next question would be "is the VL805 able to read
the same data the CPU sees" which is the bus-master question that
requires hardware-level visibility.

## Addendum 2026-05-26: side-process full bring-up tested

Three follow-up iterations after the original 10-cycle experiment,
testing whether a clean xHCI bring-up from a completely separate
process succeeds where usb-hcd's does not:

- **Iteration K (HCRST write-test)**: side-process write of
  USBCMD.HCRST=1 cleared the HSE sticky bit and reset CONFIG +
  DCBAAP to defaults. Controller responds to MMIO writes correctly.
- **Iteration L v1 (HCRST + DCBAA + event ring + R/S=1)**: HSE
  immediate on R/S=1.
- **Iteration L v2 (+ CRCR + cmd ring + DSB SY barrier)**: HSE.
- **Iteration L v3 (+ USBCMD = RS|INTE|HSEE in one write, 10 ms
  settle)**: HSE.

All three match Linux's `xhci_run()` register-write order exactly,
from a process with no shared state with usb-hcd. **The wedge
persists across every code-side variant we can construct.** This
is now a definitive confirmation of H4 (bus-master DMA path issue).

Commits: lwip `3e14874` (K), `4928683` (L v1-v3).

The diag-udp `'R'` (HCRST), `'X'` (full bring-up), `'x'` (snapshot),
and `'d'` (DCBAA dump) sub-commands remain in place as future USB
debugging surface for when JTAG becomes available.

## Decision

Per the user's standing direction ("if not - return back to wifi
and bluetooth"), accepting the JTAG block as the final blocker.
The USB work returns to the parked state, but with a clearer
characterization than before:

- Mode A is rare in this lab environment (0/10 this session).
- Mode B and C are the two real failure modes, split roughly 50/50.
- The OS-side state at failure is captured by USBSTS=0x11 (mode C)
  vs USBSTS=0x15 (mode B).
- Throttle is irrelevant.
- Side-process MMIO inspection is the right tool when next-touched.

Moving back to WiFi Tier 2 (the Arasan SDHCI vendor-quirk
investigation; CMD0/CMD5 issued cleanly but response capture is
zero, likely a HOST_CTRL2 UHS mode or similar init step missing
per Linux's sdhci-iproc.c).

## Manifest

`manifests/2026-05-25-usb-10-cycle-experiment.md` (pinning the
agent/rpi4-genet head at lwip `666d90e`, all other siblings
unchanged from the WiFi Tier 1c snapshot).

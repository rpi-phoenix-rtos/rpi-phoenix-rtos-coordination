# Open-TD review: fix difficulty + USB/WiFi/Eth cross-over (2026-05-29)

Driven by the TD-10 lesson — a "kernel hygiene" TD (SError masked) turned
out to be a major USB lead — this pass re-examines every still-open TD for
(a) fix difficulty and (b) whether it could be cross-connected to the USB /
WiFi / Ethernet bring-up problems. Code was checked, not just the doc;
several "open" items are already resolved in source ([STATUS STALE]).

## Status corrections (verified in code this session)

- **TD-17 (amap/ELF cacheable) & TD-18 (zone uncached): effectively
  RESOLVED in code.** `vm/zone.c:45` and `vm/amap.c:211,214` now use
  `MAP_NONE` (cacheable); no `TODO(TD-17/18)` markers remain. The armstub
  cache fix (`dde9bb5`) made the uncached workarounds unnecessary, and the
  current production image boots with these cacheable. ACTION: flip both to
  RESOLVED in the TD doc/checklist with the cache-enable SHAs. (The old
  cacheable→DMA-shadow USB theory was already refuted — a fresh
  `MAP_PHYSMEM` re-read still shows the sentinel.)
- **TD-15 phase 2/4 (DTB dma-ranges + reserved-memory): parsing LANDED.**
  `hal/aarch64/dtb.c` parses `/soc/dma-ranges` and `/reserved-memory`;
  `hal/aarch64/pmap.c` consumes reserved regions (marks `PAGE_OWNER_BOOT`).
  The checklist's "pending" is stale. Remaining phase-4 work is a
  driver-side `arm_to_bus` helper (not currently needed — RC_BAR2 identity
  is correct for the failing buffers).

## USB/WiFi/Eth cross-over — ranked

**Only two genuine cross-TD leads survive** (after the long list of
already-refuted USB hypotheses: cache-alias, static bridge config,
MAP_CONTIGUOUS, PA region, MMIO attr, timing/settle, retry/HCRST, MSI,
interrupt()-registration, bridge re-settle/mapping). The shortlist being
short is itself an honest finding.

1. **TD-10 — SError masked (USB: Likely, PROVEN).** Unmasking exposed a
   live external-abort SError firing during PCIe/USB bring-up
   (isolation-proven). Direct candidate for "events never post / inbound
   DMA writes don't land". Being actively worked; see
   `docs/notes/2026-05-29-usb-reanalysis.md` + memory `pi4-serror-pcie-source`.

2. **TD-15 phase 3 — VC/VPU + SCB/SLC fabric quiesce (USB: Possible→Likely;
   the best *hidden* lead).** The one mechanism consistent with every USB
   refutation: write-side, fabric-level, environment-dependent. usb-hcd
   drives at idle early boot; the only process that ever posted events
   (the lwip 'X' rig) ran with GENET DMA active. Hypothesis: the BCM2711
   SCB/SLC fabric only drains VL805 inbound writes to DDR when other fabric
   DMA is concurrently active and/or the VPU is still touching DRAM during
   early boot. TD-15 phase 3 (explicitly quiesce VC4 before handoff) is the
   planned work that would test this — and may be the **same fault as
   TD-10 seen from another angle** (the abort-raising access and the lost
   write could be one root cause in the RC/SCB inbound path).
   - Cheap experiment: defer usb-hcd bring-up until *after* GENET DMA is
     live, and observe whether (a) the TD-10 SError disappears and/or (b)
     enumeration succeeds. Yes → environmental/fabric (points at phase-3
     quiesce as the fix); no → fabric-activity ruled out, narrows back to
     TD-10's specific aborting access.
   - (weaker) TD-15 phase 4 dma-ranges: no `arm_to_bus` helper wired in,
     but RC_BAR2 identity is verified correct for the failing low-DRAM
     buffers; only bites buffers outside the verified window. Low priority.
     NB Codex's 3 GB-vs-4 GB `RC_BAR2`/`SCB*_SIZE` concern (see USB
     re-analysis note) is the inbound-decode angle worth a byte-for-byte
     compare against Linux's 3 GB model.

**WiFi cross-over = None** for every open TD: none touches the BCM43455
firmware-execution gate (hardware-bounded `WL_REG_ON`) or the SDIO PIO path
(no DMA-barrier class). **Eth cross-over = None**, except the inverse
curiosity that GENET-DMA-being-active is itself part of the USB puzzle
(TD-15 ph3).

## Per-item fix difficulty (the rest)

| TD | Difficulty | Cross-over | Plan |
|----|-----------|-----------|------|
| TD-19 TLBI/PTE hardening | Easy (validated) | None | keep; upstream-review |
| TD-06 DTB robustness | Medium | None | mostly overtaken by TD-15; add node-validation msgs |
| TD-07 QEMU refresh (Lima VM) | Easy | None | install QEMU 11.x in VM |
| TD-08 QEMU+gdb introspection | Medium (dep TD-07) | None — can't repro USB fabric wall | logic-comparison tool only |
| TD-Eth-LinkIRQ | Hard (hardware-bounded) | N/A | keep 1 Hz MDIO poll (matches Linux) |
| TD-13-spawn-cap | Medium | None | instrument prog-list head/tail; restore circular terminator; remove cap |
| TD-05 residual markers | Easy | None | gate behind compile-time macro |
| TD-14 sub-items | Easy each | None | restore canonical IPC/console paths once IPC latency rooted out |

## Bottom line

The next TD-10-style lead is **TD-15 phase 3 (VC/VPU + SCB/SLC quiesce)** —
pursue it jointly with the TD-10 SError localization (RC error-register
read over diag-udp), since they may be the same RC/SCB inbound-path fault.
The rest are genuine cleanups with no USB/WiFi/Eth coupling.

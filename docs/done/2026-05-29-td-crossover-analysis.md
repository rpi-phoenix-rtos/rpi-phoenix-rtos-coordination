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
   `docs/done/2026-05-29-usb-reanalysis.md` + memory `pi4-serror-pcie-source`.

2. **TD-15 phase 3 — VC/VPU + SCB/SLC fabric quiesce. DOWNGRADED 2026-05-29
   from "best hidden lead" to LOW probability for the USB issue.** The
   subagent ranked it high on the "concurrent fabric DMA enables inbound
   writes" theory, but three pieces of evidence now argue against it:
   - **The rig 'X' succeeds ~50% WITH HDMI/VC4 scanout fully active**
     (fbcon is up before the rig runs). So sustained VC4 fabric DMA
     demonstrably does NOT block VL805 inbound writes — if VC4 activity
     were the blocker, the rig could never enumerate.
   - **Experiment B (2026-05-29) retired the fabric-activity family:** a
     sustained GENET TX DMA flood confirmed active during the bring-up
     (14877 sends/0 fail) still gave `@idx -1`.
   - **Prior authoritative research** (RPi-engineer sources): the VC4
     SLC/L2 is NOT in the ARM-uncached / PCIe-inbound path; the A72 sees a
     flat SDRAM map, and Linux makes VL805 work with uncached rings — which
     is exactly what Phoenix does.

   TD-15 phase 3 remains valid work for its **original** purpose (TD-04-class
   memory hygiene: stop VC4 background writes from corrupting ARM DRAM; and
   the dma-ranges ARM↔BUS translation correctness), but it is **not** the
   USB inbound-write-loss root cause. The USB DMA root-cause is JTAG-gated
   (see `docs/done/jtag-day-usb-runbook.md`).
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

## Bottom line (revised 2026-05-29)

No open TD is a confirmed live lead for the USB inbound-write-loss. TD-10
(SError) localized the abort to PCIe/USB bring-up but the precise access is
JTAG-gated. TD-15 phase 3 (VC/VPU quiesce) was initially ranked highest but
is **downgraded to low probability** (the rig enumerates ~50% with VC4
scanout active; experiment B retired the fabric-activity family; VC4 SLC is
not in the PCIe path). The USB DMA root-cause is **JTAG-gated** — see
`docs/done/jtag-day-usb-runbook.md`. TD-15's other phases (memory hygiene,
dma-ranges translation) remain valid port-correctness work but are not the
USB blocker. The rest are genuine cleanups with no USB/WiFi/Eth coupling.

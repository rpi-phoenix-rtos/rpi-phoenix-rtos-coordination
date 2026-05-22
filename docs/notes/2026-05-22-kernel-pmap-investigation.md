# Kernel pmap investigation — USB-HCD 0xdead poison

Status: analysis (no fix proposed yet). Companion to
`2026-05-21-pcie-bridge-ageing-codex.md` which captured the
Codex consultation results.

## Confirmed facts

1. **Bridge HW state is intact** end-to-end through Phoenix's boot.
   `MISC_CPU_2_PCIE_MEM_WIN0_LO` reads back as `0xf8000000` (our
   programmed value) both before and after the VC firmware-notify.
   Confirmed via direct readback through the BCM2711 host bridge
   config window (PCIE_BCM2711_HOST_BASE = 0xfd500000).
2. **A 4 KiB PROT_READ mmap of the outbound CPU base
   (PCIE_BCM2711_OUTBOUND_CPU_BASE = 0x600000000) reads valid
   xhci capability data** (`0x01000020` = caplength 0x20, version
   0x0100) when done inside the pcie scan-probe callback.
3. **A 64 KiB PROT_READ|PROT_WRITE mmap of the same PA reads
   `0xdeaddead` poison** when done from xhci_init's xhci_map(),
   even after the scan callback returns and even with the host
   bridge mapping kept alive (`cfgio.destroy` skipped).
4. **The PCIe driver path is correct**: dev-0-only scan on the
   secondary bus, leaked cfgio, mailbox page-align, re-program
   `bcm2711SetOutboundWindow0` after firmware notify. None of
   these fix the poison; reads still come back 0xdead.
5. **Intermediate reads of the host-bridge config window between
   scan and xhci's first access can shift the failure** from
   rc=-19 (poison) to rc=-110 (xhci_reset times out — controller
   reachable but won't reset). But the shift is unstable
   (2/3 cycles, mixed rc=-19/-110), so it's not a workable
   workaround.

## What this rules out

- It's not a PCIe configuration bug (bridge state intact).
- It's not the second-mmap aliasing (single 64 KiB PROT_RW mmap
  also reads poison).
- It's not VL805 firmware not being loaded (firmware-notify
  succeeds after the mailbox page-align fix, and even without
  notify the boot-firmware-pre-loaded VL805 is responsive
  enough to confirm the small-mmap reads).

## What it points to

Phoenix's AArch64 pmap path for `MAP_DEVICE | MAP_PHYSMEM` of a
high-PA (above 4 GiB) MMIO region. Possible mechanisms ranked by
likelihood:

### 1. AArch64 pmap doesn't honor MAP_DEVICE Device-nGnRE for high PAs

The PTE format in `_pmap_writeTtl3` (hal/aarch64/pmap.c:435+):

    descr = DESCR_PA(pa) | DESCR_VALID | DESCR_TABLE | DESCR_AF | DESCR_ISH;
    ...
    case PGHD_DEV:
        descr |= DESCR_ATTR(MAIR_IDX_DEVICE);
        break;

`MAIR_IDX_DEVICE = 2`, and MAIR_EL1 byte 2 = `0x04` (Device-nGnRE).

`DESCR_PA(pa) = pa & ((1UL << 48) - (1UL << 12))` — bits 47:12.
For PA = `0x600000000`, this extracts the high bits correctly.

Looks correct on paper. But two ARM-specific subtleties to check:

- TCR_EL1's PS (Physical Address Size) bit-field must be wide
  enough to cover 0x600000000. Phoenix programs:

      ldr x0, =(TCR_EL1_VALUE)
      mrs x1, id_aa64mmfr0_el1
      and x1, x1, #0x7
      orr x0, x0, x1, lsl #32   ; PS = ID_AA64MMFR0.PARange

  ARM ARM defines PA_RANGE = `0x7 → 48 bits` on Cortex-A72. The
  ID_AA64MMFR0 query covers this. Should be OK.

- Cortex-A72 specifically: for MMIO outside the cluster's
  coherent DRAM range, the Memory Type field MUST be Device.
  If even a single bit of `descr` is interpreted as Normal
  (because of an attribute coding mistake), reads return UR
  (Unsupported Request) completions, which the BCM2711 root
  complex may surface as `0xdead`.

### 2. Phoenix's pmap_enter doesn't barrier between PTE write and TLB invalidate consistently

Looking at `_pmap_writeTtl3`:

    _pmap_cacheOpBeforeChange(oldDescr, descr, va, 3);
    hal_cpuDataSyncBarrier();
    if (oldDescr & DESCR_VALID) {
        pmap_common.scratch_tt[idx] = 0;
        pmap_tlbInval(va, asid);
    }
    pmap_common.scratch_tt[idx] = descr;
    hal_cpuDataSyncBarrier();
    _pmap_cacheOpAfterChange(descr, va, 3);

The TLB invalidate happens ONLY when oldDescr was valid (i.e.
re-mapping an existing entry). For a NEW mapping at a fresh VA,
no TLB invalidate runs. That's typically fine — the new PTE is
written and the next TLB walk will pick it up.

But: ARM ARM mandates DSB ISH + ISB after a PTE store that creates
a new mapping for the new entry to be visible to the CPU's
instruction-fetch path. The code does `hal_cpuDataSyncBarrier`
(DSB ISH on aarch64) but no ISB. This MIGHT leave the new entry
not visible to a subsequent load that hits it. Unlikely to cause
0xdead specifically (it would more likely cause a fault) but
worth checking.

### 3. BCM2711 outbound window translation TLB

The BCM2711 PCIe bridge may have an internal translation cache
for outbound (CPU → PCIe). Some chips invalidate this cache on
certain config-space writes; without a re-arm, subsequent reads
miss. Linux's brcmstb-pcie driver doesn't appear to special-case
this, but Linux also keeps the bridge driver running indefinitely
in kernel context — so it never observes a "cold" bridge.

The Codex Diag #2 outcome (WIN0_LO intact) suggests the bridge's
*program-visible* state isn't the issue, but an internal
non-program-visible TLB could still be the culprit. Verifying
this would need either a hardware-level probe or a power-cycle
test of the bridge's outbound-window behavior across delay
windows.

## Suggested next investigation steps

In rough order:

1. **Read VL805's BAR0 contents through a DIFFERENT VA mapping
   from the one xhci uses, just before xhci_capProbe runs.**
   If the alternate VA reads valid xhci data while xhci's own
   VA reads poison, the bug is per-VA in the kernel pmap
   (specifically PTE attribute mismatch). If both VAs read
   poison, the bridge translation has actually gone cold.

2. **Add explicit ISB after PTE store in `_pmap_writeTtl3`** —
   one-line change in `pmap.c`. Verify boot stability holds and
   xhci read behavior. Should be a no-op on most boards but
   helps disambiguate.

3. **Compare PHYSMEM-with-high-PA against PHYSMEM-with-low-PA**
   by reading some Pi 4 low-peripheral MMIO (e.g. pl011 at
   0xfe201000) through a 64 KiB PROT_RW mmap. If that works
   reliably, the bug is specific to PAs above 4 GB.

4. **Try MAP_UNCACHED (Device-nGnRnE) instead of MAP_DEVICE**.
   In the current pmap path `PGHD_NOT_CACHED | PGHD_DEV`
   selects `MAIR_IDX_S_ORDERED` which on aarch64 MAIR slot 3
   would need to be programmed correctly — verify it is.

5. **Codex consultation #3**: this analysis + the verified
   facts list, asking for narrowed hypotheses on ARM-side
   versus chip-side root cause.

## Out of scope

The investigation does not need a fix for the underlying issue
to be productive in the short term. USB-HCD is not on the boot
critical path (psh prompt comes up regardless), and the rest of
the system is stable at 5/5 cycles. The deep-dive is opportunistic.

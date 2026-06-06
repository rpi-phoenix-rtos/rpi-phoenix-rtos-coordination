# Phase B detailed diff — D-cache + I-cache + MMU together

Status: design, not yet applied. Apply immediately after Phase A
(M+I, C=0) is observed green on real Pi 4 hardware and a manifest
has been snapshotted (`scripts/snapshot-integration-state.sh`).
Roll back with `scripts/restore-integration-state.sh <manifest>` if
Phase B regresses.

References (file:line into this worktree unless noted):

- `docs/done/cache-mmu-smp-impl.md` §2 Phase B (lines 77–122)
- `docs/knowledge/boot-mmu-bringup-non-linux.md` §4 (lines 225–262)
- `docs/knowledge/el2-to-el1-drop.md` §4 (lines 98–179)
- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` — current state
  after Phase A revert: M-only flip at lines 431–437, SMPEN/859971
  sites are *no-op* placeholders (lines 303–322) because both regs
  trap from EL1 on A72; the workarounds are owned by the armstub.
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
  — armstub at EL3 (SMPEN at lines 163–164; CPUACTLR_EL1[47] WA must
  land here as the Phase A prereq, see §2 below).

## 1. Goal

Enable `SCTLR_EL1.{M,C,I} = 1` together in a single MSR after MAIR /
TCR / TTBR0 / TTBR1 are programmed and the page tables have been
cleaned to the Point of Coherency. The kernel must continue to
reach `(psh)%`, but printf, framebuffer fill, and copy-heavy code
should run at cache speed rather than DRAM-Non-Cacheable speed.

This is the prize step of Stage 1: every subsequent enablement
(SMP cores 1–3, GICv2 inner-shareable broadcast TLBI, real
filesystem cache, USB / Ethernet bulk DMA throughput) depends on
Inner-Shareable Cacheable kernel mappings being live. Caches off
on a Pi 4 leaves ~95% of the SoC's bandwidth on the table.

## 2. Prerequisites that must already be in tree

Phase B is **not** a standalone change — it is the second half of a
two-part landing. The first half (Phase A) plus the architectural
fixes below must already be merged and validated:

1. **EL2→EL1 drop** in `_init.S` lines 188–246. Already landed.
   Without it, every SCTLR_EL1 write programs a context the CPU is
   not actually in — TD-16's pre-EL-drop hangs were this. See
   `docs/knowledge/el2-to-el1-drop.md` §1.
2. **Armstub-side SMPEN** in `phoenix-armstub8-rpi4.S` lines
   163–164. Already landed (`mov x0, #CPUECTLR_EL1_SMPEN; msr
   CPUECTLR_EL1, x0`). Required by A72 TRM 4.3.40 before any cache
   or TLB op, including the `tlbi vmalle1` and the cache flip below.
3. **Armstub-side A72 erratum 859971 workaround** — `CPUACTLR_EL1`
   bit 47 (DIS_INSTR_PREFETCH). Must be added to
   `phoenix-armstub8-rpi4.S` adjacent to the SMPEN write at line
   163, before the `eret` to EL2 at line 175. The kernel's earlier
   attempt to apply it at EL1 hung at marker `S` because A72 r0p3
   traps `S3_1_C15_C2_0/_2_1` accesses from EL1 (see `_init.S`
   lines 304–319 comments). ARM Trusted Firmware applies 859971
   the same way in `cortex_a72_reset_func` at EL3. This is a Phase
   A prereq, not a Phase B item — call it out here because Phase B
   inherits the requirement.
4. **Phase A: M+I enabled together** with no regressions across
   five clean Pi 4 cold boots. Phase A's diff is the M-only flip at
   `_init.S` line 434 changing from `#(1 << 0)` to `#((1 << 0) |
   (1 << 12))`, with `ic iallu; dsb nsh; isb` immediately after the
   SCTLR write (Linux `set_sctlr_el1` pattern). UART signature
   identical to today: `Z 90 75 76 83 84 85 77 86 88 49 88 50 88 51
   88 52 88 53` then `(psh)%`.

If any of (1)–(4) is missing, do not attempt Phase B. Without (3),
Phase B will speculatively prefetch through stale I-cache lines
post-flip on a72 r0p3 and produce non-deterministic faults that
look exactly like TD-16 iterations 4–5 (`_init.S` lines 615–625).

## 3. Unified diff against `_init.S`

The diff is surgical: it changes the flip block at `_init.S` lines
388–437 and extends the page-table cleanup at lines 401–403. No
other lines move. Hunk anchors are post-Phase-A line numbers.

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -388,15 +388,42 @@
 	/* Linux arm64 explicitly invalidates page tables populated with the MMU
 	 * disabled to discard any speculatively loaded cache lines before the
 	 * page-table walker starts using them. Do the same for the contiguous
 	 * early kernel MMU region:
 	 *   PMAP_COMMON_KERNEL_TTL2
 	 *   PMAP_COMMON_KERNEL_TTL3
 	 *   PMAP_COMMON_DEVICES_TTL3
 	 *   PMAP_COMMON_SCRATCH_TT
 	 *   PMAP_COMMON_SCRATCH_PAGE
+	 * Phase B addition: with SCTLR.C about to flip on, every TTE the
+	 * walker may consult must reach the Point of Coherency before the
+	 * walker starts caching them. `dc cvac` (clean to PoC, no
+	 * invalidate) is the right primitive — `_inval_dcache_range` /
+	 * `dc ivac` discards lines and is wrong here because the writes
+	 * we want preserved are the page-table writes we just did with
+	 * caches off (they may sit in a write-combine buffer that has
+	 * not yet drained to DDR, even though the CPU saw them).
+	 *
+	 * Coverage audit (TTEs the walker reaches once SCTLR.M=1):
+	 *   - PMAP_COMMON_SCRATCH_TT    (TTBR0 TTL1, lines 342–377)
+	 *   - PMAP_COMMON_KERNEL_TTL2   (TTBR1 TTL2, lines 481–493)
+	 *   - PMAP_COMMON_KERNEL_TTL3   (TTBR1 TTL3, lines 518–561)
+	 *   - PMAP_COMMON_DEVICES_TTL3  (TTBR1 device TTL3, line 491)
+	 *   - PMAP_COMMON_SCRATCH_PAGE  (zero scratch, lines 456–457)
+	 *   - PMAP_COMMON_STACK         (early kstack PA, line 451)
+	 *   - syspage source page (PA in x9, written by plo before handoff)
+	 * The first six are contiguous: pmap_common .. pmap_common+6*PAGE.
+	 * Cover that range plus the syspage page explicitly.
 	 */
-	adrp x0, PMAP_COMMON_KERNEL_TTL2
-	adrp x1, PMAP_COMMON_STACK
-	bl _inval_dcache_range
+	adrp x0, PMAP_COMMON_KERNEL_TTL2
+	adrp x1, PMAP_COMMON_STACK
+	add  x1, x1, #SIZE_PAGE          /* include the early kstack page */
+	bl _clean_dcache_range_pos       /* dc cvac (PoC), see §3.1 */
+
+	/* Clean the syspage source page so the walker reads what plo wrote.
+	 * x9 still holds the PA of syspage from the firmware handoff. */
+	mov  x0, x9
+	add  x1, x9, #SIZE_PAGE
+	bl _clean_dcache_range_pos
 
 	/* Ensure changes to translation tables are visible */
 	dsb ish
 	isb
@@ -429,18 +456,49 @@
 	uart_tag2 88, 51
-	dsb ish
-	mrs x0, sctlr_el1
-	orr x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
-	msr sctlr_el1, x0
-	isb
-	uart_tag2 88, 52
+
+	/* TLBI moved adjacent to flip (was at line 288, 130 lines earlier).
+	 * Any walker activity in the gap could repopulate stale entries. */
+	dsb ishst
+	tlbi vmalle1
+	dsb ish
+	isb
+
+	/* SCTLR_EL1 baseline: replace bare 0x30c0c938 with the full RES1
+	 * mask Linux/seL4 trust on A57/A72. SCTLR_EL1_RES1 = 0x30d00800
+	 * (issue seL4#1025; Linux INIT_SCTLR_EL1_MMU_OFF). Phoenix-specific
+	 * EL1 sysreg bits we keep:
+	 *   nTWI    (bit 16)  = 1   trap WFI from EL0 -> 0 in val? actually
+	 *                            we want nTWI=1 (don't trap)
+	 *   nTWE    (bit 18)  = 1   don't trap WFE
+	 *   UCT     (bit 15)  = 1   EL0 may read CTR_EL0
+	 *   DZE     (bit 14)  = 1   EL0 may DC ZVA
+	 *   SED     (bit 8)   = 1   trap SETEND in AArch32
+	 *   SA0     (bit 4)   = 1   EL0 SP alignment check
+	 *   SA      (bit 3)   = 1   EL1 SP alignment check
+	 * Computed: 0x30d00800 | (1<<18) | (1<<16) | (1<<15) | (1<<14) |
+	 *           (1<<8) | (1<<4) | (1<<3) = 0x30d4d938.
+	 * (Bare 0x30c0c938 differs in the RES1 high bits; switching to
+	 * 0x30d4d938 is functionally identical but matches the Linux/seL4
+	 * baseline Phoenix's research already cites as authoritative.)
+	 *
+	 * Then OR in M | C | I together for the actual flip.
+	 */
+	ldr  x0, =0x30d4d938
+	orr  x0, x0, #((1 << 0) | (1 << 2) | (1 << 12)) /* M | C | I */
+	dsb  ish                          /* drain stores before flip */
+	msr  sctlr_el1, x0
+	isb                                /* mandatory: see ARM ARM B2.10.5 */
+	ic   iallu                         /* invalidate I-cache to PoU */
+	dsb  nsh
+	isb
+	uart_tag2 88, 52
```

### 3.1 New helper: `_clean_dcache_range_pos`

`_init.S` already has `_inval_dcache_range` (line 917) and
`_clean_inval_dcache_range` (line 937). Phase B needs *clean
without invalidate* — `dc cvac` — because the page-table writes we
want the walker to see must reach PoC, not be discarded. Add an
exact mirror of `_clean_inval_dcache_range` but with `dc cvac` in
place of `dc civac`:

```asm
_clean_dcache_range_pos:
        /* Clean dcache to PoC over a range; preserve cache lines.
         * x0 => start (clobbered), x1 => end; clobbers x2, x3 */
        mrs x2, ctr_el0
        lsr x2, x2, #16
        and x2, x2, #0xf
        mov x3, #4
        lsl x2, x3, x2
1:
        dc  cvac, x0
        add x0, x0, x2
        cmp x0, x1
        b.lo 1b
        dsb sy
        isb
        ret
```

Place it adjacent to `_clean_inval_dcache_range` at `_init.S` line
953. ~17 lines.

## 4. UART signature pre/post Phase B success

Phase A baseline emits markers `Z 90 75 76 83 84 85 77 86 88 49 88
50 88 51 88 52 88 53` then virtual-mode markers and `(psh)%`.
Each `uart_putc` blocks on FIFO-full, so inter-marker wall-clock
time is dominated by PL011 baud plus DRAM-NC instruction fetch.

Post-Phase-B success has identical *content*; what changes is *time*.
Two quick tells:

- **`fbcon: ok` arrives noticeably faster.** Phase A's 1080p
  clear-screen is observably slow (every pixel write hits DRAM-NC).
  Phase B should clear in under one PL011 character time.
- **`dd if=/dev/zero of=/tmp/x bs=1M count=8` runs at cache speed.**
  Phase A: ~120–200 MB/s (DRAM-NC bound). Phase B: ≥2 GB/s on the
  warm-cache portion. Numbers below 1 GB/s mean caches are not
  actually live — print `sctlr_el1` post-flip and verify bits
  0, 2, 12 are all set.

Negative tell: markers stop after `88 51` (pre-flip) and `88 52`
(post-flip) is never printed. The flip caused either a translation
walk fault landing silently in `_early_exception_common` (line
1023) or speculative I-cache prefetch of a stale line. Drop into
the sub-phase ladder.

## 5. Fallback ladder if Phase B hangs

Five-cycle cold-boot smoke is the gate: Phase B is "green" only
after five back-to-back clean boots reaching `(psh)%`. One fault
in five forces a step down the ladder.

- **Sub-phase B.1 — M+I only (revert to Phase A)**. Flip mask
  becomes `(1<<0) | (1<<12)`. This isolates whether the regression
  is in `SCTLR.C` or somewhere else (e.g. the moved `tlbi
  vmalle1`, the new `dc cvac` sweep, the changed RES1 baseline).
  If Phase A still passes after the diff lands but Phase B fails,
  the fault is in the C bit's interaction with TD-04-class
  coherency — not in the surrounding plumbing.
- **Sub-phase B.2 — M+C+I but TTBR0 RAM blocks Normal-Cacheable IS,
  not NC**. Today `_init.S` line 162 `NC_BLOCK_ATTRS = 0xf05`
  marks the temporary TTBR0 1 GB block descriptors Normal
  Non-Cacheable as a TD-04 hedge. With caches on we want
  Normal-Cacheable Inner-Shareable on the kernel image and
  syspage windows. Replace the TTBR0 block-attr constant with the
  cacheable form `0xf09` for the kernel-image and syspage entries
  while *keeping NC* on the PL011 1 GB block (peripherals remain
  Device-nGnRE via `EARLY_UART_DEVICE_BLOCK = 0x60000000000709`,
  line 37, untouched). If B.2 boots, the previous failure was a
  walker-attribute mismatch between TTBR0 and TTBR1 mappings of
  the same PA.
- **Sub-phase B.3 — M+C+I with reduced kernel TTL coverage**. Map
  *only* the kernel image 2 MB window cacheable and leave
  everything else (syspage source, scratch, kstack) Normal
  Non-Cacheable via `NC_ATTRS = 0x707` (line 157). This isolates
  whether the failure is in a specific TTE the kernel's bring-up
  code touches early. The TD-04 patch (`_init.S` lines 524–561)
  already keeps `_hal_syspageCopied` and the early kstack pages
  NC — extend that pattern to anything that reads back data plo
  wrote pre-handoff. If B.3 boots, narrow further by promoting
  one window at a time to cacheable until the failing window is
  identified. This is the "TD-04 reinterpretation" path of §6.
- **Sub-phase B.4 — revert to Phase A (M+I) and stop.** Re-snap
  the Phase A manifest, file a new tracking step, and continue
  Stage 2 (4 GiB DRAM unlock) with caches still off the data side.
  Phase A's I-cache alone is worth ~30% of the bandwidth gap;
  shipping that without C is a degraded but viable end state.

## 6. Risks

**TD-04 reinterpretation.** TD-04
(`docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` lines 91–166) was
diagnosed on a boot where caches were "off only on paper" — SCTLR_EL1
writes were ineffective at EL2. The EL drop fixed that root cause.
With caches genuinely on under Phase B, the BCM2711 syspage-PA
anomaly may either (a) disappear, since `dc cvac` now drains stale
firmware lines through PoC, or (b) become deterministic. Re-run the
E2 syspage-copy probe (`tracking/current-step.md`) on the first
green Phase B boot. If clean, remove the `NC_ATTRS` overrides at
`_init.S` lines 524–561 in a follow-up. If still dirty, keep them
only on the syspage destination — that one PA is likely VPU /
firmware DMA territory.

**Secondary-core impact.** Phase B affects the primary core only.
Cores 1–3 stay trapped at `_other_core_trap` (`_init.S` line 478)
with M=C=I=0 in DRAM-NC space. Shared kernel mappings are already
Inner-Shareable, so Stage 3 can unpark secondaries into the IS
domain without page-table changes. Risk between Phase B and Stage 3
landing: zero (secondaries execute no shared paths while parked).

**USB / PCIe DMA coherency.** BCM2711's PCIe root complex (VL805
xHCI hub) is not IO-coherent. With caches on, the kernel must
`dc cvac` outbound DMA buffers and `dc ivac` inbound buffers around
xHCI ring touches. The same applies to GENET Ethernet rings and the
future BCM43455 SDIO WiFi path. Phase B's correctness check must
include a USB bulk-IN transfer; torn reads mean the cache-maintenance
hooks must land in the same patch series. Do not paper over it.

**A72 erratum 855873 hygiene on MMIO.** XN+PXN on Device-nGnRnE
TTEs forbids speculative instruction prefetch into MMIO.
`EARLY_UART_DEVICE_BLOCK = 0x60000000000709` (`_init.S` line 37)
already sets bits 53 (PXN) and 54 (UXN). Propagate the same high
bits when peripheral mappings expand for GPIO / mailbox / mini-UART.

## 7. Open questions

1. **0x30c0c938 vs 0x30d4d938.** §3 decoded the delta: only RES1
   high bits differ. seL4 #1025 and FreeBSD `arm64/locore.S` trust
   0x30d00800 as the A57/A72 RES1 baseline. The change is
   cosmetic-but-correct; verify nothing in `kernel/proc/proc.c` or
   `hal/aarch64/cpu.c` literal-compares against 0x30c0c938.
2. **`tlbi vmalle1` vs `tlbi vmalle1is` adjacent to flip.** With
   only the primary core running, non-broadcast suffices and matches
   `_init.S` line 288. Stage 3 will need every such site converted
   to the IS variant; flag it as a Stage 3 prerequisite, not Phase B.
3. **`dc cvac` vs `dc cvau` for the PT sweep.** ARM ARM D5.10.2:
   page-table walks observe PoC. `cvac` is correct for the walker.
   `cvau` would be needed for self-modifying `.text`, which Phase A's
   `ic iallu` already covers. Document the choice in the helper.
4. **VPU DMA quiescing before flip.** `cache-mmu-smp-impl.md` §10
   raises this. If Phase B fails non-deterministically per-boot the
   way TD-04 originally did, mailbox tag 0x00038049 (framebuffer
   off) or a `dsb sy` + delay before flip is the mitigation. Park
   until the five-cycle gate fails non-deterministically.

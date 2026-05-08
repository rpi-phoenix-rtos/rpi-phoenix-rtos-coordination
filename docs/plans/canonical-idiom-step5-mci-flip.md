# Canonical-idiom Step 5 — single SCTLR `M|C|I` flip with full barrier ritual

Status: ready-to-apply unified diff. Apply only after Steps 1–4 of the
round-3 plan have landed and produced clean five-cycle cold-boot UART
runs (see `docs/research/round3-cache-enable-synthesis.md` §5, lines
176–217). This document supersedes the earlier draft at
`docs/plans/phase-b-detailed-diff.md` (lines 79–174); the round-3
synthesis revised the SCTLR-baseline strategy and the barrier ritual
recipe. Where this doc and the earlier draft disagree, this doc wins.

## 1. Purpose

Replace the staged Stage-1 cache-enable (currently `M=1, C=0, I=0`,
landed by TD-04 at `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
lines 471–476) with a single MSR setting `SCTLR_EL1.{M, C, I} = 1`
together, surrounded by the canonical barrier ritual every Phoenix
A-class port and every BSD on Pi 4 uses.

The round-3 audit (`docs/research/round3-phoenix-port-conventions-audit.md`
table at synthesis §3.1, lines 99–106) shows that Phoenix's own
canonical idiom — observed in `imx6ull` at
`sources/phoenix-rtos-kernel/hal/armv7a/imx6ull/_init.S` lines 519–555,
and at EL3 in plo at `sources/plo/hal/aarch64/mmu.c` lines 73–82 — is:
invalidate everything, drain, flip M+C+I together in one MSR, ISB,
re-invalidate I-cache, drain. Staged flips are not part of the canonical
pattern for any working Phoenix A-class port, and our staged Phase A and
Phase B attempts both regressed (synthesis §3.3, lines 125–132).

## 2. Prerequisites (must already be in tree)

Step 5 is the last of five canonical-idiom steps. Each of the prior four
must already have produced a green five-cycle cold boot:

1. **Step 1** — `hal_cpuInvalDataCacheAll` call at kernel `_start`,
   matching the imx6ull pattern at
   `sources/phoenix-rtos-kernel/hal/armv7a/imx6ull/_init.S` lines 529–545.
2. **Step 2** — plo `hal_cpuJump` flush range extended to full DDR.
3. **Step 3** — plo running with MMU+caches ON (matching zynqmp).
4. **Step 4** — kernel `_init.S` switched to broadcast Inner-Shareable
   variants (`tlbi vmalle1is`, `ic ialluis`, `dsb ish`).

If any of (1)–(4) is missing, do not apply Step 5. The whole point of
the canonical-idiom plan is that Step 5 only succeeds with the
preconditions in place; applying it standalone reproduces the Phase B
failure (synthesis §1, lines 31–44).

## 3. Diff target (current state in tree)

The block to replace is at
`sources/phoenix-rtos-kernel/hal/aarch64/_init.S` lines 471–477. After
TD-04 landed and Step 1 of the round-3 plan added the `dc isw` call at
`_start`, the current flip block looks like (lines 470–477):

```asm
	uart_tag2 88, 51
	dsb ish
	mrs x0, sctlr_el1
	orr x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
	msr sctlr_el1, x0
	isb
	uart_tag2 88, 52
```

The `uart_tag2 88, 51` immediately above (line 471) is the post-TLBI
pre-flip marker; `uart_tag2 88, 52` (line 477) is the post-flip marker.
Both are kept.

## 4. Unified diff

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -451,30 +451,73 @@
 	/* Ensure changes to translation tables are visible */
 	dsb ish
 	isb
 
-	/* Turn on MMU. Caches stay OFF (C=0, I=0) — known-good baseline.
-	 *
-	 * Stage 1 cache enable was attempted in many variants; all fail:
-	 *   - Phase A (M+I): boot reaches X4/X5/td15/td16, faults at
-	 *     syspage.c:476 (hal_syspageRelocate). RES1 baseline didn't
-	 *     change it. armstub-side 859971 didn't change it.
-	 *   - Phase B (M+C+I): boot hangs at X3 (before X4 marker) —
-	 *     immediate post-flip stall, classic "stale D-cache shadows
-	 *     page tables" failure mode.
-	 *
-	 * Conclusion: we are missing something fundamental. Spawned a
-	 * deep-research wave (FreeBSD/NetBSD/seL4/ARM ARM authoritative
-	 * spec, plo->kernel handoff cache maintenance, BCM2711 firmware
-	 * state, diagnostic technique survey). Reverting to known-good
-	 * (EL drop + M-only) until research wave returns with concrete
-	 * direction.
+	/* Stage 1 cache enable: M | C | I in a single SCTLR_EL1 write.
+	 *
+	 * This is the canonical Phoenix A-class idiom, mirrored in:
+	 *   - hal/armv7a/imx6ull/_init.S:519-557 (D+I cache + MMU together)
+	 *   - plo/hal/aarch64/mmu.c:73-82       (M|C|I OR-set in one MSR)
+	 * and matches FreeBSD arm64/locore.S, NetBSD aarch64 locore.S,
+	 * seL4 src/arch/arm/64/machine/hardware.c.
+	 *
+	 * Steps 1-4 of the canonical-idiom plan have landed by the time we
+	 * reach this point:
+	 *   - kernel _start invoked hal_cpuInvalDataCacheAll (full set/way
+	 *     walk via CLIDR_EL1) before any speculative D-cache fill could
+	 *     populate stale lines (Step 1).
+	 *   - plo flushed the entire DDR + OCRAM range to PoC before the
+	 *     handoff (Step 2).
+	 *   - plo itself ran with caches+MMU ON, so its writes to the
+	 *     syspage and kernel image went through the D-cache and the
+	 *     Step-2 flush is load-bearing (Step 3).
+	 *   - all TLB/I-cache maintenance below uses Inner-Shareable
+	 *     broadcast variants (Step 4).
+	 *
+	 * Barrier ritual (ARM ARM B2.10.5 + Cortex-A72 TRM 6.4):
+	 *   ic  ialluis    invalidate I-cache to PoU, broadcast IS
+	 *   dsb ish        wait for IC + every prior store to drain to
+	 *                  the Inner-Shareable observer
+	 *   tlbi vmalle1is invalidate every EL1 TLB entry, broadcast IS
+	 *   dsb ish        wait for TLBI completion
+	 *   isb            re-fetch the instruction stream after
+	 *                  invalidation (any prefetched-but-not-yet-issued
+	 *                  instructions are discarded)
+	 *   <SCTLR write>  M|C|I together — the moment of truth
+	 *   isb            mandatory: flushes the pipeline so the next
+	 *                  fetch happens with caches+MMU live
+	 *   ic  ialluis    second IC invalidate AFTER the flip — discards
+	 *                  any speculatively prefetched lines that the
+	 *                  pre-flip MMU-off (or VA-mismatched) state may
+	 *                  have populated; without this, A72 r0p3 has been
+	 *                  observed to execute stale .text post-flip
+	 *   dsb ish        wait for the post-flip IC
+	 *   isb            second ISB: by the time we fall through, the
+	 *                  next instruction is fetched through Normal
+	 *                  Cacheable Inner-Shareable mappings
+	 *
+	 * SCTLR baseline note: this revision keeps the bare 0x30c0c938 OR
+	 * model from the existing code. Switching to the BSD-style
+	 * 0x30d4d938 RES1 baseline regressed even the M-only boot (synthesis
+	 * §4.1, lines 138–150) and is deferred to the round-3 Step 6.
 	 */
 	uart_tag2 88, 51
+
+	/* Pre-flip canonical fence: drain everything before the flip. */
+	ic   ialluis
 	dsb  ish
+	tlbi vmalle1is
+	dsb  ish
+	isb
+
+	/* Single SCTLR_EL1 write: M | C | I together. */
 	mrs  x0, sctlr_el1
-	orr  x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
+	orr  x0, x0, #(1 << 0)    /* SCTLR_EL1.M  — MMU enable */
+	orr  x0, x0, #(1 << 2)    /* SCTLR_EL1.C  — D-cache + unified-cache enable */
+	orr  x0, x0, #(1 << 12)   /* SCTLR_EL1.I  — I-cache enable */
 	msr  sctlr_el1, x0
 	isb
+
+	/* Post-flip canonical fence: discard any stale prefetches that the
+	 * pre-flip address-translation regime may have populated. */
+	ic   ialluis
+	dsb  ish
+	isb
 	uart_tag2 88, 52
```

## 5. Why each instruction matters

The block is mechanical but every line is load-bearing; an incorrect
omission will reproduce the Phase B hang at marker `88 51`. The
breakdown below references the ARM ARM (DDI 0487) and the Cortex-A72
TRM (DDI 0488).

- **`ic ialluis` (pre-flip).** Invalidates all I-cache lines to the
  Point of Unification across every PE in the Inner-Shareable domain.
  This is the broadcast variant chosen in Step 4. Done before the flip
  so any I-cache line populated under the M=0 regime — typically the
  identity-mapped `_start` prologue — cannot survive into the M=1
  regime. ARM ARM D5.10.2 requires this when the IPA→PA translation
  model changes.
- **`dsb ish` (post-IC).** Inner-Shareable Data Synchronisation
  Barrier. Waits for the I-cache invalidate **and** every prior store
  (notably the `dc isw` walk from Step 1 and the `dc cvac` page-table
  cleans from earlier in `_init.S`) to be observable to every PE in
  the Inner-Shareable domain. Cortex-A72 TRM 6.4.1 calls this
  "completion of memory effects".
- **`tlbi vmalle1is` (pre-flip).** Invalidates every EL1 TLB entry on
  every PE in the IS domain. Even though we are still single-core,
  the broadcast variant is correct (Step 4) and it costs the same on
  a72 r0p3. Required because the temporary identity-mapped TTBR0
  built earlier (`_init.S` lines 401–423) and the kernel TTBR1 built
  at lines 426–430 have both been written with caches off — the TLB
  may already hold stale entries from the firmware's translation
  regime if BCM2711 ever loaded TTBR_EL3 in a prior boot stage.
- **`dsb ish` (post-TLBI).** Waits for the TLBI broadcast to complete
  on every PE. ARM ARM D5.10.2: "TLB invalidate operations are not
  required to be complete until a DSB is executed".
- **`isb` (pre-flip).** Instruction Synchronisation Barrier. Discards
  every instruction speculatively fetched under the pre-flip TLB and
  cache regime. Without this, the next four MSR/ORR instructions
  could execute against a mix of pre- and post-invalidate state.
- **`mrs x0, sctlr_el1` + 3 × `orr` + `msr sctlr_el1, x0`.** The
  flip itself. OR-set, never re-write the whole register, so any
  RES1 bits the firmware or armstub set stay set. M, C, I are
  bits 0, 2, 12 (ARM ARM D13.2.118).
- **`isb` (post-flip).** ARM ARM B2.10.5: "the effect of a write to
  any system register that affects a generic interrupt, exception,
  or data-translation behaviour is not architecturally guaranteed to
  be observed by any instruction in program order until the next
  Context-synchronization event". The next instruction must execute
  with caches+MMU live, so the ISB is mandatory.
- **`ic ialluis` (post-flip).** A72 r0p3-specific safety net. Between
  the pre-flip `ic ialluis` and the post-flip `isb`, the PE may have
  speculatively prefetched instructions through the still-incompletely-
  invalidated I-cache; some of those prefetches can survive the ISB
  (they sit in the rename queue rather than the I-cache). Re-invalidate
  after the flip to discard them. seL4 and FreeBSD both do this.
- **`dsb ish` + `isb` (final).** Drain the post-flip IC and force the
  pipeline to re-fetch the very next instruction (`uart_tag2 88, 52`)
  through the now-cacheable inner-shareable kernel mappings. From
  this point on, all execution is at cache speed.

## 6. UART signature

Pre-Step-5 baseline (today, with M-only flip): `Z 90 75 76 83 84 85 77
86 88 49 88 50 88 51 88 52 88 53` then virtual-mode markers and
`(psh)%`.

Post-Step-5 success: identical sequence, with two observable speed
deltas (synthesis §9, lines 288–298):

- `fbcon: ok` arrives in real time, not glyph-by-glyph.
- The wall-clock interval between `88 52` and the next high-marker
  shrinks visibly because every instruction fetch and every printf
  store now hits cache rather than DRAM-NC.

Failure tell: marker stream stops at `88 51` and `88 52` is never
emitted. This indicates either a translation walk fault landing
silently in `_early_exception_common` (`_init.S:1023`) or a stale
I-cache line surviving the flip. Drop into the fallback ladder
documented in `docs/plans/phase-b-detailed-diff.md` §5 (lines 232–272)
— that ladder predates the round-3 synthesis but its sub-phases (B.1
through B.4) remain valid bisection points.

## 7. What this diff explicitly does NOT change

- **SCTLR baseline**. Round-3 §4.1 (lines 138–150) marks the
  `0x30d4d938` RES1 baseline as a separate Step 6 to be retried only
  after Step 5 succeeds. The earlier `phase-b-detailed-diff.md`
  attempted both at once (lines 144–166) and produced a regression on
  even M-only boot.
- **TLBI scope**. Step 4 already converted to `vmalle1is`; this diff
  uses that variant but does not introduce it.
- **Page-table cleaning**. The `dc cvac` sweep proposed in
  `phase-b-detailed-diff.md` §3.1 (lines 176–205) is now redundant:
  Step 2 (plo full-DDR flush) plus Step 1 (kernel set/way invalidate)
  cover the same surface area at the canonical handoff boundary.
- **Armstub or EL3 changes**. The 859971 + SMPEN at EL3 stays
  exactly as it is.

## 8. Acceptance criteria

Step 5 is "green" when all four hold across five consecutive cold
boots (synthesis §9, lines 288–298):

1. Pi 4 reaches `(psh)%` with `SCTLR_EL1.{M,C,I}` all readable as 1
   from the prompt.
2. HDMI text renders in real time.
3. `dd if=/dev/zero of=/tmp/x bs=1M count=8` runs at multi-MB/s.
4. Three consecutive cold boots all clean.

Once green, snapshot a manifest with
`scripts/snapshot-integration-state.sh`, then proceed to round-3
Step 6 (RES1 baseline retry) and Step 7 (diagnostic instrumentation
landing for future debugging).

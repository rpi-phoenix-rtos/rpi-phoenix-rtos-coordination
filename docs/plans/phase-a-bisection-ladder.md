# Phase A bisection ladder — what to try after armstub-side EL3 workarounds land

Audience: the agent picking up Stage 1 cache enable after the parallel
armstub track has merged its EL3-side fixes (CPUECTLR_EL1.SMPEN re-assert
defensively, CPUACTLR_EL1[47] DIS_INSTR_PREFETCH for A72 erratum 859971).
This document does not assume those landings fix the boot — it assumes
they unblock the trap that Phase A v3 hit at `uart_putc 83`, after which
the *next* failure surface is whatever remains.

References used while drafting:

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S` (current state, post EL drop, M-only enable at line 434)
- `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/research/boot-mmu-bringup-non-linux.md` §3–§4
- `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/plans/cache-mmu-smp-impl.md` Stage 1
- Phase A failure logs in `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/`:
  - `rpi4b-uart-20260506-205625-netboot-stage1-phaseA-icache.log` — v1 reaches `(psh)%`
  - `rpi4b-uart-20260506-211839-netboot-phaseA-v2-postflip-iciallu-r2.log` — v2 walks syspage maps then resets right after the `P` marker (entering program-reloc block)
  - `rpi4b-uart-20260506-212803-netboot-phaseA-v3-smpen-erratum859971.log` — v3 hangs at `S` (SMPEN trap)

## 0. Re-reading the v2 failure signature

The prompt named v2 r2 fault PCs as `c0027090..c0027550`. Those are not
exception PCs — they are syspage entry pointers printed by the walk in
`sources/phoenix-rtos-kernel/syspage.c:430-460` (`h{...}`, `R{...}`,
`T{...}`, `O{...}` are low-32-bit dumps; with `VADDR_KERNEL =
0xffffffffc0000000` they land at `c00270xx`).

What v2 r2 actually shows: the entry-loop and map-loop complete
(`...kllmn`), `*uart = 'P'` prints (`syspage.c:473`), then the boot
resets — fault is on the *very next* statement,
`syspage_common.syspage->progs = hal_syspageRelocate(...)` at
`syspage.c:476`. With M-only the same code reaches `(psh)%`; with M+I
it faults. So the blocker is deterministic, sitting between the `P`
marker and the `p` marker, with I=1 and the syspage page mapped Normal
Non-Cacheable (TD-04 `NC_ATTRS`, `_init.S:157`).

## 1. Hypotheses, ranked

### H1 — Even with armstub-applied 859971 + SMPEN, Phase A still faults at the same point

**Rationale.** The armstub fix is necessary but not sufficient. SMPEN
unblocks broadcast TLBI / cache-coherency participation; CPUACTLR_EL1[47]
mitigates speculative I-fetch into stale lines. Neither addresses an
I-cache line fetched *before* the M+I flip. Phase A v2 already inserted
post-flip `ic iallu; dsb nsh; isb` and still failed; armstub fixes alone
may just re-expose the v2 fault.

**Predicted UART signature on retry.** Identical to v2 r2:
`X1 X2 X3 X4 X5 N!YOPSTUZbcd td15:OK td16:cf=... eF123GHIJK s{...} ...
B{...} T{...} O{...} h{...}ij R{...}klh{...} ... klmn P` then reset.
No EX=/ESR=/ELR=/FAR= dump (the early exception vector is installed
at `_init.S:381`, but a watchdog reset before vector activation, or a
loop in the exception handler chain itself, would silence it).

**Next workaround.** Insert a `dsb sy; ic iallu; isb` immediately
*before* the M+I flip (in addition to the post-flip invalidate). The
hypothesis is that any I-cache lines populated speculatively while
SCTLR.I=0 — A72 does prefetch with I=0 — survive the M-only baseline
silently because they are never used for execution, but become live
the moment I=1 turns on. Pre-flip invalidate is what Linux's
`__primary_switch` does in `arch/arm64/kernel/head.S` (see
`init_kernel_el` and `__cpu_setup`).

**Diff.**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -429,8 +429,15 @@ el1_entry:
 	 * For now the kernel boots cleanly with EL drop + M-only and
 	 * reaches (psh)% — that's the validated baseline.
 	 */
 	uart_tag2 88, 51
-	dsb ish
+	/* H1: pre-flip I-cache invalidate. Any speculative I-fetch that
+	 * happened with SCTLR.I=0 may have populated I-cache lines from
+	 * stale DRAM (caches off => DDR direct, but other masters or
+	 * prior firmware may have left dirty lines that the A72
+	 * speculator pulled in). Invalidate before the flip. */
+	dsb sy
+	ic iallu
+	dsb nsh
+	isb
 	mrs x0, sctlr_el1
-	orr x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
+	orr x0, x0, #((1 << 0) | (1 << 12))   /* SCTLR_EL1.M | I */
 	msr sctlr_el1, x0
 	isb
+	ic iallu
+	dsb nsh
+	isb
 	uart_tag2 88, 52
```

**Build/test rubric.** `./scripts/rebuild-rpi4b-fast.sh` →
`./scripts/capture-rpi4b-uart.sh` → `python3 scripts/summarize-rpi4b-uart-log.py
<log>`. Pass = `(psh)%` reached. Fail = same v2 reset pattern (try H6 next).

### H2 — Linux's `init_kernel_el` programs sysregs Phoenix omits

**Rationale.** Linux arm64 head.S sets several EL1 sysregs Phoenix
leaves untouched: `MDSCR_EL1` (debug + single-step off), `OSLAR_EL1`
(unlock OS lock so PMU works), `CONTEXTIDR_EL1` (TLB tag = 0). A72
honors MDSCR_EL1.MDE / SS bits which can take the core into single-step
traps unexpectedly. Pi 4 firmware likely leaves these zero, but explicit
init removes them as a variable.

**Predicted UART signature.** If Phase A v2's failure is caused by
*any* of these uninitialized sysregs, expect the syspage-`P+1` reset to
disappear and boot to advance. If it's caused by something else,
boot pattern unchanged.

**Diff.**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -283,6 +283,15 @@ el1_entry:
 	mov x0, #((0x3 << 20) | (0x3 << 16))
 	msr cpacr_el1, x0
 	isb
+
+	/* H2: zero sysregs Linux init_kernel_el touches that we don't.
+	 * Belt-and-braces — Pi 4 firmware probably leaves these zero,
+	 * but explicit init removes them as a variable. */
+	msr mdscr_el1, xzr            /* no debug, no single-step */
+	mrs x0, oslsr_el1             /* read to confirm OS lock state */
+	msr oslar_el1, xzr            /* unlock OS lock for PMU */
+	msr contextidr_el1, xzr       /* TLB-tag context = 0 */
+	isb
```

**Build/test rubric.** Same as H1. If H1 also lands, apply this
on top of H1, not in isolation.

### H3 — Phase B (M+C+I together) is more stable than Phase A (M+I)

**Rationale.** Counter-intuitive but supported by the cross-OS survey.
Per `boot-mmu-bringup-non-linux.md` §3, every production kernel that
enables caches at all does M+C+I in one MSR; only Phoenix and the
bztsrc tutorial do M-only. M+I (Phase A) may be unrepresentative of any
shipping config, exercising A72 paths nobody tests.

BCM2711 specifics: with M+I and C=0, I-cache fills from the PoU through
the cacheable D-side, but D-side reads bypass cache. A72's I-side and
D-side share L2 below the PoU. If a D-side write populates L2 but I-side
fetches go to PoC (not PoU), they diverge. With C=1 both sides go through
L1+L2 and divergence is impossible.

**Predicted UART signature.** Best case: boot reaches `(psh)%` *and*
subsequent operations are noticeably faster (D-cache hits saturate near
DRAM-burst, ~3 GB/s, vs the cache-off ~150 MB/s baseline). Worst case:
TD-04 cache-coherency anomaly resurfaces — syspage-copy bytes stale —
because the destination page's NC override may not be enough when the
*source* page is now also being read with C=1.

**Diff.**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -429,8 +429,15 @@ el1_entry:
 	uart_tag2 88, 51
-	dsb ish
+	/* H3: full Phase B in one MSR. Mirrors Linux/FreeBSD/NetBSD/seL4
+	 * pattern; bypasses the M+I middle state entirely. Pre-flip:
+	 * clean+invalidate D-cache over the page-table region so the
+	 * walker sees DDR, then I-invalidate. */
+	adrp x0, PMAP_COMMON_KERNEL_TTL2
+	adrp x1, PMAP_COMMON_STACK
+	bl _clean_inval_dcache_range
+	dsb sy
+	ic iallu
+	dsb nsh
+	isb
 	mrs x0, sctlr_el1
-	orr x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
+	orr x0, x0, #((1 << 0) | (1 << 2) | (1 << 12))   /* M | C | I */
 	msr sctlr_el1, x0
 	isb
+	ic iallu
+	dsb nsh
+	isb
 	uart_tag2 88, 52
```

Note: `_inval_dcache_range` is at `_init.S:917` and only invalidates
(`dc ivac`); use `_clean_inval_dcache_range` at `_init.S:937` instead so
dirty lines reach DDR before the walker activates.

**Build/test rubric.** Pass = `(psh)%` reached. If TD-04 anomaly shows
(syspage-copy bytes nondeterministic, marker `q`/`w`/`u` patterns
diverge across runs), fall back to H7 then H4.

### H4 — Aggressive D-cache maintenance before flip

**Rationale.** Even with caches "off", A72 speculatively prefetches
D-side and may have populated cache lines from DDR. When the flip turns
M+I on, the page-table walker reads through the data-side hierarchy; if
those speculatively-prefetched lines were filled with stale data (e.g.
before plo finished writing the syspage to DDR), the walker sees stale
TTEs.

`_inval_dcache_range` over `PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK`
(`_init.S:401-403`) is `dc ivac` only. Linux uses `dc cvac` (clean)
before the flip — needed if any dirty speculative lines exist. The
syspage source page is *not* in the current invalidate range; TD-04's
post-MMU `_clean_inval_dcache_range` at `_init.S:697` covers it but only
after the M-bit is already on.

**Predicted UART signature.** If H4 fixes the v2 fault, expect boot to
reach the same point as M-only (`(psh)%`) but with I-cache on. If it
doesn't help, the fault is downstream of the page-table walker.

**Diff.**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -397,9 +397,15 @@ el1_entry:
 	 * on barriers.
 	 */
 	adrp x0, PMAP_COMMON_KERNEL_TTL2
 	adrp x1, PMAP_COMMON_STACK
-	bl _inval_dcache_range
+	bl _clean_inval_dcache_range   /* clean dirty speculative lines too */
+
+	/* H4: also flush the syspage source page so the post-MMU
+	 * walker doesn't pull a stale TTE-equivalent line for it. */
+	mov x0, x9
+	ldr x1, [x9, #(SYSPAGE_SIZE_OFFSET)]
+	add x1, x0, x1
+	bl _clean_inval_dcache_range
 
 	/* Ensure changes to translation tables are visible */
 	dsb ish
 	isb
```

**Build/test rubric.** Same harness. Apply on top of H1 only if H1
alone reproduces the v2 fault.

### H5 — TLB invalidate adjacent to the SCTLR write

**Rationale.** Today `tlbi vmalle1` is at `_init.S:288`, 130 source
lines (and many actual instructions including a `bl _fill_page_zero`)
before the SCTLR write at `_init.S:434`. Between those two points the
kernel writes TTBR0_EL1 (`_init.S:378`), VBAR_EL1 (`_init.S:382`),
TTBR1_EL1 (`_init.S:385`), and clears the TTBR1 high bit in TCR
(implicitly, via the M-bit only flip — but TCR.EPD1 stays set per
`TCR_EL1_VALUE` bit 23). The TLB invalidate at line 288 happens
*before* TTBR0/TTBR1/TCR are programmed, so any speculative TLB fill
between then and the M=1 write may pick up a half-configured state.

Linux's pattern is `dsb ishst; tlbi vmalle1is; dsb ish; isb` *immediately*
before the M=1 write. NetBSD does the same. OpenBSD does the same.
Phoenix should too.

**Predicted UART signature.** If H5 alone fixes v2, the fix is solid:
boot reaches `(psh)%` with I=1. Otherwise, no change.

**Diff.**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -428,8 +428,13 @@ el1_entry:
 	 * For now the kernel boots cleanly with EL drop + M-only and
 	 * reaches (psh)% — that's the validated baseline.
 	 */
 	uart_tag2 88, 51
-	dsb ish
+	/* H5: TLB invalidate adjacent to the SCTLR write (Linux pattern). */
+	dsb ishst
+	tlbi vmalle1
+	dsb ish
+	isb
 	mrs x0, sctlr_el1
-	orr x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
+	orr x0, x0, #((1 << 0) | (1 << 12))   /* M | I */
 	msr sctlr_el1, x0
 	isb
```

### H6 — Identify the actual fault PC

**Rationale and prerequisite.** Before fixing anything, prove where the
v2 fault lands. `_early_vector_table` (`_init.S:1118`) is installed at
line 381, before SCTLR.M=1. Any post-flip exception should print
`EX=… ESR=… ELR=… FAR=…` via `_early_exception_common`
(`_init.S:1083`). The v2 r2 log shows none. Three possibilities:

1. Watchdog reset — firmware re-enters BOOTSYS after silent hang.
2. Recursive exception looping inside `_early_exception_common`.
3. Synchronous CPU reset on an SError the early vector mis-routes.

The trailing `P    \xa1\xb0` binary garbage suggests UART FIFO transition
— consistent with watchdog or SError.

**Action.** Replace `_early_vector_table` slot 0..15 with hand-rolled
slots that route SError (slot 3, 7, 11, 15) and synchronous (slot 0,
4, 8, 12) to *different* paths, each with a distinct character marker
*before* any register read. That isolates which vector is actually
taken, which then tells you the class of fault.

**Diff sketch.**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -75,8 +75,16 @@
 .macro early_vector slot
         mov x18, #\slot
+        /* H6: emit a single character before any sysreg read so we
+         * see the vector slot even if subsequent sysreg access
+         * itself faults. Reuses uart_putc inline. */
+        ldr x21, =(PL011_TTY_EARLY_VADDR + UARTFR_OFFSET)
+9001:
+        ldr w22, [x21]
+        tst w22, #UARTFR_TXFF
+        b.ne 9001b
+        ldr x21, =PL011_TTY_EARLY_VADDR
+        mov w22, #('!' + \slot)
+        str w22, [x21]
         b _early_exception_common
 .rept 30
         nop
 .endr
 .endm
```

(The slot-derived character is `! + slot`, so slots 0..15 map to
`! " # $ % & ' ( ) * + , - . / 0`. Pick a different mapping if any of
those characters collide with existing kernel UART output.)

**Predicted UART signature.** Either:

- A single distinct character (e.g. `(` for slot 7 = lower-EL SError)
  appears between the syspage `P` marker and the watchdog reset.
  Conclusion: this is an EL1 SError, almost certainly an asynchronous
  abort from a stale I-cache line.
- No new character before reset. Conclusion: not an exception — a
  watchdog or hard reset triggered externally; investigate firmware
  watchdog timeout or CPU lockup at infinite loop.

**Build/test rubric.** Run with H6 *combined with* whatever Phase A
shape is being tested (typically H1's M+I). The point of H6 is
diagnostic, not corrective.

### H7 — Full Linux RES1 SCTLR_EL1 baseline

**Rationale.** `_init.S:272` writes `0x30c0c938` as the initial
SCTLR_EL1. Per `boot-mmu-bringup-non-linux.md` §2 (seL4 issue #1025),
seL4 trusts `0x30d00800` as the A57/A72 RES1 baseline. Linux's
`INIT_SCTLR_EL1_MMU_OFF` adds LSMAOE | nTLSMD on top. Phoenix's
`0x30c0c938` omits bits 22 (RES1), 23 (SPAN, RES1 on A72), and 29
(LSMAOE, RES1) — all three are RES1 in A72 r0p3. Without SPAN, the
M=1 flip can cause SP-alignment exceptions on the first stack push;
that matches v2's silent-reset signature.

**Predicted UART signature.** If H7 fixes Phase A, it fixes the
underlying RES1 hygiene; v2's fault disappears regardless of which
flip variant (H1, H3, H5) is in play.

**Diff.**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -270,7 +270,11 @@ el1_entry:
 		UCT == 1 => don't trap accesses to CTR_EL0 (cache type reg.) from EL0
 		DZE == 1 => don't trap DC ZVA instructions from EL0
 		UCI == 0 => trap cache maintenance instructions executed at EL0
 	 */
-	ldr x0, =0x30c0c938
+	/* H7: full A72 RES1 baseline. Bits added vs 0x30c0c938:
+	 *   bit 22 (RES1), bit 23 (SPAN, RES1 on A72),
+	 *   bit 29 (LSMAOE, RES1). Matches seL4 0x30d00800 RES1 plus
+	 *   the existing nTWI/nTWE/SED/SA/SA0/UCT/DZE bits. */
+	ldr x0, =0x30d0c938
 	msr sctlr_el1, x0
```

(Resulting value `0x30d0c938` = seL4 RES1 baseline `0x30d00800` ORed
with Phoenix's existing `0xc138` trap-control bits. Re-derive against
A72 TRM r0p3 SCTLR_EL1 RES1 column before merging.)

**Build/test rubric.** Apply first (independently of H1/H3/H5) since
it's the smallest change. If M-only still reaches `(psh)%`, nothing
regressed. The real test is H7+H1.

### H8 — Staged C-then-I or I-then-C in separate writes

**Rationale.** v1 (bare M+I flip) reached `(psh)%`; v2 (same flip plus
post-flip `ic iallu`) regressed to syspage-`P+1` reset. Adding an
invalidate after the flip should never *introduce* a fault — unless the
post-flip `ic iallu` itself runs from an I-cache line that just became
live, and that line content disagrees with what the prefetcher
speculatively filled. Splitting M and I into two MSRs distinguishes
ordering from I-cache content as the problem source. Diagnostic only.

**Predicted UART signature.** Three variants to try in sequence:

1. M-only (current baseline). Should still reach `(psh)%`.
2. M-then-I (two MSRs). If this works and (3) doesn't, the fault is
   ordering-related.
3. M-then-C-then-I (three MSRs). If C-only succeeds but +I fails,
   the I-cache is the offender (revisit H1's pre-flip ic iallu).

**Diff (variant 2: M-then-I).**

```diff
--- a/hal/aarch64/_init.S
+++ b/hal/aarch64/_init.S
@@ -429,8 +429,15 @@ el1_entry:
 	uart_tag2 88, 51
-	dsb ish
+	dsb ishst
+	tlbi vmalle1
+	dsb ish
+	isb
 	mrs x0, sctlr_el1
 	orr x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
 	msr sctlr_el1, x0
 	isb
+	uart_tag2 88, 54  /* X6 marker: M alone OK */
+	ic iallu
+	dsb nsh
+	isb
+	mrs x0, sctlr_el1
+	orr x0, x0, #(1 << 12)  /* SCTLR_EL1.I */
+	msr sctlr_el1, x0
+	isb
 	uart_tag2 88, 52
```

**Build/test rubric.** Watch for `X1 X2 X3 X4 X5 X6 X4 X5 X6 X4 X6` —
if the second `X6` (after I=1) appears, I=1 ran without immediate
fault; subsequent syspage walk still tells the story.

## 2. Recommended order

Cheapest diagnostic first, then narrow with the diagnostic's output,
then fix from highest expected leverage:

1. **H6 (diagnostic, no-cost).** Identifies the v2 fault class via a
   per-slot character emitted before any sysreg read. Decisive — collapses
   the search space for everything below.
2. **H7 (cheapest fix).** Single literal change. If A72 RES1 hygiene is
   the bug, every downstream attempt inherits it. Test M-only first
   for non-regression, then H7+H1.
3. **H3 (full Phase B).** Skip the M+I middle, go to the shape every
   production OS uses. TD-04 risk is bounded by the existing NC-page
   override. Highest payoff (full cache speed).
4. **H1 (pre+post-flip `ic iallu`).** Fallback to staged Phase A with
   belt-and-braces I-cache maintenance if H3 regresses or fails.
5. **H5 (TLBI adjacency).** Cheap refinement of H1.
6. **H4 (clean+invalidate D-cache pre-flip incl. syspage src).** Next
   probe if H1+H5 still fails. Combines with H3.
7. **H2 (Linux sysreg parity).** Belt-and-braces; apply on whatever
   shape works.
8. **H8 (staged C-then-I).** Diagnostic only — use if H1/H3/H5 all fail
   to localize C vs I as the offender.

## 3. Validation harness applied to every step

Each hypothesis uses the same loop:

- Edit `_init.S` per the diff.
- `./scripts/rebuild-rpi4b-fast.sh`, flash, then
  `./scripts/capture-rpi4b-uart.sh --label phaseA-h<N>-...`.
- `python3 scripts/summarize-rpi4b-uart-log.py <log>` to extract markers.
- Pass = `(psh)%` reached. Fail = paste the marker sequence into the
  hypothesis section, move to the next.

Snapshot every pass via `./scripts/snapshot-integration-state.sh` so
rollback is one `./scripts/restore-integration-state.sh <manifest>` away.

## 4. Out of scope

- **TD-04 NC-page override removal.** Stays until a follow-up proves
  it's no longer required (`_init.S:524-561`, cache-mmu-smp-impl.md §10
  Q1).
- **SMP secondaries.** Inherit the fix automatically via `el1_entry`.
- **VPU DMA quiescing.** Open question if H3 re-reproduces TD-04
  (cache-mmu-smp-impl.md §10 Q3).
- **Stage 2 DTB / 4 GiB unlock.** Independent of cache enable.

## 5. Cross-references

- `_init.S:272` — SCTLR_EL1 baseline `0x30c0c938`.
- `_init.S:286-290` — TLBI position (130 lines before flip).
- `_init.S:303-322` — SMPEN/859971 site (currently no-op).
- `_init.S:401-407` — pre-flip dcache invalidate (ivac).
- `_init.S:431-436` — M-only flip.
- `_init.S:917-979` — dcache + icache range maintenance helpers.
- `_init.S:1083-1151` — `_early_exception_common` + vector table.
- `syspage.c:401-507` — entry/map/program walk markers (`h/i/j/k/l/m/n/P/p/Z/Y`).
- `boot-mmu-bringup-non-linux.md` §3 — five unanimous invariants.
- `cache-mmu-smp-impl.md` §2 — original Phase A plan this ladder
  supplements.

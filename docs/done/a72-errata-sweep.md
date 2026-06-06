# Cortex-A72 r0p3 errata sweep — Phoenix armstub patch plan

**Target silicon:** BCM2711, Cortex-A72 MPCore r0p3 (MIDR `0x410FD083`).
**Target patch site:** `_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
(currently at EL3, drops to EL2 with `eret`).
**Why armstub, not kernel:** the two errata-control registers we care about,
`CPUECTLR_EL1` (`S3_1_C15_C2_1`) and `CPUACTLR_EL1` (`S3_1_C15_C2_0`), are
A72-implementation-defined and **trap from EL1** on r0p3 — Phase A v3 hung
on the very first `mrs` of `CPUACTLR_EL1` from kernel context. ARM Trusted
Firmware (ATF) applies all of these workarounds in `cortex_a72_reset_func`
which runs at EL3 before the `eret` to EL2/EL1, so EL3 is the right layer.
Phoenix already runs the armstub at EL3, so the patches below land in the
exact same architectural slot.

## 1. ATF cortex_a72_reset_func — what is the canonical reset path?

The cited ATF source is
[`lib/cpus/aarch64/cortex_a72.S`](https://github.com/ARM-software/arm-trusted-firmware/blob/master/lib/cpus/aarch64/cortex_a72.S)
together with its header
[`include/lib/cpus/aarch64/cortex_a72.h`](https://github.com/ARM-software/arm-trusted-firmware/blob/master/include/lib/cpus/aarch64/cortex_a72.h).
The header defines:

- `CORTEX_A72_CPUACTLR_EL1` = `S3_1_C15_C2_0`
- `CORTEX_A72_ECTLR_EL1`    = `S3_1_C15_C2_1`
- `CORTEX_A72_L2CTLR_EL1`   = `S3_1_C11_C0_2`
- `CORTEX_A72_L2ACTLR_EL1`  = `S3_1_C15_C0_0`
- `CORTEX_A72_L2MERRSR_EL1` = `S3_1_C15_C2_3`
- `CORTEX_A72_ECTLR_SMP_BIT` = `(1 << 6)`
- `CORTEX_A72_CPUACTLR_EL1_DIS_INSTR_PREFETCH`     = bit 32
- `CORTEX_A72_CPUACTLR_EL1_DELAY_EXCLUSIVE_SNOOP`  = bit 31
- `CORTEX_A72_CPUACTLR_EL1_DCC_AS_DCCI`            = bit 44
- `CORTEX_A72_CPUACTLR_EL1_NO_ALLOC_WBWA`          = bit 49
- `CORTEX_A72_CPUACTLR_EL1_DIS_LOAD_PASS_STORE`    = bit 55
- `CORTEX_A72_CPUACTLR_EL1_DISABLE_L1_DCACHE_HW_PFTCH` = bit 56

Bit positions are confirmed in
[the ATF a72 header](https://github.com/ARM-software/arm-trusted-firmware/blob/master/include/lib/cpus/aarch64/cortex_a72.h).

`cortex_a72_reset_func` (at EL3) does, in order:

1. `sysreg_bit_set CORTEX_A72_ECTLR_EL1, CORTEX_A72_ECTLR_SMP_BIT` —
   sets SMPEN (bit 6 of `CPUECTLR_EL1`).
2. Conditional `bl errata_a72_859971_wa` (only if `ERRATA_A72_859971` and
   variant ≤ r0p3) — sets `CPUACTLR_EL1[32]` = 1.
3. Conditional `bl errata_a72_1319367_wa` (always, all revisions if
   `ERRATA_A72_1319367`) — sets `CPUACTLR_EL1[46]` = 1 (disable HW page
   aggregation).
4. Spectre-class vector overrides (`wa_cve_2017_5715_mmu_vbar`,
   `wa_cve_2018_3639` setting `CPUACTLR_EL1[55]`, `wa_cve_2022_23960`
   BHB loop). Per the user's instruction these are deferred.

> Note on bit position: the user prompt mentioned bit 47 for 859971. The
> ATF tree, the AMD/Xilinx Cortex-A72 errata page, and the public TRM all
> list bit 32 (`DIS_INSTR_PREFETCH`). Citation:
> [AMD docs.amd.com errata page](https://docs.amd.com/r/en-US/en324/Speculative-Instruction-Prefetch-To-Execute-Never-XN-Memory-Can-Cause-Deadlock-Or-Data-Integrity-Issue-Cortex-A72)
> states verbatim: "Instruction prefetch can be disabled by writing
> `CPUACTLR_EL1[32]=1`." The patches below use bit 32. Bit 47 looks like
> a transcription error from a different erratum; if you have a contrary
> primary source for bit 47, surface it before applying these.

## 2. Erratum-by-erratum sweep relevant to MMU / cache / TLB / coherency

### 859971 — Speculative I-prefetch crossing into XN page can deadlock or corrupt RO-volatile devices

- **Title (paraphrased from AMD/Xilinx errata page):** "Speculative
  instruction prefetch to Execute-never (XN) memory can cause deadlock
  or data integrity issue".
- **Applicable revisions:** all r0p0 — r0p3. **Pi 4 r0p3: APPLIES.**
- **Symptom:** When a Normal Write-Back Cacheable page immediately
  precedes an XN page, the I-cache prefetcher can issue a 64-byte
  speculative fetch into the XN page. If that XN page is a read-sensitive
  device (MMIO with read side-effects), or a region the system
  interconnect doesn't fully snoop, the result is data corruption or a
  bus deadlock.
- **Workaround:** `CPUACTLR_EL1[32] = 1` (DIS_INSTR_PREFETCH).
- **ATF policy:** unconditional for r0p0–r0p3 when `ERRATA_A72_859971=1`.
  ATF docs: "This applies errata 859971 workaround to Cortex-A72 CPU.
  This needs to be enabled only for revision <= r0p3 of the CPU."
  ([ATF cpu-specific-build-macros.rst](https://github.com/ARM-software/arm-trusted-firmware/blob/master/docs/design/cpu-specific-build-macros.rst))
- **Phoenix armstub current state:** **NOT applied.** The current stub
  only writes SMPEN to `CPUECTLR_EL1`. Search the file (lines 58–60,
  163–164) — there is no `CPUACTLR_EL1` write at all.
- **Severity for Phoenix:** very high. Phase A v3 already verified the
  TTE has XN+PXN on every device mapping, but `XN+PXN` only stops
  *committed* execution — the prefetcher is upstream of permission
  check. This is the canonical reason "MMU on, then a page-table walk
  near a UART/GIC mapping hangs the system" on A72.

### 1319367 — Speculative AT-instruction can corrupt TLB during EL1↔EL2 context switch

- **Title:** "Speculation of an AT instruction during context-switch can
  corrupt TLB". Same root cause as Cortex-A57 #1319537.
- **Applicable revisions:** all revisions.
- **Symptom:** If software issues an `AT S1E*` while S1/S2 system regs
  are mid-switch, the speculatively walked TLB entry can be allocated
  with the wrong translation. KVM/Xen are the classic triggers; bare
  metal that uses `AT` in fault handlers is also exposed.
- **Workaround:** `CPUACTLR_EL1[46] = 1` (disable HW page aggregation).
  Confirmed in the
  [TF-A patch e1c4933372 "lib/cpus: Report AT speculative erratum workaround"](https://review.trustedfirmware.org/plugins/gitiles/TF-A/trusted-firmware-a/+/e1c4933372).
- **ATF policy:** unconditional for all revisions when
  `ERRATA_A72_1319367=1`.
- **Phoenix armstub current state:** **NOT applied.**
- **Severity for Phoenix:** medium for current step (no AT issuance yet
  in early boot), but cheap to apply once and removes a future-debug
  rabbit-hole when virtualization or page-fault handlers are added.

### 853709 — DACR32_EL2 sync issue at EL1↔EL2 (CONTEXTIDR_EL1 write workaround)

- **Title:** "Cortex-A72 r0p0..r0p2: same as Cortex-A57 #852523,
  DACR32_EL2 not correctly synchronized on context-switch."
  ([Linux patchwork: arm64 Document workaround for Cortex-A72 erratum #853709](https://patchwork.kernel.org/project/linux-arm-kernel/patch/1471356182-26034-1-git-send-email-marc.zyngier@arm.com/))
- **Applicable revisions:** r0p0 — r0p2 only. **Pi 4 r0p3: DOES NOT
  APPLY.** Skipping.

### 855873 — A53/A72 confusion in user prompt

- The user prompt named 855873 as an A72 item. 855873 is in fact a
  **Cortex-A53** erratum ("eviction may overtake a cache clean") per
  [Intel/Altera 855873 entry](https://www.intel.com/content/www/us/en/docs/programmable/683470/current/855873-a-store-exclusive-instruction.html)
  and ATF's [`cortex_a53.S` doc reference](https://github.com/ARM-software/arm-trusted-firmware/blob/master/docs/design/cpu-specific-build-macros.rst).
  The closest A72 equivalent is 859971 above — speculative-prefetch
  hygiene on the MMU permission boundary, which Phoenix already partly
  mitigates by setting XN/PXN in TTEs but truly fixes by disabling the
  prefetcher (859971 workaround). No separate patch here.

### 858921 — A57/A53 cache-aware speculative atomicity

- The user prompt named 858921. Public ATF source contains
  `errata_a57_858921_wa` ([ATF cortex_a57.S](https://github.com/ARM-software/arm-trusted-firmware/blob/master/lib/cpus/aarch64/cortex_a57.S))
  but **no** `errata_a72_858921_wa`. Confirmed by re-reading the cited
  cortex_a72.S — no entry. Linux's silicon-errata table likewise lists
  858921 only under Cortex-A57. Treat as not-applicable to A72 r0p3
  unless the public Cortex-A72 SDEN says otherwise (the public SDEN at
  developer.arm.com is gated; cite the mirrored TRM
  [Cortex-A72 r0p3 TRM](https://www.scs.stanford.edu/~zyedidia/docs/arm/cortex_a72.pdf)
  if checking). No patch.

### 836870 — Non-allocating reads can prevent store-exclusive from passing

- Originally an A53 erratum; the equivalent A72 behavior is hardware-fixed
  in r0p3 per the
  [Intel/Altera 836870 entry](https://www.intel.com/content/www/us/en/docs/programmable/683470/current/836870-non-allocating-reads-may-prevent.html).
  Listed in the user's "investigate" set but not applicable to A72 r0p3.
  No patch.

### Spectre/SSBS/SSBD (CVE-2017-5715, CVE-2018-3639, CVE-2022-23960)

- **Deferred per user's explicit instruction.** ATF applies these via
  vector-table override and `CPUACTLR_EL1[55]`. Phoenix should pick
  these up before any user-visible release, but they do not affect the
  current cache-enable hang.

## 3. Drafted unified diffs against `phoenix-armstub8-rpi4.S`

All patches insert at the same point: between the existing SMPEN write
(line 163-164) and `bl setup_gic` (line 166). This is at EL3, before the
`eret` to EL2 — exactly where ATF's `cortex_a72_reset_func` runs.

### Patch 1: header `#define`s for the two CPUACTLR_EL1 bits

```diff
--- a/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S
+++ b/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S
@@ -55,8 +55,11 @@
 #define SCR_NS       BIT(0)
 #define SCR_VAL (SCR_RW | SCR_HCE | SCR_RES1_5 | SCR_RES1_4 | SCR_NS)

-#define CPUECTLR_EL1       S3_1_C15_C2_1
-#define CPUECTLR_EL1_SMPEN BIT(6)
+#define CPUECTLR_EL1                       S3_1_C15_C2_1
+#define CPUECTLR_EL1_SMPEN                 BIT(6)
+#define CPUACTLR_EL1                       S3_1_C15_C2_0
+#define CPUACTLR_EL1_DIS_INSTR_PREFETCH    (1ULL << 32) /* erratum 859971 */
+#define CPUACTLR_EL1_DIS_HW_PAGE_AGG       (1ULL << 46) /* erratum 1319367 */
```

### Patch 2: erratum 859971 (XN-prefetch deadlock) — the critical one for cache enable

```diff
@@ -161,8 +164,17 @@
 	mov	x0, #SCR_VAL
 	msr	scr_el3, x0

-	mov	x0, #CPUECTLR_EL1_SMPEN
-	msr	CPUECTLR_EL1, x0
+	/* SMPEN: must be set before any cache or TLB maintenance op (A72 TRM 4.3.40). */
+	mrs	x0, CPUECTLR_EL1
+	orr	x0, x0, #CPUECTLR_EL1_SMPEN
+	msr	CPUECTLR_EL1, x0
+	isb
+
+	/* Erratum 859971 (Cortex-A72 r0p0..r0p3): disable speculative I-prefetch
+	 * across XN page boundary. Without this, the I-cache prefetcher can fault
+	 * a read-sensitive device behind an XN mapping during MMU enable.  */
+	mrs	x0, CPUACTLR_EL1
+	orr	x0, x0, CPUACTLR_EL1_DIS_INSTR_PREFETCH
+	msr	CPUACTLR_EL1, x0
+	isb
```

> The previous SMPEN write was a single-bit `mov; msr` rather than RMW.
> The RMW form matches ATF's `sysreg_bit_set` macro and avoids clobbering
> reserved bits. Either form is correct on a fresh-out-of-reset core.

### Patch 3: erratum 1319367 (AT-speculation TLB corruption)

Append immediately after Patch 2:

```diff
+	/* Erratum 1319367 (Cortex-A72, all revisions): disable HW page-table
+	 * aggregation so a speculative AT during EL1<->EL2 switch cannot leak
+	 * a wrong translation into the TLB. Cheap; apply unconditionally.  */
+	mrs	x0, CPUACTLR_EL1
+	orr	x0, x0, CPUACTLR_EL1_DIS_HW_PAGE_AGG
+	msr	CPUACTLR_EL1, x0
+	isb
```

### Patch 4 (optional, defensive): re-prove SMPEN after CPUACTLR writes

ATF places SMPEN first; Patches 2 and 3 do not touch CPUECTLR; nothing
clears SMPEN. No additional write needed. Mention only as "verified by
inspection" comment if desired.

### Combined diff (logical, single hunk)

```diff
--- a/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S
+++ b/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S
@@ -55,8 +55,11 @@
 #define SCR_NS       BIT(0)
 #define SCR_VAL (SCR_RW | SCR_HCE | SCR_RES1_5 | SCR_RES1_4 | SCR_NS)

-#define CPUECTLR_EL1       S3_1_C15_C2_1
-#define CPUECTLR_EL1_SMPEN BIT(6)
+#define CPUECTLR_EL1                    S3_1_C15_C2_1
+#define CPUECTLR_EL1_SMPEN              BIT(6)
+#define CPUACTLR_EL1                    S3_1_C15_C2_0
+#define CPUACTLR_EL1_DIS_INSTR_PREFETCH (1ULL << 32) /* erratum 859971 */
+#define CPUACTLR_EL1_DIS_HW_PAGE_AGG    (1ULL << 46) /* erratum 1319367 */
@@ -161,8 +164,21 @@
 	mov	x0, #SCR_VAL
 	msr	scr_el3, x0

-	mov	x0, #CPUECTLR_EL1_SMPEN
-	msr	CPUECTLR_EL1, x0
+	mrs	x0, CPUECTLR_EL1
+	orr	x0, x0, #CPUECTLR_EL1_SMPEN
+	msr	CPUECTLR_EL1, x0
+	isb
+
+	/* A72 erratum 859971 (r0p0..r0p3): disable speculative I-prefetch. */
+	mrs	x0, CPUACTLR_EL1
+	orr	x0, x0, CPUACTLR_EL1_DIS_INSTR_PREFETCH
+	msr	CPUACTLR_EL1, x0
+	isb
+
+	/* A72 erratum 1319367 (all revisions): disable HW page aggregation. */
+	mrs	x0, CPUACTLR_EL1
+	orr	x0, x0, CPUACTLR_EL1_DIS_HW_PAGE_AGG
+	msr	CPUACTLR_EL1, x0
+	isb
```

## 4. Predicted UART signature pre/post fix

**Pre-fix (current):** `AS0\r\n` from the armstub, then silence at the
SCTLR_EL1.M=1 write in `_hal_init`. This matches both `TD-04`
class-of-bug (cache coherency on BCM2711) and the more specific 859971
class — they are the same hardware corner.

**Post-Patch-2 (859971 only):** Two outcomes are plausible.
1. *Hang clears, kernel proceeds past `_hal_init` to next print.* This
   would confirm 859971 was the root cause: speculative I-fetch into
   GIC/UART/local-control MMIO was the deadlock site. Strongest
   confirmation.
2. *Hang persists in the same place.* Read as: 859971 is necessary but
   not sufficient — also need to recheck SMPEN write ordering, MAIR
   attrs, and the EL2/EL1 mismatch hypothesis from the boot-mmu-bringup
   survey.

**Post-Patch-3 (859971 + 1319367):** No change to the boot signature
expected at this stage of bring-up (no AT-instruction issuance from
Phoenix yet). The patch is preventive for the next milestone (page-fault
handlers / future EL2 hosting). Cite this in the commit message so a
later regression on AT semantics has the matching workaround already in
place.

**Diagnostic UART probe (recommended):** between the two `msr
CPUACTLR_EL1` writes, insert a `uart_putc 'P'` so a successful boot log
becomes `AS0\rP\r\nP\r\n...`. This proves the writes did not themselves
trap (which is exactly what the kernel-side Phase A v3 attempt did).
Remove the probe before merging per the project's "remove disproved
diagnostic code" policy in `CLAUDE.md`.

## 5. What this patch deliberately does *not* fix

- **EL2/EL1 mismatch on SCTLR enable.** The boot-mmu-bringup-non-linux
  survey (R1) is the highest-confidence fix for the silent hang;
  errata-sweep is orthogonal. Apply both — order does not matter.
- **MMIO XN/PXN attrs in stage-1 TTEs.** Phase A v3 already set these.
  Errata 859971 makes them load-bearing-correctness, not just hygiene.
- **D-cache invalidate before MMU on.** Standard `dsb ishst; tlbi
  vmalle1is; dsb ish; isb` sequence is kernel-side, not armstub-side.
- **Spectre/SSBD/CSV2 mitigations.** Deferred per user instruction.

## 6. Confidence and test plan

- **High confidence the patches are syntactically correct** — `S3_1_*`
  encodings come from the ATF header verbatim; bit positions
  cross-checked against AMD/Xilinx public errata pages and the ATF
  header. Caveats: bit 47 vs bit 32 discrepancy noted in §1.
- **High confidence the patches will not regress.** Both writes are
  monotonic (set bit, never clear), happen at EL3 once-per-boot before
  any cache/TLB op, and match ATF's behavior on every BCM2711 board
  that ships with ATF (e.g. Pi 4 builds via U-Boot+TF-A). If they were
  going to break a Pi 4, ATF would have noticed.
- **Test order:** apply Patch 1 (defines) + Patch 2 (859971) first,
  rebuild, run `./scripts/rebuild-rpi4b-fast.sh && ./scripts/capture-rpi4b-uart.sh`
  per `CLAUDE.md`. If 859971 alone unblocks the SCTLR enable, stop and
  validate before adding 1319367. Snapshot a manifest in `manifests/`
  on success per project rollback discipline.

## Sources

- [ATF cortex_a72.S](https://github.com/ARM-software/arm-trusted-firmware/blob/master/lib/cpus/aarch64/cortex_a72.S)
- [ATF cortex_a72.h](https://github.com/ARM-software/arm-trusted-firmware/blob/master/include/lib/cpus/aarch64/cortex_a72.h)
- [ATF cpu-specific-build-macros.rst](https://github.com/ARM-software/arm-trusted-firmware/blob/master/docs/design/cpu-specific-build-macros.rst)
- [TF-A AT-speculate report patch](https://review.trustedfirmware.org/plugins/gitiles/TF-A/trusted-firmware-a/+/e1c4933372)
- [AMD/Xilinx Cortex-A72 XN-prefetch errata page](https://docs.amd.com/r/en-US/en324/Speculative-Instruction-Prefetch-To-Execute-Never-XN-Memory-Can-Cause-Deadlock-Or-Data-Integrity-Issue-Cortex-A72)
- [Linux ARM64 silicon-errata reference](https://docs.kernel.org/arch/arm64/silicon-errata.html)
- [Linux Kconfig (arm64) — ARM64_ERRATUM_1319367 / 1742098](https://github.com/torvalds/linux/blob/master/arch/arm64/Kconfig)
- [Linux 853709 documentation patch (Marc Zyngier)](https://patchwork.kernel.org/project/linux-arm-kernel/patch/1471356182-26034-1-git-send-email-marc.zyngier@arm.com/)
- [Xen cpuerrata.c (A72 entries)](https://github.com/xen-project/xen/blob/master/xen/arch/arm/cpuerrata.c)
- [Cortex-A72 r0p3 TRM mirror](https://www.scs.stanford.edu/~zyedidia/docs/arm/cortex_a72.pdf)
- [Phoenix armstub source under inspection (read-only)](file:///Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S)

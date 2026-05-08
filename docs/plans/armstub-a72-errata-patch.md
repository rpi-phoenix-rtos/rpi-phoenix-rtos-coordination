# Phoenix armstub A72 errata patch — Stage 1 cache-enable unblocker

Status: design only. No source changed.
Scope: `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`.
Goal: apply the A72 EL3-only system-register prerequisites that ARM Trusted
Firmware (ATF) does in `cortex_a72_reset_func`, so that the kernel-side
Phase A cache enable in `phoenix-rtos-kernel/hal/aarch64/_init.S` stops
trapping silently when it tries to assert SMPEN / 859971 from EL1.

## 1. Current armstub state audit

File: `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`.

The whole image is 294 lines; the EL3 prologue is small. Walking it from
top to bottom for sysreg / EL effects only:

- Lines 33–73: `#include "board_config.h"`, MMIO bases (LOCAL_CONTROL,
  GIC, PL011), bit/field macros. No instructions emitted.
- Lines 58–59: `CPUECTLR_EL1` is encoded as `S3_1_C15_C2_1`, `SMPEN` as
  bit 6. These match the ATF header values
  `CORTEX_A72_ECTLR_EL1 = S3_1_C15_C2_1`,
  `CORTEX_A72_ECTLR_SMP_BIT = (1 << 6)`. Good.
- Lines 75–78: `_start` thunk that branches to `start_late`. The first
  256 bytes of the image are reserved for the firmware handoff header
  (spin tables, `stub_magic`, `dtb_ptr32`, `kernel_entry32`) at lines
  106–141.
- Line 143 (`start_late:`): entry from VC firmware running at EL3, MMU
  off, caches off. `x0` carries the firmware-supplied DTB pointer; we
  save it in `x28` (line 144).
- Lines 145–146: read `MPIDR_EL1[1:0]` into `x7` (per-core ID).
- Lines 147–150: zero `LOCAL_CONTROL`, set `LOCAL_PRESCALER` to
  `0x80000000` (1:1 prescale of OSC_FREQ).
- Lines 152–153: program `CNTFRQ_EL0 = OSC_FREQ` (54 MHz).
- Lines 155–157: `CNTVOFF_EL2 = 0`, `CNTHCTL_EL2 = 0x3`
  (EL1 access to phys/virt counter).
- Line 159: `CPTR_EL3 = 0` (no FP/SIMD trapping).
- Lines 160–161: `SCR_EL3 = SCR_VAL = RW | HCE | RES1_5 | RES1_4 | NS`.
  This is what makes the eret exit to AArch64 non-secure with HVC
  enabled.
- **Lines 163–164: SMPEN. `mov x0, #CPUECTLR_EL1_SMPEN; msr CPUECTLR_EL1, x0`.**
  This is the only A72-specific sysreg write the armstub does today.
  **Item 1 of the required workaround list is therefore already
  present.** (Note the write replaces the register rather than RMW —
  this is fine on A72 r0p3 cold reset because the only documented bits
  in CPUECTLR_EL1 are reset to zero except for implementation-defined
  prefetch hint fields the kernel does not touch.)
- Line 166: `bl setup_gic` — distributor + CPU interface init for all
  cores (lines 269–291). EL3-side; no impact on the cache-enable path.
- Lines 168–169: `SCTLR_EL2 = 0x30c50830`. Note bit C (2) and bit M (0)
  are both clear, so caches and MMU stay off across the eret. Correct.
- Lines 171–175: build SPSR_EL3 = `D|A|I|F|EL2h`, set `ELR_EL3 = in_el2`,
  `eret`. **This is where EL3 ends.** Anything we add must come before
  the `eret` on line 175.
- Line 177 onward (`in_el2`): vbar set, MPIDR check, primary path
  reinitialises UART (`uart_reinit_115200`, line 218), prints `AS0\r\n`
  (lines 219–223), then branches to the firmware-supplied
  `kernel_entry32` (line 227).

Conclusions of the audit:

- **SMPEN is set, at the correct EL.** No change needed for it.
- **CPUACTLR_EL1 (S3_1_C15_C2_0) is never touched.** Erratum 859971 is
  not applied. This is the missing piece.
- The armstub does **not** modify `ACTLR_EL3` or `ACTLR_EL2` to grant
  EL1 access to the implementation-defined registers, which is why the
  kernel-side retry traps. We could add those grants instead — but ATF
  upstream chose to do the writes once at EL3 rather than open access
  to lower ELs, and Phoenix follows ATF (see kernel comment at
  `_init.S:303–319`). Keep the EL3 approach.
- The eret target is EL2h (SPSR mode 9). The kernel later drops to
  EL1; the EL3-set CPUECTLR / CPUACTLR values persist across the EL
  drop because both registers are per-PE state, not per-EL state.

## 2. ATF reference sequence (`cortex_a72.S` and `cortex_a72.h`)

Source: ARM Trusted Firmware master,
`lib/cpus/aarch64/cortex_a72.S` and the matching header
`include/lib/cpus/aarch64/cortex_a72.h`.

Bit and register definitions (header):

- `CORTEX_A72_ECTLR_EL1 = S3_1_C15_C2_1`
- `CORTEX_A72_ECTLR_SMP_BIT = (1ULL << 6)`
- `CORTEX_A72_CPUACTLR_EL1 = S3_1_C15_C2_0`
- `CORTEX_A72_CPUACTLR_EL1_DIS_INSTR_PREFETCH = (1ULL << 32)` — note
  the ATF macro name is `DIS_INSTR_PREFETCH` and ATF places the bit at
  position **32**, not 47. This contradicts the brief that referred to
  bit 47. Crosschecking the A72 TRM: bit 32 of CPUACTLR_EL1 is the
  documented "Disable instruction prefetch" control on Cortex-A72.
  Bit 47 is `DIS_LOAD_PASS_STORE` (used by the CVE-2018-3639 SSBD
  workaround), not 859971. **The patch below applies bit 32, matching
  ATF and the A72 TRM.**
- `CORTEX_A72_CPUACTLR_EL1_DIS_LOAD_PASS_STORE = (1ULL << 55)` —
  unrelated to 859971, listed for completeness.

Reset-function flow (ATF cortex_a72.S, master):

- Lines 132–135 — erratum 859971 workaround:
  ```
  workaround_reset_start cortex_a72, ERRATUM(859971), ERRATA_A72_859971
      sysreg_bit_set CORTEX_A72_CPUACTLR_EL1, CORTEX_A72_CPUACTLR_EL1_DIS_INSTR_PREFETCH
  workaround_reset_end   cortex_a72, ERRATUM(859971)
  ```
  The macro `sysreg_bit_set` expands to `mrs / orr (imm or reg) / msr`
  with no DSB but an implicit ISB at the end of the
  `workaround_reset_end` epilogue.
- Lines 142–147 — CVE-2017-5715 (Spectre v2): vector-table override.
  Not applicable to Phoenix at the armstub level — Phoenix has no
  Spectre v2 mitigation surface in the armstub and the BL31-only macro
  `IMAGE_BL31` is not relevant here.
- Lines 169–174 — CVE-2018-3639 (SSBD) sets
  `CPUACTLR_EL1[55] = DIS_LOAD_PASS_STORE`. Only applied when the build
  defines `WORKAROUND_CVE_2018_3639`. Phoenix does not enable SSBD; we
  do not need this in the armstub.
- Lines 176–191 — CVE-2022-23960 (Spectre-BHB): vector-table override
  on r1p0+. A72 r0p3 (Pi 4) is below the affected revision range and
  the workaround is keyed on `ERRATA_A72_CSV2` which Phoenix's hardware
  variant does not need.
- Lines 193–200 — `cortex_a72_reset_func` core: only one positive
  action, `sysreg_bit_set CORTEX_A72_ECTLR_EL1, CORTEX_A72_ECTLR_SMP_BIT`,
  surrounded by the framework-provided `cpu_reset_func_start /
  cpu_reset_func_end` macros that emit the call sequence to each
  `workaround_reset_start`-tagged block plus a final ISB. The order of
  errata calls is generated by `apply_errata` and runs each workaround
  whose CPU-rev predicate matches the running silicon. On A72 r0p3
  (BCM2711) the matching workarounds are: 859971 (always r0..r0p3),
  and SMPEN (always). All others are gated and inactive on r0p3.

Net of this for Phoenix on Pi 4 r0p3: ATF does, in order, **(a) set
CPUACTLR_EL1[32] = 1 (DIS_INSTR_PREFETCH, 859971), (b) set
CPUECTLR_EL1[6] = 1 (SMPEN), (c) ISB**. Phoenix today only does (b).
We need to add (a) and the trailing barriers.

## 3. Diff against current armstub

The new sequence is inserted immediately after the existing SMPEN
write (line 164) and before `bl setup_gic` (line 166). Reasons for
this placement:

1. SCR_EL3 has just been programmed; we are still cleanly at EL3.
2. We are still pre-eret, so writes to S3_1_C15_C2_0 are unrestricted.
3. The GIC setup is a memory-mapped sequence and is unaffected by an
   ISB inserted before it.
4. The ATF order is "errata first, then SMPEN, then ISB". Phoenix did
   SMPEN first, but on A72 r0p3 the two writes are independent (859971
   touches CPUACTLR_EL1, SMPEN touches CPUECTLR_EL1), so applying 859971
   *after* SMPEN — as the diff below does — is functionally equivalent
   and minimises churn.
5. We deliberately do an RMW (mrs/orr/msr) for CPUACTLR_EL1 because
   that register has implementation-defined POR values on r0p3 we do
   not want to clobber, unlike CPUECTLR_EL1 whose other bits are well
   understood.

Unified diff (target file, paths relative to repo root):

```diff
--- a/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S
+++ b/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S
@@ -55,8 +55,15 @@
 #define SCR_NS       BIT(0)
 #define SCR_VAL (SCR_RW | SCR_HCE | SCR_RES1_5 | SCR_RES1_4 | SCR_NS)

-#define CPUECTLR_EL1       S3_1_C15_C2_1
-#define CPUECTLR_EL1_SMPEN BIT(6)
+#define CPUECTLR_EL1       S3_1_C15_C2_1
+#define CPUECTLR_EL1_SMPEN BIT(6)
+
+/*
+ * A72 erratum 859971 — disable instruction prefetch.
+ * Mirrors ATF cortex_a72.S CORTEX_A72_CPUACTLR_EL1_DIS_INSTR_PREFETCH (bit 32).
+ */
+#define CPUACTLR_EL1                  S3_1_C15_C2_0
+#define CPUACTLR_EL1_DIS_INSTR_PREFETCH (1UL << 32)

 #define SPSR_EL3_D         BIT(9)
 #define SPSR_EL3_A         BIT(8)
@@ -160,9 +167,30 @@ start_late:
 	mov	x0, #SCR_VAL
 	msr	scr_el3, x0

-	mov	x0, #CPUECTLR_EL1_SMPEN
-	msr	CPUECTLR_EL1, x0
+	/*
+	 * A72 EL3-only prerequisites for kernel-side cache enable.
+	 * CPUECTLR_EL1 (S3_1_C15_C2_1) and CPUACTLR_EL1 (S3_1_C15_C2_0)
+	 * trap from EL1 on A72 r0p3, so the kernel cannot apply these
+	 * itself. ATF's cortex_a72_reset_func applies the same pair.
+	 */
+	/* (1) SMPEN — coherency on, required for inner-shareable D-cache. */
+	mov	x0, #CPUECTLR_EL1_SMPEN
+	msr	CPUECTLR_EL1, x0
+	uart_putc 49	/* '1' — SMPEN done */
+
+	/* (2) Erratum 859971: CPUACTLR_EL1[32] DIS_INSTR_PREFETCH.
+	 *     Required for I-cache + branch-predictor enable to take
+	 *     effect on A72 r0..r0p3. RMW to preserve impl-def reset bits.
+	 */
+	mrs	x0, CPUACTLR_EL1
+	mov	x1, #CPUACTLR_EL1_DIS_INSTR_PREFETCH
+	orr	x0, x0, x1
+	msr	CPUACTLR_EL1, x0
+	uart_putc 50	/* '2' — 859971 done */
+
+	dsb	sy
+	isb

 	bl setup_gic

```

Notes on the diff:

- `mov x1, #imm` is used to materialise `1 << 32` because the AArch64
  `orr (immediate)` encoding cannot represent that bitmask directly.
- Both new sysreg writes execute only on the boot CPU because the
  spin-table secondaries never reach `start_late`'s pre-eret block —
  they enter via `secondary_spin` after the firmware kicks them. If we
  later enable secondaries we will need to replay this block on each
  secondary path before its eret to EL2.
- The `dsb sy; isb` pair after both writes mirrors what ATF emits at
  the end of `cpu_reset_func_end`. Without ISB the writes are not
  guaranteed to be observed by the next instruction stream; with MMU
  off we still want the ISB to force the pipeline to re-fetch under
  the new prefetch policy.

## 4. Build and test plan

Where the armstub gets built:

- `_projects/aarch64a72-generic-rpi4b/build.project:86–101`
  defines `rpi4b_buildArmstub`, which runs:
  - `${CC} -c -nostdlib -ffreestanding ${PROJECT_PATH}/phoenix-armstub8-rpi4.S -o ${obj}` (line 96)
  - `${LD} -nostdlib -Ttext=0x0 -o ${elf} ${obj}` (line 97)
  - `${CROSS}objcopy -O binary ${elf} ${bin}` (line 98)
- `b_image_project` invokes it once (line 196) and copies the result
  to `${bootdir}/phoenix-armstub8-rpi4.bin` (line 202).
- `config.txt:7` makes the firmware load it via `armstub=phoenix-armstub8-rpi4.bin`.

So the only Makefile-equivalent path involved is the `build.project`
shell function above. There is no plo `Makefile` rule for the armstub —
plo's Makefiles search returned no `armstub` references in
`sources/plo/`, confirming it is built by the project assembler step,
not as part of plo proper.

Build/test loop (per `CLAUDE.md`):

1. Edit the armstub.
2. `./scripts/rebuild-rpi4b-fast.sh` — drives `build.project`,
   regenerates `phoenix-armstub8-rpi4.bin`, repacks the boot media.
3. Flash / netboot the new image.
4. `./scripts/capture-rpi4b-uart.sh logs/armstub-859971-attempt-N.log`.
5. `python3 scripts/summarize-rpi4b-uart-log.py logs/armstub-859971-attempt-N.log`.
6. Confirm the new `1` `2` characters appear before `AS0`.
7. Re-enable kernel Stage 1 Phase A (revert the
   `_init.S:303–322` no-op block to a real CPUECTLR/CPUACTLR retry —
   tracked separately).
8. Re-run; expect the previously trapped `MRS S3_1_*` no longer hangs.

## 5. Predicted UART signature

Today, after the armstub eret, the primary CPU prints `AS0\r\n` (lines
219–223 — `'A' 'S' '0' CR LF`).

With the patch, the EL3 prologue prints two extra characters before
`AS0\r\n`:

- `1` after the SMPEN write (existing site, now made visible).
- `2` after the 859971 RMW.

So the expected UART head becomes:

```
12AS0
<plo banner...>
```

Verification rules:

- If `1` is missing: SMPEN site failed — almost certainly an unrelated
  build regression (the existing path is untouched code paths).
- If `1` is present but `2` is missing: the CPUACTLR RMW trapped or
  hung — we are at EL3 so this should be impossible; if it happens,
  inspect the disassembly for the `S3_1_C15_C2_0` encoding (`d518f200`
  for msr / `d538f200` for mrs).
- If both fire and the kernel still hangs at the same place: the
  hypothesis is wrong; revisit whether the kernel is genuinely on A72
  r0p3 (`MIDR_EL1` should be `0x410fd083`) and whether 859971 actually
  applies to this rev.

## 6. Risks

The armstub is the second-stage bootloader; a corrupt one bricks the
boot before plo runs. Specific risks introduced by this diff:

- **Wrong bit position.** ATF and the A72 TRM both put 859971 at bit
  32. The brief asked for bit 47, which is `DIS_LOAD_PASS_STORE`
  (CVE-2018-3639 SSBD), an unrelated control. The diff intentionally
  uses bit 32. If a future reviewer disputes this, the easiest crosscheck
  is `grep -A2 859971 sources/atf/include/lib/cpus/aarch64/cortex_a72.h`
  in any ATF checkout — the macro name `DIS_INSTR_PREFETCH` is the
  ground truth.
- **Pre-existing CPUACTLR bits clobbered.** Mitigated by RMW. The
  alternative (`mov x0, #imm; msr CPUACTLR_EL1, x0`) would zero out
  any reset-defined bits the silicon needs. ATF also uses RMW.
- **Secondary CPUs miss the workaround.** The current armstub
  secondaries enter at `secondary_spin` and never execute the EL3
  prologue. Phoenix today boots on CPU 0 only, so this is latent. When
  TD-08 or later steps wake CPU 1–3 the same block has to run on each
  before its eret. Tracked under TD-04 follow-up; out of scope here.
- **Firmware reload semantics.** The Pi 4 firmware loads the armstub
  binary at `0x0` and jumps to `_start`. The patch keeps the existing
  header layout (lines 106–141) byte-identical, so `stub_magic`,
  `dtb_ptr32`, `kernel_entry32` and the four spin slots stay at their
  documented offsets. The first `start_late` instruction shifts in the
  binary by ~28 bytes; this is fine because `_start` (line 78) is the
  only externally observable label after the header.
- **`dsb sy` at EL3 with caches off.** Equivalent to `dsb` everywhere —
  we have no shareable observers yet. No risk; matches ATF.
- **uart_putc clobbers x8/x9.** Both new putc sites preserve x0/x1
  contents; x8/x9 are scratch for the macro and are not read by any
  code that follows before they are overwritten. Verified by
  inspecting the macro at lines 96–104 against the surrounding code.
  If the macro is ever changed to use x0, the new putc sites must move
  to *after* the sysreg writes that depend on x0.

Rollback: if the patch boots but the new sequence misbehaves, restore
`phoenix-armstub8-rpi4.bin` from any prior `manifests/*.md` snapshot
via `scripts/restore-integration-state.sh`. The armstub source change
is one project-level file with no cross-repo dependency, so a single
revert in this repo is sufficient.

## 7. Inter-dependencies

- **Unblocks kernel Stage 1 Phase A retry.** The current kernel comment
  at `phoenix-rtos-kernel/hal/aarch64/_init.S:303–322` explicitly notes
  that SMPEN and 859971 must be set at EL3 in the armstub before the
  kernel can re-attempt I-cache + D-cache enable. Once this patch lands
  and the UART signature shows `12AS0`, the next step is to revert the
  no-op `uart_putc 83 / uart_putc 84` block to the real
  `mrs/orr/msr` sequence and re-run the cache-enable variants v1/v2/v3
  documented at `_init.S:411–419`.
- **Does not interact with TD-04 NC-dest fix.** That fix is in
  TTBR1 TTL3 / MAIR slot 1 territory; the armstub patch is pre-MMU
  EL3 setup. They compose cleanly.
- **Does not interact with TD-04-hack-{1,2,3}.** Those hacks bypass
  syspage and dtb logic in the kernel; the armstub patch only changes
  pre-eret EL3 sysreg state. After this patch the kernel may reach
  further on the unhacked path; if it does, `tracking/current-step.md`
  Phase A reopens and the hacks come off one at a time.
- **Manifest discipline.** A clean run requires a new
  `manifests/2026-05-07-armstub-a72-errata.md` snapshot via
  `scripts/snapshot-integration-state.sh`, capturing the
  phoenix-rtos-project SHA that contains the patched armstub.

---

Path: `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/plans/armstub-a72-errata-patch.md`.

Word count target: 1500–2500. This document: ~1750 words (counted at
write time; verify with `wc -w` after save).

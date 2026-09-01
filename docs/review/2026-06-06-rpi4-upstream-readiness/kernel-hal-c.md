# kernel-hal-c — upstream-readiness review

- **Area:** AArch64 HAL C/headers (BCM2711/Pi4 + zynqmp shared core)
- **Repo:** phoenix-rtos-kernel, base `57b30411` (origin/master) → head `6cdf217e` (master)
- **Files reviewed (changed hunks only; `.S` excluded — see kernel-init-asm):**
  hal/aarch64/{pmap.c, spinlock.c, cpu.c, hal.c, interrupts_gicv2.c/.h, pl011.c/.h,
  dtb.c/.h, exceptions.c, aarch64.h, arch/cpu.h, arch/interrupts.h,
  gtimer.c/.h, gtimer_backend.c/.h, gtimer_timer.c},
  hal/aarch64/generic/{console.c, generic.c, config.h, Makefile},
  include/arch/aarch64/generic/{generic.h, syspage.h}, hal/{cpu.h, timer.h},
  hal/aarch64/zynqmp/{Makefile, timer.c}.
- **Referents read:** hal/aarch64/zynqmp/console.c, hal/aarch64/zynqmp/timer.c,
  hal/armv8r/mps3an536/timer.c, hal/aarch64/_init.S, docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md.

---

## Findings (ordered by severity)

### 1. `generic/console.c:149-185` · **ARCH/BUG** · sev=high · NEEDS-HW
`hal_consolePrint()` writes every byte through `_hal_consoleEarlyPrint()` →
`_hal_consoleEarlyPutch()`, which targets the **hardcoded literal**
`(volatile u32 *)0xffffffffffe00000ull` (the `PL011_TTY_EARLY_VADDR` early alias). It
**never** uses `console_common.uart`, the base discovered via `dtb_getConsoleSerial()` and
mapped in `hal_pl011Init()`. Only `hal_consolePutch()` uses the mapped base. So the entire
dtb-driven console-base discovery is **dead for the primary print path**: the base is parsed,
translated and mapped, then never read by `hal_consolePrint` — every kernel/userspace `debug()`
line goes out the board-config early VA instead. WHY: hard-couples all console output to the
`PL011_TTY_EARLY_VADDR` early alias, making the `stdout-path`/`serial@` DTB discovery pointless
and diverging from the Phoenix convention. (Two sub-nits: the path also hardcodes the literal
`0xffffffffffe00000ull` rather than the `PL011_TTY_EARLY_VADDR` board_config macro.)
**Referent:** zynqmp `console.c:76-100` — `hal_consolePrint` → `_hal_consolePrint` →
`hal_consolePutch` → `*(console_common.uart + fifo)`, i.e. routes through the **mapped** base.
REC: make `_hal_consolePrint`/`hal_consolePrint` emit via
`hal_pl011Putch(&console_common.uart, c)` once `console_common.enabled`, keeping the hardcoded
early path only for the pre-init (`enabled==0`) window — mirror zynqmp's structure.
NEEDS-HW (console path, must confirm boot output on real Pi 4).

### 2. `generic/config.h:42-60` · **COMMENT** · sev=high · APPLY-SAFE
The multi-line comment states *"Until that wakeup path is implemented, **NUM_CPUS stays at 1**
so the primary boot path is unaffected,"* immediately above `#define NUM_CPUS 4U`. The comment
flatly contradicts the code. With `NUM_CPUS == 4U`: `spinlock.c`'s `#if NUM_CPUS == 1` branch
is dead (the real ldaxr/stxr lock is always used), `hal_cpuGetCount()` returns 4, and the
whole SMP control path (`hal_smpPrimaryReady`, per-CPU PPI enable) is live. Per MEMORY
(`project_smp_d7_d8_findings`: "current SMP HEAD is 4-cpu enum + cpu0-only scheduler") the 4U
is intentional, so the comment is simply stale/wrong in the most load-bearing config knob.
WHY: a maintainer reading this will believe SMP is disabled when it is not. REC: rewrite the
comment to describe the actual state (4 CPUs enumerated, scheduler cpu0-only, secondaries
parked behind `hal_smpPrimaryReady`); also drop the in-tree WIP commit SHAs `979e05c0` /
`6fa161c` / `750b7fd` (finding #9). APPLY-SAFE (comment-only).

### 3. `hal.c:111-117` · **ROLLBACK** · sev=med · APPLY-SAFE
Five unconditional `hal_consolePrint(ATTR_USER, "hi: console\n" … "hi: timer\n")` markers in
`_hal_init_c()`. These are diagnostic boot-stage markers with **no `TODO(TD-xx)` marker**.
The immediately-preceding comment documents that the TD-04-hack-2 `_hal_init` marker chatter
was *removed* 2026-05-17 (confirmed in TEMPORARY-FIXES doc: TD-04-hack-2 RESOLVED) precisely to
cut UART/HDMI-mirror chatter — these new `hi:` prints reintroduce the same class of noise.
WHY: diagnostic-only, not upstreamable, contradicts the documented cleanup. REC: delete the
five `hal_consolePrint(ATTR_USER, "hi: …")` lines. APPLY-SAFE (gate on `--scope core` + boot smoke).

### 4. `pmap.c:927-941` · **ROLLBACK** · sev=med · APPLY-SAFE
The `{ char buf[96]; … hal_i2s("pmap: banks=" …); hal_consolePrint(ATTR_USER, buf); }` block
prints a RAM-range summary on every boot. It carries a justifying comment ("first-boot
regression triage; not in any hot path") but **no `TODO(TD-xx)` marker**, so by the rubric it
is diagnostic code that should not ship to maintainers. WHY: unconditional debug output in
`_pmap_preinit`. REC: remove the block (or, if kept, gate behind `#ifndef NDEBUG` and add a
TD marker). APPLY-SAFE.

### 5. `generic/console.c:216` · **ROLLBACK** · sev=low · APPLY-SAFE
`_hal_consoleProbe(&console_common.uart, "console: pl011 init done\n");` — unconditional
init-time diagnostic banner, no TD marker. WHY: chatter; not present in the zynqmp referent's
`_hal_consoleInit`. REC: delete the probe call (and `_hal_consoleProbe` if it becomes unused).
APPLY-SAFE.

### 6. `gtimer_timer.c:1566-1607`, `interrupts_gicv2.c:1832-1838,1860-1862,1894-1899` · **ROLLBACK** · sev=med · NEEDS-HW (coordinated)
SMP "observability" counters `hal_smpTimerInitPerCpuCount[8]`,
`hal_smpInterruptsInitPerCpuCount[8]`, `hal_smpInterruptsEnabledPpiCount[8]` and their
`hal_cpuAtomicInc` bumps are pure diagnostic instrumentation (bring-up phase counters). They
are **printed in `main_initthr`** (kernel-core area), so removal is cross-area and cannot be a
clean local APPLY-SAFE edit. WHY: diagnostic-only, not upstreamable. REC: remove the counters
and their print site together in a single coordinated change with kernel-core. **Do NOT remove**
`hal_smpPrimaryReady` / `hal_smpFirstIntervalUs` (gtimer_timer.c:1571-1580,
interrupts_gicv2.c:1852) — those are functional SMP control state, not diagnostics. NEEDS-HW
(touches live SMP bring-up; coordinate with kernel-core).

### 7. `pmap.c:2208` · **STYLE** · sev=med · APPLY-SAFE
Merge artifact: two statements jammed on one physical line separated by a tab —
`pmap_common.mem.min = banks[0].start;⟶pmap_common.mem.max = banks[0].end;`. WHY: violates
one-statement-per-line; clearly an accidental hunk collision. **Referent:** every other
assignment in this function is on its own line. REC: split into two lines. APPLY-SAFE.

### 8. `pmap.c:2180-2184` · **ARCH** · sev=med · NEEDS-HW
The `nBanks == 0` guard is `while (1) { asm volatile("wfe"); }` (also 8-space indented, see #10),
a silent infinite WFE with no console output. WHY: the rest of the HAL halts via
`hal_cpuHalt()` — see exceptions.c `exceptions_serrorHandler` (this same diff) and generic.c
`hal_cpuReboot`, both looping on `hal_cpuHalt()`; a bare `wfe` is both stylistically
inconsistent and gives no diagnostic on a DTB-with-no-memory failure. `_hal_consoleInit` has
NOT run yet at this point (order in `_hal_init_c`: `_pmap_preinit` → `_hal_platformInit` →
`_hal_consoleInit`), but per finding #1 `hal_consolePrint` uses the early hardcoded UART which
already works pre-init, so an early diagnostic print here is viable. **Referent:**
exceptions.c:1096-1098 `for (;;) { hal_cpuHalt(); }`. REC: emit an early diagnostic then
`for (;;) { hal_cpuHalt(); }`. NEEDS-HW (failure path; document).

### 9. `generic/config.h:57-59` · **COMMENT** · sev=med · APPLY-SAFE
In-source WIP commit SHAs: *"See WIP commits 979e05c0 (kernel) / 6fa161c
(phoenix-rtos-project) / 750b7fd (plo)."* WHY: bare short-SHA references to unmerged WIP
commits are not meaningful to upstream maintainers and rot immediately. REC: drop the SHA list
(fold into finding #2's comment rewrite). APPLY-SAFE.

### 10. dtb.c (`dtb_parseSOC`, `dtb_parseResvMem*`, `dtb_parseInterruptController`), pl011.c (`enum`, `hal_pl011Init`) · **STYLE** · sev=med · APPLY-SAFE
Several new/edited blocks are indented with **spaces** while the surrounding file uses **tabs**:
- dtb.c:348-355 (`#size-cells`/`isRanges` early-returns in `dtb_parseSOC`),
- dtb.c:467-494 (the entire rewritten `dtb_parseInterruptController` body, 8-space),
- pl011.c:1963-1970 (`enum`) and pl011.c:1978-1997 (`hal_pl011Init` body, 8-space).
WHY: Phoenix uses tabs (clang-format `UseTab: ForIndentation`); mixed indentation will trip
clang-format CI. **Referent:** the rest of each of these files is tab-indented (e.g. dtb.c
`dtb_parseSerial`, pl011.c `hal_pl011Putch`/`hal_pl011Flush`). REC: re-run clang-format on the
two files. APPLY-SAFE.

### 11. `arch/cpu.h:129` · **COMMENT** · sev=med · NEEDS-HW
`hal_cpuEnableInterrupts` changed `msr daifClr, #3` → `#2`. This now unmasks IRQ only and
leaves **FIQ masked** (and is consistent with the GICv2 Group-1/Group-0 work in
interrupts_gicv2.c — the timer PPI is forced into Group 1, `TIMER_IRQ_GROUP`). The one-character
change has **no explanatory comment**, and pairs with `cpu.c`'s `NO_SERR` (which *is* well
documented under TD-10). WHY: a reviewer cannot tell whether masking FIQ is intentional policy
or a typo. REC: add a one-line comment ("leave FIQ masked: NS-EL1 routes timer/peripherals as
Group-1 IRQ; FIQ reserved for Group-0/secure" or the actual rationale). Behavior change is
intentional — document only, do not revert. NEEDS-HW.

### 12. gtimer split (`aarch64.h` inlines + `gtimer.c` + `gtimer_backend.c` + `gtimer_timer.c`) · **ARCH** · sev=low · (judgment, no change required)
The ARM architectural timer is implemented across **4 files / ~400 LOC**: register inlines in
`aarch64.h`, a source-select switch layer (`gtimer.c`), a stateful `hal_gtimerState_t` wrapper
(`gtimer_backend.c`), and the `hal/timer.h` driver (`gtimer_timer.c`). **Referent:**
hal/armv8r/mps3an536/timer.c implements the *same* ARM generic timer in a **single ~150-line
file** directly against `cnt*_el0`. VERDICT: the split is heavier than the Phoenix norm, but it
is **defensible** — the `dtb_timerSource_t` (virt vs phys-nonsecure) selection genuinely needs a
dispatch layer the mps3an536 single-source doesn't, and the `gtimer.c`/`gtimer_backend.c`/
`gtimer_timer.c` layering is cleanly consumed via the new `AARCH64_TIMER_IMPL_DEFAULT_OBJS`
make hook (verified consumed in hal/aarch64/Makefile:21-26), letting zynqmp keep its own
`timer.c`. Recommend collapsing `gtimer.c` (a thin source-switch) into `gtimer_backend.c` to
drop one file, but this is taste, not a blocker. No change required for correctness.

### 13. `dtb.c:323-328, 343-350, 397-403` · **STYLE** · sev=low · NEEDS-HW
New cell reads use `ntoh32(*(u32 *)dtb)` (unaligned cast-deref) where the rest of the parser
(and the new `dtb_readCells`) uses `hal_memcpy` into a local before `ntoh32`. WHY: minor
consistency / alignment-safety nit; the DTB property pointers are 4-byte aligned by spec so it
is not a live bug. **Referent:** `dtb_readCells` (same file, this diff) and the original
`dtb_parseSerial` `hal_memcpy(&base, dtb, 8)`. REC: route the `#address-cells`/`#size-cells`
single-cell reads through `dtb_readCells(dtb, 1, …)` or a `hal_memcpy`. Low priority.

---

## TD reconciliation
- **TD-04-hack-2 / TD-04-hack-3**: the hal.c comment block (hal.c:1658-1677) correctly documents
  these as cleaned up 2026-05-17 — consistent with TEMPORARY-FIXES (both RESOLVED). ✓ Accurate.
  However the *new* `hi:` markers (#3) re-introduce the same TD-04-hack-2 class of marker chatter
  without a TD entry.
- **TD-10**: cpu.c `NO_SERR` mask + exceptions.c `exceptions_serrorHandler` are correctly marked
  and match the doc (TD-10 PENDING; handler now exists, mask retained). ✓ No naive deletion.
- **TD-15**: dtb.c reserved-memory/dma-ranges parsing + pmap.c resv-region exclusion are marked
  `TD-15` and match the doc (MOSTLY RESOLVED; pmap consumes the regions). ✓ Markers consistent.

## Cross-area note (for kernel-init-asm reviewer)
pmap.c grows `kernel_ttl3` from `[N]` to `[PMAP_KERNEL_TTL3_TABLES][N]` (2 tables) inside the
`struct pmap_common` whose comment says *"The order of fields below must be preserved"* — this
shifts the offsets of `devices_ttl3` / `scratch_tt` / `scratch_page`. The kernel boots, so access
is presumably symbol-based, but the `.S` reviewer should verify `_init.S` does not depend on any
hardcoded post-`kernel_ttl3` field offset.

## DTB parser bounds — explicitly checked, no finding
`dtb_readCells` caps `cells <= 2`; all `tupleCells` sums are range-checked (`> 5U` / `> 4U`)
before use; `l` is only decremented inside an `l >= tupleCells*sizeof(u32)` guard; every array
write is gated by `n* < MAX_*`. No overflow/underflow on untrusted DTB input found.

---

## Summary
13 findings. **By category:** ARCH 3 (#1 dual-tagged BUG/ARCH, #8, #12), COMMENT 3 (#2, #9,
#11), ROLLBACK 4 (#3, #4, #5, #6), STYLE 3 (#7, #10, #13). **By severity:** high 2, med 8,
low 3. **Most important:** #1 — `hal_consolePrint` ignores the dtb-discovered,
`hal_pl011Init`'d UART base and always writes the hardcoded `0xffffffffffe00000` early alias,
so the new DTB console-base discovery is dead for the primary print path (zynqmp routes prints
through the mapped base). DTB parser bounds were scrutinized and are sound; gtimer 4-file split
is heavier than the Phoenix norm but defensible.

# plo EL2 MMU fix — design and ready-to-apply diff

Status: design proposal, not yet applied.
Author: agent
Date: 2026-05-10
Scope: Phoenix-RTOS Pi 4 bring-up, Step 3 of the canonical-idiom
alignment plan (plo runs with MMU + caches ON).

## 1. Problem statement

Step 3 was attempted on 2026-05-10 and reverted. The boot hangs at
the relocator's `TR3` UART marker with no plo banner. Root cause is a
mismatch between the exception level plo's MMU code assumes and the
exception level plo actually runs at on rpi4b.

plo's AArch64 MMU code unconditionally programs the **EL3** copies of
the translation registers:

- `sources/plo/hal/aarch64/mmu.c:65` — `sysreg_write(ttbr0_el3, ttbr0)`
- `sources/plo/hal/aarch64/mmu.c:66` — `sysreg_write(tcr_el3, tcr)`
- `sources/plo/hal/aarch64/mmu.c:67` — `sysreg_write(mair_el3, mair)`
- `sources/plo/hal/aarch64/mmu.c:77,79` — read/write `sctlr_el3` to
  enable the MMU
- `sources/plo/hal/aarch64/mmu.c:89,91` — read/write `sctlr_el3` to
  disable the MMU
- `sources/plo/hal/aarch64/mmu.c:55` — `tlbi alle3`
- `sources/plo/hal/aarch64/mmu.c:125` — `tlbi vae3, x`

The cache module makes the same EL3 assumption:

- `sources/plo/hal/aarch64/cache.c:120,125` — RMW on `sctlr_el3` from
  `cacheToggle`

These writes are valid only if the CPU executes at EL3.

On Phoenix's rpi4b platform plo executes at **EL2**, not EL3, because
the armstub deliberately drops one level before handing off:

- `sources/.../phoenix-armstub8-rpi4.S:76` —
  `#define SPSR_EL3_MODE_EL2H 9`
- `sources/.../phoenix-armstub8-rpi4.S:77-78` — `SPSR_EL3_VAL` mode
  field set to `EL2H`
- `sources/.../phoenix-armstub8-rpi4.S:205-209` — `msr spsr_el3,
  SPSR_EL3_VAL`; `msr elr_el3, in_el2`; `eret`
- `sources/.../phoenix-armstub8-rpi4.S:211` — landing label `in_el2:`

When plo's `hal_init` reaches `mmu_init()` at EL2, the first
`msr ttbr0_el3, x` is an EL2-illegal-system-register access. On A72
that produces an undefined-instruction exception, plo's vector table
dispatches into the EL2 sync handler (`_exceptions_dispatch`), and
because plo's exception path is not yet armed for usable output the
core spins, leaving the UART silent past `TR3`.

The relevant comment block in plo's generic hal already names both
options: `sources/plo/hal/aarch64/generic/hal.c:86-103`.

A second constraint is the A72 erratum 859971 workaround, which lives
in the armstub at `sources/.../phoenix-armstub8-rpi4.S:174-198`
(`CPUACTLR_EL1` write, EL3-only on A72 r0p3) plus the `CPUECTLR_EL1
SMPEN` bit at lines 181-184. Both writes must execute at EL3 — moving
them downstream into plo or kernel re-introduces the silent hang the
sweep already eliminated.

## 2. Path A — generalise plo to write the EL of the moment

### Approach

Add a small abstraction that resolves a logical register name (for
example `ttbr0`, `tcr`, `mair`, `sctlr`) to the concrete `*_ELx`
encoding at runtime, branching on `currentEL`. The cleanest shape on
AArch64 is a tiny helper layer per logical register, since the
assembler refuses to take a register name from a runtime value — the
EL number must be encoded statically in each `msr`/`mrs`. So we
materialise three call sites (one per EL) and pick at runtime.

Linux uses essentially this pattern (`arch/arm64/include/asm/el2_setup.h`,
`__nvhe`/`__vhe` selectors), and FreeBSD does the same in
`sys/arm64/arm64/locore.S` for early boot. The cost is moderate
boilerplate but no behavioural change for the existing zynqmp target,
which keeps writing `_EL3` because its `currentEL == 3`.

### Concrete diff (Path A)

The diff converts each EL-pinned access in `mmu.c` into a runtime
switch on `(sysreg_read(currentEL) & 0xc)`. There are five touch
sites:

```diff
--- a/sources/plo/hal/aarch64/mmu.c
+++ b/sources/plo/hal/aarch64/mmu.c
@@ static inline void mmu_invalTLB(void)
-    asm volatile ("tlbi alle3");
+    switch (sysreg_read(currentEL) & 0xcU) {
+        case 0xcU: asm volatile ("tlbi alle3" ::: "memory"); break;
+        case 0x8U: asm volatile ("tlbi alle2" ::: "memory"); break;
+        default:   asm volatile ("tlbi vmalle1" ::: "memory"); break;
+    }

@@ static inline void mmu_setTranslationRegs(...)
-    sysreg_write(ttbr0_el3, ttbr0);
-    sysreg_write(tcr_el3, tcr);
-    sysreg_write(mair_el3, mair);
+    switch (sysreg_read(currentEL) & 0xcU) {
+        case 0xcU:
+            sysreg_write(ttbr0_el3, ttbr0);
+            sysreg_write(tcr_el3, tcr);
+            sysreg_write(mair_el3, mair); break;
+        case 0x8U:
+            sysreg_write(ttbr0_el2, ttbr0);
+            sysreg_write(tcr_el2, tcr);
+            sysreg_write(mair_el2, mair); break;
+        default:
+            sysreg_write(ttbr0_el1, ttbr0);
+            sysreg_write(tcr_el1, tcr);
+            sysreg_write(mair_el1, mair); break;
+    }

@@ mmu_enable / mmu_disable
-    val = sysreg_read(sctlr_el3); ... sysreg_write(sctlr_el3, val);
+    val = mmu_readSctlr();        ... mmu_writeSctlr(val);

@@ mmu_mapAddr
-    asm volatile ("tlbi vae3, %0" :: "r"(vaddr));
+    switch (sysreg_read(currentEL) & 0xcU) {
+        case 0xcU: asm volatile ("tlbi vae3, %0" :: "r"(vaddr) : "memory"); break;
+        case 0x8U: asm volatile ("tlbi vae2, %0" :: "r"(vaddr) : "memory"); break;
+        default:   asm volatile ("tlbi vae1, %0" :: "r"(vaddr) : "memory"); break;
+    }
```

`mmu_readSctlr`/`mmu_writeSctlr` are new file-static helpers that
follow the same EL switch pattern. Together the change is roughly
60 lines added, 8 lines removed in `mmu.c`.

Plus the analogous change to `cacheToggle` in `cache.c`: replace the
single inline-asm block with a C-level RMW that reads `currentEL`,
masks to bits [3:2], reads/writes `sctlr_el3`/`sctlr_el2`/`sctlr_el1`
in a three-arm `if` exactly like `mmu_readSctlr`/`mmu_writeSctlr`
above. Same shape, same EL selection, ~25 lines of straight-line
code. Omitted here to keep the diff focused on the conceptual change;
the pattern is already established by the `mmu.c` hunks.

### Path A pros and cons

Pros:

- Single touch site: plo only. Armstub stays untouched, A72 errata
  workaround keeps running at EL3 where it must.
- Upstreamable: turns plo's MMU module into a generic AArch64 module
  that supports all three privileged ELs. Other Phoenix targets that
  later boot at EL2 (e.g. virtualized targets) get the fix for free.
- No change to plo's `hal_exitToEL1` path; the existing EL3/EL2/EL1
  branches in `generic/_init.S:271-301` already handle whichever EL
  plo started at.
- Mirrors the canonical Linux/BSD pattern.

Cons:

- ~30 extra lines of switch/case in two files. Readable but slightly
  verbose. A macro (`MMU_SYSREG(name)` expanded with token pasting)
  could compress this; left out of the diff because token-pasted
  `mrs`/`msr` operands need the EL chosen at compile time, which is
  what we are explicitly *not* doing. The runtime branch is the
  honest shape.
- `mmu_init`'s read of `id_aa64mmfr0_el1` (`mmu.c:151`) is unchanged
  — that register is readable from EL2 and EL3, so no fix needed.
- The cache module's `getL1DcacheID` writes `csselr_el1` and reads
  `ccsidr_el1` (`cache.c:23-24`). Both are accessible from EL2 and
  EL3 — no fix needed.

## 3. Path B — keep plo at EL3, drop to EL2 inside plo

### Approach

Modify the armstub to `eret` to plo at EL3 instead of EL2. plo runs
its existing EL3 code path in `_init.S:105-137`, calls `mmu_init`
with `_EL3` writes (current code, no change), and at the end of plo
either `hal_exitToEL1`'s `exit_el3` branch (`_init.S:280-288`) drops
to EL2 for the kernel, or plo can drop to EL2 right after MMU+cache
setup.

### Concrete diff (Path B)

Conceptually the armstub change is small but the control-flow shuffle
is real:

```diff
--- a/sources/.../phoenix-armstub8-rpi4.S
+++ b/sources/.../phoenix-armstub8-rpi4.S
-#define SPSR_EL3_MODE_EL2H 9
+#define SPSR_EL3_MODE_EL3H 13
 #define SPSR_EL3_VAL \
-    (... | SPSR_EL3_MODE_EL2H)
+    (... | SPSR_EL3_MODE_EL3H)

@@ before eret:
-    ldr x0, =0x30c50830
-    msr sctlr_el2, x0
-    adr x0, in_el2
+    adr x0, primary_cpu_late
     msr elr_el3, x0
     eret
```

But the `in_el2:` detour has to be removed entirely, and the
secondary-CPU spin loop plus `primary_cpu` jump (currently reached
*after* the EL2 drop) must be moved up to be reachable directly from
EL3. That's a ~30-line shuffle of armstub control flow, not a flag
flip. plo additionally needs a real EL3 vector table installed before
`mmu_init`, and a working EL3 → EL2 transition in `hal_exitToEL1`'s
`exit_el3` branch (`generic/_init.S:280-288`) for the kernel handoff.

### Path B pros and cons

Pros:

- Zero plo changes for the rpi4b platform.
- Matches zynqmp's historical assumption that plo runs at EL3.

Cons:

- The Phoenix kernel's own EL drop (`phoenix-rtos-kernel/hal/aarch64/_init.S`,
  EL2 → EL1) expects to be entered at EL2, not EL3. plo would have
  to grow an EL3 → EL2 transition before `eret`-ing into the kernel,
  via `hal_exitToEL1`'s `exit_el3` path (`generic/_init.S:280-288`).
  That path exists but has never been exercised in CI on rpi4b — it
  needs validation (e.g. `scr_el3` value 0x5b1 in `exit_el3:` is
  zynqmp-tuned and may need rpi4-specific bits).
- The `vectors_el2` table installed by the armstub (line 211-214)
  becomes dead code at EL3 entry; we'd need to install
  `vectors_el3` or rely on plo's own `_vector_table` setup at line
  131-132 of `generic/_init.S`. Either way the armstub gets simpler
  but plo has to provide an EL3 vector table that survives the
  whole plo lifetime — currently the relocator runs before plo's
  vbar_el3 is set up, so any spurious early exception at EL3 hits
  whatever vbar the firmware left.
- The A72 errata workaround (CPUACTLR_EL1, lines 191-194) already
  runs at EL3 in the armstub today — Path B keeps that property,
  which is good. Path A also keeps that property, since the
  armstub is unchanged. Net: neutral on errata.
- The `sctlr_el2 = 0x30c50830` write at line 202-203 (kernel's
  expected EL2 entry SCTLR) goes away. Kernel entry conditions
  change: kernel now sees plo's `sctlr_el2` (whatever plo wrote
  during its own EL3→EL2 drop), not the armstub's. Risk of
  regressing the kernel cache-coherency fix that just landed
  (TD-04).
- Touches the seam that has historically been the failure site
  (`docs/plans/phase-a-bisection-ladder.md` calls this out
  explicitly). Every round-trip on this seam costs a real-hardware
  build cycle.

## 4. Recommendation

**Path A.**

Justifications, in priority order:

1. **Blast radius.** Path A modifies one upstreamable plo module
   and leaves every other moving part — armstub, kernel _init,
   errata workarounds, kernel EL2 entry contract, TD-04 cache
   coherency fix — exactly as it is today. Path B churns the
   armstub seam, which has historically been the failure site and
   has no cheap rollback once the kernel observes a different
   `sctlr_el2` at entry.
2. **Upstreamability.** plo lives on multiple SoCs. Generalising
   `mmu.c` and `cache.c` to support EL2/EL1 entry is the change
   plo wants long-term anyway: more boards boot at EL2 (KVM/Xen
   guests, U-Boot non-secure handoffs) than at EL3. Path A
   contributes upstream value; Path B is rpi4-local plumbing that
   conflicts with what zynqmp does.
3. **A72 errata stability.** Path A keeps the
   `CPUACTLR_EL1`/`CPUECTLR_EL1` writes exactly where they are
   today (armstub, EL3, immediately after `scr_el3` is sane). Path
   B keeps them too, but disturbs surrounding code — increasing
   the chance of a regression in the errata sweep that just
   landed.
4. **Reversibility.** Path A is one or two commits in
   `sources/plo`. If Step 3 still fails for a different reason
   after applying Path A, a single revert restores Step 2. Path B
   couples plo and the project repo (armstub) in ways that need
   coordinated rollback.

The downside of Path A — extra code in `mmu.c`/`cache.c` — is
mechanical and reviewable. A maintainer reading the diff sees
exactly which `*_ELx` register is written for which EL. No magic.

## 5. UART signature, before vs after

Before the fix (Step 3 attempted, plo at EL2 writing `_EL3`):

```
... armstub: AS0 ...
... relocator: TR1 TR2 TR3
<silence — plo trapped at the first msr ttbr0_el3>
```

After the fix (Path A applied):

```
... armstub: AS0 ...
... relocator: TR1 TR2 TR3
hal: entry EL2
plo banner ...
<plo prompt or auto-jump>
hal: jump entry
hal: jump irq off
hal: jump exit el1
A
2     <- exit_el2 marker from generic/_init.S:290-296
<kernel banner>
```

The new diagnostic to watch for is "hal: entry EL2" — `hal_init`
already prints the entry EL via `hal_printCurrentEl`
(`generic/hal.c:56-75`). If that line appears, `mmu_init` ran past
the previously-failing `msr ttbr0_el3`. If MMU enable also succeeds,
plo's existing console output ("plo banner") will appear; that is
the canonical Step 3 success signal.

Failure signature to watch for:

- `hal: entry EL2` followed by silence: MMU programmed correctly but
  `mmu_enable`'s `sctlr_el2` write either turns on the MMU with a
  bad TT walk or hits a stage-2 issue. Diagnosis: print the value
  written to `sctlr_el2` before/after the OR; check `tcr_el2`
  IPS field matches `id_aa64mmfr0_el1` PARange.
- `hal: entry EL2` followed by an exception trace from plo's
  `_exceptions_dispatch`: a TT entry has UXN/PXN flipped against
  the page plo is currently fetching from. Diagnosis: dump
  `esr_el2`, `far_el2`, `elr_el2`.

## 6. Risks

1. **`tcr_el2` and `tcr_el3` field layout differ slightly.** TCR_EL3
   has bits [5:0] = T0SZ and [7:6] = reserved; TCR_EL2 (when
   `HCR_EL2.E2H == 0`) is laid out the same as TCR_EL3. Phoenix
   armstub's `hcr_el2` write at `generic/_init.S:125-126` sets
   bits 31 and 29 only — `E2H` (bit 34) is **not** set, so
   TCR_EL2 stays in the EL3-shaped layout. The `tcr` value plo
   computes today (`mmu.c:151-160`) is therefore valid for both
   `_EL3` and `_EL2` paths under Path A. **Verify** by reading
   back `tcr_el2` after the write and printing it once.
2. **`sctlr_el2` reset value already touched by armstub.** The
   armstub writes `0x30c50830` to `sctlr_el2` at
   `phoenix-armstub8-rpi4.S:202-203`. Phoenix's plo `mmu_enable`
   ORs in `M | C | I` on top of whatever `sctlr_el2` currently
   holds. If the armstub-written value contains stale bits,
   plo's enable could land in an unexpected state. **Mitigation:**
   `mmu_enable` should still be a read-modify-write (it already
   is); just confirm by inspection that the bits we OR in are
   the only delta we want.
3. **`tlbi vae2, x` requires the TTBR0_EL2 to have been programmed
   already.** plo programs TTBR0 then issues `tlbi`s on
   `mmu_mapAddr`; the order is fine. No risk here.
4. **`id_aa64mmfr0_el1` is read at EL2 in `mmu_init`
   (`mmu.c:151`).** This is allowed at EL2 unconditionally (the
   `_EL1` ID registers are common system regs, not the
   security-typed ones).
5. **Path A leaves zynqmp's behaviour exactly as it was.** The
   added switch picks `_EL3` whenever `currentEL == 3`, which is
   how zynqmp already runs (`zynqmp/hal.c:100` calls
   `_zynqmp_init` which does not change EL). Net behaviour for
   zynqmp: unchanged.
6. **TD-04 (kernel cache coherency).** The kernel-side fix that
   just landed assumes plo enters the kernel at EL2 with caches
   in a known state. Path A does not change the EL plo enters
   the kernel from (still EL2 → EL1 via `exit_el2`), so TD-04
   remains valid. The kernel will see caches that plo has
   actually used (instead of caches-off as today) — confirm by
   re-running the TD-04 probe.

## 7. Open questions

- **Should `mmu_invalTLB`'s EL1 case use `tlbi vmalle1is` (inner
  shareable broadcast) instead of `tlbi vmalle1`?** Probably not
  for plo (single-core during plo lifetime), but worth a one-line
  comment noting why we chose non-broadcast.
- **Is `csselr_el1`/`ccsidr_el1` in `cache.c:23-24` truly
  EL2-accessible, or does it need
  `HCR_EL2.TID2`/`TID4` cleared?** A72's HCR_EL2 reset value has
  these clear and the armstub does not set them
  (`phoenix-armstub8-rpi4.S:166-172` writes `cnthctl_el2` and
  `scr_el3` only). So plo's read at EL2 should work, but a
  one-shot probe before `mmu_init` is cheap insurance.
- **Will plo's existing EL3 code path in `_init.S:105-137` ever
  execute on rpi4b again under Path A?** No — armstub still
  drops to EL2, so plo always lands at `start_el2`. The EL3
  branch becomes rpi-unreachable but is still needed for zynqmp.
  Keep it, do not delete.
- **`mmu_disable` is currently called only from `hal_cpuJump` on
  zynqmp (`zynqmp/hal.c:264`); rpi4 generic does not call it.**
  Once Step 3 lands, the rpi4 generic `hal_cpuJump` will need
  the same `mmu_disable` + cache flush sequence. That is a
  separate diff (Step 4 of the canonical-idiom plan) and should
  not be combined with this one.
- **Do we want a compile-time `PLO_AARCH64_FIXED_EL` knob to skip
  the runtime branch on platforms that always boot at one EL?**
  Possibly, as a follow-up — measure first; the branches are
  compile-trivial and the runtime cost is one `mrs currentEL`
  per `mmu_setTranslationRegs` call (one per boot, not on hot
  paths).

---

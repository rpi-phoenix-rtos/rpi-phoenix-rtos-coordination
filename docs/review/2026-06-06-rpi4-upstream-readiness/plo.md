# plo — upstream-readiness review (2026-06-06)

- **Area:** `plo` (bootloader)
- **Repo:** `sources/plo` · base `origin/master` `ce4eab9` → head `master` `ae05823` (24 files, +2385/-35)
- **Files reviewed (changed hunks only):** `Makefile`, `README.md`, `_startc.c`,
  `cmds/{call.c,go.c,vbe.c}`, `plo.c`, `syspage.{c,h}`, `hal/hal.h`,
  `hal/aarch64/Makefile`, `hal/aarch64/{mmu.c,cache.c}`,
  `hal/aarch64/generic/{Makefile,_init.S,config.h,console.c,hal.c,interrupts.c,timer.c,types.h,video.c}`,
  `ld/{aarch64a72-generic.ldt,aarch64a53-generic.ldt}`
- **Referents:** `hal/aarch64/zynqmp/*` (the existing aarch64 plo port), `ld/common/plo-aarch64.lds`.

Net: this is the plo aarch64 `generic` (BCM2711/Pi4) port. The architecture is sound and
closely mirrors the zynqmp aarch64 port for the C HAL pieces (types.h is a verbatim copy;
console/interrupts/timer follow the same shape). The dominant upstream-readiness problem is
**pervasive diagnostic scaffolding** that was never stripped — most prominently a diagnostic
`_vector_table` in `_init.S` that disables real exception/IRQ dispatch. TD-05 (the documented
"debug-marker strip") is the governing item for almost all of it and is marked "LARGELY
RESOLVED — residual prints to remove case-by-case"; this diff is the residual.

---

## Findings (ordered by severity)

### 1. `hal/aarch64/generic/_init.S:478-668` · ROLLBACK · sev=high
**WHAT:** The entire exception-vector machinery is diagnostic. `_vector_table` (lines 619-670)
replaces every SP_Elx slot with an `exc_tag` halt-and-print stub, and slot 0x200 branches to
`_slot_e_dump` (lines 540-617), a 78-line ESR/ELR/FAR/SCTLR/TTBR/TCR/MAIR hex-dump probe. The
supporting `uart_put_hex64` / `uart_put_hexnibble` / `exc_tag` macros (lines 490-528) exist only
to serve it. The block's own comment (478-489) says: *"After diagnosis we either fix the offending
exception cause or restore the original vector table."*
**WHY:** The zynqmp referent (`hal/aarch64/zynqmp/_init.S:268-282`) routes 0x200/0x280/0x300/0x380
to `_exceptions_dispatch` / `_interrupts_dispatch`. Here the IRQ slot (0x280) is `exc_tag 'F'`
(halt), so **no vector routes to `_interrupts_dispatch` and `interrupts_dispatch()` is never
reached** — see finding 2. This is self-admitted temporary code and the single biggest blocker
to presenting the file.
**REC:** Restore the canonical vector table (model on zynqmp `_init.S:252-311`): SP_Elx sync/SError
→ `_exceptions_dispatch`, IRQ/FIQ → `_interrupts_dispatch`, lower-EL slots → `b .`. Delete
`_slot_e_dump`, `uart_put_hex64`, `uart_put_hexnibble`, `exc_tag`. **NEEDS-HW** (changes fault
control flow; must re-verify a clean boot still reaches the kernel and that no latent SError now
lands in a live dispatcher).

### 2. `hal/aarch64/generic/interrupts.c` (whole file) · ARCH · sev=med
**WHAT:** `interrupts_init()` programs the GICv2 and `interrupts_dispatch()` is a correct
dispatcher, but because of the diagnostic vector table (finding 1) no vector branches to
`_interrupts_dispatch`, so dispatch is unreachable in the current build. Additionally, on this
target nothing registers a handler: `hal_interruptsSet` has no caller in the generic tree
(`PLO_ALLDEVICES := ram-storage` per `generic/Makefile`; console and timer are polled — see
`console.c` busy-wait and `timer.c` `cntpct` read).
**WHY:** This is *not* dead code to delete — `hal_interruptsSet`/`hal_interruptsEnable` are the
public plo HAL API every target implements (cf. `hal/aarch64/zynqmp/interrupts.c`). The IRQ path
is here for parity / future drivers, not active use. The finding is that the GIC bring-up is
inert until finding 1 is fixed, and a maintainer should know the path is currently untested
end-to-end.
**REC:** Fix finding 1 (restore dispatch routing) so the GIC path is at least reachable; note in
the commit message that no generic-target driver currently registers an IRQ handler, so the path
is parity/future-use. **NEEDS-HW** (coupled to finding 1).

### 3. `hal/aarch64/generic/hal.c:440-528` (`hal_cpuJump`) · ROLLBACK · sev=med
**WHAT:** Litters the kernel-handoff path with print probes: `"hal: jump entry"`, `"hal: jump irq
off"`, `"hal: jump exit el1"`, and the cold-boot marker sequence `Cd` / `Ci` / `Id` / `Ii` / `Md`
(lines 514-522) whose comment explicitly says *"The Cd/Ci/Id/Ii/Md markers stay until cold-boot
stability is confirmed."*
**WHY:** This is the most-scrutinised handoff in the loader and these one-letter markers are pure
TD-05 scaffolding. zynqmp `hal_cpuJump` carries no such prints.
**REC:** Remove the `hal: jump *` strings and the `Cd/Ci/Id/Ii/Md` prints; keep the cache/MMU
teardown calls themselves (`hal_dcacheInvalAll`, `hal_icacheInval`, `mmu_disable`). **APPLY-SAFE**
(pure print removal; teardown logic unchanged).

### 4. `hal/aarch64/generic/hal.c:79-101,233-295,424-437` · ROLLBACK · sev=med
**WHAT:** `hal_printHex64` + `hal_readBe32` + the `hal_printHex64(...)` dump calls in
`hal_syspageSet` (lines 277-278, 288-289) and `hal_printCurrentEl`. The DTB-acceptance logic
itself is real (TD-06), but the four hex dumps of `hal_firmwareDtb` / `armstub[0xf8]` / DTB
addr+size are diagnostic.
**WHY:** TD-06 is PENDING and the DTB read is legitimate, but the verbose per-boot hex tracing is
TD-05-class noise; no other target prints its syspage internals like this.
**REC:** Drop the four `hal_printHex64` dump calls in `hal_syspageSet`; keep one terse
`"plo: firmware DTB accepted/rejected"` line (already present). Remove `hal_printHex64` if it then
has no callers. `hal_readBe32` stays (used by the accept check). **APPLY-SAFE** (print removal).

### 5. `hal/aarch64/generic/hal.c:87-101,177-179` · COMMENT/STYLE · sev=med
**WHAT:** `hal_memoryInit`'s 15-line comment (87-101) is a Step-3 *bisection diagnostic* narrative
("Expected post-fix UART output: ...", "Whichever marker DOES print..."), and `hal_init` keeps the
inline note *"console_init runs BEFORE hal_memoryInit (caches-off PL011 MMIO writes work without
MMU)"* plus a chain of `hal_consolePrint("hal: ... done\n")` progress prints (lines 178-215) and a
large dead-narrative comment block (187-214) describing abandoned SMP Phase A/D experiments.
**WHY:** These document a debugging session, not the code. zynqmp `hal_init` is a plain sequence of
init calls with no progress prints. The SMP narrative refers to `PLO_SMP_ENABLE` paths that are
compiled out by default.
**REC:** Reduce the `hal_memoryInit` comment to the factual remap-policy paragraph (lines 109-119
are good; delete 87-101). Remove the `"hal: ... done"` progress prints. Collapse the SMP narrative
(187-214) to one line. The print removal is **APPLY-SAFE**; the `console_init`-before-
`hal_memoryInit` *reorder* is behavioral — if you also want to restore canonical ordering that is
**NEEDS-HW** (document only; leave the order as-is for the print-strip batch).

### 6. `cmds/go.c:31-50` · ROLLBACK · sev=med
**WHAT:** The only change to this shared command file is diagnostic: `lib_printf("\ngo: enter")`,
`"go: devs done"`, `"go: hal done"`, `"go: jump"`, and capturing `res = hal_cpuJump()` to print
`"go: jump returned %d"`. The original `/* Never reached */` comment was deleted.
**WHY:** `go.c` is shared across all targets; these prints are TD-05 scaffolding. `hal_cpuJump`
returns `int` in all impls (`hal/hal.h:56`), so capturing the return is fine, but it exists only
to feed the print.
**REC:** Revert `cmd_go` to the upstream body (call `hal_cpuJump();`, restore `/* Never reached */`,
`return CMD_EXIT_FAILURE;`). **APPLY-SAFE** (shared-file print removal; verify zynqmp still builds).

### 7. `cmds/call.c:28-31,61-65,82-86,101-104` · ROLLBACK · sev=med
**WHAT:** Adds `cmd_callTrace()` (returns true iff the script name is the literal `"user.plo"`) and
three trace prints (`"call: opened ..."`, `"call: magic ok ..."`, `"call: exec ..."`). `cmds/call.c`
already exists upstream (the README's "new 'call' command" is inaccurate — only the trace was added).
**WHY:** Hard-coding a magic filename `"user.plo"` to gate debug output is a debugging hook, not a
feature; it is shared-file noise.
**REC:** Delete `cmd_callTrace` and the three trace blocks; restore `cmd_call` to upstream.
**APPLY-SAFE** (print/branch removal in shared file).

### 8. `_startc.c:20,53-61` · ROLLBACK/ARCH · sev=med
**WHAT:** Adds `extern char __heap_base[], __heap_limit[]` and an unconditional
`hal_memset(__heap_base, 0, __heap_limit - __heap_base)` with a TD-05 comment ("zero the heap so
any byte ... reads back as 0 ... was showing up as bit-level nondeterminism across boots on Pi 4").
**WHY:** `_startc.c` is the **shared** plo entry, compiled for every target. `__heap_base`/
`__heap_limit` are exported by all `ld/common/plo-*.lds` (verified: `plo-aarch64.lds:136-139`,
also arm/ia32/riscv64/sparc), so it links everywhere — but it imposes a boot-time heap wipe on
zynqmp/ia32/etc. that have no such Pi-4 nondeterminism, and the comment itself says it *masks* an
unexplained symptom rather than fixing it.
**REC:** This is the only changed `_startc.c` hunk and is generic-only motivated; move it behind a
target gate or into generic init, and ideally root-cause the nondeterminism instead of masking it.
**NEEDS-HW** (the comment states removal can re-expose observed nondeterminism on Pi 4; document,
do not blind-apply).

### 9. `hal/aarch64/generic/video.c:59-60` · ROLLBACK · sev=low
**WHAT:** `tag_getclkrate = 0x30002u, /* TD-16-1: get_clock_rate */` and `tag_clkid_arm = 0x3u`
enum members are declared but never used — the mailbox sequence in `video_framebufferInit` never
issues a get_clock_rate tag.
**WHY:** TD-16-1 is RESOLVED and was stripped elsewhere (per `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`
line 1821: "probe served its purpose, stripped in plo `c988e6a`"). These two members are leftover.
**REC:** Delete both enum members. **APPLY-SAFE** (dead declarations).

### 10. `hal/aarch64/generic/_init.S:302-420,151-214 (hal.c)` · ROLLBACK · sev=low
**WHAT:** `secondary_smoke_entry` SMP smoke trampoline (prints `cN: alive` via raw PL011 MMIO) plus
the `secondary_handoff` block guarded by `PLO_SMP_ENABLE`. hal.c's `secondary_smoke_entry` extern
+ `hal_smpBringupSecondaries` narrative (151-214) are the C side. `PLO_SMP_ENABLE` is not defined
in the generic `config.h`, so all of this is compiled out by default and the C extern is unused.
**WHY:** This is an abandoned/parked SMP experiment carried as defense-in-depth; it is dead in the
default build and the long comments describe debugging history.
**REC:** Either gate the whole secondary path behind one clearly-documented `PLO_SMP_ENABLE` block
(including `secondary_smoke_entry`, which is currently always assembled but never branched to in the
default build) or remove it pending a real SMP step. Trim the hal.c narrative to a one-line pointer.
**NEEDS-HW** (touches the handoff `_init.S`; document — but the dead C extern and the `cN: alive`
smoke print are safe to drop).

### 11. `hal/aarch64/generic/video.c:102-128` (`video_mailboxCall`) · ARCH · sev=low
**WHAT:** Writes `mbox_write` without first polling `status & mbox_full == 0`; the comment
hand-waves it ("the VC4 firmware has the mailbox FIFO drained from previous boots, so the write can
proceed without an explicit wait").
**WHY:** The canonical VC property-mailbox protocol is: poll FULL clear before write, poll EMPTY
clear before read. The project's own userspace mailbox-driver pattern (`diag_mboxProp1in1out`, the
basis for `sensors/rpi4-thermal`) checks FULL before writing. Skipping it relies on prior-boot
state and is fragile on a cold first boot.
**REC:** Add a `while ((status & mbox_full) != 0) {}` spin before the `mbox_write` store, mirroring
the read-side wait already present. **NEEDS-HW** (mailbox timing; document).

### 12. `hal/aarch64/generic/_init.S:530-540` · COMMENT · sev=low
**WHAT:** `_slot_e_dump`'s comment header says it "Reads EL2 exception registers (ESR_EL2, ELR_EL2,
FAR_EL2, SPSR_EL2)" but the code reads `esr_el1`/`elr_el1`/`far_el1`/`spsr_el1`.
**WHY:** Stale comment (plo runs at EL2 but reads the *_el1 banks here). Low value because the whole
block should be deleted (finding 1).
**REC:** Subsumed by finding 1 (delete `_slot_e_dump`). If kept, fix the comment. **APPLY-SAFE.**

---

## Notes (not findings)

- **mmu.c / cache.c EL-aware generalisation is good.** The `currentEL`-dispatch refactor
  (mmu.c `mmu_currentEL` switch on EL3/EL2/EL1 sysreg banks; cache.c `cache_readSctlr`/
  `cache_writeSctlr`) is the correct way to share the file between zynqmp (EL3) and generic (EL2),
  and the TCR_EL2 IPS-relocation comment (mmu.c) is accurate and load-bearing. No finding.
- **`syspage_graphmodeSet` by-value → by-pointer** (syspage.c/h + the `cmds/vbe.c` call-site fix
  `&graphmode`) is a clean, correct, shared-file improvement. No finding.
- **`hal/aarch64/Makefile` generalisation** (wildcard `$(TARGET_SUBFAMILY)/Makefile` instead of a
  hard-coded `zynqmp` check) is a clean, backward-compatible improvement. No finding.
- **`README.md` fork-warning banner** is appropriate for the not-yet-upstreamed state; remove before
  actual upstream submission.
- **types.h** is a verbatim copy of zynqmp/types.h (correct convention; matches the per-target
  pattern). No finding.

## TD reconciliation

- **TD-05** (debug-marker strip, "LARGELY RESOLVED — residual prints to remove case-by-case") is the
  governing item for findings 1,3,4,5,6,7,8,10. This diff is exactly the residual TD-05 calls for.
- **TD-06** (DTB robustness, PENDING): the DTB-read logic in `hal_syspageSet` is legitimate TD-06
  work; only its hex-dump prints (finding 4) should go.
- **TD-16-1** (RESOLVED, stripped): video.c's `tag_getclkrate`/`tag_clkid_arm` (finding 9) are
  leftover from this resolved item and should be removed.

---

## Summary

12 findings: ROLLBACK ×7 (1 high, 4 med, 2 low), ARCH ×2 (med, low), COMMENT/STYLE ×2 (med, low),
ROLLBACK/ARCH ×1 (med). The port's *structure* is upstream-quality — the mmu/cache EL-dispatch
refactor, the Makefile generalisation, and the by-pointer graphmode fix are all clean — but the
code is saturated with un-stripped TD-05 diagnostic scaffolding (one-letter UART markers, hex
dumps, hard-coded `"user.plo"` trace gate, heap-wipe in shared `_startc.c`).

**Most important issue:** `_init.S`'s diagnostic `_vector_table` (finding 1) — every exception slot
is a halt-and-print stub instead of routing to `_exceptions_dispatch`/`_interrupts_dispatch` (cf.
zynqmp `_init.S:268-282`). It is self-admitted temporary, and as a side effect the entire GICv2
dispatch path (`interrupts.c`) is unreachable in the current build. This must be restored before the
port is presentable. **NEEDS-HW.**

Most findings are APPLY-SAFE print removals (3,4,6,7,9, and the print-only parts of 5/10); findings
1,2,5(reorder),8,10,11 are behavioral/NEEDS-HW and should be documented, not blind-applied overnight.

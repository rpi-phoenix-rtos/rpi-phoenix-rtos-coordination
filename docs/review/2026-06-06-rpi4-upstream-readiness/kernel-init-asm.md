# kernel-init-asm — upstream-readiness findings

- **Area:** kernel early-boot / MMU / cache / SMP startup assembly
- **Repo:** `phoenix-rtos-kernel` — base `57b30411` → head `6cdf217e` (origin/master → master)
- **Files:** `hal/aarch64/_init.S` (+1257), `hal/aarch64/_exceptions.S`, `hal/aarch64/_memset.S`, `hal/aarch64/Makefile`
- **Note:** `_init.S` / `_exceptions.S` / `_memset.S` / `Makefile` are **shared** between the `zynqmp` and `generic` (Pi4) aarch64 subfamilies (`hal/aarch64/{zynqmp,generic}/`). Unconditional changes here affect both targets; compile-gated (`#if defined PL011_*`, `__TARGET_AARCH64A72`) and runtime-gated (EL2-entry) changes do not.

Findings ordered by severity.

---

## HIGH

### 1. `_init.S:569-585` · COMMENT · sev=high
**WHAT:** A live comment block immediately above the SCTLR write reads *"Turn on MMU. Caches stay OFF (C=0, I=0) — known-good baseline … Reverting to known-good (EL drop + M-only) until research wave returns."* The code directly below (`_init.S:640-645`) unconditionally sets `SCTLR_EL1.M | C | I` in one write. The comment asserts the exact opposite of what the code does, on the single most safety-critical instruction in the file.
**WHY:** A maintainer reading top-down concludes caches are disabled and will reason wrongly about every coherency decision downstream (syspage copy attrs, barriers, the `_memset` ZVA guard). This is archaeological residue: an old "reverted to M-only" narrative was never deleted when M|C|I was re-enabled (TD-16 RESOLVED 2026-05-17, confirmed in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` status table). Two further stacked blocks (`586-599`, `607-639`) re-describe the *same* flip three times.
**REC:** Delete the stale `569-585` "caches stay OFF / reverting" block entirely. Collapse the three remaining narrations (`586-599`, `607-621`, `623-639`) into one short comment: "Enable MMU+D-cache+I-cache (SCTLR.M|C|I) in a single write; A72; canonical barrier ritual; defensive set/way invalidate immediately prior." Keep the barrier ritual code unchanged. **APPLY-SAFE** (comment-only).

### 2. `_init.S:151-159, 173-184, 776-791, 1366-1372` · COMMENT · sev=high
**WHAT:** Four separate live comments assert the page holding `_hal_syspageCopied` is mapped **Normal Non-Cacheable (AttrIndx=1 / `NC_ATTRS`)** as the TD-04 workaround:
- `151-159` ("Used for the single TTL3 entry that maps the page containing `_hal_syspageCopied` … bypass the cache entirely")
- `173-184` ("The TD-04 NC override on the single `_hal_syspageCopied` TTL3 entry is deliberately preserved")
- `776-791` ("The high-VA TTL3 entry was just overridden to AttrIndx=1 (Normal Non-Cacheable), so str instructions … bypass the A72 D-cache")
- `1366-1372` (.bss: "That single entry is overridden in `_start` to use Normal Non-Cacheable attributes")

But `_init.S:515-533` ("Phase Z3 … TD-04 NC override DELETED") removed that override, and the syspage TTL3 pages (`PMAP_COMMON_KERNEL_TTL3`/`_TTL3_1`) are now filled with `DEFAULT_ATTRS` (0x703 = cacheable) at `_init.S:501-513`. `NC_ATTRS` is now referenced **only** for the TD-15 mailbox alias at `_init.S:492`. `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` confirms TD-04 RESOLVED (cleanup complete). So the syspage copy now goes through a **cacheable** mapping; the comments claiming it bypasses the D-cache are factually inverted.
**WHY:** This is the TD-13 lead flagged in the review brief. A reviewer trusting these comments believes the syspage destination is uncached and will mis-diagnose any future syspage-coherency bug (and may "restore" a workaround that is already intentionally gone). Most dangerous: the `776-791` comment sits on the live copy loop and justifies the choice of the high-VA pointer on a cache-bypass premise that is no longer true.
**REC:** Rewrite all four comments to state current reality: `_hal_syspageCopied` is mapped cacheable (`DEFAULT_ATTRS`); coherency relies on plo's cache-on + `dc civac` DDR teardown plus the kernel's `_clean_inval_dcache_range` over `[x9, x9+size)` at `_init.S:756`. Fix the `NC_ATTRS`/`151-159` comment to say it now serves only the TD-15 mailbox alias. Update the `.bss` alignment comment (`1366-1372`) to drop the "overridden to NC" claim (page-alignment is still wanted for clean TTL3 bookkeeping, just not for an NC override). **APPLY-SAFE** (comment-only; verify no code path still depends on the NC mapping — grep confirms none does).

---

## MEDIUM

### 3. `_init.S:340-345` · ARCH · sev=med
**WHAT:** New code unconditionally enables FP/SIMD (`CPACR_EL1` FPEN=0b11) and SVE (ZEN=0b11) for **all** aarch64 targets, with rationale comment *"like Circle OS and bare metal examples."* Upstream trapped FP here (`mrs x0, cpacr_el1; and x0, x0, #(~(0x3 << 20))`).
**WHY:** (a) The build compiles `-mcpu=$(cpu)+nofp` (`hal/aarch64/Makefile:15`) — the kernel is explicitly built without FP, so enabling EL1 FP access is at best inert and at worst hides accidental FP use that `+nofp` is meant to forbid. (b) The change is **unconditional**, so it also flips zynqmp's FP-trap policy away from upstream. (c) A72 (armv8.0) has no SVE, so the ZEN bits (16:17) write a field that is RES0/unimplemented on this core — meaningless. (d) Citing "Circle OS / bare metal examples" is not an upstream-appropriate rationale.
**REFERENT:** The upstream line this replaced (the `cpacr_el1` FP-trap masking in the same function) and the `+nofp` CFLAG in `hal/aarch64/Makefile:15`. The kernel-wide no-FP convention is the established Phoenix policy for this arch.
**REC:** If FP truly must be enabled (e.g. a driver uses it), drop the ZEN bits and the "Circle OS" comment, set only FPEN, and gate it so zynqmp keeps its upstream behavior; otherwise revert to the upstream FP-trap. Document *why* FP is needed given `+nofp`. **NEEDS-HW** (changes EL1 FP-trap semantics for both targets; confirm nothing in the boot path executes FP).

### 4. `_init.S:1074-1110, 69-74` · ROLLBACK · sev=med
**WHAT:** A cluster of UART debug helpers with **zero callers**: macro `uart_tag2` (`69`), and routines `uart_putc_reg` (`1074`), `uart_puthex4` (`1084`), `uart_puthex64` (`1094`), `_early_uart_print_tag` (`1104`). Grep across `hal/aarch64/` shows `uart_putc_reg` is referenced only inside a *comment* (`944`, describing a removed bug); the rest call only each other. None has a `TODO(TD-xx)` marker.
**WHY:** Dead diagnostic scaffolding from the cache-enable investigation. Carrying it into an upstream presentation invites "what calls this?" review churn and bloats `.init`.
**REC:** Delete `uart_tag2`, `uart_putc_reg`, `uart_puthex4`, `uart_puthex64`, `_early_uart_print_tag`. **APPLY-SAFE** (verified callerless; gate on `--scope core` build + boot smoke).

### 5. `_init.S:1246-1261` · ROLLBACK · sev=med
**WHAT:** `.asciz` tag strings `_early_ex_tag` ("EX="), `_early_esr_tag` ("ESR="), `_early_elr_tag` ("ELR="), `_early_far_tag` ("FAR=") are defined but never referenced — the live early-exception dump (`_early_exception_common`, `1175-1207`) emits these labels via inline `early_putc_inline` char-by-char, not by pointing at these strings.
**WHY:** Orphaned data from an earlier string-walker version of the dump (the one that used `_early_uart_print_tag`). No marker.
**REC:** Delete the four `.asciz` tags. **APPLY-SAFE** (verified unreferenced).

### 6. `_init.S:212` · ROLLBACK · sev=med
**WHAT:** `uart_putc 75` — a live "K" cold-boot diagnostic marker emitted as the first thing after baud reinit, with a long explanatory comment but **no `TODO(TD-xx)` marker**.
**WHY:** Pure diagnostic output in the hot boot path; per the review's ROLLBACK category, diagnostic code lacking a TD marker should be surfaced. It is genuinely useful for "did `_start` run at all," but emitting a bare `K` to the console on every production boot is not upstream-appropriate.
**REC:** Either remove it, or gate it behind a debug macro (e.g. the same `PL011_TTY_EARLY_VADDR`/`NDEBUG`-style gate the rest of the early-UART scaffolding could share) and add a `TODO(TD-05)` marker (TD-05 = the debug-marker strip/gate item in the cleanup doc). **APPLY-SAFE** if removed; **NEEDS-HW** is not required (pre-userspace, observable on UART).

### 7. `_init.S:1114-1163` (`TODO(TD-16-exdump)`) + `711` · COMMENT · sev=med
**WHAT:** The early-exception dump carries `TODO(TD-16-exdump)`, but TD-16 (and TD-16-cache-enable) are **RESOLVED** per the cleanup-doc status table (2026-05-17). The dump's own comment says it is "Kept across builds as the diagnostic floor for any future early-MMU/cache transition" — i.e. intentionally permanent. A permanent facility tagged with a now-closed TD is contradictory. Separately, `_init.S:711` states "Cache enable (TD-16 RESOLVED 2026-05-17)" which is correct and a good anchor.
**WHY:** TODO-marker hygiene: a `TODO(TD-16-*)` pointing at a resolved TD will read as stale/forgotten to a maintainer, yet the code is deliberately retained. The intent (permanent early-fault floor vs. temporary scaffolding) is ambiguous.
**REC:** Decide and state the intent. If the dump stays permanently, drop the `TODO(TD-16-exdump)` and reword as a plain "permanent early-exception diagnostic" comment (no TD). If it is meant to be removed before upstreaming, keep a marker but point it at an open cleanup item (TD-05), not resolved TD-16. **APPLY-SAFE** (comment/marker only).

### 8. `_memset.S:24-31` · COMMENT · sev=med (BUG-adjacent, document)
**WHAT:** New `#if defined(__TARGET_AARCH64A72) && !defined(MEMSET_WITHOUT_ZVA)` block force-defines `MEMSET_WITHOUT_ZVA`, disabling the `dc zva` fast path (used at `_memset.S:122`) for A72. The comment attributes this to a real Pi-4 hang in `_log_init`'s memset and the unproven EL2 DC-ZVA trap state.
**WHY:** This is a legitimate, correctly-gated workaround (`__TARGET_AARCH64A72` only, so zynqmp keeps ZVA). But it is a functional behavior change premised on "until the EL2 trap state for DC ZVA is proven," with no TD marker linking it to the TD-10/EL2 work, so it risks being silently forgotten. Not a bug; the gating is sound.
**REC:** Add a `TODO(TD-xx)` marker tying this to the EL2/DC-ZVA-trap investigation so it is revisited once that state is proven, and so it is not mistaken for an unconditional perf regression. Keep the gate. **NEEDS-HW** to lift (the hang is HW-only, doesn't repro in QEMU per the comment); the marker itself is **APPLY-SAFE**.

---

## LOW

### 9. `_init.S` (added blocks throughout) · STYLE · sev=low
**WHAT:** Many added regions use **8-space indentation** (e.g. EL2→EL1 drop `_init.S:319-345`, the dcache/icache range helpers `1048-1131`, `_early_exception_common` and the early vector table `1175-1261`), while the file and surrounding upstream code are **tab-indented** (e.g. the unchanged `_fill_page_descr`/`hal_cpuInvalDataCacheAll` bodies). The TCR_EL1_VALUE hunk was reflowed tabs→spaces with no value change.
**WHY:** Mixed tab/space indentation in one file fails clang-format consistency and is the first thing an upstream maintainer flags.
**REFERENT:** The tab-indented bodies elsewhere in the same `_init.S` (e.g. the upstream `_fill_page_descr` and `_set_up_vbar_and_stacks` bodies) and Phoenix's clang-format rule (tabs) used across `hal/aarch64/*.S`.
**REC:** Reindent all added blocks to tabs to match the file. Revert the cosmetic tabs→spaces reflow of `TCR_EL1_VALUE` (value is byte-identical to upstream). **APPLY-SAFE** (whitespace only).

### 10. `Makefile:8-32` · ARCH · sev=low
**WHAT:** The platform dispatch was generalized from the hardcoded `findstring zynqmp` include to a generic `PLATFORM_MAKEFILE := hal/aarch64/$(TARGET_SUBFAMILY)/Makefile` wildcard include, plus a new timer-impl override mechanism (`AARCH64_TIMER_IMPL_OVERRIDE` / `_DEFAULT_OBJS`) and unconditional `gtimer.o gtimer_backend.o`.
**WHY:** This is a clean, upstream-friendly generalization (good). One nit: `interrupts_gicv2.o` was previously added inside the zynqmp branch; verify it is now contributed by the per-platform Makefiles for **both** zynqmp and generic (otherwise zynqmp loses its GIC driver). The diff only shows it removed from the shared Makefile.
**REFERENT:** The previous shared-Makefile zynqmp branch (the `-OBJS += ... interrupts_gicv2.o` line in this diff) and `hal/aarch64/zynqmp/Makefile` / `hal/aarch64/generic/Makefile` (out of this area's file list — cross-check in `kernel-hal-c`).
**REC:** Confirm `interrupts_gicv2.o` is present in both per-subfamily Makefiles; no change if so. **NEEDS-HW** only for zynqmp regression (Pi4 path is exercised by the netboot smoke); cross-reference `kernel-hal-c` review.

---

## TD reconciliation (review step 4)

- **TD-04 (RESOLVED, cleanup complete):** code correctly deleted the NC override (`_init.S:515-533`); but four comments (Finding 2) still describe it as live — comment debt, not code debt.
- **TD-10 (PENDING):** the `_exceptions.S` `daif #7→#2` changes and the `hal_jmp` `NO_SERR` at user entry are the live TD-10 SError-masking mitigation — correctly marked with an in-source TD-10 comment (`_exceptions.S` `hal_jmp`). No action; this is the intended state until the PCIe/USB external-abort source is fixed. NB: the whole `_exceptions.S` daif cluster (exception/syscall dispatch `#7→#2`, interrupt dispatch dropping the `#4` SError unmask/remask) is **one** coherent change — keep IRQ (`I`, bit 1) unmasked, keep SError (`A`, bit 2) and FIQ (`F`, bit 0) masked. It is unconditional and therefore also changes zynqmp's dispatch masking; that is the intended TD-10 behavior but should be called out in the upstream cover note.
- **TD-16 / TD-16-cache-enable (RESOLVED):** correctly reflected at `_init.S:711`; but the stale "caches off" block (Finding 1) and the `TODO(TD-16-exdump)` marker on a permanent facility (Finding 7) contradict the resolution.
- **TD-15 (mostly resolved):** `NC_ATTRS` mailbox alias (`_init.S:492`) is correctly `PLO_RPI_MAILBOX_BUFFER_ADDRESS`-gated; cleanup doc notes no in-source TD-15 marker yet (out of this area's strict scope).

## Summary

- **Counts:** 10 findings — COMMENT 4 (2 high, 2 med), ROLLBACK 3 (med), ARCH 2 (1 med, 1 low), STYLE 1 (low). High=2, med=6, low=2. Plus a `_memset.S` workaround that is sound but unmarked.
- **Most important issue:** The file's comments are stratified archaeological layers that **actively misdescribe the most dangerous code**: live comments say caches are OFF (they are M|C|I ON) and say the syspage page is mapped Non-Cacheable (it is now cacheable; TD-04 override deleted). Both are the same failure mode — new comment layers stacked atop old ones without deleting the obsolete ones — and both are comment-only fixes (APPLY-SAFE) that materially reduce the risk of a future maintainer mis-diagnosing a coherency bug.
- Dead UART/diagnostic scaffolding (Findings 4-6) is safe to excise after a build+boot smoke. The FPU-enable (Finding 3) and the daif/TD-10 cluster are the unconditional changes that also touch zynqmp and warrant an explicit upstream cover note.

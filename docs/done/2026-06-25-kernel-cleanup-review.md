# Phoenix-RTOS Pi4 kernel — cleanup & upstreamability review (2026-06-25)

READ-ONLY review of the BCM2711/aarch64 kernel changes in
`sources/phoenix-rtos-kernel` (branch `agent/rpi4-program-reloc`, HEAD
`90ce5766`) to inform **Phase B** of
`docs/inprogress/2026-06-25-cleanup-upstreamability-plan.md`. No edits made.

## Headline conclusion

The kernel tree is in **much better shape than the TD-doc prose implies**. The
`TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` Tracking Checklist (its own authoritative
table) is the accurate state; the per-item narrative prose above it is mostly
*pre-resolution* and reads far hackier than the source actually is. **Verified in
source:** only **4 `TODO(TD-…)` markers** remain kernel-wide, **zero** `/Users/`
macOS paths, and the boot-marker scaffolding (TD-05) is gone. What remains is
(a) **one genuine active workaround** (TD-20 DC-ZVA gate), (b) **one
documented-limitation pair** masquerading as a hack (TD-10 SError mask, TD-13/TD-11
single-core atomics), and (c) **low-priority prose/diagnostic trimming** for
publication. There is essentially **no unexplained kludge** left in the kernel.

A framing point the report keeps separate (per reviewer guidance): a `TODO(TD-NN)`
*marker* is a cleanup/correctness signal; a *comment that merely references a
resolved TD* is low-priority prose. Conflating them overstates hackiness. Below,
the two are tagged distinctly.

---

## Prioritized cleanup table (value-per-risk order)

Disposition key: **REMOVE** (delete code/marker), **DOC** (keep code, fix/relabel
comment or move to a known-limitation note), **ROOT-CAUSE** (needs a focused
investigation before it can be removed). Unattended = safe to do/validate over
netboot without HW/JTAG.

| # | File:line | What it is | Disposition | Effort | Risk | Unattended? |
|---|---|---|---|---|---|---|
| 1 | `hal/aarch64/_init.S:699` | `mov x14, x9` — the "E2 probe" stash. x14 is **set and never read** (its consumer probe was deleted; only the 695/731 comments still mention it). Dead instruction. | REMOVE (instr + the 695-700 comment) | S | Very low | ✅ yes |
| 2 | `hal/aarch64/_init.S:695-700, 712-720, 729-736` | Bring-up *narrative* comments: the "E2 probe" stash rationale, the "(Reverted: experimental pre-copy DEST flush…)" do-not-redo note, the "earlier comment here noted instruction-side TTBR1 fetch is broken" meta-commentary. Code is correct; comments are investigation chatter. | DOC (trim to a 2-3 line factual note on why the copy goes through the high-VA NC-then-cacheable path) | S | Very low | ✅ yes |
| 3 | `hal/aarch64/_init.S:1039-1044` | Inside the (legitimate, keep) `_early_exception_common` header comment: "Rejected by the previous agent on the grounds that real Pi was 'slow not crashed' — but the user clarified…". Informal session chatter inside otherwise-good doc. | DOC (delete the 1039-1044 paragraph; keep the rest of the block + the dumper code) | S | Very low | ✅ yes |
| 4 | `lib/lib.h:35` | `TODO(TD-13)` marker on the single-core `lib_atomicIncrement/Decrement` DAIF-masked fallback. **TD-13 is RESOLVED** (user-mode silence). This code is actually the **TD-11/TD-01 single-core-scope** decision; the TD-13 tag is **stale/misattributed**. | DOC (retag `TODO(TD-13)` → a known-limitation note: "single-core build uses DAIF-masked atomics; SMP builds (`NUM_CPUS!=1`) use `__atomic_*` — see below 'known limitations'"). Do **not** remove the code. | S | Low | ✅ yes |
| 5 | `hal/aarch64/hal.c:146,148,150,152,154` | `hal_consolePrint(ATTR_USER, "hi: console/exc/intr/cpu/timer\n")` — bring-up progress banners in `_hal_init_c()`. TD-05-class residual (not a `.asciz` marker, but the same intent). **Verified not consumed by the UART-summary tooling** (`grep "hi: cpu" scripts/` = no match), so removing them won't blank stage-health rows. | REMOVE (delete; or gate behind a default-on debug flag if you want to keep the trace) | S | Low | ✅ yes |
| 6 | `hal/aarch64/hal.c:98-117` | ~20-line historical comment block narrating the now-removed TD-04-hack-2/-3 markers + fake `dtbEnd`. Good diff-history value, but verbose dead-history for a published tree. | DOC (compress to ~3 lines: "dtbEnd now read from `firmwareDtbSize`/`dtb->end`; earlier Pi4 cache-coherency workaround removed 2026-05-17, see TD-04") | S | Very low | ✅ yes |
| 7 | `hal/aarch64/_init.S` (199 of 867 indented lines use 8-space) | TD-05-flagged indentation inconsistency: the early-exception/early-vector macros (l.70-73), the TCR `#define` block (l.119-132), and the MMU-setup block (l.195-219) use 8-space indent against the file's tab convention. | DOC/REMOVE (reindent to tabs to match the file + kernel style) | M | Low (whitespace-only; assemble-identical) | ✅ yes (but noisy diff — do as its own commit) |
| 8 | `hal/aarch64/_memset.S:24,31-33,113` | `TODO(TD-20)` — A72 `dc zva` disabled in `hal_memset` (`MEMSET_WITHOUT_ZVA`). **Genuine active workaround.** Gate is correctly A72-scoped (zynqmp keeps ZVA). | ROOT-CAUSE then REMOVE, **or** DOC as limitation | M (root-cause is HW/EL2-trap work) | Med (perf only; correctness safe) | ❌ HW-gated (EL2 DC-ZVA trap proof; does not repro in QEMU) |
| 9 | `hal/aarch64/cpu.c:79,83`; `hal/aarch64/_exceptions.S:386`; `arch/cpu.h:41` | `NO_SERR` mask (TD-10). SError handler **is implemented + registered** (`exceptions.c:208 exceptions_serrorHandler`, wired at `:401`). Mask stays because unmasking exposes a live PCIe/VL805 external-abort SError. | DOC as limitation (handler ready; unmask blocked on the USB/PCIe abort root-cause) | L | High (unmasking regresses boot) | ❌ HW-gated (needs the PCIe/VL805 NACK root-caused) |
| 10 | `log/log.c:407-440` (`RPI4_LOG_TO_FILE`, unconditional UART mirror) | #31 change: clean compile-time gate (defaults 0), panic path never gated, well-commented. But the **unconditional per-byte `hal_consolePutch` mirror** of every klog byte is a Phoenix-fork divergence from the upstream ring-buffer/reader model. | DOC (call out as a deliberate Pi4 observability choice; upstreamable only behind the board flag, not as default kernel behavior) | S (doc only) | Low | ✅ yes |
| 11 | `hal/aarch64/pmap.c:947-961` | `pmap: banks=… min=… max=…` one-line RAM-range summary at boot. Low-volume, genuinely useful for first-boot triage (cf. Linux `mem:` line). | DOC (keep; or gate if upstream reviewers object — borderline) | S | Very low | ✅ yes |

---

## TD-by-TD classification (hack vs documented limitation)

Verified against source, not relayed from the doc.

### Resolved in source — markers/code already gone (Area 1)
- **TD-13** (user-mode silence): RESOLVED. No `TODO(TD-13)` except the **misattributed** one at `lib/lib.h:35` (item #4 — actually TD-11-class).
- **TD-16 / TD-16-cache-enable** (caches on since 2026-05-17): RESOLVED. `SCTLR_EL1.M|C|I` is enabled in `_init.S`. **No `TODO(TD-16)` markers** — only explanatory comments at `_init.S:207,564,657,1022` referencing the resolution (prose-trim candidates, low priority; they document *why* the cache-enable sits where it does — arguably keep).
- **TD-02** (pre-MMU cache inval): **RESOLVED in source** — the doc narrative says "PENDING" but the Tracking Checklist and the code agree: `_init.S:554-556` has a live `_inval_dcache_range` over `PMAP_COMMON_KERNEL_TTL2 … PMAP_COMMON_STACK` before the first `SCTLR_EL1.M` write (restored by `5e727dcc`). Classify as **resolved limitation note**, not a hack. *Doc fix: retire the stale "PENDING" narrative.*
- **TD-17 / TD-18** (cache hygiene / uncached zone): RESOLVED. Spot-checked: `vm/zone.c:45`, `vm/amap.c:211,214` all map `MAP_NONE` (cacheable); **no `MAP_UNCACHED`** remains on those paths. Boots to psh.
- **TD-04 / TD-04-hack-1/-2/-3**: RESOLVED. `hal.c` reads the real `dtbEnd`; no markers. Only the verbose history comment (item #6) remains.
- **TD-03** (syspage copy / BSS mapping shortcut): **RESOLVED-limitation.** The doc narrative says "PENDING," but the Tracking Checklist says "RESOLVED by Stage 1 pre-MMU syspage copy," and source agrees: `_init.S:737+` copies the syspage through the high-VA mapping (cacheable, with the pre/post `_clean_inval_dcache_range`), and the early MMU map now covers the destination — the original "BSS not reliably mapped" concern is retired. No marker in source. *Doc fix: retire the stale "PENDING" narrative (same as TD-02).*
- **TD-05** (UART marker scaffolding): RESOLVED. `grep .asciz|main_uartMark` = 0. Residual = the `hi:` banners (item #5) + indentation (item #7).

### Genuine known-limitations — DOCUMENT honestly, do not "fix" as a hack
- **TD-10 — SError masked** (item #9). The honest story: a correct dump-and-halt handler exists and is armed; the mask stays only because real Pi4 has a live, currently-unfixed external-abort SError in PCIe/VL805 USB bring-up (`esr=0xbf000002`, IMP-DEF, isolation-proven: 0 SErrors with USB disabled). This is a **legitimate documented limitation**, not a kludge. Realistically fixable only by root-causing the bridge NACK (HW-deep, tied to the USB investigation) — **not unattended**.
- **TD-20 — DC-ZVA gate** (item #8). Sound A72-scoped gate; a functional perf change premised on an unproven EL2 DC-ZVA trap state that hangs only on real HW. **Document as limitation**; removal needs HW EL2-trap proof — **not unattended**.
- **TD-11 / TD-01 — single-core scheduler + DAIF atomics** (item #4). A real scope decision (SMP enumerates 4 cores but cpu0-only scheduling in the shipping config). The `NUM_CPUS==1` DAIF-masked spinlock + `lib_atomic*` fallback are **correct** for that scope and switch to real `LDAXR/STLXR` + `__atomic_*` when `NUM_CPUS!=1`. **Document as "single-core scheduler; SMP is future work,"** keep the code.
- **TD-19 — TLBI/PTE hardening**. The `_pmap_writeTtl3` invalidate-after-write (`pmap.c:435+`, `pmap_tlbInval` at `:154`) is present and correct; **upstreamable as-is, keep code, trim doc entry only.** ⚠️ **Discrepancy worth noting:** the TD-19 doc claims "TLBI helpers now end with `dsb; isb`, not just `dsb`," but in source the `hal_tlbInval*` helpers (`aarch64.h:201-241`) end with `hal_cpuDataSyncBarrier()` = **`dsb ish` only** — no trailing `isb`. Either the doc overstates what landed, or the ISB lives only on the `_pmap_writeTtl3` path. Reviewer should reconcile before claiming the ARMv8 break-before-make sequence is complete in the generic helpers.
- **TD-06 / TD-12 / TD-15-residual** — DTB-driven memory layout. Kernel side is DTB-driven (`dtb.c` parses `/memory@0` + `/reserved-memory` + `/soc/dma-ranges`; `pmap.c` consumes them; 4 GiB usable). Residual = plo's *static* `map ddr/ddrh` (mis-maps 2/8 GiB boards) — **not in the kernel**, and HW-gated (needs 2/8 GiB boards). Document as a portability limitation.

---

## Upstreamability assessment

Whole-kernel diff vs `origin/master`: **~3,813 insertions across 45 files.**

**Clean BCM2711-platform additions (upstreamable to phoenix-rtos with light cleanup):**
- `hal/aarch64/generic/` (new board: `config.h`, `console.c`, `generic.c`, `Makefile`) — net-new platform dir, mirrors the zynqmp pattern. Clean.
- `hal/aarch64/{pl011.c/.h, gtimer*.c/.h, interrupts_gicv2.c}` — standard SoC driver glue.
- `hal/aarch64/dtb.c` (+632 lines: `/reserved-memory`, `/soc/dma-ranges`, `dtb_armToBus`) — genuine, reusable DTB infrastructure. One stale `TODO` at `dtb.c:62` ("on ZynqMP this is not populated") is pre-existing, not Pi4 cruft.
- `hal/aarch64/exceptions.c` SError handler (+87 lines) — correct, dormant infrastructure; upstreamable.
- `hal/aarch64/pmap.c` (+131: DTB-bank build, reserved-region marking) — clean.

**Phoenix-fork-specific shims (upstreamable only behind a board flag, or document as port-local):**
- `log/log.c` unconditional UART mirror + `RPI4_LOG_TO_FILE` gate (item #10) — Pi4 observability choice, not default kernel behavior.
- `lib/lib.h` single-core DAIF atomics (item #4) — a scope decision, gated on `NUM_CPUS==1`.
- `proc/name.c` `TODO(TD-14-devfs-direct)` fast-path (`:34`, `:258`) — devfs namespace special-case; the doc itself flags it as "special-cases a well-known name instead of fixing generic dcache semantics." A reviewer-visible shim, not in scope here (proc/, not hal/aarch64) but noted for Phase D.3.

**Diagnostic-print sweep (definitive):** a full unscoped grep of `hal/aarch64/`
for `hal_consolePrint`/`hal_consolePutch`/`lib_printf` surfaces **only** the `hi:`
banners (`hal.c`, item #5) and the `pmap:` RAM summary (`pmap.c:960`, item #11) as
boot-time diagnostics. Everything else is either the print *implementation*
(`generic/console.c`, `zynqmp/console.c`) or the **legitimate fault reporters** in
`exceptions.c` (general exception dump `:182`, SError halt `:213-214`, watchpoint
`:261-262` — all keep). So: the kernel boot path carries **two** removable/gateable
diagnostic prints, nothing more.

**`_init.S` (+1,145 lines)** is the file that most needs a polish pass before it reads as upstream-quality: the indentation (item #7) and the bring-up narrative comments (items #1-3) are the bulk of the "looks like a port-in-progress" surface. None of it is *incorrect* — it's all whitespace + prose.

---

## "Honest known-limitations" section (document, don't hide)

These belong in a published `KNOWN-LIMITATIONS` / SHOWCASE note, phrased as design
notes — they are defensible engineering decisions, not defects:

1. **SError is masked (TD-10).** A real handler is implemented and armed; the mask
   remains because real Pi4 hardware has a live external-abort SError originating in
   the PCIe/VL805 USB controller bring-up that has not yet been root-caused.
   Unmasking before that fix regresses boot. Tracked; HW-deep.
2. **DC-ZVA disabled on Cortex-A72 in `hal_memset` (TD-20).** The first large
   cache-line-zeroing op via `dc zva` hangs on real Pi4 (not in QEMU), pending proof
   of the EL2 DC-ZVA trap state set by the firmware/armstub path. Normal stores are
   used instead — correct, slightly slower.
3. **Single-core scheduler (TD-01/TD-11/TD-13-atomic).** Four cores enumerate but
   the shipping config schedules on cpu0 only; single-core builds use DAIF-masked
   atomics/spinlocks. Full SMP scheduling is future work. The code already switches
   to real exclusives + `__atomic_*` when `NUM_CPUS!=1`.
4. **Memory layout / board portability (TD-06/TD-12/TD-15-residual).** Validated on
   4 GiB Pi4B. plo's static syspage map would mis-size 2 GiB / 8 GiB boards; the
   kernel page allocator itself is already DTB-driven. Needs those boards to validate.
5. **`/soc/dma-ranges` parsed but not wired into drivers.** GENET/USB/SD use identity
   `va2pa()` (correct today — they sit on the `scb` bus). Do **not** naively apply
   `dtb_armToBus` (the legacy `0xc0000000` alias) to GENET — it would break working
   Ethernet. Per-bus, only where non-identity.

---

## Recommended unattended execution order (the safe ones)

Pure-text / netboot-smoke-validated, value-per-risk:

1. **Items #1, #2, #3, #6** — `_init.S` dead `x14` + narrative-comment trims, `hal.c`
   history-comment compression. One small commit; netboot-smoke. (Lowest risk.)
2. **Item #4** — retag the stale `TODO(TD-13)` in `lib/lib.h` to a known-limitation
   note. Doc-only behavior; build-validate.
3. **Item #5** — gate or remove the `hi:` `_hal_init_c` banners. Build + netboot-smoke
   to confirm boot still reaches `(psh)%`.
4. **Item #7** — `_init.S` reindent to tabs. **Its own commit** (whitespace-noisy);
   verify assemble-identical + netboot-smoke.
5. **Items #10, #11** — documentation calls (log mirror, pmap summary); no code change
   needed unless reviewers object.
6. **Trim the TD-doc narrative** so the "PENDING" prose for TD-02 (and similar)
   matches the authoritative Tracking Checklist + verified source state.

Deferred (HW/JTAG-gated, attended): **#8 TD-20 DC-ZVA**, **#9 TD-10 SError unmask** —
document as limitations now; root-cause later with a board in hand.

**Reconcile before publishing:** the **TD-19 `dsb;isb` discrepancy** (doc claims a
trailing `isb` the generic `hal_tlbInval*` helpers don't have). Confirm whether the
ISB is genuinely required on those paths or only on `_pmap_writeTtl3`; correct
whichever of doc/code is wrong. This is the one item where the doc and source
disagree on a *correctness*-relevant claim.

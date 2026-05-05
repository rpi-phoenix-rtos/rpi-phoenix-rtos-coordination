# Current Implementation Step

## Active step (2026-05-05): Stage 2 — 4 GiB DRAM unlock (after Stage 1 cache enable parked)

**Roadmap:** `docs/roadmap-cache-ram-smp.md` (single source of truth for
the 4-stage trajectory: caches → 4 GiB DRAM → SMP → HDMI/USB).

### Stage 1 cache-enable: PARKED after 4 real-Pi cycles (2026-05-04 → 05)

The Stage 1 architectural refactor (Linux `__enable_mmu` shape: all PT
and syspage writes pre-MMU; single SCTLR.M|C|I flip; no memory writes
post-flip) was attempted across 4 real-Pi netboot cycles. Every cycle
produced a silent hang on real Pi 4 silicon while QEMU passed cleanly:

| # | Variant | Image SHA | Real Pi result |
|---|---|---|---|
| 1 | All PT/syspage writes pre-MMU; single SCTLR.M\|C\|I flip; TTBR0 RAM blocks NC | `4a5575b3` | Silent hang at X3 (post-flip, pre-`br x0`) |
| 2 | + TTBR0 RAM blocks switched to Normal Cacheable IS (match TTBR1) | `4f8c5ea7` | Silent hang at X3 |
| 3 | + Linux 2-step SCTLR (C+I in baseline, M-only at flip) | `1d219133` | Silent hang at X3 |
| 4 | + CPUECTLR_EL1.SMPEN write at EL1 | `823c84bc` | Silent hang at L→M (SMPEN write itself trapped — armstub already sets SMPEN at EL3, EL1 access likely traps to EL2) |

**Pattern:** all 4 real-Pi cycles hang somewhere between the first
SCTLR write that affects cache state and the first post-flip
instruction. No exception fires (no-call exception dump emits
nothing). QEMU passes every variant. Suggests a BCM2711-specific
hardware coherency interaction (SCU / L2 cache state / firmware-
shared coherency domain) that doesn't show up in QEMU's cache model.

**What landed at HEAD `49ca0c66`** (current baseline, restored):
- VA-range cache helpers: `_clean_inval_dcache_range`,
  `_inval_dcache_range`, `_inval_icache_range`.
- No-call early exception dump (kernel `2a5b6a05`).
- TTBR0 NC-block alias safety (`7f7684c4`).
- Early TTBR0 drop after syspage copy (`d52f6c3a`).
- Restored pre-MMU PT invalidation (`5e727dcc`).
- 5-iteration cache-enable comment block (kernel `49ca0c66`).

The 4 Stage 1 cycles never landed kernel commits — they existed only
in working-tree experiments. The repo state at the end of Stage 1 is
identical to its state at the start (kernel HEAD `49ca0c66`, working
tree clean). The diagnostic infrastructure remains in place for any
future cache-enable attempt.

**Stage 1 follow-up requires:** lab-grade debugging access (JTAG +
real-time trace, or ARM RealView equivalent) to observe what the core
does between the SCTLR.M write and the first post-flip instruction.
This is beyond the scope of UART-only iteration. Tracked for a
future investment session in `docs/TEMPORARY-FIXES-AND-FUTURE-
CLEANUP.md` under TD-16-cache-enable.

### Pivot to Stage 2

Stage 2 (4 GiB DRAM unlock + GPU memory partitioning) is the user's
explicitly-stated near-term goal and is independent of Stage 1
caches. With Stage 1 parked, Stage 2 work proceeds at the cache-off
boot speed (~7 min real-Pi cycle).

Stage 2 progress:

**Phase 5 (config.txt `total_mem=4096` + `gpu_mem=64`):
ATTEMPTED → REVERTED.**
The 2026-05-05 cycle showed:
- Firmware *ignored* `total_mem=4096` (still reported `Starting ARM
  with 960MB`). Pi 4 firmware on this board treats the device as
  1 GiB-aware unless something else (newer firmware, hardware ID
  detection) says otherwise. Setting `total_mem` higher than detected
  is a clamp-only parameter on Pi 4 — it cannot enlarge ARM-visible
  RAM beyond what firmware auto-detects.
- `gpu_mem=64` (vs the firmware default 76 in 1 GiB-mode) regressed
  boot: kernel hung at K→L (between SCTLR baseline write and the L
  marker). Likely cause: HDMI/VC4 firmware needs the larger reserve
  to keep scanout buffers stable at 1024×768×32bpp.
- `phoenix-rtos-project` master `4bd9c83` reverts the change (revert
  of `dd419e1`).
- True 4 GiB unlock will require either a firmware update or a
  different mechanism (not just config.txt). Tracked as a follow-up.
- Phase 5 lesson: kernel-side phase 4 (parse what firmware actually
  reports via DTB) is the structurally correct path; the firmware-
  side total_mem trick is unreliable.

**Phase 4a (kernel DTB parsers for /reserved-memory + /soc/dma-
ranges): LANDED 2026-05-05 in kernel `e5b768fa`.**
- New `dtb_resvMemRegion_t`, `dtb_dmaRange_t`.
- New parser state machine for /reserved-memory (root node + child
  nodes' reg property → up to MAX_RESV_REGIONS=16).
- /soc/dma-ranges parsing extends the existing `dtb_parseSOC` (same
  shape as ranges).
- Public accessors: `dtb_getReservedMemory`, `dtb_getDmaRanges`,
  `dtb_armToBus(cpuAddr, busAddr)`.
- Build clean. QEMU rpi4b + generic both reach `(psh)% help`.
- Real-Pi validation deferred to phase 4b (which adds first kernel-
  side use; any parser regression would surface there).

**Phase 4b (kernel allocator integration): LANDED 2026-05-05 in
kernel `832cc1f2`.** First real-Pi PASS in this Stage 2 work
iteration:
- `pmap_common.mem.resvRegions[]` cache populated from
  `dtb_getReservedMemory()` during `_pmap_init`.
- `_pmap_getPage` walks the cached list and marks pages inside any
  reserved region `PAGE_OWNER_BOOT` instead of `PAGE_FREE`.
- Real-Pi netboot:
  `artifacts/rpi4b-uart/rpi4b-uart-20260505-152214-netboot-stage2-phase4b-allocator-integration.log`
  reaches `(psh)%`. End-to-end validation that:
  - Phase 4a parsers populate correctly on the Pi 4 DTB.
  - Phase 4b allocator path doesn't orphan any page the kernel
    needs.

**Phase 4c (NEXT): drop plo's hardcoded `SIZE_DDR = 0x3b400000` in
`sources/plo/ld/aarch64a72-generic.ldt`; have plo build the syspage
memory entries from the DTB instead of the LDT constant.** This is
the last piece for the kernel to actually USE the larger ARM_MEM
when firmware reports more.

**Phase 2, Phase 3, Phase 6:** queued behind phase 4c. Mailbox-buffer
relocation (Stage 2 phase 2), VC4 quiesce (phase 3), DMA audit
across pcie/xhci using `dtb_armToBus()` (phase 6).

### Previous step framing

### Previous step framing

### Stage 1 success criteria

- Real Pi 4 netboot reaches `(psh)%` in **under 30 s** capture window
  (currently ~420 s with caches off).
- TD-16-1 nop-loop probe reports `dt ≈ 0x23280` (≈144,000 ticks at
  1.5 GHz with caches enabled), down from `0x872d51` (~62× off) today.
- QEMU Pi 4 + generic AArch64 smoke both reach `(psh)% help`.
- No exception markers in the UART log; no-call exception dump is in
  place to catch any new fault.

### Stage 1 plan

1. **Inventory pass (this session, no code change):** read
   `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` end-to-end and
   produce a written list of every memory store between the existing
   `SCTLR_EL1.M` write and `b main`. Include syspage copy, TD-04 NC
   mapping for `_hal_syspageCopied`, any TTL3 fixup, vector-table
   install, post-copy `_clean_inval_dcache_range`, the `TTBR0_EL1`
   drop to scratch table (kernel `d52f6c3a`), etc. Save the inventory
   into this file under "Stage 1 inventory" so the next session has
   the exact list.

2. **Refactor pass (next session):** for each store identified in
   step 1, either:
   - Move it to *before* the SCTLR.M write (preferred), or
   - Document why it cannot move (must run in high VA).
   Stores that must remain in high VA force a smaller refactor: keep
   them, but ensure their target regions are explicitly cleaned to
   PoC before SCTLR.M and that no PT entry mutation happens between
   the flip and `b main`.

3. **Cache-maintenance unification:** replace `bl
   hal_cpuInvalDataCacheAll` (set/way; A72 #851672 makes this
   unreliable) with one VA-range pass over the union of:
   - kernel image `[__init_start, __init_end]` and
     `[_kernel_image_start, _kernel_image_end]`
   - PT region `[PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK]`
   - syspage destination region (if it stays distinct from the kernel
     image after step 2)
   Reuse the existing `_clean_inval_dcache_range` and
   `_inval_icache_range` helpers (kernel `49ca0c66`).

4. **Single atomic SCTLR write:** combine M | C | I (and any clear
   bits like nTLSMD as needed) into one MSR. Validate the encoding
   matches Linux's `__cpu_setup` output for ARMv8.0-A.

5. **Minimal post-flip path:** `isb` → `tlbi vmalle1is` → `dsb ish`
   → `isb` → `br x0` to `_core_0_virtual` → register-only stack
   pointer setup → `b main`. **No memory writes between the flip and
   `b main`.**

6. **Validation:** rebuild → QEMU Pi 4 smoke → generic smoke → real
   Pi netboot. If real Pi reaches `(psh)%`, capture TD-16-1 numbers
   and snapshot a manifest. If it faults, the no-call exception dump
   prints ESR/ELR/FAR; iterate on whichever store didn't move
   correctly.

### Rollback baseline

- Kernel `49ca0c66` on `agent/rpi4-program-reloc` (current head with
  VA-range helpers + iteration log; cache enable code already removed).
- Coordination repo `db05f7b` on `main`.
- Manifest: `manifests/2026-05-04-td16-va-range-investigation.md`.

### Risk + escalation

If the refactor doesn't land within 1-2 focused sessions:

- Re-read Linux `arch/arm64/kernel/head.S` and `arch/arm64/mm/proc.S`
  for the exact bit pattern and ordering used at MMU enable.
- Compare against Circle's Pi-specific `startup64.S`.
- Consider an intermediate stage exit: I-cache only, with a tracked TD
  for D-cache. This unblocks Stage 2 while preserving Stage 3
  (SMP) as a known follow-up.
- Do **not** continue iterating on "more VA-range invalidation
  tactics"; the 5-iteration log is conclusive that this is the wrong
  axis to vary.

### Stage 1 inventory (populated 2026-05-04)

Memory stores between the SCTLR.M flip
(`_init.S:351-353`) and `b main` (`_init.S:726`). Read at kernel HEAD
`49ca0c66`.

**Two execution windows after the SCTLR.M flip:**
- *Window A* (low-VA stream, lines 354-562): MMU on, TTBR0 identity
  active, TTBR1 disabled until `tcr_el1.EPD1` clear at line 488. PC
  still on low-PA stream.
- *Window B* (high-VA stream, lines 564-726): after `br x0` to
  `_core_0_virtual` at line 561-562. PC in TTBR1.

**Stage 1 target shape:** all writes below either move into the
caches-off pre-MMU window or are eliminated.

| Line | Window | Store | Pre-MMU plan |
|---|---|---|---|
| 369-371 | A | `sp ← PMAP_COMMON_STACK + SIZE_INITIAL_KSTACK` | **REGISTER** — leave; no memory write. |
| 374-375 | A | `_fill_page_zero(PMAP_COMMON_SCRATCH_PAGE)` | Move pre-MMU. Trivial — zeroes one page in pmap_common region. |
| 382-391 | A | `nCpusStarted = 0` via LDAXR/STLXR flag dance | Move pre-MMU. Same logic, executed earlier. Note: uses exclusive monitor — pre-MMU exclusive monitor works on TTBR0-identity Normal Cacheable; verify this is still true after we go MMU-off for the inventory. Worst case: simple non-exclusive write since core 0 is alone here. |
| 398-414 | A | `_fill_page_zero(PMAP_COMMON_KERNEL_TTL2)` + 2 TTL2 entry writes (kernel + devices) + `_fill_page_zero(PMAP_COMMON_DEVICES_TTL3)` | **Move pre-MMU.** This is the bulk of TTBR1 PT setup. |
| 416-420 | A | PL011 early TTL3 entry (`PL011_TTY_BASE \| EARLY_UART_DEVICE_DESCR`) | Move pre-MMU. Static value. |
| 422-433 | A | Optional VC4 mailbox TTL3 alias (TD-15-mboxprobe) | Move pre-MMU. |
| 435-440 | A | `_fill_page_descr` over PMAP_COMMON_KERNEL_TTL3 with kernel-image pages, AttrIdx=DEFAULT | **Move pre-MMU.** ~512 entries, big write. |
| 442-463 | A | TD-04 NC override of `_hal_syspageCopied` page | Move pre-MMU. Computes index into TTL3 from link-time symbol VA; works pre-MMU since `_hal_syspageCopied` resolves at link time. |
| 465-479 | A | NC override of PMAP_COMMON_STACK page (2 entries) | Move pre-MMU. |
| 482-493 | A | `tcr_el1` clear EPD1, `tlbi vmalle1is`, barriers | **SYSTEM REG / TLB OP.** Keep — these stay between PT build and SCTLR.M flip in the new shape. |
| 561-562 | A | `br x0` to `_core_0_virtual` | **CONTROL FLOW.** In new shape, this is the *first thing* after the SCTLR.M flip. |
| 565, 569, 572 | B | `uart_putc_virt` markers | Cosmetic — PL011 device write through high-VA mapping. Acceptable post-flip *if* PL011 device TTL3 entry was set up pre-MMU. |
| 593-598 | B | Write `relOffs` and `hal_syspage` globals through high VA | **Move pre-MMU.** Compute PA = `pkernel + (sym_VA - VADDR_KERNEL)`, write directly with caches off. Both globals live in kernel BSS. |
| 600-604 | B | `mov x14, x9` | **REGISTER.** Leave. |
| 612-615 | B | `_clean_inval_dcache_range(x9, x9 + size)` over plo source | **CACHE OP.** Subsumed by the unified pre-MMU VA-range pass over the kernel image union. |
| 643-658 | B | **Syspage copy** loop (low-PA src → high-VA NC dest) | **Move pre-MMU.** Compute dest PA directly (pkernel + offset of `_hal_syspageCopied` in the kernel image). Copy with caches off. The TD-04 NC mapping becomes redundant once we're caches-off for the whole copy. |
| 675-679 | B | Post-copy `_clean_inval_dcache_range` over LOW-PA dest range | **CACHE OP.** Subsumed by the unified pre-MMU sweep. |
| 685-688 | B | TTBR0 ← scratch table (drop low identity) | **SYSTEM REG.** Keep. Move it earlier in Window A (right before SCTLR.M flip) or fold into the same atomic transition. |
| 692-694 | B | `vbar_el1 ← _vector_table` | **SYSTEM REG.** Keep — write before SCTLR.M flip. |
| 700-705 | B | TTBR0 ← scratch (duplicate of 685-688) | Likely redundant — investigate; one of these can be removed. |
| 711-717 | B | SP setup, register only | **REGISTER.** Leave. |
| 720, 723 | B | `uart_putc_virt` markers Z, b | Cosmetic. Same constraint as the earlier markers. |
| 726 | B | `b main` | **CONTROL FLOW.** Final destination. |

**Summary by category:**
- **REGISTER / SYSTEM REG / CACHE OP / CONTROL FLOW:** lines 369-371,
  482-493, 561-562, 600-604, 612-615, 675-679, 685-688, 692-694,
  700-705, 711-717, 726. Either stay where they are, become trivial,
  or move to "before SCTLR.M" without semantic difficulty.
- **MEMORY WRITES that must move pre-MMU:** lines 374-375, 382-391,
  398-440, 442-479, 593-598, 643-658.

**Architectural shape after refactor (target):**

```
[caches off, MMU off]
  build TTBR0 identity (existing pre-MMU code at 279, 321)
  build TTBR1 PT structures      ← new: moved from 398-479
  zero scratch page              ← moved from 374
  init nCpusStarted              ← moved from 382-391
  copy syspage to dest PA        ← moved from 643-658
  write relOffs / hal_syspage    ← moved from 593-598
  set vbar_el1                   ← moved from 692-694
  msr ttbr1_el1, x0              ← already at 322
  msr ttbr0_el1, scratch         ← from 700-705 (or fold into 315)
  VA-range dc civac over union(kernel image, PT region, syspage dest)
  ic ivau over kernel image
  dsb ish; isb
  clear tcr_el1.EPD1
  tlbi vmalle1; dsb ish; isb

  [single atomic flip]
  msr sctlr_el1, x0  /* M | C | I */
  isb
  tlbi vmalle1is; dsb ish; isb     /* belt-and-braces TLB sweep */
  br x0  ; x0 = _core_0_virtual

[caches on, MMU on, high VA]
_core_0_virtual:
  set sp                          ← register only (line 711-717)
  b main                          ← no memory writes between flip and here
```

**Risks identified during inventory:**

1. **Pre-MMU `nCpusStarted` flag dance uses LDAXR/STLXR.** With MMU
   off, exclusive monitor behavior on Cortex-A72 is implementation-
   defined. Either (a) the monitor still works because we're on Normal
   Cacheable identity-mapped memory once we set up TTBR0 (but that's
   *during* MMU-on already), or (b) we replace the dance with a plain
   store since core 0 is the only writer at this point. **Decision:
   replace with plain store; the `_initNCpusFlag` race is between core
   0 and the other cores' wait loops, all of which are at MMU-off
   waiting on the flag. Atomic semantics are not actually needed
   here — just program order with `dsb sy`.** Verify by reading
   `_other_core_trap` semantics.

2. **Syspage source `x9` is plo's PA.** Plo wrote the syspage with
   caches enabled then `eret`'d. The post-handoff `_clean_inval_dcache_range`
   over the source is currently essential to flush plo's dirty
   D-cache lines. In the new shape, with our caches off and plo's
   caches still potentially holding the only correct copy, we need
   to do the source flush *first* — but plo's TTBR1 is gone by the
   time we run, and we're in TTBR0 identity. The flush over `[x9,
   x9 + size)` works through identity mapping. Leave as the first
   step in the pre-MMU caches-off block.

3. **PL011 early UART writes (line 416-420 alias) require the
   device TTL3 entry to be set up before any `uart_putc_virt`
   call.** The current pre-MMU phase already does this; new shape
   keeps it. Markers in window B (565, 572, etc.) need the device
   alias intact through the SCTLR flip and `br x0`. Confirm: the
   tlbi between the flip and `br x0` does not invalidate static
   device entries (correct: tlbi invalidates TLB, not PT).

4. **`_fill_page_descr` writes 512 TTL3 entries — large region.**
   The pre-MMU VA-range sweep must cover the entire PT region
   `[PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK]`. Already
   computed correctly in the existing code at line 338-340.

5. **Window B markers (lines 565, 569, 572, 720, 723).** These are
   diagnostic. With Stage 1 success we can keep them, but they're
   not on the critical path — they print to PL011 through a
   pre-established device mapping, so they're harmless.

### Stage 1 next-session start point

The first code-changing session should:

1. Open `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` at
   line 351 (the SCTLR.M flip).
2. Cut the block from line 354 through line 562 and stage it for
   relocation into the pre-MMU window.
3. Build the new shape per "Architectural shape after refactor"
   above, in a single commit on `agent/rpi4-program-reloc`.
4. Drop the `_clean_inval_dcache_range` calls in `_core_0_virtual`
   (they become redundant) — but only after confirming the
   pre-MMU sweep covers the same regions.
5. Keep the no-call exception dump and VA-range helpers; they are
   the safety net for the validation pass.

---

## 2026-05-04 update — TD-16 VA-range investigation captured

Five iteration cycle on TD-16 cache enable, all decoded via the
no-call early exception dump landed earlier:

| # | Approach | Result |
|---|---|---|
| 1 | Set/way invalidation + M\|C\|I together | EC=0 unknown instruction (A72 #851672) |
| 2 | VA-range invalidation pre-MMU enable | Silent hang Pi between X3/X4 |
| 3 | VA-range post-MMU, kernel image only, C\|I (M already set), pre br x0 | ESR=0x96000003 level-3 fault, FAR=0xffffffffc0001890 |
| 4 | Same + post-enable tlbi vmalle1 | ESR=0x96000001 level-1 fault, FAR=0xfe201018 |
| 5 | Same + PT region invalidation | Same fault as #4 |

Pattern: every attempt produces walk-time translation faults on
real Pi 4 even though the page tables were written correctly with
caches off. Most plausible remaining hypothesis: **Cortex-A72
speculatively populates D-cache for PAs while D-cache is
"disabled"**, and those lines persist past `dc civac`. Linux's
`__enable_mmu` avoids this by completing all page-table writes
**before** turning on the MMU, not after. Phoenix currently does
the opposite (MMU on, then TTL3 setup, then cache enable). To
match Linux's shape we'd need to restructure the post-MMU
sequence, which is a larger refactor than fits in this session.

What landed (kernel `49ca0c66`):
- `_inval_icache_range` helper (VA-range I-cache via `ic ivau`,
  matching Linux's `__inval_icache_area`).
- Comment block at the cache-enable site capturing all 5
  iteration decodes.
- Cache-enable code itself reverted; baseline boot reaches
  `(psh)%` reliably.

What landed earlier (kernel `2a5b6a05`):
- `_early_exception_common` rewritten to use direct PL011 MMIO
  writes only (no `bl`). Survives recursive faults.

## Sequencing decision (resolved 2026-05-04)

The earlier "Option A vs Option B" framing has been superseded by
the four-stage roadmap in `docs/roadmap-cache-ram-smp.md`. User
direction 2026-05-04: all four goals (caches, 4 GiB, SMP, HDMI/USB)
are crucial; agent should choose the optimal trajectory. Dependency
analysis put **Stage 1 (caches via Linux `__enable_mmu` shape)** at
the top because:

- SMP cannot work correctly without IS-shareable cacheable memory
  (LDXR/STXR exclusive monitor's cross-core semantics).
- Iterating on 4 GiB unlock and DMA correctness at 1/62 speed
  amortizes badly across many cycles.
- The 5-iteration cache log is conclusive: more invalidation tactics
  is the wrong axis. Only the structural fix remains.

4 GiB unlock (former Option B) is now Stage 2. SMP (TD-01 + TD-11)
is Stage 3. HDMI/USB-keyboard console is Stage 4.

## Previous step framing


## 2026-05-04 update — exdump landed, M+C+I fault decoded

Sibling commit:
- kernel `2a5b6a05` (`aarch64: TD-16 no-call early exception dump +
  cache-enable fault evidence`).

What changed:
1. `_early_exception_common` rewritten to use **direct PL011 MMIO writes
   only** (no `bl`, no literal-pool string walker). Survives faults
   that previously caused recursive `E E E` spam. The previous agent
   rejected an exception-dump rewrite of this kind; the user clarified
   that the "real Pi timed out" observation was the cache-disabled
   slow boot, not a crash, so the diagnostic value of the dump
   outweighs its install cost. Real Pi 4 baseline boot reaches
   `(psh)%` cleanly with zero spurious exceptions
   (artifacts/rpi4b-uart/rpi4b-uart-20260503-234148-netboot-td16-exdump-baseline.log).
2. The "do not enable caches here" comment near the MMU enable is
   updated with the concrete fault the M+C+I-together attempt
   produced after the alias-safety prep work landed (7f7684c4 +
   d52f6c3a + 5e727dcc + 1a4eb297):

   ```
   EX=0000000000000004
   ESR=0000000002000000          (EC=0, "Unknown reason")
   ELR=ffffffffc00005ac          (high-VA target of br x0)
   FAR=0000000000000000
   ```

   The fault fires at the FIRST instruction the I-cache fetches from
   the high-VA kernel image after the post-MMU `br x0`. The fetched
   bytes don't decode as a valid AArch64 instruction. Most plausible
   cause is **Cortex-A72 erratum #851672** (set/way cache maintenance
   can leave cache in inconsistent state). The Linux arm64 fix is to
   use VA-range cache maintenance (`dc civac` / `ic ivau` per cache
   line) instead of set/way for kernel image regions.

## Next TD-16 step

Replace `bl hal_cpuInvalDataCacheAll` (set/way) in the cache-enable
sequence with VA-range invalidation over the kernel image, matching
Linux's `__inval_dcache_area` style:
- Compute kernel image PA range from syspage.
- Loop `dc civac` per cache line over that range.
- Then `ic ivau` per line over the same range.
- `dsb ish; isb`.
- THEN flip SCTLR.M | C | I in one write.

If that still faults, the no-call exdump will tell us the new ESR/ELR/
FAR within seconds.

## Sequencing decision for next session (unchanged from yesterday)

**Option A**: Continue TD-16 with VA-range invalidation. 1-2 cycles to
converge or definitively rule out cache enable on Pi 4 silicon.

**Option B**: Pivot to TD-15 phases 2-6 for the **4 GiB DRAM unlock**
goal. Pi 4 is slow during validation (~420 s per cycle to reach
`(psh)%`) but each phase produces visible progress on the memory
layout work regardless of cache state. The slowdown is a quality-of-
life issue, not a critical-path blocker for the user's stated goal.

## Previous step framing


**Date**: 2026-05-02 night

**Manifest**: `manifests/2026-05-02-td14-uart-shell-prompt.md`

## What just changed

Sibling commits:
- kernel `60703368` — relative `proc_portLookup("devfs")` fix, direct
  stored OID for the `devfs` namespace, bounded TD-14 `proc_send("devfs")`
  timing probe.
- devices `63f1d438` — PL011 minimal stat/attr support plus direct
  `/dev/console` alias.
- devices `3ee4702` — `TIOCSPGRP` now stores the requested foreground
  process-group ID directly.
- libphoenix `3c76bba` — temporary `/dev/console` open trace plus a narrow
  fast path that skips the second `resolve_path()` walk for the direct console
  alias.
- utils `da2f541` — psh early probes use `debug()` and bracket tty open,
  `isatty`, `tcsetpgrp`, and first `readcmd`.

Validation:
- QEMU Pi 4 smoke reaches `(psh)% help`.
- Real Pi image SHA256:
  `d219efa27dd617ea171465f601742427ca1c96f3d505fb3979a1c7a27d0c520e`.
- Real Pi log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`.

## New known boundary

The first real Pi 4 UART prompt is reached:

```text
psh: tty ready
psh: tcsetpgrp
psh: tcsetpgrp done
psh: readcmd
(psh)%
```

## Next action

Run a cleanup-focused iteration:
- Strip or gate the highest-volume TD-04/TD-14 boot probes that are no longer
  needed for the prompt boundary.
- Keep the functional fixes: `devfs` direct OID, PL011 stat/attr support,
  `TIOCSPGRP` semantics, and the temporary direct console alias/fast path.
- Rebuild and run QEMU smoke.
- Run real Pi netboot long enough to verify `(psh)%`, then run an interactive
  UART smoke if the current helper supports sending commands.

Then run:

```bash
./scripts/rebuild-rpi4b-fast.sh
./scripts/qemu-shell-smoke.sh rpi4b
./scripts/test-cycle-netboot.sh --label td14-clean-prompt --capture-secs 240 --dhcp-wait-secs 90
python3 scripts/summarize-rpi4b-uart-log.py artifacts/rpi4b-uart/<latest>.log
```

## 2026-05-03 update — TD-16-1 landed; cache-enable attempted, reverted

After landing TD-16-1 measurement probes (kernel `843e6c61` and plo
`61927ba`), we now have hard data on the Pi 4 slowdown:

- `td16: arm_freq Hz = 0x59682f00` = **1.5 GHz** — firmware confirms
  the ARM core is at full turbo. CPU is NOT throttled.
- `td16:cf=0337f980 dt=0000000000872d51` →
  - cntfrq = **54 MHz** (correct for BCM2711),
  - dt = **8,858,961 ticks for 1M nops** (~62× slower than physics
    says it should be at 1.5 GHz with caches enabled).

**The slowdown is caches being disabled in the kernel** — confirmed.
Not CPU throttling, not timer rate, not power management.

Attempts to enable I-cache + D-cache in `_init.S` were investigated:

1. I+D enable right after `_core_0_virtual:` → **recursive
   exception loop** on real Pi 4 (`E E E E ...`).
2. I+D enable just before `b main` → similar fault inside
   `syspage_init`'s first `hal_syspageAddr()` call.
3. I-cache only just before `b main` → no fault, per-nop loop is
   117× faster, but **overall boot doesn't progress meaningfully
   faster** (480 s capture stalls at the same `kllmnP` marker).
4. D-cache later in `_hal_init` (after syspage_init completes) →
   hung QEMU smoke inside `bl hal_cpuInvalDataCacheAll`. Not
   tested on real Pi 4.

All cache-enable code has been reverted to baseline. The TD-15
phase 1 + TD-16-1 probes remain in place as documented diagnostic
infrastructure. The Pi 4 boot reaches `(psh)%` reliably in ~420 s
capture per the 2026-05-02 manifest.

The detailed findings + hypothesis space are in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` under TD-16-cache-enable.

## 2026-05-03 follow-up — I-cache-only late placements rejected; D-cache maintenance helper fixed

Two additional I-cache-only placements were tested after reviewing the
current logs and cache-enable history:

- End of `_hal_init_c()`: QEMU reached `(psh)% help`; real Pi showed
  the synthetic TD-16 speedup (`td16b:dt=0x126ee`) but then hung after
  marker `h`, before `_usrv_init()` returned. Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-202432-netboot-td16-late-icache-long.log`.
- `_hal_start()`: QEMU reached `(psh)% help`; real Pi progressed through
  VM/proc/syscall init but then hung immediately after
  `main_initthr: enter`, before `_hal_start()` returned. Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-203723-netboot-td16-icache-hal-start.log`.

Conclusion: I-cache-only is not a safe functional fix. It can make a
nop-loop fast, but on real Pi 4 it exposes another cache/coherency issue
as soon as normal kernel initialization continues. The live boot path has
no SCTLR cache enable.

The useful source change kept from this investigation is kernel
`1a4eb297`, the `hal_cpuInvalDataCacheAll()` rewrite: the old set/way
loop selected only L1, missed the required `isb` after `csselr_el1`, used
invalidate-only instead of clean+invalidate, and had no final barriers.
The new helper is CLIDR-driven and uses `dc cisw` across every
data/unified cache level. That is a prerequisite for the next D-cache
attempt, not a complete cache enable fix.

Next action for TD-16: remove the unsafe mixed cacheable/Normal-NC aliases
from the bootstrap mappings before trying SCTLR.C again. In particular,
do not enable D-cache while `_hal_syspageCopied` and `PMAP_COMMON_STACK`
are reachable through both TTBR0 cacheable identity entries and TTBR1 NC
entries.

External OS comparison changes the cache strategy:

- Linux arm64 and FreeBSD arm64 both enable `SCTLR_EL1.M`, `C`, and `I` as
  part of the early MMU transition after MAIR/TCR/TTBR setup and page-table
  cache/TLB maintenance.
- Circle's Pi-oriented bare-metal path likewise treats cache enable as early
  memory-management infrastructure tied to a consistent map, not as a late
  C-level performance optimization.
- Therefore the Phoenix Pi 4 fix should not be another late I-cache-only
  placement. It should make the early bootstrap maps alias-safe, restore
  correct page-table cache maintenance, and then enable MMU + I-cache +
  D-cache together in a Linux/FreeBSD-shaped transition.
- Re-check upstream references before implementation:
  `arch/arm64/kernel/head.S` and `arch/arm64/mm/proc.S` in Linux, FreeBSD
  `sys/arm64/arm64/locore.S`, and Circle `startup64.S` / memory code.

## 2026-05-03 TD-16 step 1 — TTBR0 RAM identity blocks made Normal-NC

Implemented the first alias-reduction step toward the Linux/FreeBSD cache
plan: the temporary TTBR0 level-1 RAM identity block descriptors now use
Normal Non-Cacheable attributes (`NC_BLOCK_ATTRS`) instead of Normal
cacheable. This keeps the low identity aliases consistent with the existing
TD-04 NC mappings for `_hal_syspageCopied` and `PMAP_COMMON_STACK` while
the live path still has `SCTLR.C` disabled.

Validation:
- `./scripts/rebuild-rpi4b-fast.sh` completed and exported image SHA256
  `f6e77484512867c68f880923687342ec510469b61b59d09d4fb22be935a9795c`.
- `./scripts/qemu-shell-smoke.sh rpi4b` reached `(psh)% help`.
- `./scripts/qemu-shell-smoke.sh generic` reached `(psh)% help`.
- `./scripts/test-cycle-netboot.sh --label td16-ttbr0-nc-blocks
  --capture-secs 600 --dhcp-wait-secs 90` reached `(psh)%` on real Pi 4.
  Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-213203-netboot-td16-ttbr0-nc-blocks.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported `Verification: OK`.
- Real Pi firmware: expected netboot-path messages only (`sdcard` open
  failures before network fallback, missing `cmdline.txt`, HDMI1 EDID/DSI
  messages while HDMI0 is active). No new Phoenix runtime fault.

Next TD-16 step: shrink the TTBR0 identity map further or disable TTBR0
earlier after the higher-half branch so the same PA is not reachable through
low and high aliases when `SCTLR.C` is eventually enabled. Do not enable
caches until that alias boundary is handled.

## 2026-05-03 TD-16 step 2 — TTBR0 dropped after syspage copy

Implemented the next alias-boundary cleanup in kernel commit `d52f6c3a`:
after the boot code copies the syspage and runs the post-copy
`_clean_inval_dcache_range`, it immediately switches `TTBR0_EL1` to the
scratch translation table. That prevents later bootstrap and C code from
accidentally touching the same physical syspage region through both the low
identity map and the higher-half TTBR1 mapping. The obsolete E2 syspage
source/destination byte-dump block was removed in the same commit.

Validation:
- `./scripts/rebuild-rpi4b-fast.sh` completed and exported image SHA256
  `c82fa3be79c9a13f35c72a8717e97adfb6d5d7cb719ea31ebb1c7586bdae15b9`.
- `./scripts/qemu-shell-smoke.sh rpi4b` reached `(psh)% help`.
- `./scripts/qemu-shell-smoke.sh generic` reached `(psh)% help` on rerun.
  The first generic run timed out once after the VM log had reached
  `psh: tty open`; record this as a warning to watch for, but not as a
  reproduced regression.
- `./scripts/test-cycle-netboot.sh --label td16-early-ttbr0-drop
  --capture-secs 600 --dhcp-wait-secs 90` reached `(psh)%` on real Pi 4.
  Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-214816-netboot-td16-early-ttbr0-drop.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported `Verification: OK`.
- Real Pi firmware: expected netboot-path messages only (`sdcard` open
  failures before network fallback, missing `cmdline.txt`, HDMI1 EDID/DSI
  messages while HDMI0 is active). No new Phoenix runtime fault.

Next TD-16 step: inspect remaining early page-table/cache-maintenance
differences against Linux/FreeBSD. Do not enable caches until the remaining
bootstrap aliases and page-table visibility path are explicitly accounted for.

## 2026-05-04 TD-16 step 3 plan — restore early page-table invalidation

Objective: restore the Linux-shaped page-table visibility step that Phoenix
currently comments out before the first `SCTLR_EL1.M` write. This is still
cache-disabled execution; it does not enable I-cache or D-cache yet.

In scope:
- Kernel `hal/aarch64/_init.S` only.
- Restore `_inval_dcache_range` over the contiguous early page-table/scratch
  region populated with the MMU off:
  `PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK`.
- Keep the existing TTBR0 NC block descriptors and early TTBR0 drop.

Out of scope:
- Enabling `SCTLR_EL1.C` or `SCTLR_EL1.I`.
- Changing normal runtime `pmap` attributes.
- Removing TD-04/TD-14 runtime workarounds.

Acceptance criteria:
- Pi 4 fast rebuild/export completes with no compiler, linker, DTB,
  packaging, or image-verification warnings.
- QEMU Pi 4 shell smoke reaches `(psh)% help`.
- Generic QEMU shell smoke reaches `(psh)% help`.
- Real Pi 4 netboot reaches `(psh)%` or, if it regresses, the step is
  reverted and documented as still unsafe on BCM2711.

Rollback baseline:
- Kernel `d52f6c3a`.
- Coordination repo `9222cec`.

Result: PASSED 2026-05-04 in kernel commit `5e727dcc`.

Validation:
- `./scripts/rebuild-rpi4b-fast.sh` completed with image SHA256
  `0f6dc1a9e8254d9c42f41d6ee308eff074a9a6a2e0810cc1fa25044d9c260115`.
- `./scripts/qemu-shell-smoke.sh rpi4b` reached `(psh)% help`.
- `./scripts/qemu-shell-smoke.sh generic` reached `(psh)% help`.
- `./scripts/test-cycle-netboot.sh --label td16-early-pt-inval
  --capture-secs 600 --dhcp-wait-secs 90` reached `(psh)%` on real Pi 4.
  Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-221342-netboot-td16-early-pt-inval.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported `Verification: OK`.
- Real Pi firmware/netboot: expected SD/USB boot misses before network
  fallback, expected missing per-MAC TFTP files before root `config.txt`,
  expected missing `cmdline.txt`, and expected HDMI1 EDID/DSI messages while
  HDMI0 is active.
- UART helper used `picocom` and printed `STDIN is not a TTY`; capture was
  still valid and completed cleanly.

Key finding: the restored pre-MMU page-table invalidation is safe after the
TD-16 alias cleanup, but it does not change performance. The TD-16 loops still
report `dt≈0x883e**`, proving caches remain disabled and the next step must be
the actual early `M|C|I` transition or better early fault capture around it.

## 2026-05-04 TD-16 step 4 plan — harden early exception dump

Objective: make the temporary early exception vector print ESR/ELR/FAR without
using stack setup or `bl` calls. Previous cache-enable experiments sometimes
degenerated into repeated `E` output, which suggests the handler itself can
refault before completing the diagnostic dump.

In scope:
- Kernel `hal/aarch64/_init.S` only.
- Replace `_early_exception_common` with a terminal, no-call UART dump that
  emits vector slot, `ESR_EL1`, `ELR_EL1`, and `FAR_EL1`.
- Do not enable caches in this step.

Out of scope:
- Changing the normal post-bootstrap exception vectors.
- Interpreting cache-enable faults.
- Changing runtime scheduling, pmap, or driver code.

Acceptance criteria:
- Pi 4 fast rebuild/export completes with no compiler, linker, DTB,
  packaging, or image-verification warnings.
- QEMU Pi 4 shell smoke reaches `(psh)% help`.
- Generic QEMU shell smoke reaches `(psh)% help`.
- Real Pi 4 netboot reaches `(psh)%`, proving the diagnostic path is inert on
  the non-faulting path.

Rollback baseline:
- Kernel `5e727dcc`.
- Coordination repo `4fbb341`.

Result: FAILED and reverted locally; no kernel commit made.

Evidence:
- First rebuild failed in assembly because numeric macro-local labels expanded
  into invalid branch targets such as `99145`. The macro labels were corrected
  to named local labels and the rebuild then passed.
- Rebuild/export after the label fix produced image SHA256
  `1559c85756df97bb4d18e4c6fc9702c606a55a7c1e95c3a86d15ecf585c018c1`.
- QEMU Pi 4 and generic QEMU shell smokes both reached `(psh)% help`.
- Real Pi 4 netboot with label `td16-early-exdump` did not reach `(psh)%`
  in 600 s. It reached `psh: readcmd`, then timed out amid heavy interleaved
  process-spawn/debug output. Log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-222821-netboot-td16-early-exdump.log`.

Warnings observed:
- Build attempt 1: assembler errors from bad macro label expansion. Fixed in
  the working tree before validation, then ultimately reverted because the
  hardware acceptance criterion failed.
- Real Pi firmware emitted many `xHC-CMD err` diagnostics during SD/USB-MSD
  probing before network fallback. Netboot and Phoenix startup still
  proceeded, so this is classified as firmware-stage noise for this run, but
  it should be watched if USB-MSD probing starts delaying or disrupting future
  netboot cycles.
- UART helper used `picocom` and printed `STDIN is not a TTY`; capture was
  still valid.

Conclusion: do not commit the no-call early exception dump as written. The
next cache-enable diagnostic should either use QEMU gdbstub first or add a
smaller fault path that is explicitly tested by forcing a controlled exception
under QEMU before running on hardware.

## Sequencing decision for the next session

The user's stated goal is **fully unlocking 4 GiB DRAM and
correctly controlling VC6 memory access** (TD-15). The slowdown
investigation (TD-16) is important for quality-of-life, but it's
not strictly on the critical path for 4 GiB unlock.

Two viable directions:

**Option A: Continue TD-16-cache-enable.** Read Linux's
`arch/arm64/kernel/head.S` cache-enable sequence for A72 / Pi 4,
replicate precisely. Add a more-isolated early-exception handler
that prints ESR_EL1 / ELR_EL1 / FAR_EL1 via direct PL011 MMIO
(no `bl` calls) so we can see the actual fault. Likely 1-2 more
Pi cycles to converge.

**Option B: Pivot to TD-15 phases 2-6 for 4 GiB unlock.**
- Phase 2: move PLO_RPI_MAILBOX_BUFFER_ADDRESS out of ARM-usable RAM.
- Phase 3: VC4 quiesce mailbox sequence before plo `eret`.
- Phase 4: DTB `/reserved-memory` + `/soc/dma-ranges` parsing.
- Phase 5: `total_mem=4096` in `config.txt` + 4 GiB validation.
- Phase 6: DMA correctness audit across `pcie/xhci`.
This addresses the user's stated near-term goal directly. Pi 4 is
slow during validation cycles but each Phase produces visible
progress on the memory layout work regardless of cache state.

## 2026-05-03 reframe — TD-15 (VC6 hygiene + 4 GiB) is the next investment

The TD-15 phase 1 mailbox-buffer drift probe ran on real Pi 4. Result:
**`td15:OK`** — the 64-byte pattern plo wrote at PA `0x02000000` was
intact when the kernel read it through the NC alias. So **VC4 is
NOT writing to the mailbox-buffer page during the plo→kernel
handoff window.** That eliminates one of the top suspects for
TD-04-class corruption.

In the same cycle the user provided HDMI photographs taken ~1 minute
apart showing only ~12 visible characters of new kernel output across
that whole minute. Combined with the TD-14 timing probe data (a
single `proc_send("devfs")` round trip took anywhere from 1 ms to
43 s on the same hardware), this confirms the system is running
**~1000–60 000× slower than expected**, which is timer-driven and
silicon-specific.

This finding is now tracked as **TD-16** with a planned **TD-16-1**
probe (read CNTFRQ_EL0 + CNTPCT_EL0 deltas at boot to confirm whether
the architectural timer ticks at the rate `cntfrq_el0` advertises).
The Phoenix armstub at
`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S:153`
sets `CNTFRQ_EL0 = 54000000` (`OSC_FREQ`) which is correct for
BCM2711, so either the cntfrq value is being silently overwritten
later, or the actual hardware tick rate doesn't match it on real
silicon, or the timer IRQ is delivered late due to long DAIF-masked
sections.

**Sequencing implication:** TD-16 jumps the queue ahead of the rest
of TD-15. If the actual root cause of every "slow user-space" symptom
on Pi 4 is timer ticks running at the wrong rate, then fixing TD-16
likely makes Gate 2 (HDMI text console) and Gate 3 (PCIe + USB +
keyboard) trivially observable because they'll run at full speed.
TD-15 phases 2-6 still need to land for correctness and the 4 GiB
unlock, but the immediate next investment is TD-16-1.

## 2026-05-03 reframe — TD-15 (VC6 hygiene + 4 GiB) is the next investment

User direction 2026-05-03: handle Pi 4 VideoCore VI memory access
correctly even if it doesn't end up explaining the residual TD-14
IPC slowness. Reasoning: VC6 memory hygiene is on the critical path
to **(a)** unlocking the full 4 GiB DRAM and **(b)** ensuring kernel
and user-space allocations are safe from VC6 / firmware DMA
interference. It's also the single most plausible TD-04 root-cause
candidate we have not yet eliminated.

TD-15 has a complete phased plan in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`:

  1. VC4 / firmware audit + cheap probes (no source changes; just
     read back `PLO_RPI_MAILBOX_BUFFER_ADDRESS` post-handoff and
     confirm whether VC4 keeps writing).
  2. Move VC4 mailbox buffer out of ARM-usable RAM (`0x02000000`
     is currently inside plo's `map ddr 0x00400000 0x3b400000`).
  3. Quiesce VC4 background tasks via mailbox before plo's `eret`.
     Keep HDMI scanout alive. If TD-04-class corruption disappears,
     we have a causal answer.
  4. DTB-driven memory layout (`/memory@0` consistency check,
     `/reserved-memory` parsing, `/soc/dma-ranges` parsing). Drop
     hardcoded `SIZE_DDR`. Provide `arm_to_bus_addr()` helper.
  5. Unlock 4 GiB: `total_mem=4096` + `gpu_mem=64` in `config.txt`,
     validate end-to-end. Watch for TTBR1 map sizing regressions.
  6. Tighten DMA correctness across drivers (pcie, xhci, future).

The rest of this document — Gates 1-5 toward HDMI text console +
USB keyboard — is still valid and will resume after TD-15 phases
1-3 land. In particular Gate 2 (HDMI text console) becomes much
easier once we know the framebuffer region is the only place VC4
writes.

## 2026-05-03 forward plan — toward HDMI text console + USB keyboard

The user's near-term milestone is **fully booted Phoenix-RTOS on Pi 4
with HDMI0 text console and USB keyboard input**. UART (psh)% is
verified. Remaining gates, in order:

### Gate 1 — clean reproducible boot

- Re-run real Pi netboot at the current main checkpoint
  (manifest `manifests/2026-05-02-td14-uart-shell-prompt.md`,
  image SHA `d219efa27dd6...`) twice in a row, ~20 min apart, to
  confirm `(psh)%` is reliably reached (not a one-off).
- If not reliable, investigate residual TD-14 races before going
  further.
- Strip the high-volume probes the previous agent noted:
  TD-13 spawn-cap log lines, TD-04 hack debug markers, TD-14
  per-syscall timing prints. Keep just enough to identify the
  pl011-tty boundary and the `(psh)%` event.

### Gate 2 — HDMI text console (HDMI0)

The wiring already exists end-to-end:

```
plo video.c (mailbox firmware framebuffer fetch)
  -> syspage_graphmodeSet(width, height, bpp, pitch, framebuffer)
  -> kernel pctl_graphmode handler in hal/aarch64/generic/generic.c
     reads hal_syspage->hs.graphmode and returns it
  -> pl011-tty/pl011-tty.c::pl011_fbcon_init() does platformctl(),
     mmap(framebuffer, MAP_DEVICE|MAP_UNCACHED|MAP_PHYSMEM),
     clears rows, prints "Phoenix-RTOS HDMI console\r\n".
  -> pl011_thr's TX path calls pl011_fbcon_write() per character
     (line 633), so kernel klog mirrored to UART also appears on HDMI.
```

Suspected current failure mode: in the latest psh-prompt log
(`rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`),
there is no "Phoenix-RTOS HDMI console" string, so
`pl011_fbcon_init()` either returned `-ENOSYS` (graphmode not
populated by plo) or never ran. The previous agent's probe-strip
removed both branches' debug() prints, so we can't tell which.

Plan:
1. Add a single `pl011_writeRaw(uart, "fbcon: ok\r\n")` /
   `"fbcon: skip <err>\r\n"` after `pl011_fbcon_init()` returns —
   one cheap UART line, not a debug() syscall.
2. If "fbcon: skip" with an error, drop into `pctl_graphmode`'s
   syspage read and confirm width/height/bpp/pitch/framebuffer are
   sane. They might be zero (plo never set them) or corrupt
   (TD-04-class on the syspage handoff).
3. If plo never set them: probe plo's `video_init` (file
   `plo/hal/aarch64/generic/video.c`). The mailbox sequence at
   `tag_setphywh / tag_setdepth / tag_setpxlordr / tag_getfb /
   tag_getpitch` may be timing out on real Pi 4 mailbox.
4. If plo set them but kernel reads garbage, that's TD-04-class on
   the syspage `hs.graphmode` field — same fix pattern as
   `_hal_syspageCopied` (NC mapping or DCIVAC + DSB).

Once fbcon prints its banner, kernel klog should mirror to HDMI
automatically because pl011_thr's TX path is the same on both
devices.

### Gate 3 — PCIe + xHCI + HID for keyboard

The pieces are present but their first IPCs probably hang on Pi 4
(same TD-14 IPC slowness as pl011-tty had). Pipeline:

```
phoenix-rtos-devices/pcie/server/pcie.c  (pid 8)
  - probes BCM2711 PCIe host bridge at PCIE_BCM2711_HOST_BASE
  - enumerates VL805 USB host controller
phoenix-rtos-devices/usb/xhci/xhci.c  (pid 9, "usb")
  - mmaps XHCI_BCM2711_MMIO_BASE
  - resets, runs xhci_init, scans ports
phoenix-rtos-usb/usb/dev.c
  - enumerates USB devices, dispatches to class drivers
phoenix-rtos-usb/libusb/hid_client.c
  - HID class driver, parses report descriptors,
    publishes /dev/kbd0 (boot-protocol keyboard)
phoenix-rtos-devices/tty/pl011-tty pl011_kbdthr() (PL011_TTY_KBD_PATH=/dev/kbd0)
  - opens /dev/kbd0, read()s keystrokes, libtty_putchar() into pl011 tty
  - psh sees keystrokes as if typed on UART
```

In the latest log, `pcie` and `usb` are spawned but produce no
progress beyond `main: spawned`. We need to:
1. Add cheap UART markers in `pcie/server/pcie.c` and
   `usb/xhci/xhci.c` `main()` entry to confirm they reach init.
2. If they hang on first lookup() or namespace IPC, apply the
   same TD-14 mitigations the other agent applied to pl011-tty
   (devfs-direct OID lookup, fast path, retries).
3. Confirm HID server publishes `/dev/kbd0` on real Pi 4 with a
   physical USB keyboard plugged into HDMI0-side port.
4. Confirm pl011-tty kbdthr opens `/dev/kbd0` and feeds keys.

### Gate 4 — interactive UART smoke

Once Gates 1-3 are stable:

- `help`, `ps`, `ls /dev` via picocom send.
- Boot-time prompt + interactive command in <60 s end-to-end.

### Gate 5 — interactive HDMI + USB keyboard smoke

Combined Gate 2 + Gate 3 result. Visual confirmation: type on
USB keyboard, see characters echo on HDMI display.

## Risk register for the plan

- **TD-04-class slowness still active.** Each Pi 4 cycle is
  expensive (~5 min minimum). Aggressive testing budget: at most
  3 Pi cycles per session. Use QEMU + careful single-edit changes.
- **HDMI mailbox** is a separate TD-04-class candidate. If plo
  mailbox doesn't drain reliably, fbcon won't init.
- **PCIe + USB on Pi 4** has never been exercised by Phoenix-RTOS
  per the commit history; first-time bring-up may surface its
  own TD-NN class of issues.
- **HID server / /dev/kbd0** existence is assumed by pl011-tty
  but not verified to actually be created by the usb stack on
  Pi 4. May need additional wiring.

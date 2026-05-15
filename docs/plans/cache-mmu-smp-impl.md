# Phoenix-RTOS Pi 4 cache + MMU + SMP enablement plan

Stage 1–3 implementation roadmap building on the EL2→EL1 drop that just
landed. Audience: implementers picking up the next bring-up step.

References:

- `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/research/el2-to-el1-drop.md`
- `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/research/boot-mmu-bringup-non-linux.md`
- `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/tracking/current-step.md`
- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S` (current-state, post EL drop)
- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`

## 1. Status today (already landed)

The boot now reaches `(psh)%` with MMU.M=1 actually taking effect and a
single core running cache-off. The corner stones in place:

- **EL2→EL1 drop in `_init.S` lines 188–246** — fixes the long-standing
  TD-16 silent hang. Pre-fix, `msr sctlr_el1` ran while the executing
  context was governed by SCTLR_EL2. Post-fix, every core does the
  Linux-nvhe-style HCR_EL2.RW=1 / VTTBR_EL2=0 / CNTHCTL_EL2=0x3 /
  CPTR_EL2=0 / SPSR_EL2=0x3c5 / ELR_EL2=el1_entry / `eret` sequence
  before any EL1 sysreg write. Verified: `mrs x0, CurrentEL` after the
  drop reads `0x4` and the existing markers `K`/`L`/`M`/`X1`–`X4`
  continue. `_hal_init` is reached, `main` runs, userspace launches.
- **VL805 BAR programming** — explicit BAR write before USB stack
  brings up xHCI; required because the VPU does not always seed the
  PCIe root for the on-board hub.
- **PCI BME/MSE enable + reorder** — bus master + memory-space enable
  are set in PCI command register *before* xHCI capability probing;
  reorder fixed an enumeration race.
- **`xhci_capProbe` retry loop** — first read of `HCSPARAMS1` was
  intermittently returning 0xffffffff on cold boot; re-read until
  non-all-ones with bounded backoff.
- **USB CMD_RING shrunk to 4 KiB** — keeps xHCI command ring within a
  single page-allocator page, removing a multi-page-coherency edge
  case in the cache-off boot path.

The MMU is on but `SCTLR_EL1.{C,I}` are still zero (line 414 in
`_init.S` still ORs in only bit 0 (M) into the live SCTLR). All of
RAM is mapped Normal Non-Cacheable (`NC_BLOCK_ATTRS = 0xf05`,
`MAIR_EL1_VALUE = 0x444FF`) per the TD-04 cache-coherency workaround.
Performance is correspondingly poor.

## 2. Stage 1: cache enable (D-cache + I-cache)

The prize. Now that SCTLR_EL1 writes actually take effect, walk the
C/I bits up under controlled, reversible phases so a regression in any
one phase tells us where the BCM2711-specific issue lives.

### Phase A — I-cache only (`SCTLR_EL1.I=1`, C=0, M=1)

Lowest risk. I-cache is virtually-indexed-physically-tagged on A72,
fed from the same TTBRn walker the MMU is already using.
Linux's `init_kernel_el` enables I bit early; non-Linux survey
(`docs/research/boot-mmu-bringup-non-linux.md` §4) shows every
project keeping I=1 from first SCTLR write.

Diff target: `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` ~lines
410–415 (the post-MMU-only enable). Replace:

```
mrs x0, sctlr_el1
orr x0, x0, #(1 << 0)
msr sctlr_el1, x0
isb
```

with a sequence that does `ic iallu; dsb nsh; isb` immediately *before*
the M|I write, and the SCTLR set-mask becomes `(1<<0) | (1<<12)`.

UART signature: existing `uart_tag2 88, 52` should print, then `(psh)%`
must still appear. If it does, Phase A is done — caches are not
coherent yet but instruction fetch is cached.

### Phase B — D-cache + I-cache (`SCTLR_EL1.{M,C,I}=1` in one MSR)

Risky. This is the historic TD-16 failure point on pre-EL-drop boots,
and the EL drop only fixes the EL2/EL1 mismatch — it does not fix any
BCM2711-specific cache-coherency hazard.

Convergent recommendations from the cross-OS survey (FreeBSD, NetBSD,
OpenBSD, seL4-loader, Circle, Genode):

- **Full RES1 baseline.** Replace the bare `0x30c0c938` SCTLR_EL1
  initial value (`_init.S` line 272) with a constant derived the
  Linux way: `INIT_SCTLR_EL1_MMU_OFF = 0x30c50838 | LSMAOE | nTLSMD`.
  Concretely, build it as `(SCTLR_EL1_RES1 = 0x30d00800) | nTWI |
  nTWE | UCT | DZE | I-already-on bits`. seL4 and Linux trust
  `0x30d00800` as A57/A72 RES1 — match that.
- **`dc cvac` over freshly-written page tables before the flip.**
  We already do `_inval_dcache_range` over the kernel TTL2..STACK
  region (`_init.S` lines 399–401). Audit that range covers every
  TTE the walker can reach when MMU comes up: `PMAP_COMMON_KERNEL_TTL2`,
  `PMAP_COMMON_KERNEL_TTL3`, `PMAP_COMMON_DEVICES_TTL3`,
  `PMAP_COMMON_SCRATCH_TT`, `PMAP_COMMON_SCRATCH_PAGE` *and* the
  syspage source page that plo wrote. Use `dc cvac` (clean to PoC)
  rather than invalidate, so any speculatively-fetched dirty lines
  reach DRAM before walker activation.
- **`tlbi vmalle1` immediately before the flip.** `_init.S` line 288
  already does `tlbi vmalle1` followed by `dsb ish; isb` — keep it,
  but move it adjacent to the SCTLR write rather than 130 lines
  earlier so no walker activity intervenes.
- **`isb` after SCTLR write.** Already present (line 415).
- **`ic iallu` after SCTLR write.** A72's PoU may differ from PoC for
  I-cache; an extra invalidate flushes any I-cache lines fetched
  before C=1 against the wrong attribute.
- **Verify CPUECTLR_EL1.SMPEN=1.** Armstub line 163–164 sets it for
  the primary core. Secondaries get it set when they are released
  through their own armstub paths but the kernel currently has the
  SMPEN write commented out (`_init.S` lines 305–311). Re-enable it
  unconditionally; it is required by the A72 TRM 4.3.40 before any
  cache or TLB op.

Diff target: `_init.S` lines 286–290 (move TLB invalidate down),
407–415 (the enable block), and 305–311 (SMPEN).

UART signature: same as Phase A but `(psh)%` should appear *fast* —
heavy printf no longer DRAM-bound. A `dd if=/dev/zero of=/tmp/x bs=1M
count=8` should saturate at cache speed, not DRAM speed.

### Phase C — A72 erratum 859971 (instruction prefetch)

A72 r0p0–r0p3 erratum 859971: under specific speculative-prefetch
conditions, the CPU may execute an instruction from a stale I-cache
line. Linux carries the workaround:
`CPUACTLR_EL1[bit 47] = 1` to disable instr-prefetch. Phoenix already
has the CPUACTLR_EL1 path in `_init.S` lines 313–320, currently
disabled with comment "DISABLED FOR A72 TESTING". Re-enable with the
859971 mask applied, conditional on `__TARGET_AARCH64A72`.

Diff target: `_init.S` lines 313–320. Replace the disabled stanza with
an active write that ORs in `(1ULL << 47)`.

### Phase D — XN/PXN on MMIO TTE (855873 hygiene)

A72 erratum 855873 (and the broader speculative-fetch concern from
Cortex-A72 TRM ch. on speculative behaviour) recommends marking
Device-nGnRnE / Device-nGnRE regions XN+PXN at stage 1, so speculative
instruction prefetch into MMIO is forbidden. Phoenix's current
`EARLY_UART_DEVICE_BLOCK = 0x60000000000709` (line 37) and
`EARLY_UART_DEVICE_DESCR = 0x6000000000070b` (line 36) set bit 53 (PXN)
and bit 54 (UXN) — verify by decoding: the upper 0x6000... = bits 53
and 54 set. Good. The peripheral 1 GB block (0x40000000–0x80000000 on
BCM2711, mapped through `EARLY_UART_DEVICE_BLOCK`) is only used for
PL011 today. When the peripheral mappings get expanded for GPIO /
mailbox / mini-UART, propagate the same 0x6000... high-attribute bits
plus an explicit MAIR slot pointing at index 2 (Device).

Diff target: when adding a peripheral 1 GB block descriptor, confirm
its TTE has bits 53 and 54 set. Document the diff in the
`NC_BLOCK_ATTRS` comment (`_init.S` line 159–162).

### Phase E — ISB before final `br x0` (post-MMU branch hygiene)

Cross-OS survey (§3, agent #2 finding) notes that several projects
insert an explicit `isb` between `msr sctlr_el1, …` and the high-VA
branch. Phoenix's flow: line 415 `isb`, then bootstrap, then `br x0`
to high-VA at line ~620. Restructure so an `isb` sits immediately
before any `br` crossing translation domains (TTBR0 identity →
TTBR1 high-VA). Cheap insurance against speculative prefetch.

Diff target: `_init.S` around `_core_0_virtual` /
`_other_core_virtual` branches (lines 800–810).

## 3. Stage 2: 4 GiB DRAM unlock + GPU memory partition

Today only the 1 GiB block containing `syspage->pkernel` and one block
containing PL011 are mapped (`_init.S` lines 344–376). The Pi 4B has
4 GiB physical with 1 GiB at low alias plus 3 GiB above 0x40000000. We
need:

- **DTB consumption to learn GPU mem partition.** Pi firmware applies
  `gpu_mem` from `config.txt` and exposes the resulting reservation
  through `/memreserve/` ranges and `/reserved-memory` nodes in the
  flat DTB. Extend `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`
  (`dtb_common.cpus`, `dtb_common.memory` parsing) to also parse
  `reserved-memory` and the `vc4` / `gpu` carve-out at the top of
  DRAM. The frame allocator must skip those PAs.
- **TTBR1 high-VA mappings for upper 3 GiB.** `pmap_common`
  pre-allocates one TTL2 / TTL3 pair (`PMAP_COMMON_KERNEL_TTL2`,
  `PMAP_COMMON_KERNEL_TTL3` at `_init.S` lines 173–174). Extend
  `pmap.c` `pmap_kernelmap` (or equivalent) to populate additional
  TTL3 pages covering all of post-GPU-carveout DRAM. Stage 1 caches
  must be on first — non-cacheable mappings of 3+ GiB DRAM are not
  the target end-state.
- **Page allocator extension.** The Phoenix frame allocator (`vm/page.c`
  upstream) currently consumes the syspage memory map. Confirm the
  syspage map produced by plo on Pi 4 already advertises 4 GiB
  (it does, per `_projects/aarch64a72-generic-rpi4b/syspage.plo`),
  and remove any kernel-side clamp that limits to 1 GiB.
- **Test.** psh shell `meminfo` (or `_get_meminfo`) should report
  ≥ 3.5 GiB usable (4 GiB − ~256 MiB GPU + firmware reserved). Heavy
  malloc loop in psh: allocate 3 × 1 GiB blocks, write a pattern,
  read back, free. Must not OOM.

Diff targets:

- `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c` — add reserved-memory
  parsing (~50 lines).
- `sources/phoenix-rtos-kernel/hal/aarch64/pmap.c` — extend kernel
  mapping setup to cover full TTBR1 window.
- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` lines 459–475
  (kernel TTL2 fill) — extend to multiple 1 GiB block descriptors
  rather than the single one today.

## 4. Stage 3: SMP cores 1–3

Currently `_init.S` line 457 traps all non-zero MPIDR.Aff0 cores in
`_other_core_trap` (line 790). Bring them online:

- **Secondary entry from armstub spin-table.** Armstub
  `secondary_spin` (`phoenix-armstub8-rpi4.S` lines 187–195) wfes on
  `spin_cpuN` slots and `br`s to the address core 0 writes there.
  Today that destination is the same `_start` address, so the new
  EL2→EL1 drop block runs on every core unmodified — confirmed in
  `docs/research/el2-to-el1-drop.md` §6 "Secondary cores". No armstub
  change needed.
- **Per-core EL2→EL1 drop.** Already happens unconditionally per core
  via the `mrs x0, CurrentEL; b.ne el1_entry` block. No change.
- **Per-core stack.** `_other_core_virtual` at `_init.S` line 809
  calls `_set_up_vbar_and_stacks` which loads SP from
  `PMAP_COMMON_STACK + (cpuid * SIZE_INITIAL_KSTACK)`. Confirm
  `pmap_common` reserves enough stack pages for 4 cores; today it
  reserves one (`PMAP_COMMON_STACK = pmap_common + 5*SIZE_PAGE`,
  `_init.S` line 178). Extend to 4 stack pages or grow
  `SIZE_INITIAL_KSTACK` × 4 reservation.
- **Per-core MMU walk.** Each core's MMU is enabled by the same code
  path through `el1_entry`. The TLBs are per-core; the page tables
  are shared. `tlbi vmalle1` is per-core; `tlbi vmalle1is` is the
  inner-shareable variant required for cross-core invalidation
  post-Stage-1 (broadcast TLBI requires SMPEN=1 on every core, plus
  cacheable inner-shareable kernel mappings).
- **Phoenix existing SMP infrastructure.** ZynqMP has a working SMP
  path: `sources/phoenix-rtos-kernel/hal/aarch64/zynqmp/zynqmp.c`
  lines 38–533 use `nCpusStarted` and `hal_cpuAtomicInc` to barrier
  all cores at boot. The generic A72 path
  (`sources/phoenix-rtos-kernel/hal/aarch64/generic/generic.c` lines
  29–108) also has the `nCpusStarted++` increment. Reuse this.
  `kernel/proc/proc.c` SMP scheduler hooks already exist (per
  ZynqMP).
- **LDXR/STXR exclusive monitor.** A72's exclusive monitor for
  load-acquire / store-release across cores requires the target
  cache lines to be Inner-Shareable Cacheable. Stage 1 must succeed
  first; without C=1, LDAXR/STLXR in `_init.S` lines 446–448 work
  because they fall back to a non-cacheable global monitor, but
  spinlocks used by `kernel/lib/spinlock` will deadlock under
  contention until Stage 1 caches are on.

Diff targets:

- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` line 457: change
  `cbnz x8, _other_core_trap` to `cbnz x8, _secondary_continue` and
  let secondaries fall through to MMU enable + per-core init.
- `_init.S` line 178: grow `PMAP_COMMON_STACK` reservation.
- `sources/phoenix-rtos-kernel/hal/aarch64/generic/generic.c` —
  ensure SMP barriers match the ZynqMP pattern.

## 5. Detailed file/diff list

| Phase | File | Approx. lines | Key sequence |
|---|---|---|---|
| 1A I-only | `_init.S` | 410–415 | `ic iallu; dsb nsh; isb; mrs x0, sctlr_el1; orr x0, x0, #((1<<0)\|(1<<12)); msr sctlr_el1, x0; isb` |
| 1A I-only | `_init.S` | 272 | replace `0x30c0c938` with derived RES1 mask |
| 1B D+I | `_init.S` | 286–290, 407–415 | move TLBI adjacent to flip; SCTLR set bits = M\|C\|I; post-write `ic iallu; isb` |
| 1B D+I | `_init.S` | 399–401 | extend `_inval_dcache_range` → `_clean_dcache_range` over PMAP_COMMON_*; cover syspage src |
| 1B SMPEN | `_init.S` | 305–311 | uncomment `mov x0, #(1<<6); msr s3_1_c15_c2_1, x0; isb` |
| 1C 859971 | `_init.S` | 313–320 | re-enable CPUACTLR_EL1 with bit 47 set |
| 1D MMIO XN | `_init.S` | 159–162, 36–37 | document XN+PXN; ensure new device blocks reuse `EARLY_UART_DEVICE_BLOCK` template |
| 1E ISB | `_init.S` | 800–810 | add `isb` immediately before each `br x0` post-MMU |
| 2 DTB | `dtb.c` | new ~50 lines | parse `reserved-memory` / `vc4` carveout |
| 2 pmap | `pmap.c` | extend `pmap_kernelmap` | iterate all DRAM blocks under TTBR1 |
| 2 init | `_init.S` | 459–475 | multiple TTL2 block descriptors |
| 3 SMP entry | `_init.S` | 457 | route secondaries through MMU enable, not `_other_core_trap` |
| 3 stack | `_init.S` | 178 | grow `PMAP_COMMON_STACK` to 4 × `SIZE_INITIAL_KSTACK` |
| 3 SMP barrier | `generic/generic.c` | 29–108 | mirror ZynqMP `nCpusStarted` pattern |

## 6. Test / validation per phase

- **1A**: kernel reaches `(psh)%`. UART markers `Z 75 76 83 84 85 77
  86 88 49 88 50 88 51 88 52 88 53` complete; `psh` runs `uname` and
  prints. Optional: check `mrs x0, sctlr_el1` after the write reads
  with bit 12 set via a deliberate test print.
- **1B**: `(psh)%` appears noticeably faster (printf no longer
  DRAM-bound). `time dd if=/dev/zero of=/tmp/x bs=1M count=64` shows
  bandwidth at L2 / DRAM-burst speed (~3 GB/s) rather than NC speed
  (~150 MB/s). Re-run the TD-04 syspage probe — if syspage copy
  still works, TD-04's NC-page hack may now be unnecessary (re-test
  before removing).
- **1C/D/E**: regression-only — phases 1B should not regress.
- **2**: `meminfo` ≥ 3.5 GiB usable. 3 × 1 GiB malloc / pattern /
  free passes. No spurious data abort under heavy pressure.
- **3**: `cat /proc/cpuinfo` (or Phoenix equivalent) shows 4 cores.
  IPC stress test (multiple psh instances + heavy pipe traffic) does
  not deadlock or corrupt. All 4 cores increment `nCpusStarted` to
  4 before the first `wfi`.

Validation harness: `./scripts/rebuild-rpi4b-fast.sh →
./scripts/capture-rpi4b-uart.sh → python3
scripts/summarize-rpi4b-uart-log.py <log>` (see project CLAUDE.md).
Per phase: snapshot a manifest with
`scripts/snapshot-integration-state.sh` immediately after the phase
boots green, so rollback is one command.

## 7. Inter-dependencies

- **Stage 1 unblocks** measurable performance for everything: HDMI
  framebuffer rendering, USB enumeration speed, network throughput,
  overall responsiveness of psh and any TLS workload. Without it the
  Pi 4 runs at ~5% of its capability — every load goes to DRAM.
- **Stage 1 unblocks Stage 3.** `LDAXR` / `STLXR` exclusive monitor
  semantics across cores require Inner-Shareable Cacheable lines.
  SMP without caches is a research curiosity, not a usable system.
- **Stage 2 unblocks** any workload needing > 1 GiB working set. The
  kernel itself fits in 1 GiB easily; userspace will not, especially
  with HDMI framebuffers and a real filesystem cache.
- **Stage 2 is independent of Stage 1** in the strict sense (you can
  map 4 GiB Non-Cacheable) but useless without it because the
  bandwidth dies on the second GiB.
- **Stage 3 cache coherency requires Stage 1 Phase B.** No exception.

## 8. Risks

- **Stage 1 Phase B may still hang despite EL drop.** TD-04 documents
  a BCM2711-specific cache-coherency anomaly affecting
  `_hal_syspageCopied` (`tracking/current-step.md`). The EL drop fixed
  one failure mode (SCTLR_EL1 writes ineffective); TD-04 was diagnosed
  on a boot where caches were "off only on paper" — anomaly may
  resurface with C=1. Fallback paths:
  1. M+I only (skip C) — isolates I-cache coherency.
  2. M+C only (skip I) — isolates D-cache coherency.
  3. M only with explicit `dc cvac` over all DRAM the kernel touches.
  4. Mark only syspage source page NC (TD-04 `NC_ATTRS`); rest cacheable.
- **Stage 2 GPU mem partition firmware variability.** `config.txt` /
  `gpu_mem` differs across firmware revisions and user configs.
  DTB-driven discovery is the only robust path; do not hardcode the
  carveout location. Test with at least two `gpu_mem` settings
  (`gpu_mem=64` and `gpu_mem=256`).
- **Stage 3 cache coherency.** Requires Stage 1; do not attempt
  earlier. Even with caches on, broadcast TLBI requires SMPEN
  on every core — verify per-core in the secondary entry path.
- **Stage 3 GIC distribution.** GICv2 distributor is initialized by
  armstub `setup_gic` but per-core CPU interface (PMR, BPR) needs
  init on each secondary. `interrupts_gicv2.c` exists; confirm the
  per-CPU init runs from `_other_core_virtual`.

## 9. Effort estimate

| Stage | Best case | Likely | Worst case |
|---|---|---|---|
| 1 (cache enable) | 2 days (EL drop unblocked everything) | 1–2 weeks | 3 weeks if more BCM2711-specific issues remain |
| 2 (4 GiB + GPU partition) | 1 week | 1.5 weeks | 2 weeks if DTB parser needs structural changes |
| 3 (SMP) | 1.5 weeks | 2 weeks | 3 weeks if GICv2 per-core init or SMP scheduler edge cases bite |

The Stage 1 spread is wide because TD-04 tells us BCM2711 has
class-of-problem cache-coherency issues we have not fully
characterized. Phase A (I-only) is almost certainly hours of work.
Phase B is the open question.

## 10. Open questions

1. **Does TD-04 NC-page workaround still apply with C=1?** TD-04 was
   diagnosed pre-EL-drop. Re-run the E2 probe (currently in plo /
   `tracking/current-step.md`) immediately after Phase B succeeds.
   If the syspage-copy probe is clean with C=1 and no NC override,
   remove TD-04. If not, keep `NC_ATTRS = 0x707` for that one page
   and document why caches still don't help there.
2. **Is the bare `0x30c0c938` SCTLR_EL1 baseline correct for A72?**
   seL4 issue #1025 trusts `0x30d00800` as RES1 for A57/A72.
   `0x30c0c938` differs in bits we should audit before relying on
   it as a "RES1 baseline" for the cache-on flip.
3. **VPU DMA quiescing before Phase B?** Linux uses `bcm2835-mbox`.
   Phoenix has no equivalent. If Phase B sees per-boot-randomized
   faults like TD-04 originally did, VPU coherency is the suspect.
   Mitigation: mailbox property tag `0x00038049` (framebuffer off)
   before D-cache enable, or `dsb sy` + delay before the flip.
4. **Cortex-A72 TRM r0p3 RES1 audit on HCR_EL2.** The EL drop
   research §6 flagged this as "double-check before merging".
   Re-read `phoenix-armstub8-rpi4.S` lines 163–169 and post-drop
   HCR_EL2 at `_init.S` line 217 vs A72 TRM r0p3 RES1 cases.
5. **TTBR1 1 GB VA range (`TCR_EL1_VALUE` line 122) sufficient for
   Stage 2?** Phoenix uses on-demand kernel mappings; 1 GB likely
   still suffices, confirm before Stage 2 ships.
6. **`_other_core_trap` `tcr_el1` EPD1 clear (line 802) under
   Stage 1 caches?** With C=1, the MMU re-config window must be
   ordered with `dsb ish` + `tlbi vmalle1is` (broadcast) rather
   than the non-broadcast `tlbi vmalle1`.

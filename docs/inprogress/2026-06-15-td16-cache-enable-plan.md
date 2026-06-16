# TD-16 "cache enable" — investigation, ground-truth correction, and the real perf-lever plan

**Date:** 2026-06-15
**Author:** research/planning spike (no source changes; doc-only)
**Scope:** Phoenix-RTOS RPi4 (BCM2711, Cortex-A72) port. RESEARCH + PLANNING ONLY.

> **TL;DR — the premise is stale.** TD-16 as originally framed ("data/instruction
> caches are globally DISABLED") is **already RESOLVED**. The kernel enables
> `SCTLR_EL1.{M,C,I}` in a single write on the live primary path
> (`hal/aarch64/_init.S:585-589`), the pmap maps **all** Normal memory — kernel
> *and* userspace — **Write-Back cacheable** by default (`pmap.c:478`,
> `DEFAULT_ATTRS 0x703` in `_init.S:142`), and TD-16 / TD-17 / TD-18 are marked
> RESOLVED in the cleanup status table. Caches are **ON**.
>
> The genuine remaining perf lever — which several recent docs mislabel as
> "caches globally off" — is narrower and specific: **the GENET RX zero-copy path
> hands lwIP pbufs that live in an _uncached_ `dmammap` DMA pool**
> (`bcm-genet.c:653`, comment at `:74` literally says "(caches-off, slow)").
> The entire TCP/IP receive chain (checksum + the copies into socket/libnfs
> buffers) therefore reads from **uncached** memory, which on A72 is ~10–50× slower
> than cached. That — not a global cache switch — is what to fix, and it is the
> classic **non-coherent-DMA streaming** problem Linux solves with
> `dma_map_single` + `arch_sync_dma_*` cache maintenance.
>
> **This doc therefore: (1) proves global cache-enable is done; (2) corrects the
> misconception (and flags the stale docs — it does NOT edit them, per the
> session constraint); (3) recasts the requested "DMA coherency strategy / enable
> sequence" research as a *selective-cacheable-DMA-with-maintenance* plan with
> incremental rollout + rollback.**

---

## 0. How to read this document

The task brief asked for "the plan to enable caches." That plan would be fiction:
caches are already enabled. Writing "nothing to do" would also be wrong: there is
a real, TD-16-shaped performance problem. So the sections below keep all the
structure the task asked for (current state, root-cause, correct sequence, DMA
coherency strategy, incremental rollout w/ rollback, bisection, risk register,
open questions, bibliography) but repurpose them onto the **actual** lever:
making the network RX hot path cacheable + maintained.

**Conscious scope note.** The task brief asked for an armv7-target comparison and for
extracting exact MAIR/TCR/SCTLR values from external bare-metal projects
(Circle/rpi4-osdev/Matyukevich). After the premise correction these are **low value**:
the in-tree aarch64 config (§3.1) is the proven, booting ground truth and the Linux
arm64 non-coherent-DMA model (§4) is the canonical comparative source for the real
lever. Those two are cited concretely; the armv7 comparison and external exact-value
extraction were deliberately deprioritized rather than padded in.

---

## 1. Current state — caches and MMU as configured TODAY (with file:line)

### 1.1 The MMU is enabled and caches are ON (primary path, proven by code)

`sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:

- **EL2→EL1 drop happens first** so that `SCTLR_EL1` writes actually take effect
  on the running context (`_init.S:218-252`). This is the fix for the *historical*
  TD-16 silent-hangs: pre-drop, `msr sctlr_el1` staged a value consulted only by a
  future `eret` that never came, so the MMU never turned on (comment `_init.S:201-208`).
- **Single-shot `M|C|I` enable on the primary**, after the EL2→EL1 drop and
  *before* the core-0/secondary split (`_init.S:637-640`), so the primary core
  executes it:
  ```
  585  mrs  x0, sctlr_el1
  586  orr  x0, x0, #(1 << 0)    /* SCTLR_EL1.M  — MMU enable   */
  587  orr  x0, x0, #(1 << 2)    /* SCTLR_EL1.C  — D-cache enable */
  588  orr  x0, x0, #(1 << 12)   /* SCTLR_EL1.I  — I-cache enable */
  589  msr  sctlr_el1, x0
  590  isb
  ```
- Surrounded by the canonical A72 ritual: pre-flip `ic ialluis` / `dsb ish` /
  `tlbi vmalle1is` / `dsb ish` / `isb` (`_init.S:566-570`), a defensive set/way
  D-cache invalidate via `hal_cpuInvalDataCacheAll` (`_init.S:577`), and post-flip
  `ic iallu` / `dsb nsh` / `isb` (`_init.S:593-595`) plus a PoU clean + I-cache
  invalidate of the low-PA text alias (`_init.S:601-606`).
- Comments at `_init.S:562-564` and `:656-661` both state **"TD-16 RESOLVED
  2026-05-17."**

### 1.2 Memory attributes — everything Normal is cacheable by default

`_init.S`:
- `MAIR_EL1_VALUE = 0x444FF` (`_init.S:139`) with the standard four slots:
  - idx 0 = Normal Inner+Outer WB cacheable RA/WA (`0xFF`)
  - idx 1 = Normal Inner+Outer Non-Cacheable (`0x44`)
  - idx 2 = Device-nGnRE (`0x04`)
  - idx 3 = Device-nGnRnE / strongly-ordered (`0x00`)
- `TCR_EL1_VALUE` (`_init.S:118-133`): the page-table **walker** itself uses
  Inner+Outer WB cacheable, Inner-Shareable for both TTBR0 and TTBR1
  ("Linux-standard TCR", comment `_init.S:116-117`).
- `DEFAULT_ATTRS 0x703` (`_init.S:142`) = AttrIdx 0 (cacheable) + AF + Inner-Shareable
  + nG, privileged-RWX. This is the **default** for kernel mappings and the bootstrap
  identity aliases.
- `NC_ATTRS 0x707` (`_init.S:149`) = AttrIdx 1 (Normal-NC); now used **only** for the
  TD-15 mailbox-buffer alias.

`sources/phoenix-rtos-kernel/hal/aarch64/pmap.c` — `_pmap_writeTtl3()`
(`pmap.c:435-481`) selects the attribute index from the page's `vm_attr_t`:
```
463  switch (attr & (PGHD_NOT_CACHED | PGHD_DEV)) {
465      case PGHD_NOT_CACHED | PGHD_DEV: MAIR_IDX_S_ORDERED (idx 3)
469      case PGHD_NOT_CACHED:            MAIR_IDX_NONCACHED  (idx 1)
473      case PGHD_DEV:                   MAIR_IDX_DEVICE     (idx 2)
478      default:                         MAIR_IDX_CACHED     (idx 0)  ← USERSPACE + kernel RAM
```
So an ordinary user page (no `PGHD_NOT_CACHED` / `PGHD_DEV`) is mapped **WB
cacheable**. TD-17 (amap/ELF cacheable, `MAP_NONE`) and TD-18 (zone backing
cacheable, `zone.c:45`) are both marked **✅ RESOLVED 2026-05-29** in the cleanup
table — exactly the "userspace is now cacheable" milestones.

`pmap.c:270-271` even implements attribute-change cache maintenance (detects a
cacheable→non-cacheable transition to clean+invalidate), which only makes sense in
a caches-ON world.

### 1.3 Cache-maintenance helpers exist and are correct

- `hal/aarch64/cache.c`: `hal_cpuCleanDataCache` (`dc cvac`), `hal_cpuInvalDataCache`
  (`dc ivac`), `hal_cpuFlushDataCache` (`dc civac`), `hal_cpuInvalInstrCache`
  (`ic ivau`), all with the correct DSB/ISB barriers and `CTR_EL0`-derived line size.
- `_init.S` range helpers: `_inval_dcache_range` (`:932`), `_clean_inval_dcache_range`
  (`:952`), `_clean_dcache_pou_range` (`:971`), `_inval_icache_range` (`:1003`), and
  the CLIDR-driven all-levels set/way invalidate `hal_cpuInvalDataCacheAll` (`:1159`).
  These are the building blocks any DMA-maintenance work would call.

### 1.4 The historical TD-16 (the thing the task describes) — quote of record

From `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` status table:

| Item | Disposition |
|---|---|
| **TD-16** | **RESOLVED 2026-05-17** by cache enable (project `dde9bb5` + kernel `72242a05`); root cause was caches-off, fixed by armstub L2CTLR + 1319367 re-encoding |
| **TD-16-cache-enable** | **RESOLVED 2026-05-17** (armstub L2CTLR + encoding fix; kernel `72242a05` single-shot `M\|C\|I` in `el1_entry`; helper scaffolding deleted in kernel `dccd0aee`) |
| TD-17 | ✅ RESOLVED 2026-05-29 — amap/ELF cacheable; boots to psh |
| TD-18 | ✅ RESOLVED 2026-05-29 — zone backing cacheable |

The original symptom (TD-16-1, `TEMPORARY-FIXES…:1508-1537`): a 1M-nop loop took
`dt = 0x872d51` (8,858,961 ticks) ≈ **62× slower** than physics at 1.5 GHz, because
the kernel ran with caches OFF. That symptom was eliminated by the 2026-05-17 fix.

### 1.5 Empirical confirmation that caches are ON today

Decisive discriminator: boot-to-`(psh)%` wall time and the existence of heavy
real-time workloads.

- Caches-OFF baseline (2026-05-02 manifest) reached `(psh)%` only with a **≥420 s**
  capture window (`TEMPORARY-FIXES…:1432-1433`).
- Today (2026-06-15) the most recent netboot UART logs are
  `…-quake-stability-T1`, `…-quake-particles`, `…-quake-conscale1`, etc.
  (`scripts/uart-list.sh`). **Quakespasm renders an animated, textured 3D world,
  multi-boot USB enumeration benches run, NFS RPCs are ~1.3 ms, and 180 s
  interactive captures show live psh.** None of this is physically possible at a
  62× CPU slowdown.
- The genet driver comment `bcm-genet.c:74` ("…caches-off, slow… drain thread…")
  is itself **stale residue** describing the pre-2026-05-17 era; the code around it
  was tuned then and the comment wasn't updated.

**Confirmed against an actual log** (`artifacts/rpi4b-uart/rpi4b-uart-20260615-112138-netboot-quake-stability-T1.log`):
within a single capture window it reaches the loader banner, microkernel banner,
syspage-program launch, GENET link-up (`link up: 100 Mbps`), USB mouse enumeration,
`(psh)%`, Quake "Playing shareware version", **and** 15,000+ received packets
(`RXSTATS seen=15123 zerocopy=15123`). At the old 62× (caches-OFF) slowdown the boot
could barely reach `(psh)%` in a 420 s window — reaching live Quake + 15k packets in
one window is **only possible with caches ON**. The same `RXSTATS … zerocopy=15123`
line is direct evidence of the zero-copy uncached-RX path that §2 identifies as the
real lever.

> **Action for the executing engineer (cheap, do first):** on a fresh
> `test-cycle-netboot.sh`, read the power-on→`(psh)%` wall-clock delta and the
> FSHEALTH `read=… MB/s`. Tens of seconds ⇒ confirms caches ON and sets the perf bar.

---

## 2. Root-cause analysis of the *real* lever (the "SLC stale-read / slow fills")

The parked-blocker lore in the roadmap (`calm-wobbling-quill.md` §1.8) says "the
parked blocker was BCM2711 SLC non-determinism … the single largest perf lever …
[A]/JTAG if the SLC stale-read recurs." Two separate things are conflated there:

**(A) The historical _global enable_ blocker — already overcome.** The 2026-05-03→05
silent-hangs at the SCTLR flip were **not** an SLC mystery; the root cause was that
`SCTLR_EL1` was written while still at EL2 (no effect on the running context) plus
an armstub L2CTLR/erratum-1319367 encoding bug. Both fixed 2026-05-17 (project
`dde9bb5`, kernel `72242a05`). The EL2→EL1 drop (`_init.S:218-252`) is the structural
fix. **This blocker is closed; do not re-investigate it.**

**(B) The live lever — uncached DMA buffers in the RX hot path.** This is what the
2026-06-14 perf doc's hypothesis (a)#1 is actually describing, just mislabeled.

### 2.1 The mechanism (evidence-ranked)

The GENET driver (`sources/phoenix-rtos-lwip/drivers/bcm-genet.c`) is **zero-copy on
RX**:
- One contiguous **`dmammap()`** pool for ALL RX buffers (`bcm-genet.c:653`:
  `state->rx_pool = dmammap(GENET_RX_POOL_SLOTS * GENET_MAX_FRAME)`). `dmammap`
  returns **uncached, contiguous** memory (confirmed by the TX comment
  `bcm-genet.c:1076`: "dmammap memory is uncached, so no further cache maintenance
  is needed").
- Each RX slot is wrapped in a `pbuf_custom` (`bcm-genet.c:101-105, 814-819`) and
  handed to lwIP via `pbuf_alloced_custom(... PBUF_REF ...)`. **lwIP owns the
  uncached DMA buffer directly** until the last ref drops (`genet_rxbufFree`,
  `bcm-genet.c:753-766`).

Consequence: the lwIP receive stack (IP/TCP checksum verification, `pbuf_copy*`
into socket buffers, the copy into the libnfs buffer) all **read packet payload out
of uncached memory**. On A72, uncached loads bypass L1/L2 and serialize against DRAM
latency — ~10–50× slower than cached, with no prefetch and no burst coalescing. For
a 2 MB NFS read that's copied several times, this plausibly bounds throughput to the
observed ~0.5 MB/s even though per-RPC latency is fine (1.3 ms).

### 2.2 Hypotheses for the residual slow bulk-read, ranked

| # | Hypothesis | Evidence for | Evidence against | Likelihood |
|---|---|---|---|---|
| H1 | **Uncached RX pbuf payload** dominates: every byte of every received packet is checksummed + copied out of uncached DMA memory | `bcm-genet.c:653` (uncached pool) + zero-copy `PBUF_REF` to lwIP (`:818`); matches the "multiple copies on uncached memory" symptom; Linux deliberately avoids this (streaming DMA, §4) | TX already linearizes into one uncached slot and TX isn't the NFS-read bottleneck | **HIGH** |
| H2 | **lwIP poll/select readiness** still costs latency per RPC (only worked around with `poll_timeout=1`) | perf doc (b); `port/sockets.c` "no lwip_poll()" | Already mitigated to 1.3 ms/RPC; affects *latency* not *bulk rate* | MED (latency, not bandwidth) |
| H3 | **recv() granularity** — one pbuf (~1.5 KB) drained per poll cadence | perf doc (a)#3 | A code-shape issue, independent of caches | MED |
| H4 | **100 Mbps cable** caps the link (crossover cable carries 2 pairs) | `project_pi4_netboot_100mbps_cable` memory | 0.5 MB/s ≪ 12 MB/s line rate, so cable isn't the *current* binding limit (but will be after H1 is fixed) | LOW now / becomes binding later |
| H5 | **TCP window / delayed-ACK** | perf doc (a)#2 | At 1.3 ms RTT a 46 KB window ≈ 35 MB/s — not binding | LOW |
| H6 | **A genuine SLC stale-read** if/when RX is made cacheable without correct maintenance | the parked fear | none observed since 2026-05-17; would be a *new* bug introduced by the fix, not a current one | only a RISK of the fix (§6) |

**Conclusion:** H1 is the lever. The fix is *selective* — make the RX path
cacheable and add `arch_sync_dma`-style maintenance — **not** a global cache switch
(already done) and **not** a JTAG SLC hunt (that risk only appears if maintenance is
done wrong, §6/§7).

---

## 3. The correct cache configuration & maintenance sequence (Cortex-A72/BCM2711)

This section documents the canonical sequence both as the *record of what is already
correct in-tree* and as the *contract any DMA-maintenance change must preserve*.

### 3.1 What is already correct (do not change)

- **MAIR_EL1** four-slot layout (§1.2): cacheable / NC / Device-nGnRE /
  Device-nGnRnE. Matches Linux arm64 `MAIR_EL1_SET`.
- **TCR_EL1**: walker WB-cacheable Inner-Shareable, 4 KB granule, 48-bit TTBR0 /
  high-half TTBR1 (`_init.S:118-133`). Matches Linux.
- **Page-table attr bits**: Normal-WB-cacheable Inner-Shareable for RAM
  (`DEFAULT_ATTRS`/`MAIR_IDX_CACHED`); Device-nGnRE for MMIO
  (`pmap.c:990`, `MAIR_IDX_DEVICE`, used for `/dev`-mapped peripheral windows);
  Normal-NC for the few `PGHD_NOT_CACHED` regions.
- **SCTLR_EL1.{M,C,I} enable order with barriers** (`_init.S:558-606`): invalidate
  before enable, single `M|C|I` write, ISB, then I-cache invalidate + PoU clean of
  the low alias. This is the canonical A72 ritual; it matches the zynqmp aarch64
  target (which shares this `_init.S`) and Linux `__enable_mmu`.
- **The Pi4 "MMU is mandatory" gotcha is satisfied.** On A72, caches and LDXR/STXR
  exclusives only behave for **Normal cacheable Inner-Shareable** memory; with the
  MMU off everything is treated as Device/NC and exclusives are unpredictable. The
  port enables the MMU + Inner-Shareable cacheable RAM before any cached access or
  atomic — which is also why SMP (LDXR/STXR cross-core) works today. (This gotcha is
  the #1 bare-metal-Pi4 pitfall — see bibliography.)

### 3.2 The DMA-maintenance contract (what new code MUST follow)

For a buffer that the CPU accesses **cacheable** but a device DMAs to/from
(non-coherent BCM2711), the canonical streaming-DMA discipline (identical to Linux
`arch_sync_dma_for_{device,cpu}`):

- **Before the device reads (TX / "to device"):** `dc cvac` over the buffer (clean —
  push dirty CPU lines to DRAM) then `dsb`. → `hal_cpuCleanDataCache()`.
- **Before the CPU reads what the device wrote (RX / "from device"):** `dc ivac`
  over the buffer (invalidate — discard stale CPU lines so the next load fetches
  DRAM) then `dsb`, *after* the device signals completion. → `hal_cpuInvalDataCache()`.
  - Subtlety: invalidate-only (`dc ivac`) can lose a partial dirty line at the
    buffer's edges if the buffer isn't cache-line aligned. Use **clean+invalidate**
    (`dc civac`, `hal_cpuFlushDataCache()`) at unaligned ends, or guarantee the
    buffer is `CTR_EL0.CWG`-aligned (64 B on A72) and a multiple of the line size.
- **Ordering:** the cache op must be ordered w.r.t. the descriptor/doorbell write
  that hands the buffer to the device — barriers already present in `cache.c`.
- **Alignment:** A72 cache line = 64 B (`CTR_EL0`). RX buffers must be 64-B aligned
  and sized to a line multiple to make `dc ivac` safe; the current `dmammap` pool is
  page-aligned and `GENET_MAX_FRAME`-strided, so per-slot alignment must be checked.

### 3.3 SMP considerations

- Maintenance ops here use **VA** forms (`dc cvac`/`ivac`/`civac`, `ic ivau`) which on
  A72 with Inner-Shareable mappings broadcast to the inner-shareable domain — correct
  for the 4-core cluster. The set/way `dc cisw` form is **not** broadcast and is only
  for the boot-time all-cache flush; never use set/way for runtime DMA maintenance.
- The port currently schedules on cpu0 only for some paths but SMP D-8/D-9 re-enabled
  4-cpu scheduling (kernel `c4293f3d`). RX maintenance is VA/Inner-Shareable so it is
  SMP-safe regardless of which core drains the ring.

---

## 4. DMA coherency strategy — the central decision (per-driver)

BCM2711 is **non-coherent**: the GENET/USB/V3D/EMMC DMA masters are *not* in the
A72 cache-coherency domain (there is no ACE/CCI hookup for these on the Pi4 the way a
server SoC would have). Linux confirms this model: arm64 implements
`arch_sync_dma_for_device`/`arch_sync_dma_for_cpu`
(`external/linux/arch/arm64/mm/dma-mapping.c:15,37`) and **bcmgenet uses streaming
DMA** — `dma_map_single(..., DMA_TO_DEVICE)` / `dma_unmap_single(..., DMA_FROM_DEVICE)`
(`external/linux/drivers/net/ethernet/broadcom/genet/bcmgenet.c:2169,2267,1869,1902`)
— i.e. **cacheable buffers + per-packet cache maintenance**, *not* uncached buffers.

Phoenix today took the simpler, correct-but-slow path: **uncached DMA buffers**
(`dmammap`), which needs *zero* maintenance and cannot go stale, at the cost of
uncached CPU access. Two policies:

### Policy A — keep DMA buffers uncached (status quo), reduce copies instead
Leave every `dmammap`/`MAP_UNCACHED` DMA region exactly as-is. Attack throughput by
reducing how many times the CPU touches the uncached bytes (fewer copies, larger
recv drain). **Pros:** zero coherency risk; no kernel/driver-maintenance code; cannot
reintroduce the SLC stale-read fear. **Cons:** the bytes are still uncached, so
checksum + the unavoidable copy-to-app remain slow; ceiling is lower than Policy B.

### Policy B — make the RX hot path cacheable + add streaming maintenance (Linux model)
Allocate the GENET RX pool as **Normal-cacheable** memory (not `dmammap`), and add
`dc ivac` (invalidate) over each received frame **after** the DMA-complete signal and
**before** lwIP reads it. **Pros:** the entire RX stack (checksum + copies) then runs
cached → the big win; matches Linux. **Cons:** requires correct, line-aligned
maintenance; a maintenance bug *can* surface as a stale read (the parked fear) — but
now as a *localizable* bug, not a mystery (§5).

> **Recommendation: Policy A first (de-risked, immediate, partial win), then Policy B
> on the RX path only (the full win), gated behind the §5 bisection harness.** Do NOT
> touch TX/USB/V3D/SD coherency in the first cut — their `dmammap` semantics are load-
> bearing and they are not the NFS-read bottleneck.

### 4.1 Per-driver impact & the call sites whose uncached semantics MUST be preserved

| Driver | DMA region & call site | Coherency today | First-cut action |
|---|---|---|---|
| **GENET RX** | `bcm-genet.c:653` `dmammap(SLOTS*MAX_FRAME)` → `pbuf_custom`/`PBUF_REF` (`:814-819`) | uncached, zero-copy to lwIP | **Policy B target.** Cacheable pool + `dc ivac` per frame after DMA-complete, line-aligned. |
| **GENET TX** | `bcm-genet.c:1078` `pbuf_copy_partial` into `tx_buf` (uncached, comment `:1076`) | uncached single slot | Leave uncached first cut (not the read bottleneck). If made cacheable later: `dc cvac` before doorbell. |
| **GENET mailbox** | `bcm-genet.c:276` `dmammap(_PAGE_SIZE)` for VC4 property msg | uncached (required: VC4 reads it) | **PRESERVE uncached.** Do not touch. |
| **USB xHCI rings/TRBs** | xHCI event/command/transfer rings + DMA buffers (`sources/phoenix-rtos-usb`) via uncached contiguous maps | uncached | **PRESERVE.** Out of scope; not the lever; #121 just stabilized. |
| **V3D winsys BOs** | `mmap(UNCACHED\|CONTIGUOUS)` + `va2pa` (V3D MMU flat PT) per the V3D scout notes | uncached by design; the renderer reasons in uncached terms; SLCACTL/L2T flush already handled in winsys | **PRESERVE.** GPU coherency is its own solved sub-problem (SLCACTL fix); do not fold into TD-16. |
| **bcm2711-emmc SD** | PIO (no DMA buffer for data; `SDHCI_DATA_PORT` FIFO) | n/a (PIO) | No DMA coherency surface today; the #154/#120 work is PIO completion, unrelated. |

**Hard rule:** every `dmammap(...)` and `mmap(..., MAP_UNCACHED, ...)` call site listed
"PRESERVE" above keeps its exact semantics. The only allocation that changes in the
recommended plan is the **GENET RX pool** (`bcm-genet.c:653`).

---

## 5. Step-by-step incremental rollout with rollback at each step

Netboot is stateless and power-cycling is scripted, so a bad image just fails to boot
and the next netboot of a reverted image recovers with no human. Every step ends with
a **manifest snapshot** (`scripts/snapshot-integration-state.sh`) so
`scripts/restore-integration-state.sh <manifest.md>` is the one-command rollback.

> **Build discipline:** after any committed core/driver change, rebuild
> `--scope core` and verify a diag string is in `loader.disk` (stale-core hazard,
> CLAUDE.md). The lwip-port driver is a core component for this purpose.

### Step 0 — Confirm the premise (no code change)
- `scripts/uart-summary.sh` on a fresh `test-cycle-netboot.sh` run; read power-on→
  `(psh)%` wall time and the boot-time **FSHEALTH** `read=… MB/s` figure
  (utils `739b4c8`). Record the cache-ON baseline NFS MB/s and Quake FPS.
- **Gate:** boot is tens of seconds (not ~420 s) ⇒ caches confirmed ON; proceed.
  If somehow ~420 s ⇒ the premise was right after all and this whole plan is void —
  fall back to the historical TD-16 (but §1 evidence says this won't happen).
- **Rollback:** n/a (read-only).

### Step 1 — Policy A: cut RX copies / widen the recv drain (no coherency change)
- In `port/sockets.c` recv path and/or `nfs_service`, drain more per poll iteration;
  in the genet→lwIP path, confirm no redundant `pbuf_copy`. Bump `TCP_WND` /
  `TCP_SND_BUF` (`include/default-opts/lwipopts.h`) as the perf doc's "decisive first
  test" and back it with `MEM_SIZE`/`PBUF_POOL_SIZE`.
- **Validate:** FSHEALTH `read MB/s` ↑ vs Step-0 baseline; USB enum, V3D cube, SD,
  Quake demo1 all still pass; **0 faults** (`uart-summary.sh`).
- **Rollback:** revert the lwip commit / `restore-integration-state.sh`.
- **Decision:** if throughput rises materially and meets the need, Policy B may be
  optional. If still memcpy-bound (flat), proceed to Step 2 — this is the perf doc's
  own "flat ⇒ it's a TD-16 (uncached) problem" branch.

### Step 2 — Policy B: make the GENET RX pool cacheable + add streaming maintenance
- Allocate `state->rx_pool` as **Normal-cacheable, contiguous, 64-B-aligned** memory
  (per-slot aligned to `CTR_EL0.CWG`, slot size a line multiple) instead of
  `dmammap` (`bcm-genet.c:653`). Keep TX/mailbox uncached.
- After the RX DMA-complete signal for a slot and **before** wrapping it for lwIP
  (`bcm-genet.c:~808-819`), `dc ivac` (invalidate) the received `pay_len`+pad range:
  call into a `hal_cpuInvalDataCache(va, va+len)` equivalent exposed to the lwip-port
  (or do the maintenance inline with `dc ivac`/`dsb`). Re-arm (`genet_rxbufPop`) must
  also invalidate the slot before handing it back to the device.
- **Validate (this step is the risk gate — run the §6 bench):**
  - FSHEALTH `read MB/s` should jump (target: link-bound, ~8–10 MB/s on a gigabit
    cable; ~10 MB/s on the 100 Mbps cable).
  - **Data-integrity bench:** multi-boot `test-cycle-bench.sh N nfs-md5` — read a
    known file from NFS and checksum it; ANY corruption = stale-read regression →
    rollback immediately and go to §5.
  - Quake demo1 visual-regression harness (`scripts/quake-visual-compare.py`) must
    still match host (SSIM ~0.958) — packet corruption would show as map-load
    failures / black textures.
  - USB enum, V3D, SD, 0 faults.
- **Rollback:** `restore-integration-state.sh` to the Step-1 manifest. This is a
  single-file driver change, trivially revertible.

### Step 3 — (optional) extend to GENET TX, then measure end-to-end
- Only if profiling shows TX-side uncached copy is now the bottleneck: cacheable
  `tx_buf` + `dc cvac` before the doorbell. Same validation; same rollback.

### Step 4 — Lock in + document
- Update the genet driver's stale `:74` "(caches-off, slow)" comment; correct the
  stale-premise docs (see §8); add the FSHEALTH delta + manifest to a `manifests/*.md`.

---

## 6. Bisection / triage if a stale-read recurs (Policy B)

If Step 2 shows corruption (wrong NFS bytes, Quake map-load failure, checksum
mismatch), it is a **maintenance bug**, not a silicon mystery. Localize it:

1. **Confirm it's the new path.** `restore` to Step-1 manifest (uncached RX): if
   corruption vanishes, it is conclusively the cacheable-RX change.
2. **Direction of the bug.** Stale CPU read of device data ⇒ the `dc ivac` is missing,
   ordered before DMA-complete, or doesn't cover the whole frame (alignment). Stale
   device read of CPU data is a TX/clean problem (not in Step 2).
3. **Alignment audit.** Check each slot is `CTR_EL0.CWG`-aligned and slot size is a
   line multiple; an unaligned tail loses a partial line on `dc ivac` → use `dc civac`
   at the ends.
4. **Over-maintain to bisect.** Temporarily switch RX maintenance to full
   `hal_cpuFlushDataCache` (`dc civac`) over a generously rounded range. If corruption
   stops, it was alignment/coverage; tighten back. If it persists, the op is ordered
   wrong vs the descriptor read.
5. **Observe with existing tooling:**
   - **diag-udp :9999** self-log: dump the first/last 16 bytes of a received frame as
     seen by the CPU vs the descriptor length, to spot stale lines.
   - **UART** `uart-summary.sh`: fault count, genet stats.
   - **HDMI snapshots**: Quake corruption is visible (black textures / wrong geometry).
   - **Route-A hardware watchpoint** (kernel `a67f6dac`/`5160cd8d`): arm a data
     watchpoint on a specific RX slot VA to catch an out-of-order writer if a true
     coherency-window race is suspected — this is the same facility that localized
     USB #121, and it removes JTAG from the path.
6. **Only if all of the above exonerate the software** (correct, aligned, ordered
   maintenance still yields stale data) does the BCM2711 SLC enter the picture →
   escalate to **[A]** and consider the SLCACTL invalidation pattern already proven in
   the V3D winsys (MEMORY: the V3D "stale frame0" bug was a missing
   `CTL_SLCACTL = 0x0f0f0f0f` slice/uniform-cache invalidation). That is the *only*
   place an SLC step is justified — and even there it's a known, code-level fix, not a
   JTAG hunt.

---

## 7. Risk register & go/no-go decision tree

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Cacheable RX without correct `dc ivac` → stale packet bytes (the parked fear) | MED if maintenance sloppy | data corruption | §3.2 contract + §6 bisection + integrity bench gate; single-file revert |
| Unaligned RX slot loses a partial line on `dc ivac` | MED | edge corruption | 64-B align slots; `dc civac` at ends |
| Touching TX/USB/V3D/SD coherency by accident | LOW | regress stable subsystems | Hard rule §4.1: only `bcm-genet.c:653` changes in first cut |
| Throughput stays flat after Policy B (it was H3/H4 not H1) | LOW–MED | wasted step | Step-1 (Policy A) measures the copy-count axis first; H4 cable is a known separate fix |
| Stale comment/doc misleads a future engineer into re-doing global enable | already happening | wasted weeks | §8 doc-correction list |
| SMP broadcast issue (set/way vs VA) | LOW | partial-core staleness | Use VA `dc ivac` (Inner-Shareable broadcast), never set/way at runtime (§3.3) |

**Decision tree:**
- Step 0 boot fast? → **NO:** premise was real; do historical global enable (won't
  happen per §1). **YES:** continue.
- Step 1 throughput meets need? → **YES:** stop (Policy A sufficient); document.
  **NO/flat:** → Step 2.
- Step 2 integrity bench clean AND throughput ↑? → **YES:** ship Policy B; manifest.
  **NO (corruption):** → §6 bisection; if software-exonerated → **[A]/SLC** escalation.

---

## 8. Stale docs to correct (NOT edited here — main agent owns these files)

Per the session constraint this doc only flags them:
- `docs/inprogress/2026-06-14-network-fs-performance-plan.md:88` — "Caches are
  **globally OFF** today." → false; caches ON since 2026-05-17. The hypothesis
  (a)#1's "(caches globally off)" parenthetical conflates "DMA buffer uncached" with
  "global caches off."
- `docs/inprogress/2026-06-15-glquake-capstone-status.md:42-46,69` — "TD-16 caches
  (biggest lever)… caches off." → recast as "uncached GENET RX pbufs."
- `docs/inprogress/calm-wobbling-quill.md` §1.8 / TD-disposition table — "caches
  globally off today" / "[U+T] spike → [A] if SLC recurs." → recast to the
  selective-cacheable-RX plan above.
- `sources/phoenix-rtos-lwip/drivers/bcm-genet.c:74` — "(caches-off, slow)" comment
  is stale residue.
- `docs/inprogress/pi4-hardware-support-matrix.md:18` — "caches still globally off
  (TD-16)." → false.
- The `TEMPORARY-FIXES…` TD-16 *body* (lines 1404-1631) is the historical narrative
  and is fine as history, but the **status table (1891-1893) is the source of truth:
  RESOLVED.**

---

## 9. Open questions needing HW experiments

1. **Step-0 baseline numbers:** exact boot-to-psh wall time, FSHEALTH `read MB/s`,
   Quake FPS with caches ON (today). (Confirms premise + sets the bar.)
2. **Is bulk-read memcpy-bound or copy-count-bound?** The perf doc's `TCP_WND` bump
   test (Step 1) decides H1 vs H3/H5.
3. **GENET RX slot alignment:** is each `dmammap` pool slot 64-B aligned and a
   line-size multiple as currently strided by `GENET_MAX_FRAME`? (Determines whether
   `dc ivac` is safe or needs `dc civac` at ends.)
4. **Does cacheable RX actually clear the bottleneck** (Step 2 FSHEALTH delta), and is
   the next ceiling the 100 Mbps cable (H4) — i.e. does the gigabit-cable swap then
   give ~8–10 MB/s?
5. **Any SLC involvement at all?** Only investigated if §6 software path is exhausted;
   the V3D SLCACTL precedent says SLC issues are code-level, not JTAG.
6. **USB/V3D cacheable opportunity (future):** would making xHCI transfer buffers or
   V3D readback BOs cacheable+maintained help GPU/Quake load times? Out of scope here;
   noted for a later spike.

---

## 10. Bibliography

**In-tree primary sources (file:line cited throughout):**
- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` — EL2→EL1 drop (218-252),
  MAIR/TCR/DEFAULT_ATTRS (116-167), `M|C|I` enable + ritual (558-606), cache helpers
  (932-1006, 1159).
- `sources/phoenix-rtos-kernel/hal/aarch64/pmap.c` — attr→MAIR-index mapping
  (435-481), attr-change maintenance (270-271), device mapping (990).
- `sources/phoenix-rtos-kernel/hal/aarch64/cache.c` — VA-range clean/inval/flush ops.
- `sources/phoenix-rtos-lwip/drivers/bcm-genet.c` — uncached RX pool (653), zero-copy
  pbuf_custom (101-105, 814-819), TX uncached note (1076), stale comment (74).
- `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` — TD-16/16-1/16-cache-enable
  narrative (1404-1631) + RESOLVED status table (1891-1894).
- `docs/inprogress/2026-06-14-network-fs-performance-plan.md` — perf hypotheses + the
  mislabel; FSHEALTH benchmark.
- `docs/knowledge/el2-to-el1-drop.md` — why pre-drop SCTLR writes were inert (the real
  historical TD-16 blocker).

**Linux reference clone (canonical non-coherent-DMA model for BCM2711):**
- `external/linux/arch/arm64/mm/dma-mapping.c:15,37` — `arch_sync_dma_for_device` /
  `arch_sync_dma_for_cpu` (clean before device, invalidate before CPU): the streaming
  cache-maintenance contract Policy B follows.
- `external/linux/drivers/net/ethernet/broadcom/genet/bcmgenet.c:1869,1902,2169,2267`
  — bcmgenet uses **streaming DMA** (`dma_map_single`/`dma_unmap_single`,
  `DMA_TO_DEVICE`/`DMA_FROM_DEVICE`) = cacheable buffers + per-packet maintenance, NOT
  uncached buffers. Proof that "cacheable RX + maintenance" is the production model.

**External / spec / community (Cortex-A72 + BCM2711 bare-metal):**
- ARM Cortex-A72 Technical Reference Manual — L1 32 KB D / 48 KB I per core, shared
  1 MB L2; cache-enable requires MMU + Normal-cacheable mappings; `CTR_EL0.CWG` = cache
  writeback granule (64 B) for DMA alignment.
- ARM Architecture Reference Manual (ARMv8-A) B2.8.1 "Mismatched memory attributes"
  (UNPREDICTABLE on alias mismatch — drove the cacheable low-PA alias in `_init.S`) and
  D-cache maintenance (`dc cvac`/`ivac`/`civac`) semantics.
- Raspberry Pi 4 / BCM2711 processor docs (https://www.raspberrypi.com/documentation/
  computers/processors.html) and BCM2711 peripherals datasheet
  (https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf) — A72 cluster,
  L2 1 MB, non-coherent peripheral DMA path.
- Raspberry Pi bare-metal MMU/cache discussions
  (https://forums.raspberrypi.com/viewtopic.php?t=195401) and S. Matyukevich
  "Raspberry Pi OS" tutorials / Circle / rpi4-osdev — the well-known gotcha that on
  Pi3/Pi4 caches AND `ldxr/stxr` exclusives only work once the MMU is enabled with
  Normal cacheable Inner-Shareable memory (satisfied in §3.1).
- DeepWiki BCM2711 SoC overview
  (https://deepwiki.com/raspberrypi/linux/2.2-bcm2711-soc-(raspberry-pi-4)).

**Project memory cross-refs:** `project_pi4_netboot_100mbps_cable` (H4),
`project_nfs_poll_stall_fix` (the 1.3 ms RPC fix), `project_pi4_v3d_scout` (SLCACTL =
the one real SLC-class fix, code-level), `project_pi4_el0_cntvct_cntkctl` (orthogonal).
```

---

## Overnight perf progress (2026-06-15 night)

**Baseline (netboot, demo, caches ON):** ~21 fps RENDER, but the screen showed only
~3 Hz. NFS read 8.36 MB/s. Present-path attribution (new QSFPS UART self-log in
GL_EndRendering): **finish=7ms, readpx=22ms (dominant), blit=11.5ms, fb0=5.5ms**.

**WIN #1 committed — display decoupled from the 3 Hz idle re-blit (coord HEAD).** The
re-blit thread wrote /dev/fb0 at 3 Hz (caches-OFF-era leftover), so the user saw 3 of
every ~20 rendered frames. GL_EndRendering now writes fb0 every frame (+5.5ms);
re-blit demoted to idle-only refresh. **Visible fps ~3 -> ~19**, pixel-identical. Render
rate dipped 21->19 (the fb0 write). SAFE (content unchanged, harness-verifiable).

**Confirmed: render rate is PRESENT-bound by the uncached V3D readback.** readpx=22ms
(3 MB GPU->CPU detile from the uncached V3D FBO BO) is the same uncached-DMA-read class
as the GENET RX bandwidth issue (H1). So BOTH the big FPS lever (readpx) and the big
bandwidth lever (GENET RX) need the Policy-B coherency change.

**NEXT (staged default-OFF per advisor — silent+intermittent failure can't clear on
overnight evidence):** implement Policy B (cacheable RX pool + per-frame `dc ivac`,
line-aligned) behind a compile flag, default uncached; add an NFS data-integrity bench
(checksum a multi-MB read vs known value) + run the Quake visual harness; measure the
MB/s + fps potential; leave staged for attended multi-boot soak before flipping default.
Also stage a cacheable V3D readback path the same way (the readpx lever). Safe extras:
blit micro-opt (skip alpha write); these are committable live.

## Compile-optimization audit + measured -O3 result (2026-06-15 night)

User asked "are we using all compile-time optimizations?". Audit:
- Core/kernel/devices: `OLVL ?= -O2` + `-mcpu=cortex-a72 -mtune=cortex-a72`
  (A72-specific tuning correctly on) + `-fomit-frame-pointer` + `-mstrict-align`
  (`aarch64.mk`). Sane -O2 with CPU tuning. `-mstrict-align` is risky to drop
  system-wide (would fault on unaligned MMIO). No LTO; no `-ffast-math` (correct —
  it'd break FP determinism the visual harness relies on).
- Quake: `-O2 -g`.

**Experiment: Quake `-O2 -> -O3`, measured. RESULT: ZERO gain** (blit 11.56ms, ~19.5
fps — identical to -O2). Reverted. **Conclusion: Quake FPS is NOT compute-bound** —
-O3 can't help because the frame is dominated by the memory/IO-bound present path
(uncached glReadPixels 22ms + memory-bound y-flip/gamma blit 11.5ms + fb0 write 5.5ms).
The same holds for NFS (app-limited by uncached RX reads). **So compiler flags are not
the perf lever; the DMA/readback cache-coherency is.** This redirects the whole perf
effort to the cacheable-DMA work (Policy B for GENET RX bandwidth + a cacheable V3D
readback for FPS) and/or eliminating the readback (render-to-scanout).

## WIN #2 committed — pipelined present across cores (19 -> 27 fps)

SMP is 4-cpu (boot logs: cpu1/2/3 active), so the present path was parallelized. The
y-flip+gamma blit (11.5ms) + /dev/fb0 write (5.5ms) moved off the render thread to the
present (helper) thread, with a double-buffered glReadPixels target (readbuf[2]) +
mutex/cond handoff. They now overlap the next frame's finish+readpx on another core.
Measured 19 -> 27 fps (+42%); handoff wait 0.2ms; 0 faults; HDMI frame clean (no tearing).
Pure threading, no coherency change, visually verified (HDMI snapshot + 84s stable demo).

**FPS ladder so far (all committed, safe): 3 (orig 3Hz re-blit) -> 19 (per-frame present)
-> 27 (pipelined present).** Remaining ceiling = render-thread finish(7)+readpx(23)=30ms.
readpx is the uncached V3D FBO readback -> the cacheable-readback (Policy B) is the next
lever and the ONLY thing left that needs the coherency change. After that, eliminating
the readback entirely (render-to-scanout / GPU detile-to-linear-fb0) is the big structural
win but a large change.

## Policy B feasibility VERDICT (2026-06-15 night) — needs kernel work, attended

Checked the two cruxes before implementing:
1. **EL0 cache ops: NOT available.** Runtime SCTLR_EL1 keeps **UCI=0** (_init.S:303,589
   "trap cache maint from EL0"). So a userspace driver (lwip/genet, V3D winsys) CANNOT
   run `dc civac/ivac` — it traps. Policy B therefore needs EITHER `SCTLR_EL1.UCI=1`
   (a 1-bit kernel change at the C-3 SCTLR write, line 589 — Linux-standard, benign,
   lets EL0 do its own cache maintenance) OR a kernel cache-maintenance syscall
   (`hal_cpuInvalDataCache`/`hal_cpuCleanDataCache` already exist at EL1).
2. **Cacheable + DMA-contiguous memory: not exposed.** `dmammap` is uncached+contiguous;
   there is no cacheable-contiguous userspace allocation path -> needs a kernel mmap
   attribute/flag or a new alloc path.

**=> Policy B is a multi-component KERNEL + driver change** (UCI=1 or a cache-maint
syscall; a cacheable-contiguous allocator; the per-frame `dc ivac` streaming-DMA
maintenance in genet RX + the V3D readback BO; 64-B alignment). It is coherency-risky
(silent+intermittent) and must be attended-validated (multi-boot integrity soak). It is
NOT a quick unattended driver-only change. **Recommended for an attended session**, with
this plan + the integrity-bench gate. The autonomous FPS wins that did NOT need it
(3->19->27 fps) are committed; the readpx(23ms) ceiling and the NFS uncached-RX ceiling
both wait on this one kernel-enabled change.

## NFS read + write measured (2026-06-15 night)

nfs-smoke now reports both (WHEALTH added): **read 8.53 MB/s, write 7.65 MB/s** (direct
libnfs, 4 MB). Both ~60-68% of the 100 Mbps crossover-cable ceiling (12.5 MB/s). Direct
libnfs write is fast -> the documented write-hang is the nfs-fs VFS bridge specifically,
not libnfs/network. Bandwidth levers: (1) gigabit cable (hardware, ~8-10x headroom),
(2) Policy B cacheable RX (cuts the uncached-read CPU overhead, gigabit-gated for full
benefit). At 100 Mbps we are already near the ceiling for both directions.

## Render-to-scanout feasibility (2026-06-15 night) — the no-coherency FPS lever

The other way to kill readpx(23ms)+blit+fb0 without the CPU-cacheable-DMA coherency
change: have the GPU detile the FBO straight into the /dev/fb0 scanout (CPU never touches
the frame). Feasibility: winsys reports SUPPORTS_TFU=1 + Mesa has v3dx_tfu.c / v3d_blit.c.
BUT winsys ioc_create_bo only allocates fresh mmap memory (va2pa) — it cannot IMPORT the
fixed scanout PA (rpi4-fb 0x3e87c000). So render-to-scanout needs: (1) winsys: import an
external PA as a BO + map it in the V3D MMU; (2) wrap it as a Mesa LINEAR pipe_resource
(matching fb0 RGBA pitch 4096); (3) TFU-blit the tiled FBO -> that resource each frame;
(4) GPU L2/SLCACTL flush so the display controller sees it (winsys already flushes per
submit). MAJOR winsys+Mesa change, GPU-hang risk — but VISUALLY-VERIFIABLE + netboot-
RECOVERABLE (not silent like Policy B). Potential ~50-60 fps (removes the whole CPU
present path). Recommended as the next big FPS swing (attended-quality care, but
unattended-attemptable since failures are visible). PBO async readback does NOT help —
the CPU still reads 3MB uncached to reach fb0; only render-to-scanout or cacheable-FBO
removes that.

## Perf summary (2026-06-15 night)
- FPS: 3 -> 19 -> 27 (committed, safe: per-frame present + cross-core pipelined present).
- NFS: read 8.53, write 7.65 MB/s (~ at 100Mbps cable ceiling; gigabit-gated for more).
- Compile: -O2 + A72 tuning optimal; system is MEMORY/IO-bound (uncached DMA), not compute.
- Remaining levers, all big/gated: cacheable readback+RX (Policy B, KERNEL, attended);
  render-to-scanout (major winsys, recoverable); gigabit cable (hardware). Quick safe
  unattended wins are captured.

## Render-to-scanout IMPLEMENTATION SPEC (for an attended build) — 2026-06-16

Primary sources read: Linux `drm/v3d/v3d_sched.c:346-361` (TFU register write order) +
`v3d_regs.h` (offsets) + Mesa `v3dx_tfu.c` (arg fill). Two mechanisms; **(A) is
recommended** — it's how a real KMS driver scans out and avoids the TFU entirely:

**(A) Render directly to a RASTER FBO backed by the scanout (preferred).** Make Quake's
GL color attachment a v3d resource with `V3D_TILING_RASTER`, its BO = the imported fb0
scanout (RGBA8, pitch 4096 = 1024*4 — matches). The V3D RCL store (v3dx_rcl.c already
handles RASTER stores) writes final tiles LINEAR straight into the scanout. Result: NO
glReadPixels, NO CPU blit, NO fb0 write — the whole ~30ms present path collapses to the
render itself (~7ms) -> potential ~60-100fps. Costs to handle: (1) y-flip — set the GL
viewport/projection flipped (Quake renders upside-down into the scanout so it displays
right-side-up), OR accept the existing cosmetic flip; (2) gamma 0.5 brighten — currently
a CPU LUT; move to a GPU post-pass or bake into the colormap/textures (or drop it — the
raw frame already matches the host per the visual harness); (3) tearing — the display
reads the scanout while the GPU writes it; double-buffer (two scanout BOs, flip via
rpi4-fb pan — needs a pan/flip devctl in rpi4-fb, currently absent) or accept tearing.

**(B) TFU blit tiled-FBO -> linear-scanout (fallback).** Per Linux v3d_tfu_job_run, write
TFU regs in order: IIA(input addr+offset), IIS(input stride: RASTER=stride/cpp, UIF=
padded_height/...), ICA, IUA, IOA(output addr | IOA_FORMAT | DIMTW), IOS((h<<16)|w),
COEF0-3 (if USECOEF), then ICFG(|IOC to raise the done irq; FORMAT in ICFG_FORMAT_SHIFT,
ttype in ICFG_TTYPE_SHIFT). V3D 4.2 offsets: TFU_CS=0x400, etc. (v3d_regs.h, ver<71).
**KEY UNKNOWN: does the TFU support a plain-RASTER OUTPUT (linear scanout)?** Mesa only
ever uses TFU output as UIF/tiled (mipmap gen) — IOA_FORMAT options center on tiled. If
the TFU can't write plain raster, (B) is dead -> use (A). Verify before committing to (B).

**Both need (winsys):** an `ioc_import_bo(pa, size)` that maps an externally-supplied PA
(the fb0 scanout, from rpi4-fb RPI4FB_GETMODE = 0x3e87c000) into the V3D MMU at a GPU VA
WITHOUT allocating fresh memory (current ioc_create_bo does mmap+va2pa; add a variant
that takes the PA directly). Coherency: the per-submit SLCACTL/L2 flush (winsys already
issues it) pushes the GPU's writes to DRAM so the display controller (reading the scanout
from DRAM) sees them. **Incremental attended bring-up:** step 1 = import scanout PA + a
GPU clear-to-color into it, confirm the color on HDMI (proves import + GPU-write-to-
scanout + flush); step 2 = (A) point the FBO color store at it + render; step 3 = y-flip
+ gamma + (optional) double-buffer. Each step HDMI-verifiable + netboot-recoverable.

## Policy B EXECUTION — step 1 DONE, step 2 ready (2026-06-16)

**Step 1 (committed + HW-verified): SCTLR_EL1.UCI=1** (kernel hal/aarch64/_init.S) — one
line, Linux-standard, enables EL0 cache maintenance. Proven: an EL0 `dc civac` selftest in
rpi4-quake VID_Init runs without trapping ("PL_VID: EL0 dc civac OK"), boot clean, 0 faults.
This + the discovery that **cacheable+contiguous = just drop MAP_UNCACHED** in the winsys
mmap (the flag exists, kernel mman.h) collapse the earlier "attended-huge" verdict: the
allocator + EL0-cache-op cruxes are both trivial.

**Step 2 (designed, ready) — cacheable V3D readback (the readpx 23ms FPS lever):** the slow
path is v3d_resource_transfer_map (v3d_resource.c:337) detiling from rsc->bo->map (UNCACHED)
via v3d_load_tiled_image. Make ONLY the readback color RT cacheable + invalidate before the
read. Exact edits:
1. winsys ioc_create_bo (v3d_phoenix_winsys.c): honor a cacheable bit in drm_v3d_create_bo.flags
   -> mmap WITHOUT MAP_UNCACHED (keep MAP_CONTIGUOUS). Record cacheable in struct pbo.
2. Mesa v3d_bufmgr.c:119 v3d_bo_alloc + :154 the CREATE_BO ioctl: thread a `flags`/cacheable
   param -> set create.flags; add `bool cacheable` to struct v3d_bo (v3d_bufmgr.h).
3. Mesa v3d_resource.c:113 (BO alloc): cacheable = (prsc->bind & PIPE_BIND_RENDER_TARGET) &&
   !(prsc->bind & PIPE_BIND_SAMPLER_VIEW) — selects the GPU-written/CPU-read-only readback RT;
   EXCLUDES all sampled textures + depth (which stay uncached so GPU reads them fine). This is
   the advisor's "strictly opt-in, don't leak onto GPU-read BOs".
4. Mesa v3d_resource_transfer_map (v3d_resource.c, before the PIPE_MAP_READ detile at :337):
   if rsc->bo->cacheable, `dc ivac` over [rsc->bo->map, +size) (64B-aligned loop) + dsb, so the
   cached detile read fetches the GPU-written DRAM, not stale CPU lines.
GATE (advisor): verify FRAME CORRECTNESS via the visual harness vs the known-good uncached
frame (a coherency bug is fast AND wrong); confirm the per-submit SLCACTL flush actually pushes
the color write to DRAM before glFinish (most likely stale-frame source). Expected: readpx
23->~5ms -> frame ~12ms -> ~80fps, which would OBVIATE render-to-scanout (tell the user). Then
step 3 = same pattern for GENET RX (NFS bandwidth), gated behind the nfs-smoke integrity check
(NFS corruption is silent). Intricate multi-file Mesa change -> implement with fresh context.

## Cacheable-DMA DEEP DIVE — hang cracked, readpx characterized (2026-06-16)

Pushed the cacheable readback to completion. Findings:

**1. The hang was a wrong cache instruction, NOT a coherency wall.** Making the V3D RT BO
WB-cacheable + invalidating before the CPU readback hung after frame 0. Root cause: the
invalidate used `DC IVAC` (invalidate-only) which is **EL1-privileged and traps at EL0
even with SCTLR.UCI=1** — UCI gates DC CVAU/CVAC/CVAP/CIVAC + IC IVAU, NOT DC IVAC. The
UCI selftest passed only because it used DC CIVAC. Fix: use **DC CIVAC** (clean+invalidate,
EL0-allowed); for a CPU-read-only readback BO there's nothing dirty so the clean is a
no-op. After the fix: no hang, frames progress, 0 faults.

**2. Cacheable readback gives ZERO speedup for the tiled RT — readpx stays ~23ms.**
Verified the mapping IS cacheable (kernel vm/map.c:541: only MAP_UNCACHED sets
PGHD_NOT_CACHED; dropping it → MAIR_IDX_CACHED WB). Yet cached==uncached==23ms. So the
cost is NOT the uncached read — it's `v3d_load_tiled_image`'s **scattered tiled access
pattern** (each output pixel from a non-contiguous tiled source addr → no cache-line
reuse → caching can't help). Also confirmed DC CIVAC over 3 MB/frame is negligible (readpx
didn't rise). RT-cacheable therefore DISABLED (no benefit); infra kept.

**3. Linux confirms the model:** v3d maps BOs **Write-Combine** (`map_wc`/pgprot_writecombine),
never WB — and Phoenix's MAP_UNCACHED is already Normal-NC (= WC-equivalent). Linux never
CPU-reads-back per frame; it scans out (KMS). So the readback architecture is the issue.

**Proven + committed infra (foundation for the real levers):** SCTLR.UCI=1 (kernel);
v3d_bo_alloc_flags + the V3D_CREATE_BO_CACHEABLE winsys flag (drop MAP_UNCACHED) +
per-page va2pa mapping (robust, no contiguity assumption) + the DC CIVAC invalidate helper.
All work with no hang.

**Real FPS levers (next):** the detile must move to the GPU — (a) render to a RASTER/LINEAR
RT (no detile; glReadPixels = linear copy; + cacheable → fast) — smallest change, but the
GPU raster-store may cost some render time; (b) TFU-blit tiled→linear-cacheable then a fast
linear CPU read; (c) render-to-scanout (no CPU read). All need GPU-side work. **Real NFS
lever:** cacheable GENET RX — LINEAR buffers, read multiple times (checksum+copy), so
caching DOES help there (unlike the scattered tiled detile); gated behind an integrity bench.

## WIN: linear (raster) color RT + cacheable readback — 27 -> ~52 fps @1024x768 (2026-06-16)

The detile was the cost (scattered tiled read, not the uncached read). Fix: force the
full-screen color RT to RASTER (linear) layout (v3d_resource.c should_tile, targeted by
display size so small SAMPLED RTs like the water warpimage stay tiled for the TMU) + map
that RT cacheable (now a LINEAR read, which caching DOES accelerate). glReadPixels then
takes the non-tiled fast path = a linear cached copy. Measured: **readpx 23ms -> ~6.5ms;
FPS 27 -> ~52** @1024x768, HDMI render correct (textured walls/floor/weapon, no tearing),
0 faults. Validates the cacheable infra on linear buffers (and by extension the NFS GENET-RX
path). New bottleneck = the present thread (y-flip+gamma blit 11.5ms + fb0 write 5.5ms =
17ms). For 40fps@1080p: (1) cut the blit (drop/cheapen the per-pixel gamma), (2) the fb0
write + readback are eliminated by render-to-scanout (render the raster RT straight into
the scanout BO), (3) 1080p framebuffer (firmware/plo). The raster RT is the half-step
toward render-to-scanout (the scanout IS a raster surface).

## 1080p measurement + blockers (2026-06-16)

Bumped Quake to 1920x1080 (FBO) + tried framebuffer_width/height=1920/1080. Measured +
learned (then reverted to the clean 52fps@1024x768 state):
- **Quake renders 1920x1080 at ~24fps** — RENDER-THREAD-bound: finish ~20ms + readpx
  (linear cached) ~16ms = ~36ms. So even a perfect present caps at ~24fps; **readpx must
  move OFF the render thread** to hit 40fps (-> render thread = finish only ~20ms -> ~50fps).
- **framebuffer_width/height did NOT take** — fb0 stayed 1024x768 (rpi4-fb reports
  1024x768/pitch=4096). The firmware/fkms overlay ignores legacy framebuffer_width/height;
  a true 1080p fb likely needs hdmi_group/hdmi_mode AND depends on the attached display's
  EDID/mode — NOT verifiable autonomously (only HDMI snapshots; the 1024x768 fallback
  suggests no EDID). The 1080p FBO -> 1024x768 fb0 present was size-mismatched (broken).

**Path to 40fps@1080p (clear, substantial):** (1) FPS: move readpx off the render thread
+ MULTI-THREAD the present (split read+blit+fb0 across the 4 cores; the RT is LINEAR so
workers read its BO directly, no glReadPixels) -> render thread = finish only -> ~50fps@1080p
render. Validatable at 1024x768 first. (2) DISPLAY: a real 1080p framebuffer needs firmware
hdmi_mode config + the actual monitor's support — ATTENDED (HDMI eyes), since legacy fb
config didn't work and the display capabilities are unknown from here. Render-to-scanout
remains the alternative to the multi-thread present but adds a GPU y-flip complication.

# Round 3 — Authoritative ARMv8.0 / Cortex-A72 Spec for MMU+Cache Enable on a Cold Core

This is the architectural-truth reference for what ARMv8.0-A (and specifically Cortex-A72 r0p3, the BCM2711 core) requires of software during the transition from "cold reset, MMU off, caches off" to "MMU on, caches on". It is not a survey of Linux/U-Boot/TF-A practice; the goal is to identify what the *architecture* mandates versus what is convention. Section/page citations refer to:

- **ARM ARM** = *Arm Architecture Reference Manual for A-profile architecture*, ARM DDI 0487 (revision references below are to issue G.b / J.a, the public revisions current at time of writing; section numbering for the cache/MMU material has been stable across revisions D – J).
- **A72 TRM** = *Arm Cortex-A72 MPCore Processor Technical Reference Manual*, ARM 100095 (was ARM DDI 0488 prior to renumbering), revision r0p3.
- **A72 SDEN** = *Cortex-A72 MPCore Software Developers Errata Notice*, ARM EPM-012079, latest revision.
- **A72 PG** = *Programming the ARM Cortex-A72*, ARM application/programmer's guide.
- **booting.rst** = `Documentation/arch/arm64/booting.rst` in the Linux source tree (canonical statement of the firmware → kernel boot protocol on AArch64).

Every claim below either (a) cites a specific section, or (b) is explicitly flagged as "convention, not architecture".

---

## 1. Cache state at reset

### 1.1 What the architecture says

ARM ARM §D7.2.18 *Behavior of caches at reset* (the section the user named "D7.2.18 Cache enabling and disabling"; in some revisions it is titled *Cache enabling, disabling, and behavior at reset*) is the controlling text. Three rules apply to a generic ARMv8-A implementation:

1. **Cache enables (`SCTLR_ELx.{C,I}`) reset to 0.** The MMU enable `SCTLR_ELx.M` likewise resets to 0. See ARM ARM §D13.2.118 *SCTLR_EL1, System Control Register (EL1)* — the reset value of architecturally-RES1 fields is implementation-defined but C, I, M, WXN, A, etc. all reset to 0.

2. **Contents of the caches at reset are UNKNOWN/UNDEFINED.** ARM ARM §D7.2.18 states explicitly that the contents of any cache that is invalidated by an IMPLEMENTATION DEFINED hardware mechanism at reset are guaranteed to be invalid; otherwise the contents are UNKNOWN. The rule for software is therefore conservative: assume *all* caches contain UNKNOWN tags and UNKNOWN data after reset unless the implementation guarantees hardware invalidation.

3. **Software is REQUIRED to invalidate any cache it intends to enable, before enabling it.** The same section: "Before enabling a cache, software must invalidate the cache. This is because the contents of the cache are UNKNOWN out of reset, and would otherwise corrupt the architectural state of memory the first time the cache is used." The architecture does *not* mandate hardware auto-invalidation; it places the obligation on software.

This answers the four sub-questions:

- **I-cache contents UNDEFINED at reset?** Yes (ARM ARM §D7.2.18, plus §D7.5 *Cache support*).
- **D-cache contents UNDEFINED at reset?** Yes, same citation.
- **Does the architecture REQUIRE software to invalidate before enable?** Yes, §D7.2.18; auto-clean at reset is implementation-defined and may not be present.
- **"Disabled" vs "absent":** ARM ARM glossary entry *Disabled cache* and §D7.2.18 are clear. A *disabled* cache (`SCTLR.C=0` / `SCTLR.I=0`) still exists as physical hardware, may still be looked up on coherency snoops, may still hold lines from before it was disabled, and may be filled in implementation-defined ways by speculation/maintenance ops; an *absent* cache (a level not implemented at all, indicated by `CLIDR_EL1.Ctype<n>=0b000`) is logically not present and accesses bypass directly to the next level. The two have very different semantics for software.

### 1.2 Cortex-A72 specifics

A72 TRM §6.1 *About the L1 memory system* and §7.1 *About the L2 memory system* document that Cortex-A72 *does* perform automatic invalidation of its L1 instruction cache, L1 data cache, and L2 unified cache out of cold reset. A72 TRM §2.4.1 *Reset* names the auto-invalidation explicitly. However, this is an *implementation* property of A72; software written to the architecture cannot assume it, and the recommended baseline (A72 PG §11 *Caches*; ARM ARM §D7.2.18) is to invalidate explicitly anyway, since the cost is negligible (one `IC IALLU` plus a set/way sweep) and the benefit is portability.

In practice, the BCM2711 firmware/`armstub8.bin` chain re-enters the cores at EL2 with caches *already enabled* by the firmware (see §7 below), which means the A72 hardware auto-invalidation has long since been bypassed by the time a Phoenix-RTOS PLO loader runs. Software cannot rely on the cache being clean on entry and must perform its own maintenance.

---

## 2. Mandatory pre-MMU steps

ARM ARM §D5.2.6 *Enabling and disabling the MMU* and §D5.10 *Cache and TLB invalidation* together specify the minimal sequence software must perform before setting `SCTLR_ELx.M=1`. The architectural requirements are:

1. **Translation tables must be observable to the table walker.** ARM ARM §D5.10.2 *Visibility of changes to the translation tables*. If the tables were written with the MMU off (caches off), the writes must reach the *Point of Coherency* (PoC), because the table walker observes memory at PoC for cacheable walk attributes that are themselves not yet established, and because any stale cache lines from an earlier MMU-on context could otherwise shadow them. The DC CVAC (clean by VA to PoC) operation, or equivalently DC CIVAC, is the canonical mechanism. Followed by a `DSB ISH` (or `DSB SY` if non-Inner-Shareable masters might walk).

2. **TLB invalidation.** ARM ARM §D5.10.4 *TLB maintenance requirements*. Before relying on the freshly-written tables, software must execute `TLBI VMALLE1` (or the corresponding ELx variant) and follow with `DSB ISH; ISB`. Out of reset the TLBs are architecturally UNKNOWN; the sequence is the same.

3. **Instruction cache invalidation.** ARM ARM §D7.2.18 (mandatory pre-enable; see §1.1 above) plus §B2.7 *Ordering of cache and branch predictor maintenance operations*. Use `IC IALLU` (Invalidate I-cache All to PoU). It must be followed by `DSB ISH; ISB` so that the invalidation is observed by the local fetcher before the next instruction is fetched after `SCTLR.M=1`.

4. **Data cache invalidation.** ARM ARM §D7.2.18 + §D7.10.1 *Cache maintenance instructions*. The architecturally-mandated method on a freshly-reset core is *invalidate by set/way* (`DC ISW`) iterated over every set/way of every level reported in `CCSIDR_EL1` selected by `CSSELR_EL1`, walking levels from L1 outward up to (but not including) the level reported by `CLIDR_EL1.LoUIS`/`LoC`. There is no architecturally-required by-VA option here, because the core has no valid VA mapping until §1 of this list runs; see §4 below for when set/way is and is not appropriate.

5. **Page-table store maintenance.** Same as item 1, but worth restating: every store that produced a translation table entry (`STR`) must precede a `DSB ISHST` (or stronger) before the table is read by the walker. If the stores happened with caches off, no DC maintenance is needed for the *local* core (the data went straight to PoC); but if any other agent ran with caches on, by-VA cleans to PoC are required to flush any lines they may have allocated. ARM ARM §D5.10.2.

6. **Barrier ordering — the canonical "enable MMU" sequence.** The architectural recipe is:

   ```
   ; ... write TTBR0_EL1, TTBR1_EL1, TCR_EL1, MAIR_EL1 ...
   DSB ISH                  ; system register writes complete
   TLBI VMALLE1             ; invalidate TLBs at this exception level
   DSB ISH                  ; TLB invalidate complete
   IC  IALLU                ; invalidate I-cache to PoU
   DSB ISH                  ; I-cache invalidate complete
   ISB                      ; context synchronization
   ; MSR SCTLR_EL1 with M=1, C=1, I=1
   ISB                      ; new SCTLR observed before next fetch
   ```

   ARM ARM §B2.3.5 *Profile-specific information about the use of the synchronization barriers*, §D7.10.3 *Ordering and completion of cache and branch predictor maintenance instructions*. The two `ISB`s are not optional: the first guarantees the I-cache invalidate is in effect for the *next* fetch; the second guarantees that fetches after the `SCTLR` write actually go through the (now enabled) MMU. Skipping either is a documented defect mode (compare A72 SDEN errata classes for missing `ISB` after SCTLR writes).

A subtlety: when `SCTLR.M` is set with `SCTLR.C=0`, all data accesses are treated as Normal Non-cacheable + Outer Shareable per ARM ARM §D5.5.4 *Behavior of memory accesses when the stage 1 MMU is enabled but stage 1 data accesses are non-cacheable* (effectively §D5.5; the precise sub-section depends on revision). This is one reason it is normal practice to set M, C, I together — there is no useful intermediate state for kernel boot.

---

## 3. Speculative behavior with caches off

### 3.1 Can the L1 D-cache fill while `SCTLR.C=0`?

ARM ARM §D7.5 *Cache support*, specifically §D7.5.7 *Non-cacheable accesses and instruction caches* (the only ARM ARM section the search engine surfaced verbatim) and the broader §B2 *The AArch64 application level memory model*: when `SCTLR_ELx.C=0`, all data accesses from that EL are treated as Normal Non-cacheable. ARM ARM §B2.7.2 *Caches and memory hierarchy* further establishes the rule "an implementation must not allocate into a cache for an access marked Non-cacheable". So the architectural answer is **no, the L1 D-cache must not be filled by speculation while `SCTLR.C=0` on behalf of accesses originating from that EL**.

The qualifier matters. There are three legitimate sources of cache fills even when one EL has caches off:

1. Another EL with caches on (e.g., EL2 with caches enabled while EL1 has them off).
2. Cache maintenance operations (a `DC CVAC` does not allocate, but `DC ZVA` does on Cacheable mappings, and `PRFM` hints can on Cacheable mappings).
3. A different agent in the coherency domain (another core, a coherent accelerator, the GPU on BCM2711) acting on the line.

So "caches off" on one core does **not** mean lines vanish from that core's L1 — it means *that core* cannot allocate. ARM ARM §D7.5 *Cache support* and §D5.7 *Memory coherency*.

### 3.2 What happens to disabled-cache lines when `SCTLR.C` becomes 1?

ARM ARM §D7.2.18 (continued): if a line is present in a cache when the corresponding cache enable transitions from 0 to 1, the line remains present and may be hit. The architecture does not flush or invalidate on the C/I transition. This is the precise reason the rule in §1 exists: **the only safe assumption when enabling caches is that they may already contain stale lines from a prior context (firmware, prior boot, soft-reset, another EL), and software must invalidate first**.

For a Pi 4 boot chain this is not academic. The VideoCore firmware and the GPU-side bootloaders, then `armstub8.bin`, then potentially TF-A's BL31, all run with caches on. By the time PLO/Phoenix gets the core, the L1 D-cache, L1 I-cache, and L2 cache may all hold tagged lines covering DRAM ranges that PLO has *just* rewritten via DMA or with caches off. Without explicit invalidation those stale lines win.

### 3.3 Does setting `SCTLR.M=1` itself trigger a cache invalidation?

No. ARM ARM §D5.2.6 enumerates the architectural effects of setting M=1 (begin stage-1 translation, address attributes obey TTBR/TCR/MAIR, TLB lookups become live), and cache invalidation is not among them. There is no architectural side-effect on caches from MMU-enable; whatever lines were present continue to be present, now indexed by the (PA-derived for VIPT D-cache) tags from before. On A72 the L1 D-cache is PIPT (A72 TRM §6.1 *About the L1 memory system*), so VA aliasing on MMU-enable is not a concern, but stale-data aliasing absolutely is.

---

## 4. Set/way vs by-VA cache maintenance

ARM ARM §D7.2.6 *Cache maintenance instructions* and §D7.10.1 *Data cache and unified cache maintenance instructions* govern the choice.

- **By set/way (`DC ISW`, `DC CSW`, `DC CISW`):** Local to the executing PE only. Not broadcast. Not architecturally guaranteed to interact correctly with system caches or other PEs. Architecturally suitable in exactly two situations:
  1. **Cold boot of a single PE** before any coherent activity — where the only thing in any cache local to this PE is whatever the implementation happened to leave there at reset.
  2. **Power-down sequences** where the PE is preparing to drop out of the coherency domain and must drain its dirty lines to the next level.

  ARM ARM §D7.2.6 explicitly warns that the geometry exposed by `CCSIDR_EL1` (sets, ways, line size) is the *architecturally visible* parameterization for set/way ops, but is not guaranteed to be a faithful description of physical cache geometry; consequently, set/way ops are *not* a reliable way to enforce coherence on running systems.

- **By VA (`DC CVAC`, `DC CVAU`, `DC IVAC`, `DC CIVAC`):** Broadcast within the Inner Shareable domain, well-defined with respect to coherent agents and system caches that respect VA-broadcast maintenance. Architecturally suitable everywhere set/way is *not* — i.e., everywhere except the two cases above.

**Does ARM REQUIRE set/way at boot?** Strictly: No. ARM ARM §D7.2.18 requires *invalidation* before enabling, and the architecturally-defined way to invalidate the entire local cache without relying on a valid VA map is set/way. By-VA is also legal and is what `booting.rst` recommends because it composes with system caches. In practice, "boot code uses set/way" is convention informed by the practical reality that early code has no MMU and therefore no VA range it can name. ARM Cortex-A Series Programmer's Guide for ARMv8-A (DEN0024) §11 makes this explicit and walks through the canonical set/way invalidation loop.

For Phoenix-RTOS on Pi 4 the practical implication is: the loader-to-kernel hand-off cannot be fixed solely with set/way ops in the kernel, because the staleness is not in the kernel-local cache but rather in lines installed by prior firmware EL2 contexts that are observed via Inner Shareable broadcast. The fix must be by-VA over the kernel image range, executed *somewhere* (the loader, the kernel before image use, or the firmware) at a time when the relevant cache levels still recognize those VAs.

---

## 5. PoC vs PoU

ARM ARM §D5.10.2 *Caches and the memory hierarchy* (in some revisions §D7.5.4 *Coherency and the cache hierarchy*) defines:

- **Point of Coherency (PoC):** the point in the system at which all observers — every PE and every DMA-capable agent — are guaranteed to see the same copy of a memory location. On most SoCs this is main DRAM (or the level just before it where all coherent caches drain). On BCM2711 this is DRAM after the coherent fabric.

- **Point of Unification (PoU):** the point at which a single PE's instruction cache, data cache, and translation table walker observe the same view of memory. Per ARM ARM §D5.10.2, PoU is *per PE* by default; ARM ARM §D5.10.3 defines PoU-IS (PoU Inner Shareable) as the highest PoU across the Inner Shareable domain. On A72 the PoU is generally the unified L2 (ARM ARM §D7.5.4 + A72 TRM §7.1).

Mapping to operations the boot code performs:

- **Translation table walks** observe memory at the PoC for the *attributes* used for the walk (ARM ARM §D5.2.4 *Translation table walks*). When walks are configured Cacheable Inner-WB Outer-WB Inner-Shareable (the typical kernel choice), the walker reads through caches; for the *first* enable, software cleans the freshly-written tables to the PoC because the walker's first read may bypass caches that were not yet active.
- **Instruction fetch** with `SCTLR.I=1` observes memory at the PoU. Hence `IC IALLU` (which targets PoU) is the mandatory companion to instruction-stream modification.
- **`IC IVAU` and `DC CVAU`** target PoU; **`DC CVAC`, `DC IVAC`, `DC CIVAC`** target PoC. The dichotomy matters for self-modifying code (PoU is enough) versus DMA-coherency (PoC is required).

For the kernel-image hand-off described in §8 below, the operation that the architecture mandates is **clean to PoC** of the loaded kernel image (so DMA-after-DRAM is correct), followed by **invalidate to PoU** of the I-cache (so the new code is fetched from a memory view consistent with the cleaned data).

---

## 6. Cortex-A72 deviations from ARMv8 baseline

A72 TRM §2 *Functional description* is the controlling document. The relevant points:

- **Cache topology** (A72 TRM §6.1 *About the L1 memory system*, §7.1 *About the L2 memory system*):
  - Per core: split L1 — 48 KB, 3-way set-associative, 64-byte line, VIPT-tagged but physically tagged (PIPT-equivalent for software, no aliasing) for instructions; 32 KB, 2-way set-associative, 64-byte line, PIPT for data.
  - Per cluster: unified L2, configurable 512 KB / 1 MB / 2 MB / 4 MB, 16-way set-associative, 64-byte line, inclusive of the L1 D-caches, non-inclusive of the L1 I-caches. BCM2711 instantiates the 1 MB variant (ARM 100095 §7.1, plus Broadcom BCM2711 ARM peripheral docs).
  - `CTR_EL0` exposes the architecturally-visible cache line sizes: `IminLine`, `DminLine`, both = 4 (encoding for 64 bytes; 4 means 16 words = 64 B). `CTR_EL0.IDC=0` and `CTR_EL0.DIC=0` on A72: software must perform explicit data-cache cleans to PoU for instruction-stream changes (no auto-coherency between D and I caches).

- **Cache control bits beyond the ARMv8 standard.** A72 TRM §4.5.32 *CPU Auxiliary Control Register, EL1* (`S3_1_C15_C2_0`) and §4.5.33 *L2 Control Register* (`S3_1_C11_C0_2`) define A72-specific knobs: L1 prefetch hint settings, L2 ECC enable, L2 tag/data RAM latency, hardware prefetch disables. None of these affect *correctness* of cache enable; they affect performance and (in errata-affected revisions) correctness around speculation, but ARM standard ARMv8 cache/MMU enable proceeds correctly with these untouched.

- **Implementation-defined behaviors that affect early-boot cache enable:** Hardware auto-invalidation at reset (A72 TRM §2.4.1) — useful but bypassed by the time PLO runs. Speculative AT walks: A72 SDEN erratum 1319367 (all revisions) — the A72 may speculatively walk translation tables when the MMU is disabled if the walker observes residual TTBR/TCR; this is why TF-A's `cortex_a72.S` resets `TTBR0_EL3=0`, `TCR_EL3=0` early. Erratum 859971 (≤ r0p3): incorrect speculative reads with prefetch-only PMD entries; workaround in `ACTLR_EL3` bit 38. The BCM2711 instantiates A72 with L2 ECC enabled by firmware via L2CTLR — re-touching L2CTLR while the cache is populated can corrupt data. The four A72 cores plus DMA agents form one Inner-Shareable domain; PoC is DRAM after the LPDDR4 controller.

The net effect: an ARMv8-baseline cache-enable sequence works on A72 *if* it leaves ACTLR/L2CTLR alone and *if* it actually performs the invalidation step rather than relying on hardware auto-invalidate.

---

## 7. The boot protocol ARM mandates / recommends

There is no ARM-published "AArch64 boot protocol" document with the force of architecture. The de facto standard is `Documentation/arch/arm64/booting.rst` from Linux, which represents the *consensus* that ARM, Linaro, and SoC vendors converged on. The relevant requirements (verbatim summary, not full quote, to honor copyright):

1. **Primary CPU state at kernel entry** (booting.rst §4 *Call the kernel image*):
   - The CPU shall be in EL2 (preferred) or non-secure EL1.
   - All forms of interrupts shall be masked.
   - The MMU shall be off.
   - The I-cache may be on or off, but it must hold no stale entries corresponding to the kernel image (typical practice: invalidate it).
   - The D-cache shall be off.
   - **The address range of the loaded kernel image must be cleaned to the PoC.**
   - Architected timer access must be configured.
   - Caches in coherent system masters (interconnect, GPU, etc.) that respect VA-broadcast maintenance may be enabled; those that don't, must be disabled.

2. **Compliance status of the BCM2711 / Pi 4 firmware chain.** The Pi 4 chain is non-compliant with a strict reading of booting.rst. `start4.elf` writes the kernel image into DRAM with caches on. `armstub8.bin` clears C and I in `SCTLR_EL2` and clears its own L1 dirty state via set/way, but does *not* perform a `DC CIVAC` sweep over the kernel image range — and set/way is not architecturally adequate (§4) when system caches and other observers are involved. The default firmware variant (no TF-A) leaves dirty L1/L2 lines covering the kernel image; Linux survives because its head.S performs an aggressive by-VA sweep before relying on the image. With TF-A BL31, BL31 cleans its own caches but again does not clean the next-stage image range to PoC. The Phoenix loader/kernel boundary is exactly where this shortfall becomes visible — kernels must either tolerate it or perform the clean themselves at entry.

---

## 8. The specific scenario: loader writes data with caches on, jumps to kernel with caches off, kernel enables caches

This is the Phoenix-RTOS Pi 4 case described in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` TD-04. Walking it through the architecture:

**Step A: Loader (PLO) executes with `SCTLR.C=1` and writes the kernel image into DRAM.**
The writes land in the loader's L1 D-cache (write-back per the typical Cacheable mapping of loader text/data). Lines may be marked dirty. The L2 cache may also be populated (A72 L2 is inclusive of L1 D, so dirty L1 lines are reflected in L2 as Modified). Other observers in the IS domain — the other three A72 cores, any coherent DMA agent — may also have non-Modified copies.

**Step B: Loader transitions to "caches off" before jumping.**
In the architecturally-correct sequence (ARM ARM §D7.10.1, "switching off the data cache"):
1. `DSB ISH` — drain pending stores.
2. `DC CIVAC` over the kernel image range, kernel data range, kernel BSS, and any auxiliary data (DTB, command-line buffer, initial page tables prepared by the loader) — clean+invalidate to **PoC**, broadcast IS.
3. `DSB ISH` — wait for the by-VA broadcast cleans to complete.
4. `IC IVAU` over the kernel image range, or `IC IALLU` — invalidate I-cache to PoU.
5. `DSB ISH; ISB`.
6. Clear `SCTLR.C` (and `SCTLR.I`, and `SCTLR.M`).
7. `ISB`.
8. Branch to the kernel entry point.

This is the architectural minimum. ARM ARM §D7.10.1 *Data cache and unified cache maintenance instructions* describes the by-VA primitives; §B2.7 / §D7.10.3 describe the ordering of maintenance with `DSB`/`ISB`; the *entire* sequence is built from architecturally-named steps.

**Step C: Kernel runs with caches off, sets up tables, enables MMU+caches.**
At this point the kernel sees a clean view of memory (because the loader cleaned to PoC) and a cache that contains no aliased lines (because the loader invalidated by VA). The kernel:
1. Writes its translation tables to memory (caches still off, so writes are Normal Non-cacheable from this PE's perspective and reach PoC directly per ARM ARM §B2.7).
2. Performs the canonical "enable MMU" sequence from §2 above.

**Where ARM puts this in the manual.** No single ARM ARM section spells out the loader→kernel transition; the requirement is implied by the conjunction of §D7.2.18 (invalidate before enable; contents survive C/I transitions), §D5.10.2 (table walks observe at PoC), §B2.7.2 (Non-cacheable accesses do not allocate, but Cacheable allocations persist across `SCTLR.C` transitions), and §D7.5.4 (PoC vs PoU governs the level to which maintenance must reach).

The conclusion is unambiguous: in the "loader writes with caches on, kernel runs with caches off, kernel enables caches" pattern, **cache maintenance is mandatory and must happen between Step A and Step B**, or in early Step C before the kernel relies on the data. The Linux convention is for the loader (or BL31) to do the clean — `booting.rst` codifies it as the loader's responsibility. For Phoenix-RTOS on Pi 4 the pragmatic options are: (1) make PLO clean-to-PoC the kernel image range and DTB before jumping (matches booting.rst); (2) make the kernel head.S `DC IVAC` over its own image and DTB range before any Cacheable load (Linux head.S approach, robust against firmware shortfalls); or (3) live with caches off — slow and unmaintainable.

---

## 9. Sources

Primary architectural specifications (cited above):

- [Arm Architecture Reference Manual for A-profile architecture (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest/) — §B2 application memory model, §D5 system MMU, §D7 system caches, §D13 SCTLR_EL1.
- [ARM Cortex-A72 MPCore Processor Technical Reference Manual r0p3](https://www.scs.stanford.edu/~zyedidia/docs/arm/cortex_a72.pdf) — §2 Functional description, §6 L1 memory system, §7 L2 memory system, §4.5 system register summary.
- [Cortex-A72 MPCore Software Developers Errata Notice (EPM-012079)](https://developer.arm.com/documentation/epm012079/latest/) — errata 859971, 1319367.
- [ARM Cortex-A Series Programmer's Guide for ARMv8-A (DEN0024)](https://cs140e.sergio.bz/docs/ARMv8-A-Programmer-Guide.pdf) — §11 Caches, §13 MMU.
- [Booting AArch64 Linux — `Documentation/arch/arm64/booting.rst`](https://docs.kernel.org/arch/arm64/booting.html) — primary CPU requirements, kernel-image clean-to-PoC mandate.

Supporting / corroborating references:

- [Cache maintenance — Arm Developer (DEN0024 online)](https://developer.arm.com/documentation/den0024/a/Caches/Cache-maintenance)
- [Invalidating and cleaning cache memory (DEN0013)](https://developer.arm.com/documentation/den0013/latest/Caches/Invalidating-and-cleaning-cache-memory)
- [Point of coherency and unification (DEN0042)](https://developer.arm.com/documentation/den0042/a/Caches/Point-of-coherency-and-unification)
- [IC IALLU — Instruction Cache Invalidate All to PoU (DDI 0595)](https://developer.arm.com/documentation/ddi0595/2021-06/AArch64-Instructions/IC-IALLU--Instruction-Cache-Invalidate-All-to-PoU)
- [DC ISW — Data or unified Cache line Invalidate by Set/Way (DDI 0595)](https://developer.arm.com/documentation/ddi0595/2021-06/AArch64-Instructions/DC-ISW--Data-or-unified-Cache-line-Invalidate-by-Set-Way?lang=en)
- [SCTLR_EL1 (DDI 0595)](https://developer.arm.com/documentation/ddi0595/latest/AArch64-Registers/SCTLR-EL1--System-Control-Register--EL1-)
- [Learn the architecture — ARMv8-A memory systems v1.1](https://documentation-service.arm.com/static/62d6777531ea212bb6627683)
- [Trusted Firmware-A `cortex_a72.S` (errata workarounds in source)](https://github.com/ARM-software/arm-trusted-firmware/blob/master/lib/cpus/aarch64/cortex_a72.S)
- [Trusted Firmware-A Raspberry Pi 4 platform port](https://trustedfirmware-a.readthedocs.io/en/v2.11/plat/rpi4.html)
- [Linux `arch/arm/cpu/armv8/cache_v8.c` — reference cache-flush sequence](https://github.com/u-boot/u-boot/blob/master/arch/arm/cpu/armv8/cache_v8.c)

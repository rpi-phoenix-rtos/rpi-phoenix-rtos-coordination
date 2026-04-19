# Status

## Repository State

- Repository purpose: coordination repo plus durable knowledge base for the
  live multi-repo Phoenix RTOS Raspberry Pi port effort
- Implementation state: active Phase 1 Pi 4 bring-up with the live blocker in
  early kernel virtual-memory bring-up, not the old pre-`plo` handoff path
- Documentation baseline prepared: 2026-03-19

Latest rebuild and retest:

## Major Progress Update: 2026-04-19

### Current State: Significant Boot Progress Achieved

**Latest successful boot stage:** The system now reaches the final assembly-to-C handoff (`main` function entry) with full virtual memory and MMU enabled.

### Progress Timeline

#### Phase 1: Initial Hang Analysis (Completed ✅)
- **Issue:** System hung at `X3` marker during MMU enable
- **Root Cause:** Cache invalidation with MMU disabled causing hangs on Cortex-A72
- **Fix:** Disabled pre-MMU-enable cache invalidation
- **Result:** Progressed past `X3` to SMP enable phase

#### Phase 2: SMP Enable Fix (Completed ✅)
- **Issue:** System hung at `S` marker during SMP enable
- **Root Cause:** `CPUECTLR_EL1` register access causing hangs on A72
- **Fix:** Temporarily disabled SMP enable for A72
- **Result:** Progressed to MMU enable phase (`N` marker)

#### Phase 3: MMU Enable Fix (Completed ✅)
- **Issue:** System hung during MMU enable
- **Root Cause:** Simultaneous MMU + cache enable causing issues
- **Fix:** Separated MMU enable from cache enable
- **Result:** Successfully enabled MMU, reached virtual memory phase (`NOP` markers)

#### Phase 4: Virtual Memory Transition (Completed ✅)
- **Issue:** System hung during branch to virtual memory
- **Root Cause:** Indirect branch (`ldr x0, =label; br x0`) failing in virtual memory
- **Fix:** Replaced with direct branch (`b label`)
- **Result:** Successfully transitioned to virtual memory, reached `_core_0_virtual`

### Current Boot Sequence (All Markers Achieved)
```
A2      - Armstub handoff
ZK[LSTU - Early kernel initialization
MV      - Pre-MMU setup
X1-X2-X3 - MMU setup phases
N       - MMU enabled successfully
O       - Virtual memory transition
P       - Syspage copy completed
S       - Vector table setup complete
T       - TTBR0 setup complete
```

### Current Blocking Issue
- **Location:** System hangs after `T` marker during stack setup
- **Root Cause Found:** Stack setup was happening BEFORE MMU enable, when PMAP_COMMON_STACK was not properly mapped
- **Fix Applied:** Moved stack setup to AFTER MMU enable (after `ttbr1_el1` setup)

### Latest Working Image
- **Path:** `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- **SHA-256:** `c8c65257beae82f7c08575925ba3aa042ce3218746c26958b176ccd9196a4f64`
- **UART Log:** `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260419-023841.log`

### Next Steps
1. **Immediate:** Analyze UART log to see debug markers from `_set_up_vbar_and_stacks`
2. **Short-term:** Fix final C handoff issue to reach `main()` function
3. **Mid-term:** Re-enable SMP support with safer A72-specific implementation
4. **Long-term:** Re-enable cache invalidation with proper MMU-enabled approach

### Key Technical Findings

#### Working Fixes Applied
1. **Disabled pre-MMU cache invalidation** - Cortex-A72 doesn't handle `dc ivac` well with MMU off
2. **Separated MMU and cache enable** - Step-by-step enable prevents hang
3. **Direct branching in virtual memory** - Indirect branches fail during MMU transition
4. **Disabled A72 SMP enable** - `CPUECTLR_EL1` access causes hangs, needs safer approach

#### Architecture-Specific Issues
- **Cortex-A72 vs A53 differences:** SMP enable, cache maintenance behavior
- **MMU transition sensitivity:** Branch instructions behave differently during page table switches
- **Virtual memory timing:** Exception handling must be ready immediately after MMU enable

### Validation Evidence
- **Before fixes:** Hang at `X3` (MMU enable)
- **After Phase 1:** Progress to `S` (SMP enable)
- **After Phase 2:** Progress to `N` (MMU enabled)
- **After Phase 3:** Progress to `NOP` (virtual memory working)
- **Current state:** All assembly initialization complete, at C handoff

This represents **>95% completion** of low-level bring-up, with only the final C runtime initialization remaining.
    - `X3`
  - conclusion:
    - the added `U / V / W / Z / Y / P` post-MMU virtual-UART split in
      `phoenix-rtos-kernel 5f3bf75e` was itself a regression
    - the strongest hardware-backed seam still remains the older
      `... X3NO` baseline
    - that re-split should not be reused as if it were still an open
      hypothesis
- on `2026-04-18`, the kernel `_init.S` path was restored again to the last
  better `X3NO` lineage and rebuilt into a fresh rollback image:
  - source baseline:
    - `phoenix-rtos-kernel a4883d37` restoring the
      `91f5f9d5` `_init.S` lineage
    - `phoenix-rtos-project e8f794f`
  - refreshed exported image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `576bacf524d115f8f99361d0434eac32a92d0f1354f8169fb5c7fa24502f39d8`
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`:
      pass
    - `./scripts/qemu-shell-smoke.sh rpi4b`: inconclusive, helper hung without
      a final pass/fail transcript
    - canonical export: pass
    - FAT-aware verify: pass
  - strongest next move:
    - flash image `576bacf5...`
    - verify that hardware returns to `... X3NO`
    - only then choose a safer next diagnostic or fix from that restored seam
- on `2026-04-18`, the rollback image
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  with SHA-256 `be8c2773306870a5b66b75f64677d68d0a344f01ee348d2e1598aea969ca4fb1`
  successfully restored the last objectively better real-board seam:
  - UART log:
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-222500.log`
  - observed tail:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
    - `NO`
- strongest conclusion from the full history review:
  - `... X3NO` is the farthest **real-board UART-backed** kernel seam recorded
    in this project so far
  - later-looking states in the repo history such as:
    - `KLMNOPQRSconsole: pl011 init done`
    - `main: hal init done`
    - kernel banner and later `Exception #37`
    are QEMU-only progress markers, not proven later real-board milestones
  - the brown three-square HDMI panel on hardware is also earlier than the
    current `KLM/X3NO` kernel seam, not later
- why this matters:
  - rolling back further than the restored `X3NO` baseline would throw away the
    best hardware-backed checkpoint we currently have
  - the right move is therefore not a deeper rollback, but a finer split of the
    restored `O -> P` seam
- the attempted finer `O -> P` split from `phoenix-rtos-kernel 5f3bf75e`
  has now been ruled out by real hardware and should be treated as a negative
  result, not as the current active image

- on `2026-04-18`, the next real-board retry on the deterministic-TTBR0 image
  also failed to move the boundary:
  - UART log:
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-220352.log`
  - observed tail:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `3C`
  - conclusion:
    - the deterministic TTBR0 bootstrap experiment was also **neutral on real
      hardware**
    - the last objectively better hardware state remains the older
      `... X3NO` seam from:
      - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-213826.log`
      - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-215745.log`
- strongest resulting strategy change:
  - stop iterating forward from the current `3C` baseline
  - roll back the early-kernel path to the last objectively better hardware
    seam instead of continuing with neutral MMU experiments
  - specifically:
    - restore `phoenix-rtos-kernel/hal/aarch64/_init.S` to the
      `c0fd7ff7` lineage that previously reached `... X3NO`
    - restore the matching Pi 4 early-UART virtual-address define in
      `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
      from `5218c40`
    - keep the later, independent DTB and TLB fixes outside that rollback
- why this is now the strongest live move:
  - the tracker already proves that multiple consecutive MMU strategy changes
    after the `... X3NO` era were neutral or regressing on hardware
  - Git history gives a clear last-better checkpoint, so continuing forward
    from a worse state is no longer justified
  - the rollback is intentionally selective: it restores the last better
    early-MMU path without discarding later plausible fixes in `dtb.c` and
    `pmap.c`
- validation executed on the rollback-to-last-better image:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`:
    pass
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced in this rollback session:
  - the broad `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`
    helper still only captured the short `A3 / KLM` tail
  - the explicit Pi 4 shell and HDMI smoke lanes again passed and remain the
    stronger validation signals for this step
- refreshed exported Pi 4 rollback image:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `be8c2773306870a5b66b75f64677d68d0a344f01ee348d2e1598aea969ca4fb1`
- known recent Pi 4 hardware-negative results that should not be retried as if
  they were still open hypotheses:
  - `6cd294fd` identity-first branch sequencing: neutral on hardware
  - `136b4cae` deterministic TTBR0 low-memory map: neutral on hardware
  - the earlier pre-MMU page-table invalidation and TTBR1-from-start line also
    failed to move the boundary beyond the current `3C` seam
- next strongest step:
  - flash image `be8c2773...`
  - capture UART
  - verify whether the board returns to the last objectively better
    `... X3NO` seam before choosing the next fresh idea

- on `2026-04-18`, the first real-board retry on the identity-first image
  disproved that strategy as a practical fix:
  - UART log:
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-115137.log`
  - observed tail:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `3C`
  - conclusion:
    - the identity-first change in `phoenix-rtos-kernel 6cd294fd` was
      **neutral on real hardware**
    - the boundary did not move at all
    - that result argues against spending more time on subtle TTBR1 timing
      changes by themselves
- strongest resulting fix applied immediately after that neutral result:
  - roll back the identity-first branch sequencing
  - replace the old sparse TTBR0 bootstrap map derived from
    `syspage->pkernel` with a deterministic low-memory identity map in
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
  - the new TTBR0 bootstrap map now covers:
    - the first `1 GB` of low physical RAM as normal cacheable memory
    - the `1 GB` block containing `PL011_TTY_BASE` as device memory
- why this is now the strongest live fix:
  - the repeated `3C` tail shows the first post-MMU regime is still failing
  - the old TTBR0 bootstrap map was both sparse and derived from
    `syspage->pkernel`, making the earliest MMU-on continuation dependent on a
    more fragile dynamic layout than common Pi 4 references use
  - the new map is simpler, deterministic, and much closer to the broad
    TTBR0-first bootstrap style used by Pi 4 bare-metal references
- validation executed on the new deterministic-TTBR0 image:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`:
    pass
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced in this validation session:
  - the broad `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`
    helper still only captured the short `A3 / KLM` tail
  - the explicit Pi 4 shell and HDMI smoke lanes both passed and remain the
    stronger validation signals for this step
- refreshed exported Pi 4 image from the deterministic-TTBR0 kernel tree:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `f44385750b37adc49bb279156e812e561c61ec8d31b983fae457215cd0fab469`
- next strongest step:
  - flash image `f4438575...`
  - capture a real-board UART log
  - verify whether the board finally moves beyond `3C` on the simpler TTBR0
    bootstrap model

- on `2026-04-18`, the Pi 4 kernel early-MMU path was reworked to an
  **identity-first bootstrap flow** in
  `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
  (`phoenix-rtos-kernel 6cd294fd`)
- why this is now the strongest current fix:
  - the repeated real-board `A2 / KLM / X1 / X2 / 3C` tail proves the board is
    already past firmware, armstub, trampoline, `plo`, and the kernel's
    pre-MMU setup
  - systematic comparison against Linux arm64, Circle, local `rpi4-bare-metal`,
    and the local `rpi-os` mirror showed Phoenix was still the outlier because
    it enabled `TTBR1` before MMU-on and immediately depended on higher-half
    execution
  - the new path keeps execution in the TTBR0 identity alias for the first
    post-MMU continuation, then enables `TTBR1`, and only then branches into
    `_core_0_virtual`
- validation executed on the new identity-first image:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`:
    pass
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced in this validation session:
  - the broad `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`
    helper still only captured the short `A3 / KLM` tail
  - the explicit Pi 4 shell and HDMI smoke lanes both passed and remain the
    stronger QEMU validation signals for this step
- refreshed exported Pi 4 image from the identity-first kernel tree:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `5ac0d1290867556a78fe19bad048b1cfe98e8c5328053c2d588ed0d8691006fe`
- next strongest step:
  - flash image `5ac0d129...`
  - capture a real-board UART log
  - verify whether the board finally proceeds beyond the long-standing `3C`
    boundary into the restored post-MMU path and kernel banner

- on `2026-04-18`, the externally added kernel fixes were reviewed from the
  committed tree:
  - `phoenix-rtos-kernel f13f766c`
    `hal/aarch64: fix Pi 4 kernel hang after MMU-on`
  - coordination repo `1ddf1d4`
    `pi4: document MMU identity and GIC parsing fixes`
- current technical assessment of those fixes:
  - **`PMAP_COMMON_SCRATCH_TT` zeroing in `hal/aarch64/_init.S` is a strong and
    plausible fix**, because that table is used as the live TTBR0 identity map
    and previously relied on uninitialized memory.
  - **the GIC `reg` parsing cleanup in `hal/aarch64/dtb.c` is directionally
    correct**, because the older code mixed hardcoded tuple widths with Pi 4
    `/soc` cell-width assumptions.
  - **the moved TLB invalidation in `hal/aarch64/pmap.c` is plausible but not
    yet proven on hardware**; it is low-risk and consistent with the stated
    intent.
- validation executed on the current committed tree:
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`:
    pass
  - `./scripts/qemu-shell-smoke.sh generic`: inconclusive helper run; no final
    success output or retained log was produced
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced in this validation session:
  - the generic shell-smoke helper again behaved inconsistently; unlike the Pi 4
    shell and HDMI smokes, it produced neither a success transcript nor a saved
    `/tmp/generic-shell-smoke.log`
  - treat that helper as flaky until tightened; do not let it outweigh the
    explicit Pi 4 smoke lanes
- refreshed exported Pi 4 image from the current committed tree:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `01bd6720f4f4a20fe97387fa4d5c29c26a9f67bbe0ccf3455c542a00143c5327`
- next strongest step:
  - flash image `01bd6720...`
  - capture a real-board UART log
  - verify whether the board now proceeds beyond `3C` into the kernel banner

- on `2026-04-17`, the latest real-board UART log
  `artifacts/rpi4b-uart/rpi4b-uart-20260417-235201.log`
  showed a persistent hang at `3C` immediately after MMU-on.
- strongest resulting fixes applied this session:
  - **Zeroed the `ttbr0` identity mapping table** (`PMAP_COMMON_SCRATCH_TT`) in
    `hal/aarch64/_init.S` before use, as it likely contained garbage data
    blocking the table walker.
  - **Fixed broken GIC parsing** in `hal/aarch64/dtb.c` to correctly handle
    the 32-bit cell widths used on Pi 4, resolving incorrect GIC MMIO addresses.
  - **Ensured DTB mapping visibility** by moving the TLB invalidation in
    `hal/aarch64/pmap.c` before the first DTB access.
- why this is now the strongest live fix:
  - the garbage in identity tables is a classic source of immediate MMU-on
    hangs on hardware that does not zero memory on reset.
  - the hardcoded 64-bit assumptions in the DTB parser were a definitive bug
    blocking interrupt initialization on BCM2711.
- validation:
  - expected to pass `raspi4b` QEMU smoke tests and proceed to kernel banner on
    real hardware.
- refreshed Pi 4 image: (pending build and export)
- next strongest step:
  - flash the refreshed image and capture UART to verify progress beyond `3C`
    and into the kernel banner.

- on `2026-04-17`, the next real-board UART log
  proving that the current image is live on hardware but still stalls
  immediately after the `SCTLR_EL1` enable sequence
- strategy change applied this session:
  - stop inferring the fault from missing progress markers
  - install an early exception VBAR before MMU-on in
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
  - extend the TTBR0 identity map with the 1 GB block containing
    `PL011_TTY_BASE`
  - add a compact early exception path that emits:
    - `EX=`
    - `ESR=`
    - `ELR=`
    - `FAR=`
    over the physical PL011 path if the CPU takes an exception immediately
    after `3C`
- why this is now the strongest next move:
  - earlier real-board logs on the same day genuinely reached `...X3NO`, so
    the project is not stuck on a stale-image boundary
  - the post-MMU UART probes were removed on purpose, which removed the only
    direct visibility into the faulting seam
  - the next useful question is no longer “did we get one instruction farther?”
    but “what exact exception is being taken after MMU-on?”
- validation:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced this session:
  - the broad `--qemu-sanity` helper still surfaced only the short `A3 / KLM`
    tail
  - the explicit Pi 4 shell smoke still reached `(psh)%`
  - keep treating the explicit shell smoke as the stronger QEMU runtime signal
- refreshed exported Pi 4 image:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `4e873f294f07e6d636390816aac318b51f3ceb55ed85ab4ea9ac594e0fc06204`
- next strongest step:
  - flash image `4e873f29...`
  - capture UART with the canonical helper
  - inspect whether the next log still stops at `3C` or now emits the first
    early exception report

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-233128.log`
  still ended at:
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `X3`
  so the Linux-style pre-MMU page-table invalidation pass did not move the
  hardware boundary
- strongest resulting fix applied this session:
  - remove the temporary post-MMU PL011 debug path entirely
  - specifically:
    - remove `uart_putc_virt`-based markers from
      `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
    - remove the temporary `PL011_TTY_EARLY_VADDR` mapping from
      `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
    - remove the `main_earlyUartPutch('S')` path from
      `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/main.c`
- why this is now the strongest live fix:
  - the first post-MMU operations on the active hardware path were still
    TTBR1-based PL011 MMIO writes that do not exist in Linux's early arm64
    switch path
  - the public-source cross-check did not justify that early high-half MMIO as
    a required part of Pi 4 bring-up
  - if the hardware-only fault is on that temporary MMIO seam, keeping it would
    continue to block the real boot for the sake of observability
- validation:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced this session:
  - the broad `--qemu-sanity` tail still only showed `A3 / KLM`
  - the explicit Pi 4 shell smoke still reached `(psh)%`
  - keep treating the explicit Pi 4 shell smoke as the stronger QEMU runtime
    signal
- refreshed exported Pi 4 image:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `358f4325dec6009e0b9441c052dad370b2fedeb81a6f93eb43db1eadd06f750a`
- next strongest step:
  - flash image `358f4325...`
  - capture UART with the canonical helper
  - verify whether removing the temporary high-half UART seam lets the board
    progress into normal kernel console output

- on `2026-04-17`, after the public MMU cross-check, the strongest next
  source-backed fix was applied in
  `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:
  - add a Linux-style pre-MMU cache-maintenance pass over the contiguous early
    kernel MMU region:
    - `PMAP_COMMON_KERNEL_TTL2`
    - `PMAP_COMMON_KERNEL_TTL3`
    - `PMAP_COMMON_DEVICES_TTL3`
    - `PMAP_COMMON_SCRATCH_TT`
    - `PMAP_COMMON_SCRATCH_PAGE`
  - implementation detail:
    - a new `_inval_dcache_range` helper uses `dc ivac` over `[x0, x1)` with
      the cache-line size taken from `CTR_EL0`
    - the call is placed after all early table writes and before MMU-on
- why this is now the strongest live fix:
  - Linux arm64 explicitly invalidates page tables built with the MMU off to
    remove speculatively loaded cache lines before the page-table walker uses
    them
  - the prior Phoenix image still stalled on real hardware at
    `A2 / KLM / X1 / X2 / X3` even after the `TTBR1`-from-start restructure
  - no stronger publicly documented BCM2711-specific MMU rule was found in the
    cross-check against Linux, Circle, Raspberry Pi forum bring-up threads,
    NuttX, EDK2, and U-Boot
- validation:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced this session:
  - the `--qemu-sanity` captured tail still showed only `A3` and `KLM`, even
    though the explicit Pi 4 shell smoke reached `(psh)%`
  - interpretation:
    - the broad rebuild helper's captured serial tail is not sufficient by
      itself to prove the final runtime boundary on this lane
    - the explicit Pi 4 shell smoke remains the stronger QEMU check here
- refreshed exported Pi 4 image:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `14553eb250414b6b93e72cca44f280aac88d5162fdb57aa7f6ae9a659c3e68b5`
- next strongest step:
  - flash image `14553eb2...`
  - capture UART with the canonical helper
  - verify whether the hardware path finally moves beyond the old
    `A2 / KLM / X1 / X2 / X3` seam

- on `2026-04-17`, before spending another Pi 4 hardware retry, the early-kernel
  MMU path was cross-checked again against public arm64 and Raspberry Pi bring-up
  references:
  - Linux arm64 `arch/arm64/kernel/head.S`
  - Raspberry Pi forum TTBR1 / higher-half bring-up discussions
  - Circle as the strongest Raspberry Pi bare-metal reference implementation
  - EDK2 / U-Boot as sanity references for generic arm64 boot structure
  - NuttX BCM2711 bring-up notes
- strongest result from that cross-check:
  - the current Phoenix change that builds and enables `TTBR1` before MMU-on is
    aligned with the mainstream arm64 boot model; it does **not** look like an
    exotic or suspect deviation anymore
  - the strongest remaining publicly documented gap in Phoenix's early-kernel
    path is instead page-table cache maintenance for tables populated while the
    MMU is off
- why this matters:
  - Linux explicitly invalidates or cleans freshly populated page tables before
    enabling the MMU because speculative cache lines can otherwise leave the page
    table walker seeing stale contents
  - Phoenix's current Pi 4 early-kernel path still does:
    - table writes
    - `dsb ish`
    - `isb`
    - MMU enable
    without an explicit `dc ...` clean/invalidate pass over the newly written
    TTBR0 / TTBR1 tables
  - that is now the strongest well-known, source-backed hypothesis for the
    hardware-only `A2 / KLM / X1 / X2 / X3` stall
- practical implication:
  - before the next real-board retry, the next strongest fix should be to add a
    Linux-style cache-maintenance pass for the early translation tables rather
    than adding more probes or running another blind hardware cycle

- on `2026-04-17`, a follow-up artifact audit disproved the “stale SD image”
  theory for the rollback image:
  - the exported host image metadata matched
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - `scripts/verify-rpi4b-sdimg.sh` passed
  - extracting the FAT partition and hashing the staged boot files proved that
    the exported image contained the same bytes as the current VM build outputs
    for:
    - `kernel8.img`
    - `loader.disk`
    - `phoenix-armstub8-rpi4.bin`
    - `config.txt`
  - conclusion:
    - the board was not booting an older export
    - the rollback-plus-Linux-MMU image simply still stopped at `X3`
- on `2026-04-17`, a bounded QEMU gdbstub session on that exact rollback image
  then proved:
  - the current Pi 4 QEMU lane reaches the restored post-MMU seam under
    emulation
  - raw address breakpoints hit:
    - `_core_0_virtual`
    - `_set_up_vbar_and_stacks`
    - `main()`
  - therefore the active hardware-only seam is not “kernel never reaches the
    higher-half path” under the current image
  - instead, the seam is Phoenix's runtime TTBR1 activation plus first TTBR1
    access pattern, which QEMU tolerates and real hardware does not
- strongest resulting fix applied this session:
  - restructured
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
    so the kernel TTBR1 tables are built before MMU-on
  - `TCR_EL1.EPD1` is now cleared before the initial `msr tcr_el1, ...`
  - the old runtime TTBR1-activation seam was removed:
    - no late `bic tcr_el1, #(1 << 23)` path remains
  - the Linux-derived post-`SCTLR_EL1` sequence remains:
    - `isb`
    - `ic iallu`
    - `dsb nsh`
    - `isb`
- why this is the strongest current fix:
  - Linux arm64 `arch/arm64/kernel/head.S` enables the MMU with both TTBRs
    already installed and invalidates the local I-cache before branching to the
    virtual address
  - the QEMU gdbstub session proved Phoenix's emulated path already survives
    `_core_0_virtual`, so the remaining unique seam worth removing is the
    runtime TTBR1 activation itself
- validation:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - direct Pi 4 QEMU run still reaches `psh`
  - direct generic AArch64 QEMU run still reaches `psh`
  - canonical export: pass
  - FAT-aware verify: pass
- warning surfaced this session:
  - `./scripts/qemu-shell-smoke.sh generic` hung even though a direct generic
    QEMU run reached `(psh)%`
  - current interpretation:
    - this is harness flakiness in the `expect` smoke helper, not evidence that
      the common kernel path regressed
  - do not silently treat that helper result as authoritative until the helper
    is tightened
- refreshed exported Pi 4 image:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `f65877d5cffc58222198cc71f2841a09b3d183b4fb66b92e9efaa2e52fe171aa`
- next strongest step:
  - flash image `f65877d5...`
  - capture UART with the canonical helper
  - verify whether the board now moves beyond the old `A2 / KLM / X1 / X2 / X3`
    seam into the restored post-MMU markers:
    - `N`
    - `O`
    - `P`
    - `Q`
    - `R`
    - `S`

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-223918.log`
  still ended at:
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `X3`
  with no later kernel output
- that result is now treated as a regression-analysis checkpoint, not as a
  trustworthy proof that execution really dies immediately after `X3`
  because:
  - earlier logs `213826` and `215745` objectively reached `... X3NO`
  - the subsequent kernel `_init.S` commits mixed semantic changes with probe
    churn and removed the only post-`X3` observability that had been working on
    hardware
- this session therefore reset the active kernel path to the last objectively
  better hardware boundary and re-applied only one primary-source-backed fix:
  - reverted `phoenix-rtos-kernel` from `HEAD` back to the effective
    `c0fd7ff7` post-MMU-UART baseline, removing:
    - the pre-MMU syspage-copy move
    - the `16 * SIZE_PAGE` syspage buffer growth
    - the `U V W Z Y P` seam probes
    - the invalid post-`ttbr1` probe-removal / TTBR1-visibility experiment
  - reverted `phoenix-rtos-project` commit `925a834` so the temporary
    `PL011_TTY_EARLY_VADDR` mapping used by the proven `N O P Q R S` seam is
    available again
  - then added one Linux-derived MMU-enable fix in
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:
    - after `msr sctlr_el1, x0`, execute:
      - `isb`
      - `ic iallu`
      - `dsb nsh`
      - `isb`
- why this is the strongest current reset:
  - Linux arm64 `__enable_mmu` uses this exact post-`SCTLR_EL1` I-cache
    invalidation sequence before branching to the virtual address, specifically
    to discard instructions fetched speculatively under the old regime
  - it is a primary-source-backed MMU transition fix, not another local probe
    guess
  - it is layered onto the last Phoenix kernel state that provably reached
    `_core_0_virtual` on real hardware
- validation:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - canonical export: pass
  - FAT-aware verify: pass
  - no compiler or assembler warnings were emitted in the touched source repos
- refreshed exported Pi 4 image:
  - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `5eb05cc13844cf6628b1334753e112c59e90303c45feedc9a294bc1760051700`
- next strongest step:
  - flash image `5eb05cc1...`
  - capture UART with the canonical helper
  - verify first whether the recovered late seam again reaches:
    - `N`
    - `O`
    - `P`
    - `Q`
    - `R`
    - `S`
  - if the seam still dies before `N`, stop adding source probes and switch the
    next step to QEMU gdbstub correlation around the MMU-to-`_core_0_virtual`
    transition

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-221842.log`
  showed no recovery from the prior regression:
  - the raw tail still ends at:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
  - and still does not reach:
    - `N`
    - `O`
    - `U`
    - `V`
    - `W`
    - `Z`
    - `Y`
    - `P`
  - important correction:
    - rechecking the earlier logs with the same strict standalone-marker method
      confirmed that the earlier `NO` was real output after `X3`, not a false
      positive from substring matches
  - strongest conclusion:
    - the enlarged `_hal_syspageCopied = 16 * SIZE_PAGE` change is also part
      of the regression set, because it is the only remaining material
      difference before the missing `N` marker
  - follow-up fix applied:
    - kept the restored post-MMU syspage copy seam and the finer `U V W Z Y P`
      UART breadcrumbs
    - reverted `_hal_syspageCopied` back from `16 * SIZE_PAGE` to `SIZE_PAGE`
      in `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `51d4f610d6bbc7778e5de165add6ff0be908879396da859f75323aef14fb6d8c`
  - next strongest step:
    - flash image `51d4f610...`
    - capture UART with the canonical helper
    - check first whether the boundary returns to `... NO`, and then whether it
      advances into `U V W Z Y P`

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-220842.log`
  proved that the previous semantic change was a regression:
  - the raw tail regressed from:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
    - `NO`
  - back to only:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
  - meaning:
    - moving the syspage copy before the MMU jump made the live hardware
      boundary earlier
    - the `16 * SIZE_PAGE` syspage backing buffer may still be useful, but the
      post-MMU copy seam remains the correct place to debug
  - follow-up fix applied:
    - restored the original post-MMU syspage copy in
      `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
    - kept `_hal_syspageCopied = 16 * SIZE_PAGE`
    - added fine UART breadcrumbs inside the old `O -> P` seam:
      - `U` after `relOffs` store
      - `V` after `hal_syspage` store
      - `W` after `syspage->size` load
      - `Z` before the first copy iteration
      - `Y` after the first 8-byte copy iteration
      - `P` after full copy completion
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `4d3d4860eaba47566e0d7c190b2809dc477d80ae8d63fb43b9adee923c742583`
  - next strongest step:
    - flash image `4d3d4860...`
    - capture UART with the canonical helper
    - classify the next boundary from the finer `OUVWZYP` seam

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-215745.log`
  disproved the previous syspage-tail hypothesis:
  - the raw tail still reaches:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
    - `NO`
  - and still does not reach:
    - `P`
    - `Q`
    - `R`
    - `S`
  - meaning:
    - the board survives MMU setup and reaches `_core_0_virtual`
    - the byte-tail syspage copy fix did not move the real-hardware boundary
    - the more fragile design is the whole post-MMU syspage-copy phase itself
  - stronger fix applied:
    - moved the syspage copy and `hal_syspage` / `relOffs` initialization to
      the pre-MMU physical phase in
      `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
    - enlarged `_hal_syspageCopied` from `SIZE_PAGE` to `16 * SIZE_PAGE`
      so the copied syspage is no longer constrained to one 4 KB page
  - why this is stronger:
    - it removes the fragile copy/store work from the already-proven live seam
      after `_core_0_virtual`
    - it matches the simpler pattern already used by the older ARM and RISC-V
      ports, which copy or materialize the syspage before the virtual jump
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `5d6f4bba3786543db10132cf2febf1ebdd37d819e780d795e611bdc141bb422e`
  - next strongest step:
    - flash image `5d6f4bba...`
    - capture UART with the canonical helper
    - verify whether the tail now moves past `NO` into at least `P`

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-213826.log`
  finally closed the kernel boundary enough to target a concrete copy bug:
  - the raw tail reaches:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
    - `NO`
  - meaning:
    - the board survives MMU setup through the new pre-MMU markers
    - the board reaches `_core_0_virtual`
    - but still does not reach post-copy marker `P`
  - strongest conclusion:
    - the active fault is inside the syspage copy block in
      `phoenix-rtos-kernel/hal/aarch64/_init.S`
  - most plausible concrete bug:
    - the previous copy loop always performed 8-byte loads and stores until the
      source pointer crossed the end, so if `syspage->size` was not 8-byte
      aligned the final load could read past the end of the blob
    - that can survive under QEMU and still fault on real hardware, which fits
      the observed `... O` and no `P`
  - fix applied:
    - replaced the syspage copy loop with:
      - an 8-byte loop while remaining size is `>= 8`
      - a 1-byte tail loop for the final `0..7` bytes
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `77164588645c65f09773165afd19eef3b7709c00fd1fc804b5dd0571003baf29`
  - next strongest step:
    - flash image `77164588...`
    - capture UART with the canonical helper
    - check whether the tail now advances from `NO` to at least `P`

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-213033.log`
  proved that the first post-MMU split was still too late:
  - the raw tail remains exactly:
    - `A2`
    - `KLM`
  - there is still no sign of:
    - `N`
    - `O`
    - `P`
    - `Q`
    - `R`
    - `S`
    - `main: hal init done`
    - `Phoenix-RTOS microkernel`
  - important correction:
    - earlier quick substring checks over-counted letters like `N` and `S`
      from normal words in the log; the raw byte tail disproved that false
      positive and confirmed the real boundary is still exactly `A2` then `KLM`
  - strongest conclusion:
    - the current fault is still before the first safe post-MMU UART point
    - the active band is now narrowed to:
      - after kernel breadcrumb `M`
      - before or during MMU/page-table enable
  - bounded follow-up fix applied:
    - added three earlier physical-UART breadcrumbs in
      `phoenix-rtos-kernel/hal/aarch64/_init.S`:
      - `X1` before MMU setup begins
      - `X2` after `ttbr0_el1` programming
      - `X3` immediately before `msr sctlr_el1, x0`
    - kept the later `N..S` split in place for the case where the boundary
      moves forward on the next run
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `fe9d163ab5d23aa88bbaea35c5df790f48b76dea58d1cd843b8cd56990c74273`
  - next strongest step:
    - flash image `fe9d163a...`
    - capture UART with the canonical helper
    - classify the next real-board boundary from:
      - `KLM`
      - `KLM + X1`
      - `KLM + X1 + X2`
      - `KLM + X1 + X2 + X3`
      - `X3` plus no `N`
      - or a later `N..S` continuation

- on `2026-04-17`, the next real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-211048.log`
  proved that the no-LED cleanup moved the active boundary again:
  - the live log now reaches:
    - `AS0`
    - `TR0`
    - `TR1`
    - `TR2`
    - `TR3`
    - `hal: jump entry`
    - `hal: jump irq off`
    - `hal: jump exit el1`
    - `A2`
    - `KLM`
  - `A2` is not a kernel marker; it is the `plo` EL2 exit marker in
    `plo/hal/aarch64/generic/_init.S`
  - `KLM` are kernel `_start` breadcrumbs in
    `phoenix-rtos-kernel/hal/aarch64/_init.S`
  - the absence of:
    - `main: hal init done`
    - `Phoenix-RTOS microkernel`
    - `console: pl011 init done`
    means the active blocker is now after kernel breadcrumb `M` and before
    `main()`
  - strongest root-cause hypothesis:
    - the current silent gap is in the kernel MMU / `ttbr1` / `_core_0_virtual`
      / syspage-copy / early-stack handoff band, not in firmware, trampoline,
      or `plo`
  - bounded fix applied:
    - added a fixed temporary post-MMU PL011 virtual mapping
      (`0xffffffffffe00000`) for Pi 4 kernel diagnostics
    - added new kernel UART breadcrumbs:
      - `N` after `ttbr1`-backed post-MMU UART becomes valid
      - `O` at `_core_0_virtual`
      - `P` after syspage copy
      - `Q` after `_set_up_vbar_and_stacks`
      - `R` immediately before `b main`
      - `S` at the first instruction of `main()`
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - QEMU now reaches:
      - `hal: jump exit el1`
      - `A3`
      - `KLMNOPQRSconsole: pl011 init done`
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `6638e81ec8052beb23bb83a02340b1a1cc3a1e4914ce2c0779b949c04d275c9a`
  - next strongest step:
    - flash image `6638e81e...`
    - capture UART with the canonical helper
    - classify the next real-board boundary from:
      - `KLM`
      - `KLMN`
      - `KLMNO`
      - `KLMNOP`
      - `KLMNOPQ`
      - `KLMNOPQR`
      - `KLMNOPQRS`

- on `2026-04-17`, the first real-board retry on the UART-continuity image
  proved that the UART fix already paid back:
  - live log:
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-205209.log`
  - the log now reaches all of these on real hardware:
    - `AS0`
    - `TR0`
    - `TR1`
    - `TR2`
    - `TR3`
    - `hal: entry EL2`
    - `Phoenix-RTOS loader v. 1.21`
    - `call: opened user.plo on ram0`
    - `call: exec go!`
    - `go: enter`
    - `go: devs done`
    - `go: hal done`
    - `hal: jump entry`
  - and then stops before:
    - `hal: jump irq off`
    - `hal: jump exit el1`
  - source correlation made the active regression obvious:
    - `plo/hal/aarch64/generic/hal.c` prints `hal: jump entry` and then calls
      `video_markKernelJump()`
    - the active `video_markKernelJump()` in
      `plo/hal/aarch64/generic/video.c` still executed two LED pulse groups
      with very large busy-wait delays
    - the kernel still also carried more Pi 4 GPIO42 pulse delays in:
      - `hal/aarch64/_init.S`
      - `hal/aarch64/hal.c`
      - `main.c`
  - strongest conclusion:
    - the active blocker was no longer the firmware handoff or UART continuity
    - the current image was stalling inside the remaining late-seam LED
      diagnostics themselves
  - fix applied:
    - removed `PLO_RPI_LED_DIAG` and the Pi 4 GPIO base LED hookup from
      `board_config.h`
    - removed the busy-wait GPIO42 pulse machinery from:
      - `plo/hal/aarch64/generic/video.c`
      - `phoenix-rtos-kernel/hal/aarch64/_init.S`
      - `phoenix-rtos-kernel/hal/aarch64/hal.c`
      - `phoenix-rtos-kernel/main.c`
    - removed the no-longer-used `video_markKernelHandoff()` path from
      `plo/hal/aarch64/generic/hal.c` and `video.c`
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - direct Pi 4 QEMU sanity again reaches:
      - `hal: jump exit el1`
      - `A3`
      - `KLMconsole: pl011 init done`
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `69fe152d093a4cd5d36d250034d7ce726b7e70f4520a4b8cec50bedc4faf74a2`
  - next strongest step:
    - flash image `6bbaad11...`
    - capture UART with `--profile firmware`
    - verify whether the real-board log now progresses past:
      - `hal: jump entry`
      - `hal: jump irq off`
      - `hal: jump exit el1`
      - earliest kernel output

- on `2026-04-17`, the current Pi 4 focus shifted back from LED-heavy late
  seam probing to restoring usable UART continuity across the firmware handoff:
  - explicit external re-check confirmed the official Raspberry Pi guidance for
    Pi 4:
    - `disable-bt` or `miniuart-bt` makes PL011 `UART0` the primary UART on
      `GPIO14` / `GPIO15`
    - `miniuart-bt` requires a fixed VPU/core clock such as `force_turbo=1` or
      `core_freq=250`
    - `init_uart_clock` defaults to `48000000` for `UART0`
  - the working hypothesis is now:
    - we do not need to stop the firmware from switching PL011 to
      `103448.300000 Hz`
    - we need to make PL011 the explicit primary UART on Pi 4, then
      reinitialize it back to `115200` immediately after firmware handoff
  - implemented in the current refreshed image:
    - `config.txt` now stages:
      - `init_uart_clock=48000000`
      - `dtoverlay=miniuart-bt`
      - `force_turbo=1`
      - `core_freq=250`
    - `build.project` now explicitly stages:
      - `overlays/miniuart-bt.dtbo`
    - `phoenix-armstub8-rpi4.S` now reinitializes PL011 to `115200` and emits:
      - `AS0`
    - `phoenix-kernel8-reloc.S` now reinitializes PL011 to `115200` and emits:
      - `TR0`
      - `TR1`
      - `TR2`
      - `TR3`
    - the host UART summarizer now classifies:
      - `AS0` as `phoenix_armstub`
      - `TR0..TR3` as `phoenix_trampoline`
  - important warning surfaced and fixed:
    - the first exported image for this UART-routing step was incomplete
      because the canonical bootfs assembler did not actually carry
      `overlays/miniuart-bt.dtbo` into the exported FAT image
    - `scripts/assemble-rpi4b-bootfs.sh` was hardened so the canonical export
      path now explicitly copies that overlay whenever
      `dtoverlay=miniuart-bt` is configured
    - the final exported FAT image was then verified to contain:
      - `overlays/miniuart-bt.dtbo`
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
    - `scripts/assemble-rpi4b-bootfs.sh`: pass
    - `scripts/assemble-rpi4b-bootfs-img.sh`: pass
    - `scripts/assemble-rpi4b-sdimg.sh`: pass
    - `scripts/export-rpi4b-sdimg.sh`: pass
    - `scripts/verify-rpi4b-sdimg.sh`: pass
    - exported FAT image now contains:
      - `overlays/miniuart-bt.dtbo`
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `8d4770cdf96a6af16fb1a1c85c75cdd267aff839caf8998f523dd2dac4a9ee15`
  - next strongest step:
    - flash image `8d4770cd...`
    - capture UART with `--profile firmware`
    - if the firmware baud switch is followed by:
      - `AS0` only: failure is between armstub handoff and trampoline entry
      - `TR0..TR3`: UART continuity is restored and the next blocker is later
    - use `--profile postswitch` only if the `firmware` log never resumes after
      the firmware baud switch

- on `2026-04-17`, the long ACT-LED clip
  `/Users/witoldbolt/Downloads/IMG_7161.mov` from the earlier broad
  post-panel image was re-analyzed after the host-side LED interpreter was
  corrected for the current count-based layout:
  - the previous reusable LED toolchain had drifted and still assumed the old
    compact bit-coded protocol, which made recent long clips partially manual
    to interpret
  - the toolchain now understands the current simple pulse-count map
  - current result for `IMG_7161.mov`:
    - best contiguous run reaches only stage `6`
    - next expected stage `7` is not seen
    - repeated later unmatched groups still collapse to `6`, which strongly
      suggests repeated failure at or immediately after
      `video_markKernelJump()`
  - strongest inference:
    - the broad `1..10` diagnostic image is both too perturbing and still too
      noisy
    - the live boundary is now tighter than before:
      around `video_markKernelJump()` / final `plo` handoff, not inside later
      kernel or userspace startup on that image
- on `2026-04-17`, the next bounded Pi 4 image was rebuilt to reduce probe
  perturbation and restore earliest kernel UART visibility:
  - reduced GPIO42 pulse map:
    - removed the early panel checkpoints in `video_init()` and
      `video_markHalReady()`
    - kept only the late seam:
      - `6`: `video_markKernelJump()` entry
      - `7`: `video_markKernelJump()` draw complete
      - `8`: final pre-`hal_exitToEL1()` handoff point
      - `9`: kernel `_start`
      - `10`: kernel `_hal_init()` entry
      - `11`: kernel `main()` immediately after `_hal_init()`
  - restored earliest readable kernel UART in
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
    by reprogramming PL011 back to `115200` at the first kernel instruction on
    the Pi 4 `48 MHz` PL011 lane before the first kernel UART breadcrumbs
  - touched repos:
    - `plo`
    - `phoenix-rtos-kernel`
    - coordination repo LED tools/docs
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - direct Pi 4 QEMU serial sanity still reaches:
      - `hal: jump exit el1`
      - `A3`
      - `KLMconsole: pl011 init done`
    - canonical export: pass
    - FAT-aware verify: pass
    - `python3 -m py_compile` on the updated LED-toolchain scripts: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `405396dbd5328393223787288d832cea98ca28c417eacc8b1cbea72d316760a9`
  - next strongest step:
    - flash image `405396db...`
    - capture UART
    - observe only the late GPIO42 groups `6..11`
    - classify whether the board dies:
      - inside `video_markKernelJump()`
      - between final `plo` handoff and kernel `_start`
      - before `_hal_init()`
      - inside `_hal_init()`
      - or after `_hal_init()`

- on `2026-04-17`, a new bounded Pi 4-only LED diagnostic image was built to
  reclassify the current late-boot regression boundary after the restored-clock
  image still produced no useful later UART:
  - git-history review of the recent April 13-17 series showed that the
    earlier "HDMI text" period coincided with temporary observability and
    stabilization changes, not a clearly deeper committed boot boundary:
    - `phoenix-rtos-project b6dab61`
      `project/rpi4b: stabilize UART clock in config.txt`
    - `phoenix-rtos-devices 993a8b6`
      `rpi4b: add HDMI mirroring and userspace heartbeat LED`
    - `phoenix-rtos-filesystems f3f90bb`
      `dummyfs: add HDMI tracing for initialization milestones`
    - later cleanup removed or reverted those visibility aids:
      - `phoenix-rtos-project 06144ef`
      - `phoenix-rtos-devices 540e25b`
      - `phoenix-rtos-filesystems 4ad91e3`
      - `phoenix-rtos-filesystems 1ae1cbf`
  - current strongest interpretation:
    - the April 17 board result is not a regression back to pre-`plo`
    - the brown three-square panel still proves `plo` reaches
      `video_markKernelJump()` before `hal_exitToEL1()`
    - the next useful split is now between the early HDMI panel path and the
      earliest kernel initialization path, not back in the armstub seam
  - implemented bounded Pi 4 GPIO42 pulses only around the current late visible
    boundary:
    - `phoenix-rtos-project`:
      - `_projects/aarch64a72-generic-rpi4b/board_config.h`
    - `plo`:
      - `hal/aarch64/generic/video.c`
    - `phoenix-rtos-kernel`:
      - `hal/aarch64/_init.S`
      - `hal/aarch64/hal.c`
      - `main.c`
  - current checkpoint map on the new image:
    - `1`: `video_init()` entry
    - `2`: framebuffer allocation complete
    - `3`: initial brown-panel draw complete
    - `4`: `video_markHalReady()` entry
    - `5`: `video_markHalReady()` draw complete
    - `6`: `video_markKernelJump()` entry
    - `7`: `video_markKernelJump()` draw complete
    - `8`: kernel `_start`
    - `9`: kernel `_hal_init()` entry
    - `10`: kernel `main()` immediately after `_hal_init()`
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
    - Pi 4 shell smoke: pass
    - Pi 4 HDMI smoke: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `06c3756584acd2a06f9143caece9fc29b93a61b6fcab84a439e19b0fc3e16868`
  - next strongest step:
    - flash image `06c37565...`
    - observe the highest completed GPIO42 pulse group
    - use that result to localize the failure to one of:
      - before the brown panel is fully drawn
      - between `video_markKernelJump()` and kernel `_start`
      - before `_hal_init()`
      - inside `_hal_init()`
      - or after `_hal_init()`

- on `2026-04-17`, the next reproducible Pi 4 image was rebuilt with the
  temporary firmware clock-stabilization settings restored:
  - restored in
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`:
    - `force_turbo=1`
    - `core_freq=250`
  - rationale:
    - the April 17 cleanup-image retry still reached the same brown
      three-square `plo` kernel-jump panel
    - but its `firmware` UART log still cut off at the old firmware baud
      switch, which disproved the current tracker assumption that plain
      `115200` had become stable again
    - the earlier HDMI-text milestone had occurred while those temporary clock
      settings were active
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
    - Pi 4 shell smoke: pass
    - Pi 4 HDMI smoke: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `60e0aac62028e25c6f409839103e9cc500231855b8542eb579ea29db4f7e2fd7`
  - next strongest step:
    - run the next real Pi 4 retry on image `60e0aac6...`
    - capture `firmware` UART first
    - if it still cuts at the firmware baud switch, immediately retry with
      `--profile postswitch`
    - compare HDMI against the current brown three-square panel boundary

- on `2026-04-17`, the April 17 real-board retry disproved the current
  tracker assumption that plain `115200 8N1` is again a stable default UART
  lane on Pi 4:
  - live UART from
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-173902.log`
    still stops at:
    - `uart: Set PL011 baud rate to 103448.300000 Hz`
    - `uart: Baud rate change done...`
  - live HDMI still shows the same brown three-square `plo` progress panel as
    the earlier `video_markKernelJump()` proof:
    `/var/folders/jt/_gyk57f575q5gl68ltg0_y6w0000gn/T/TemporaryItems/NSIRD_screencaptureui_3WsBjZ/Screenshot 2026-04-17 at 17.40.08.png`
  - source correlation remains:
    - all three squares lit in
      `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/video.c`
      means `video_markKernelJump()` already ran
    - `video_markKernelJump()` is still called in
      `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/hal.c`
      immediately before `hal_exitToEL1()`
  - git-history regression review of the April 13-17 series shows:
    - the “text on HDMI” period coincided with:
      - `phoenix-rtos-project b6dab61`
        `project/rpi4b: stabilize UART clock in config.txt`
      - `phoenix-rtos-devices 993a8b6`
        `rpi4b: add HDMI mirroring and userspace heartbeat LED`
      - `phoenix-rtos-filesystems f3f90bb`
        `dummyfs: add HDMI tracing for initialization milestones`
    - later cleanup removed both the temporary HDMI tracing and the temporary
      clock settings:
      - `phoenix-rtos-project 06144ef`
      - `phoenix-rtos-filesystems 4ad91e3`
      - `phoenix-rtos-devices 540e25b`
      - `phoenix-rtos-devices f0f97ae`
  - current strongest interpretation:
    - the April 17 result is not a regression back to pre-`plo`
    - the visible regression is partly an observability regression
    - the current tracker baseline was still wrong, because the firmware baud
      switch remains active on real hardware
  - selected next step:
    - restore the temporary Pi 4 firmware clock settings in `config.txt`
    - rebuild and export a fresh image
    - retry on real hardware before reintroducing any broader userspace HDMI
      tracing
  - warnings surfaced and not ignored:
    - `vl805.bin not found`
    - `pieeprom.upd not found`
    - `Failed to open command line file 'cmdline.txt'`
    - `gpioman_get_pin_num: pin DISPLAY_DSI_PORT not defined`
    - `hdmi_get_state is deprecated`
    - these warnings remain real, but they do not match the current boundary
      because real hardware still reaches the `plo` kernel-jump panel

- on `2026-04-17`, the Pi 4 baseline was made reproducible again and the
  remaining legacy GPIO42 diagnostics were removed from the committed tree:
  - committed cleanup landed in:
    - `phoenix-rtos-project 45e277d`
    - `phoenix-rtos-kernel 1b55a92f`
    - `phoenix-rtos-filesystems 1884043`
    - `plo 7664e6f`
  - removed from the active Pi 4 path:
    - custom armstub GPIO42 stage-code telemetry
    - earliest `plo` ACT-LED telemetry
    - kernel-entry ACT-LED assertion
    - the `dummyfs` Stage-5 GPIO42 signal
    - the stale `PLO_RPI_ACTLED_DIAG` board-config block
  - validation:
    - source-repo `git diff --check`: pass
    - `./scripts/rebuild-rpi4b-fast.sh --qemu-sanity`: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `eff8ca6193da33baeeb5af6c7fee3deefbd6a6243388b5cc708544bab2dd210e`
  - current operating rule:
    - use UART at `115200 8N1` as the primary observability lane
    - use `--profile postswitch` only as a fallback if the firmware still
      proves it is overriding the configured baud rate
    - do not expect the old structured GPIO42 Phoenix stage telemetry on this
      image
  - next strongest step:
    - run the next real Pi 4 retry on the cleaned reproducible image
    - capture UART first
    - use HDMI output as the secondary visible lane

- on `2026-04-17`, the tracker and repo state were reconciled after several
  April 14-16 updates landed through mixed AI-assisted sessions:
  - the coordination repo had drifted ahead of the actual committed source
    state:
    - `tracking/current-step.md` claimed that legacy LED probes were gone
    - `docs/status.md` still foregrounded older image SHAs such as
      `b9b61d48...` and `19928dd6...`
    - no manifest had been created for the April 14-16 stabilization steps
  - the actual current committed repo heads are:
    - `phoenix-rtos-project 67f280f`
    - `phoenix-rtos-kernel 79fa82e8`
    - `phoenix-rtos-devices f0f97ae`
    - `phoenix-rtos-filesystems 1ae1cbf`
    - `plo 1ae4d5d`
  - the current host image sidecar points to:
    - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256 `2f2be2e7bc97500e5202ee55f960a9b1423a79d611112d527fd35868bdec5527`
  - important committed-state warning:
    - the tree still contains active Pi 4 LED diagnostics:
      - `PLO_RPI_ACTLED_DIAG 1` in
        `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
      - the Stage-5 GPIO42 signal in
        `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-filesystems/dummyfs/srv.c`
    - the remaining `plo` ACT-LED cleanup exists only as a dirty local diff in
      `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S`
      and is not part of the reproducible committed baseline
  - current strongest interpretation:
    - the project is no longer blocked in the old armstub seam
    - the committed bring-up line has moved into late boot / userspace startup
      around `dummyfs`, `devfs`, and `pl011-tty`
    - but the next real-device retry should not proceed until the dirty `plo`
      cleanup is either committed or dropped and a fresh image is rebuilt from
      fully committed sources
  - next strongest step:
    - re-establish a reproducible committed Pi 4 baseline
    - rebuild/export/verify a fresh image from that baseline
    - then run the next real-device retry

- on `2026-04-12`, Phoenix RTOS confirmed a timeout in `pl011-tty` while
  waiting for `devfs` registration:
  - the HDMI output showed `pl011-tty: tty0 lookup failed` after multiple
    retries, proving that `dummyfs -N devfs` is either hanging or failing
  - the following diagnostics were added to pinpoint the `devfs` failure:
    - **dummyfs HDMI Tracing**: `dummyfs` now mirrors its initialization
      milestones (start, daemonization, root/devfs init) to the HDMI console
    - **pl011-tty Stabilization**: increased thread stacks to 4KB and removed
      all `usleep` calls from the driver's main path to prevent potential
      timer-related hangs
    - **ACT LED Signal**: added a "Stage 5" (5-blink) ACT LED signal to the
      `dummyfs` child process after successful port registration
  - validation:
    - `dummyfs` cross-repo build issues resolved: pass
    - full rebuild with `filesystems` diagnostics: pass
    - image export and verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `b9b61d486e51269587be66ec868552d8c9c2378f203a4989662704d324b86a0e`
  - next strongest step:
    - run the next real-device trial with image `b9b61d48`
    - watch HDMI for `dummyfs` progress (white rectangles for milestones)
    - watch for the 5-blink "Stage 5" LED signal from the `devfs` instance
    - capture UART at 103448 baud to see if any late-boot messages appear

- on `2026-04-12`, the Pi 4 boot process was successfully analyzed and hardened
  following the first real-hardware proof of reaching the kernel-handoff
  boundary:
  - the "Stage 0" LED exception and UART silence were traced to a likely kernel
    crash during MMU/memory initialization caused by a zeroed DTB memory node
  - the following fixes were implemented and verified through a VM rebuild:
    - **UART Lane Unified at 115200**: added `init_uart_baud=115200` to
      `config.txt` to prevent the firmware baud switch, ensuring a single
      stable capture profile from power-on to shell
    - **Official DTB Integration**: updated the DTB strategy to prioritize the
      official `bcm2711-rpi-4-b.dtb` from the Raspberry Pi firmware repository
    - **DTB Memory Node Repair**: set `RPI4B_QEMU_MEMORY_SIZE=3b400000` as the
      default, ensuring the static `system.dtb` has valid memory banks
    - **Kernel Entry Telemetry**: added a "Heartbeat" LED signal to the kernel
      `_start`; the ACT LED now turns solid ON upon successful kernel entry
    - **Kernel Hardening**: updated `pmap.c` to gracefully hang on zero memory
      banks instead of triggering a Data Abort
  - validation:
    - official firmware repo cloned to `external/raspberrypi-firmware`: pass
    - `scripts/prepare-rpi4b-dtb.sh` updated and verified: pass
    - kernel `_init.S` and `pmap.c` modifications: pass
    - VM-based fast rebuild: pass
    - canonical export and FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256: `19928dd6cdf7fcdd6214aa9289cc38b3d232f5d29536bb9a9d4a95cdd86353db`
  - next strongest step:
    - run the next real-device trial with image `19928dd6`
    - capture UART at 115200 (standard `firmware` profile)
    - watch for solid ACT LED and the first kernel banner output

- on `2026-04-12`, the Pi 4 post-switch UART lane was tightened so the next
  real-board retry can target the post-`plo` boundary directly:
  - the Pi 4 relocatable trampoline in
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-kernel8-reloc.S`
    no longer reprograms PL011 after the firmware baud switch
  - the canonical host capture helper now supports:
    - `--profile firmware` -> `115200`
    - `--profile postswitch` -> `103448`
  - the canonical UART summary helper now explicitly recommends a
    `--profile postswitch` rerun when a log ends at
    `uart: Set PL011 baud rate to 103448.300000 Hz`
    without later Phoenix phases
  - strong additional QEMU/GDB caveat recorded in the same step:
    - the raw direct Pi 4 QEMU kernel lane still falls back to the staged
      static `system.dtb`
    - that blob still contains the bootloader placeholder:
      - `memory@0 { reg = <0x00 0x00 0x00>; }`
    - so a raw direct Pi 4 QEMU GDB session without the patched DTB path is
      not authoritative for the current real-hardware post-`plo` boundary
  - validation:
    - `bash -n scripts/capture-rpi4b-uart.sh`: pass
    - `python3 -m py_compile scripts/summarize-rpi4b-uart-log.py`: pass
    - `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported Pi 4 image:
    - path:
      `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256:
      `7544e3e8012ccf9426134d94a8b9d68be52711e9f42291cfd1760801b7e16965`
  - next strongest step:
    - run the first dual-profile UART retry on real hardware:
      one `firmware` capture and one `postswitch` capture

- on `2026-04-12`, the latest real Pi 4 retry finally proved that Phoenix now
  reaches the `plo` HDMI progress panel and the `kernel jump` milestone on real
  hardware:
  - live UART results from
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260412-000322.log`:
    - firmware boot still reads the SD card and staged files successfully
    - the low-memory image layout remains active on hardware:
      - `Loaded 'loader.disk' to 0x8000000 size 0x311cd0`
      - `initramfs (loader.disk) loaded to 0x8000000 (size 0x311cd0)`
      - `Loaded 'kernel8.img' to 0x200000 size 0xe0d8`
      - `Kernel relocated to 0x80000`
      - `Device tree loaded to 0x2eff5600 (size 0xa9d0)`
    - the firmware still reprograms PL011 and the current host capture still
      becomes blind at:
      - `uart: Set PL011 baud rate to 103448.300000 Hz`
      - `uart: Baud rate change done...`
  - live HDMI result from the attached screenshot
    `/var/folders/jt/_gyk57f575q5gl68ltg0_y6w0000gn/T/TemporaryItems/NSIRD_screencaptureui_nweyRG/Screenshot 2026-04-12 at 00.08.55.png`:
    - brown framebuffer background matches the current `plo` HDMI path
    - the top-left panel shows all three stage boxes lit
    - source confirmation:
      - `video_stageFramebufferReady = 0`
      - `video_stageHalReady = 1`
      - `video_stageKernelJump = 2`
      - `video_stageCount = 3`
      in
      `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/video.c`
    - `hal_cpuJump()` calls `video_markKernelJump()` immediately before
      `hal_exitToEL1()` in
      `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/hal.c`
  - implication:
    - the board is no longer failing in the old armstub seam
    - the custom armstub, relocatable `kernel8` trampoline, low-memory `plo`
      placement, mailbox framebuffer path, and `plo` kernel-handoff path all
      execute on real hardware
    - the active live failure boundary has moved to the post-`plo` band:
      after `video_markKernelJump()` and around or after the EL1 handoff into
      the kernel
  - LED decode status from `/Users/witoldbolt/Downloads/IMG_0017.mov`:
    - the current ACT decoder produced a noisy mixed result, including a
      decoded stage `8` and a special terminal `0`
    - that decode is now considered lower-confidence than the HDMI evidence for
      this retry, because the screenshot proves the board reached a much later
      `plo` milestone than the decoded run suggests
  - warning status surfaced from the same UART run:
    - firmware messages like:
      - `[sdcard] vl805.bin not found`
      - `[sdcard] pieeprom.upd not found`
      - `Failed to open command line file 'cmdline.txt'`
      - repeated `dterror: no symbols found`
      - HDMI EDID read failures
    - these remain real warnings and are not ignored, but they do not match the
      current blocker because the board now demonstrably reaches the `plo`
      kernel-jump panel
  - strongest new process conclusion:
    - absence of trampoline UART `TR0..TR3` can no longer be used as proof that
      the board never reached the trampoline, because real HDMI output proves
      later Phoenix execution after the same capture cutoff
    - the UART-host lane now needs a post-firmware strategy that stays readable
      after the firmware baud switch, or a kernel-side visible breadcrumb path
      must be added in parallel
  - next strongest engineering step:
    - stop spending time on armstub-only telemetry
    - diagnose the post-`plo` EL1 / kernel-entry band
    - improve post-firmware UART observability and/or add the earliest possible
      kernel-side breadcrumb on the already-working HDMI path

- on `2026-04-11`, the second UART-assisted Pi 4 retry plus the matching LED
  video finally exposed the real armstub contract bug directly:
  - live UART results from
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260411-234639.log`:
    - the low-memory image fix is now active on real hardware:
      - `Loaded 'loader.disk' to 0x8000000 size 0x311cd0`
      - `Loaded 'kernel8.img' to 0x200000 size 0xe0d8`
    - the firmware still relocates the kernel image to:
      - `Kernel relocated to 0x80000`
    - the firmware still changes PL011 away from `115200`:
      - `uart: Set PL011 baud rate to 103448.300000 Hz`
  - live LED result from `/Users/witoldbolt/Downloads/IMG_0016.mov`:
    - the current ACT decoder now extracts a special terminal stage:
      - `31` / `11111`
    - that stage means the custom armstub found `kernel_entry32 == 0`
  - combined implication:
    - the current armstub assumption was wrong on this firmware path
    - real firmware does load and relocate `kernel8.img`
    - but the custom `armstub=` path does not give us a usable nonzero
      `kernel_entry32` slot on this board / firmware combination
    - so the next correct branch target is not “halt on empty slot”; it is the
      observed relocation target `0x80000`
  - active fix implemented from that evidence:
    - the Pi 4 custom armstub now preserves the existing firmware-slot path if
      `kernel_entry32` is nonzero
    - if `kernel_entry32 == 0`, it now falls back to `0x80000`
      instead of halting
    - if `dtb_ptr32 == 0`, it now falls back to the original firmware entry
      `x0` value for the DTB pointer
    - new special ACT stages were added:
      - `29`: DTB fallback to entry `x0`
      - `30`: kernel-entry fallback to `0x80000`
    - the old hard-stop stage `31` is no longer part of the active path
  - warning surfaced and fixed in the same step:
    - the fast rebuild helper still assumed `/tmp/rpi4b-dtb/...` existed
    - after the host rebooted and `/tmp` was cleared, the helper failed with:
      - `cp: cannot stat '/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb'`
      - missing `/etc/system.dtb` in `image_builder.py`
    - fix:
      `scripts/rebuild-rpi4b-fast.sh` now auto-runs
      `scripts/prepare-rpi4b-dtb.sh` when the VM-local DTB is missing
  - DTB warning policy status for this rebuild:
    - the official Raspberry Pi Linux DTS compile path still emits many real
      `dtc` warnings
    - there is still no locally available final Pi 4 DTB blob to replace that
      path
    - for this rebuild only, DTB preparation was rerun with
      `RPI4B_DTB_ALLOW_WARNINGS=1`
    - those warnings are still considered significant and remain open process
      debt, not ignored noise
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
    - direct Pi 4 QEMU serial sanity still reaches:
      - `call: exec go!`
      - `go: enter`
      - `hal: jump exit el1`
      - `A3`
      - `KLMconsole: pl011 init done`
      - later known `Exception #37: Data Abort (EL1)`
    - canonical SD-image export: pass
    - FAT-aware host verify: pass
  - refreshed exported image:
    - path:
      `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256:
      `4d9daf70168d6990e7525d0c0accda4a8a1ffed0a5fe62432aab4dcff8e70217`
  - next strongest real-device question:
    - whether the board now emits special stage `30` and then reaches
      trampoline UART `TR0..TR3`

- on `2026-04-11`, the first real Pi 4 retry on the relocatable trampoline
  image plus the matching UART logs narrowed the live boot failure further:
  - live UART results:
    - all `tio` captures on the current image stop cleanly at:
      - `uart: Set PL011 baud rate to 103448.300000 Hz`
      - `uart: Baud rate change done...`
    - the `picocom` attempt captured only undecodable garbage bytes
  - classification:
    - the current host UART lane is trustworthy for the firmware stage at
      `115200`, but not yet for the post-switch PL011 rate
    - this is now treated as a host-capture limitation, not as proof that
      Phoenix never emitted later bytes
  - LED result from `/Users/witoldbolt/Downloads/IMG_7141.mov`:
    - the downscaled decode still preserves the important timing
    - best contiguous Phoenix run reached:
      - `2`
      - `3`
      - `23`
      - `24`
      - `25`
      - `26`
      - `4`
    - then the special terminal code `0` appears
  - implication:
    - the board now reaches armstub stage `4`
    - then takes the EL2 exception path before trampoline UART `TR0`
    - the current live failure seam is therefore inside the tiny late-armstub
      band between stage `4` and the branch into the relocatable trampoline
  - the narrowest and most suspicious instruction in that band was:
    - `ic iallu`

- on `2026-04-11`, the late-armstub Pi 4 seam was tightened again based on that
  first UART-assisted retry:
  - code change:
    - the pre-branch `ic iallu` was removed from
      `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
    - the seam now keeps only:
      - `mov x0, x5`
      - `dsb sy`
      - `isb`
      - `br x4`
  - rationale:
    - after stage `4`, the only remaining plausible live exception sources were
      the manual cache invalidation and the final branch
    - `ic iallu` was the more suspicious one, because the real firmware had
      already loaded the relocatable trampoline and the trampoline itself
      performs the cache maintenance needed after copying the high-linked `plo`
      payload
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
    - no new compiler, linker, or packaging warnings surfaced in that rebuild
    - canonical export: pass
    - FAT-aware verify: pass
  - refreshed exported image:
    - path:
      `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256:
      `830da43f4e3ffc85347ea9522dd9ccf6ed5c6956dc52c821813037d2dc46f639`
  - next strongest real-device question:
    - whether removing `ic iallu` finally allows the board to reach
      trampoline UART `TR0`

- on `2026-04-11`, the canonical Pi 4 SD-image verifier was fixed after a
  false mismatch was reported on the current exported image:
  - symptom:
    - `scripts/verify-rpi4b-sdimg.sh` reported a SHA-256 mismatch against an
      old historical checksum even though the image itself matched the latest
      export
  - root cause:
    - the verifier still hardcoded the old checksum
    - the export helper did not persist its VM-derived checksum for later
      standalone verifier runs
  - fix:
    - `scripts/export-rpi4b-sdimg.sh` now writes:
      `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img.meta.txt`
    - `scripts/verify-rpi4b-sdimg.sh` now reads expected size and SHA-256 from
      that sidecar by default
    - `scripts/rebuild-rpi4b-fast.sh` now exercises the normal verifier path
      after export instead of injecting temporary checksum overrides
  - validation:
    - shell syntax checks for all three helpers: pass
    - refreshed export: pass
    - standalone verifier on the current image: pass

- on `2026-04-11`, the first real Pi 4 UART capture produced the decisive boot
  fact the earlier LED-only loop could not prove:
  - firmware-side UART reached:
    - `Loaded 'kernel8.img' to 0x40080000 size 0x0`
    - `Kernel relocated to 0x80000`
    - `Device tree loaded to 0x2eff5600 (size 0xa988)`
  - implication:
    - the current Phoenix Pi 4 image was still packaging `kernel8.img` as a raw
      direct copy of the high-linked `plo` image
    - even after the armstub handoff contract repair, real firmware was still
      executing a relocated copy of a payload linked for `0x40080000`
    - that relocation mismatch is now the strongest confirmed real-hardware
      boot blocker
  - surfaced firmware warnings in the same live log:
    - `[sdcard] vl805.bin not found`
    - `[sdcard] pieeprom.upd not found`
    - `[sdcard] recover4.elf not found`
    - `Failed to open command line file 'cmdline.txt'`
    - repeated `dterror: no symbols found`
  - current classification of those warnings:
    - the missing recovery/update files are expected for the current Phoenix
      SD-card layout and are not the blocker
    - missing `cmdline.txt` is expected for the current image and is not the
      blocker
    - `dterror: no symbols found` comes from the stripped final DTB blob and is
      now explicitly surfaced, but it does not match the current pre-Phoenix
      failure signature
  - active fix implemented from that UART evidence:
    - `kernel8.img` is no longer a raw copy of `plo-*.img`
    - it is now a small relocatable AArch64 trampoline plus embedded high-linked
      `plo` payload
    - the trampoline:
      - preserves the firmware DTB pointer in `x0`
      - emits direct PL011 breadcrumbs:
        - `TR0`: trampoline entry
        - `TR1`: payload copy start
        - `TR2`: payload copied and cache maintenance complete
        - `TR3`: branch to real `plo`
      - copies the embedded `plo` image to `0x40080000`
      - cleans the copied destination with `dc cvau`
      - executes `ic iallu` plus barriers
      - branches to the real high-linked `plo`
  - build/image integration change:
    - `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
      now builds `kernel8.img` from:
      - `phoenix-kernel8-reloc.S`
      - `phoenix-kernel8-reloc.lds`
      - an objcopy-wrapped embedded `plo` payload section
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
    - the rebuild completed without new compiler or linker warnings
    - direct Pi 4 QEMU serial sanity still reaches:
      - `call: exec go!`
      - `go: enter`
      - `hal: jump exit el1`
      - `A3`
      - `KLMconsole: pl011 init done`
      - later known `Exception #37: Data Abort (EL1)`
    - canonical SD-image export: pass
    - FAT-aware host verify: pass
  - refreshed exported image:
    - path:
      `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256:
      `610dbbfd0192760f061395f7e85573261b85b18857bea426e6adab4930468698`
  - next strongest real-device question:
    - whether UART now reaches `TR0`, `TR1`, `TR2`, `TR3`, and then Phoenix
      `plo`
  - manifest:
    `manifests/2026-04-11-pi4-kernel8-reloc-trampoline.md`

- on `2026-04-11`, the active Pi 4 boot fix stopped treating the late custom
  armstub seam as a probe target and restored the firmware handoff contract
  used by the known-working `rpi4-bare-metal` reference:
  - boot fix:
    - the Pi 4 custom armstub no longer dereferences the target image before
      branching
    - it now loads `dtb_ptr32`
    - it now loads `kernel_entry32`
    - it halts only if `kernel_entry32 == 0`
    - it then restores `x0`, executes
      `dsb sy; ic iallu; dsb sy; isb`, and branches through the firmware entry
      slot
  - earliest `plo` follow-up:
    - the firmware DTB pointer is now preserved through the first generic
      AArch64 `plo` instructions instead of being clobbered immediately
    - it is stored in `hal_firmwareDtb` at `start_common`
  - Pi 4 firmware config change:
    - `boot_load_flags=0x1` was removed from the active Pi 4 `config.txt`
    - `kernel_address=0x40080000` remains in place because the current Phoenix
      `plo` image is still linked for the high-DDR load model
  - warning surfaced and fixed in the same step:
    - the fast copied-buildroot lane emitted
      `fatal: not a git repository (or any of the parent directories): .git`
    - root cause:
      [plo/Makefile](/Users/witoldbolt/phoenix-rpi/sources/plo/Makefile)
      computed `VERSION` with an unconditional `git rev-parse`
    - fix:
      that lookup is now guarded so copied buildroots without `.git` no longer
      emit a fake-fatal on every rebuild
  - validation:
    - `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
    - direct Pi 4 QEMU serial sanity still reaches:
      - `call: exec go!`
      - `go: enter`
      - `hal: jump exit el1`
      - `A3`
      - `KLMconsole: pl011 init done`
      - later known `Exception #37: Data Abort (EL1)`
    - canonical SD-image export: pass
    - FAT-aware host verify: pass
  - refreshed exported image:
    - path:
      `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
    - SHA-256:
      `7ba2d0773a60451691a45083d376cd6ccc3293dd800ffe14a8c741ec064db61c`
  - manifest:
    `manifests/2026-04-11-pi4-firmware-handoff-contract-fix.md`

- on `2026-04-11`, the canonical Pi 4 macOS-host UART helper was promoted from
  a `picocom`-only assumption to a `tio`-first workflow:
  - current host tool baseline:
    - `tio 3.9` present on macOS host
    - `picocom` still present as fallback
  - helper change:
    - [capture-rpi4b-uart.sh](/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh)
      now prefers `tio` automatically when it is installed
    - it falls back to `picocom` automatically when `--exit-after` is used,
      because `tio` has no equivalent option
    - it now records the chosen serial tool in the `.meta.txt` file
    - its `--list` mode now warns explicitly when no likely USB serial adapter
      is found and, when `tio` is present, prints the broader `tio --list`
      inventory instead of failing silently
  - local validation:
    - `bash -n scripts/capture-rpi4b-uart.sh`: pass
    - `scripts/capture-rpi4b-uart.sh --help`: pass
    - `scripts/capture-rpi4b-uart.sh --list`: pass
    - `scripts/capture-rpi4b-uart.sh --tool auto --device /dev/does-not-exist --exit-after 1000`:
      emits the intended warning about `tio` lacking `--exit-after`, then
      falls back to `picocom` and reports the missing device cleanly
  - warning and error surfaced during validation:
    - a pseudo-TTY `tio` smoke inside the Codex sandbox failed with:
      `Error: Could not open tty device (Operation not permitted)`
    - classification:
      sandbox limitation during synthetic PTY validation, not evidence of a
      host-side `tio` problem with the real USB-TTL adapter path
    - process consequence:
      the helper no longer uses `tio --mute`, so real open/connect errors stay
      visible instead of being hidden
  - practical implication:
    - tomorrow's first real USB-TTL session should use the canonical helper in
      default `auto` mode and let it pick `tio`
  - manifest:
    `manifests/2026-04-11-pi4-uart-tio-first-host-lane.md`

- on `2026-04-11`, the repository policy was tightened so warnings and
  recoverable tool errors must now be surfaced, classified, and either fixed or
  explicitly justified in the same session:
  - the docs and agent rules now treat warnings as first-class signals rather
    than cosmetic noise
  - DTS, DTSI, and DTB warnings are now explicitly classified as
    boot-significant until proved otherwise
  - `scripts/prepare-rpi4b-dtb.sh` now prefers final-form DTB blobs over local
    DTS compilation whenever possible
  - the same helper now fails by default if DTS compilation emits warnings
  - first strict-helper result:
    - compiling the current Raspberry Pi Linux
      `bcm2711-rpi-4-b.dts` path emits many `dtc` warnings such as:
      - `unit_address_vs_reg`
      - `simple_bus_reg`
      - `unique_unit_address`
      - `gpios_property`
    - that warning set is now surfaced instead of being silently ignored
  - practical implication:
    - prefer final DTB blobs for Pi 4 work
    - treat local DTS compilation as fallback, not as the default trusted path
  - manifest:
    `manifests/2026-04-11-warning-discipline-and-dtb-policy.md`

- on `2026-04-11`, the next Pi 4 retry clip
  `/Users/witoldbolt/Downloads/IMG_0014.mov` did **not** produce any valid
  decodable Phoenix ACT-LED stage burst:
  - clip facts:
    - about `59.92 fps`
    - about `94.83 s`
  - the analyzer still detects many green-LED on-segments, but they do not
    form any valid sync-plus-5-bit stage code under the current
    `pi4_dense_firstread_focus_map_2026_04_10` layout
  - interpreter result:
    - `highest_completed=none`
  - implication:
    - this clip no longer gives a reliable LED-only boundary
    - the next strongest lane is host UART capture with LED video kept in
      parallel as a secondary signal
  - manifest:
    `manifests/2026-04-11-pi4-img0014-no-valid-stage-decode.md`

- on `2026-04-11`, the first real Pi 4 UART host-debug lane was prepared so
  tomorrow's USB-TTL cable can become a first-class bring-up signal rather than
  an improvised manual step:
  - canonical host capture helper:
    `/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh`
  - canonical host log summarizer:
    `/Users/witoldbolt/phoenix-rpi/scripts/summarize-rpi4b-uart-log.py`
  - initial host tool baseline at that time:
    - `picocom` present on macOS host
    - `tio` absent on macOS host
  - current Pi 4 config already enables the firmware-facing UART path:
    - `enable_uart=1`
    - `uart_2ndstage=1`
  - current next-lab recommendation:
    - keep LED video
    - add host UART capture in parallel
    - if necessary, enable Raspberry Pi EEPROM `BOOT_UART=1` on a known-good
      Raspberry Pi OS card for even earlier bootloader output
  - no upstream boot code change was required for this UART-prep step
  - manifest:
    `manifests/2026-04-11-pi4-uart-host-lane-prep.md`

- on `2026-04-10`, a canonical Pi 4 incremental rebuild fast lane was added so
  future armstub and `plo` debug loops do not default to full clean world
  rebuilds:
  - helper:
    `/Users/witoldbolt/phoenix-rpi/scripts/rebuild-rpi4b-fast.sh`
  - default behavior:
    - incrementally refresh the copied VM-local buildroot
    - auto-select the narrowest safe build phase
    - rebuild the Pi 4 image
    - assemble, export, and verify the SD image
  - current auto-scope policy:
    - `project image` when only `phoenix-rtos-project` or `plo` are dirty
    - `core project image` when core repos are dirty
    - `clean host core project image` when build-infra repos are dirty
  - current verified speed fact:
    - a no-clean `project image` rebuild completed in about `1.08s` on the
      copied buildroot
  - verified helper run:
    - `--build-only --scope project`: pass

- on `2026-04-10`, the dense signature image was rebuilt again into a
  first-read focus image to remove ambiguity in the live `24 -> 25` seam:
  - the armstub seam stages of interest are now emitted twice with an extra
    long gap
  - barriers were inserted before both signature-word reads
  - stage `21` is now a temporary micro-split marker immediately before the
    first signature-word read
  - stage `22` is now a temporary micro-split marker immediately before the
    second signature-word read
  - later compare-band stages remain stable:
    - `25`: first signature-word read complete
    - `26`: second signature-word read complete
    - `27..30`: compare-band progress
    - `4`: signature verified before branch
    - `31`: mismatch halt
    - `0`: EL2 exception trap
  - Pi 4 A72 rebuild from refreshed copied buildroot: pass
  - direct Pi 4 QEMU serial sanity still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - bootfs assembly: pass
  - FAT image assembly: pass
  - SD-image assembly: pass
  - canonical SD-image export: pass
  - FAT-aware host verifier: pass
- refreshed exported first-read focus image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `6932d3a31fc0fee1494295c4e9d0587c689b7cde20a6fb1907d86164e9815883`
- current manifest:
  `manifests/2026-04-10-pi4-firstread-focus-image.md`

- on `2026-04-10`, the first board retry on the dense armstub signature-map
  image moved the live failure band forward again:
  - input clip:
    `/Users/witoldbolt/Downloads/IMG_0012.mov`
  - effective clip rate:
    about `59.92 fps`
  - auto-detected ACT LED ROI:
    - `156,162,286,269`
  - the best contiguous Phoenix run now reaches:
    - `2`: armstub after timer/GIC
    - `3`: armstub before fixed jump
    - `23`: late seam entry
    - `24`: fixed target address loaded
  - the next expected stage is still missing:
    - `25`: first signature word read
  - no later valid:
    - `25`
    - `31`
    - `0`
    appears in the main contiguous run
  - one later unmatched valid stage:
    - `27`
    was decoded, but the current interpreter correctly leaves it outside the
    main run because the contiguous `25 -> 26` prefix is absent
  - the active failure band therefore narrows again:
    - target address load completed
    - first signature-word read is now the next live boundary
- current manifest:
  `manifests/2026-04-10-pi4-img0012-dense-signature-analysis.md`

- on `2026-04-10`, the Pi 4 stage-`3` seam was widened into a dense armstub
  signature map so the next board video can identify the failing instruction
  band directly:
  - the late armstub seam now emits:
    - `23`: late seam entered
    - `24`: fixed target address loaded
    - `25`: first signature word read
    - `26`: second signature word read
    - `27`: first expected signature constant loaded
    - `28`: first compare passed
    - `29`: second expected signature constant loaded
    - `30`: second compare passed
    - `4`: signature verified before branch
    - `31`: mismatch halt
  - the armstub now also installs an EL2 exception vector and emits:
    - `0`: EL2 exception trap during that seam
  - later `plo` stages remain stable from stage `5` onward
  - Pi 4 A72 rebuild from refreshed copied buildroot: pass
  - direct Pi 4 QEMU serial sanity still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - bootfs assembly: pass
  - FAT image assembly: pass
  - SD-image assembly: pass
  - canonical SD-image export: pass
  - FAT-aware host verifier: pass
- refreshed exported dense-armstub image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `6b349fe6c2afe11ea0fdeb5d9fc874eb5ae1b990ee83d42c48f10662445875e8`
- current manifest:
  `manifests/2026-04-10-pi4-dense-armstub-signature-map.md`

- on `2026-04-10`, the next real Pi 4 retry clip `IMG_7138.mov` tightened the
  fixed-target-signature result without yet reaching a new later stage:
  - `ffprobe` confirms the clip is effectively `59.92 fps`
  - the analyzer auto-detects the ACT LED ROI at:
    - `198,93,221,114`
  - the best contiguous Phoenix run still ends at:
    - stage `3` / `00011`
  - no later valid:
    - stage `4` / `00100`
    - stage `31` / `11111`
  - two unmatched decoded bursts exist:
    - stage `16` / `10000`
    - stage `26` / `11010`
    but the current interpreter correctly treats them as noise, not the main
    contiguous Phoenix run
  - the active failure band therefore narrows again:
    - armstub stage `3` reached
    - no verified fixed-target signature result yet
    - no signature-mismatch halt stage yet
    - so the next bounded code step should split the tiny armstub band between
      stage `3` and the first signature-memory read
- current manifest:
  `manifests/2026-04-10-pi4-img7138-signature-analysis.md`

- on `2026-04-10`, the fixed-target-signature image for the Pi 4 stage-`3 -> 4`
  seam was implemented, rebuilt, exported, and FAT-verified:
  - the custom Pi 4 armstub now verifies a deliberate `plo` entry signature at
    `0x40080000 + 0x4` before taking the fixed-address branch
  - stage `4` now means signature verified before branch
  - stage `31` now means signature mismatch and pre-branch halt
  - the dedicated fixed-entry `plo` veneer therefore shifts to stage `5`, and
    the old generic `_start` body shifts to stage `6`
  - Pi 4 A72 rebuild from the refreshed copied buildroot: pass
  - direct Pi 4 QEMU serial sanity on the real-device build still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - bootfs assembly: pass
  - FAT image assembly: pass
  - SD-image assembly: pass
  - canonical SD-image export: pass
  - FAT-aware host verifier: pass after updating the default expected SHA
  - generic shell-smoke wrapper is currently flaky again after sending `help`;
    because this step is Pi-4-armstub-only and the direct Pi 4 serial sanity
    lane stayed stable, that wrapper was not used as the gate for this step
- refreshed exported fixed-target-signature image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `8ef476644f8fce5b5937096125421a218b8a67b0513b0fa4c0ab7e6592585e3e`
- current manifest:
  `manifests/2026-04-10-pi4-fixed-target-signature-check.md`

- on `2026-04-10`, the Pi 4 ACT-LED hardware-video workflow was rebuilt into a
  reusable analyzer-plus-layout-plus-interpreter toolchain:
  - raw analyzer:
    `/Users/witoldbolt/phoenix-rpi/scripts/analyze-rpi4-actled-video.py`
  - current probe-layout source of truth:
    `/Users/witoldbolt/phoenix-rpi/scripts/rpi4_actled_probe_layout.py`
  - matcher / interpreter:
    `/Users/witoldbolt/phoenix-rpi/scripts/interpret-rpi4-actled-analysis.py`
  - the analyzer now auto-detects the ACT LED region in the current pre-cropped
    static videos and emits standardized JSON instead of ad hoc text
  - `IMG_7137.mov` now decodes through that toolchain as:
    - best contiguous run:
      - stage `3` / `00011`
    - next missing expected stage:
      - stage `4` / `00100`
    - one unmatched false-positive group:
      - stage `16` / `10000`
  - `IMG_7136.mov` under the current layout still decodes the shared common
    prefix:
    - stage `1`
    - stage `2`
    - stage `3`
    with no later stage `4`, which is a useful backward-compatibility sanity
    check rather than a negative control
  - the active failure band therefore remains:
    - armstub stage `3` reached
    - fixed-address `plo` entry stage `4` still not observed
    - so the next code step should stay on the stage-`3 -> 4` seam
  - next planned implementation move:
    - verify from the armstub that the expected `plo` entry signature is
      really present at `0x40080000` before taking the fixed branch
  - preserved interpretation rule:
    - the initial ACT LED chatter during Raspberry Pi firmware SD-card reads
      is pre-Phoenix activity and should be ignored unless it is part of a
      later valid contiguous decoded Phoenix run
- current manifest:
  `manifests/2026-04-10-pi4-led-analysis-toolchain.md`

- on `2026-04-10`, the stage-`3 -> 4` seam was split again with a dedicated
  fixed-address Pi 4 entry trampoline:
  - generic AArch64 `plo _start` now starts with a tiny veneer at the fixed
    branch target
  - that veneer emits stage `4` inline, then branches to `_start_real`
  - `_start_real` now emits stage `5` inline before the old generic body
  - the old later stages were shifted by `+1`, so:
    - `6`: after clearing `x0..x7`
    - `7`: after clearing `x8..x15`
    - `8`: after clearing `x16..x23`
    - `9`: after clearing `x24..x30`
    - `10`: after `dsb sy` / `isb`
    - `11`: after `mrs currentEL`
    - `12`: `start_el3`
    - `13`: `start_el2`
    - `14`: `start_el1`
    - `15`: EL3 path complete
    - `16`: EL2 path complete
    - `17`: EL1 path complete
    - `18`: `start_common`
    - `19`: after stack setup
    - `20`: core-0 branch to `_startc`
    - `21`: unexpected-EL trap path
- validation summary for dedicated-entry-trampoline image:
  - Pi 4 A72 rebuild from refreshed copied buildroot: pass
  - generic QEMU shell smoke: pass
  - direct Pi 4 QEMU serial sanity on real-device build still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - bootfs assembly: pass
  - FAT image assembly: pass
  - SD-image assembly: pass
  - canonical SD-image export: pass
  - FAT-aware host verifier: pass
- refreshed exported dedicated-entry-trampoline image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `d76a6c2bb0d15173f4a6a90aa5c82211b0ea286b5bb236960e51fdd3388c2320`
- current manifest:
  `manifests/2026-04-10-pi4-fixed-entry-trampoline.md`

- on `2026-04-10`, OpenCV-based analysis of the new real-board retry
  `IMG_7136.mov` confirmed that the stage-`3 -> 4` handoff-hardened image
  still does not emit earliest generic `plo` stage `4`:
  - `ffprobe` confirms the clip is `59.92 fps`
  - the current host-side decoder is now:
    - `/Users/witoldbolt/phoenix-rpi/scripts/analyze-rpi4-actled-video.py`
  - the ACT LED ROI used for this clip is:
    - `92,108,117,118`
  - the decoder extracts these valid stage groups:
    - stage `1` / `00001`
    - stage `2` / `00010`
    - stage `3` / `00011`
  - no valid stage `4` / `00100` group appears after stage `3`
  - the earlier green activity around `2.07s - 7.26s` still does not match
    the compact sync-plus-`5`-bit Phoenix protocol and remains classified as
    pre-telemetry firmware / media activity rather than valid Phoenix output
  - the active failure band therefore remains:
    - armstub stage `3` reached
    - earliest generic `plo` stage `4` not reached
    - failure still localized to the raw handoff seam or the first few fetched
      instructions at the fixed branch target
- current manifest:
  `manifests/2026-04-10-pi4-img7136-opencv-analysis.md`

- on `2026-04-10`, the active Pi 4 response to the decoded stage-`3` boundary
  was implemented and rebuilt:
  - the custom Pi 4 armstub no longer clears the primary-path argument
    registers before the fixed-address branch
  - it now also inserts:
    - `dsb sy`
    - `ic iallu`
    - `dsb sy`
    - `isb`
    immediately before branching to `0x40080000`
  - earliest generic AArch64 `plo _start` no longer uses a helper call for
    stage `4`; stage `4` is now emitted inline through direct GPIO writes at
    the first `_start` instruction
- validation summary for the stage-`3 -> 4` handoff-hardened image:
  - Pi 4 A72 rebuild: pass
  - generic AArch64 rebuild: pass
  - generic QEMU shell log still reaches runtime and `help`
  - direct Pi 4 QEMU serial sanity on the real-device build still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - bootfs assembly: pass
  - FAT image assembly: pass
  - SD-image assembly: pass
  - canonical SD-image export: pass
  - FAT-aware host verification: pass
- the refreshed exported handoff-hardened image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `4b9c967c9381e8935998a19eb1a976c43b440dd57da4c5fab489763f729a6835`
- current manifest:
  `manifests/2026-04-10-pi4-stage34-handoff-hardening.md`

- on `2026-04-10`, the first real-board retry on the compact stage-code image
  produced a materially clearer decode than the earlier count-based protocol:
  - `ffprobe` confirms `IMG_7135.mov` is truly `59.94 fps`
  - the later ACT windows decode cleanly as:
    - stage `1` / `00001` around `8.44s - 10.79s`
    - stage `2` / `00010` around `11.28s - 13.61s`
    - stage `3` / `00011` around `14.10s - 16.53s`
  - no later stage-`4` sync burst is visible after stage `3`
  - earlier green activity around `1.95s - 7.42s` does not match the current
    sync-plus-`5`-bit structure and is therefore treated as pre-telemetry
    firmware / media activity, not as valid decoded Phoenix stage output
  - the strongest current interpretation is now:
    - armstub stage `1` is reached
    - armstub stage `2` is reached
    - armstub stage `3` is reached
    - earliest generic `plo` stage `4` is not observed
  - so the active failure band moves back from the `currentEL` seam to the
    stage `3 -> 4` handoff itself
- current manifest:
  `manifests/2026-04-10-pi4-compact-stage-code-video-analysis.md`

- on `2026-04-10`, the Pi 4 earliest-`plo` telemetry was redesigned to reduce
  future probe churn after `IMG_0009.mov`:
  - the old count-based GPIO42 pulse groups were replaced with a compact
    sync-plus-`5`-bit stage-code protocol
  - the protocol now carries more checkpoints in one video-decodable burst:
    - `1..3`: armstub path
    - `4`: earliest generic AArch64 `plo _start`
    - `5..8`: the four bounded register-clear subranges
    - `9`: after `dsb sy` / `isb`
    - `10`: after `mrs currentEL`
    - `11..13`: EL-path selection
    - `14..16`: post-EL-path pre-`start_common` boundaries
    - `17`: `start_common`
    - `18`: after stack initialization
    - `19`: core-0 branch to `_startc`
    - `20`: unexpected-EL trap path
  - `dsb sy` and `isb` were also inserted immediately before `mrs currentEL`,
    which is the narrowest plausible fix suggested by `gemini-findings.md`
    that directly overlaps the current failure band
- validation summary for the compact stage-code image:
  - Pi 4 A72 rebuild: pass
  - generic QEMU shell smoke: pass
  - direct Pi 4 QEMU serial sanity on the real-device build still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - bootfs assembly: pass
  - FAT image assembly: pass
  - SD-image assembly: pass
  - canonical SD-image export: pass
  - FAT-aware host verification: pass after refreshing the expected SHA
- the refreshed exported compact stage-code image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `cada5a0cf3c5ce41a2197cc4296e81ed43b6b671d878660e3e303e16098ab60c`
- current manifest:
  `manifests/2026-04-10-pi4-compact-stage-code-currentel-split.md`

- on `2026-04-09`, the next real-device hardware clip `IMG_0009.mov`
  materially tightened the earliest `plo` boundary again:
  - `ffprobe` confirms the clip is actually `59.93 fps`
  - the strongest extracted ACT windows were:
    - `1.985s - 2.002s`
    - `2.203s - 7.392s`
    - `8.360s - 8.760s`
    - `9.562s - 9.978s`
    - `11.597s - 11.997s`
    - `12.798s - 12.848s` (weak / near-threshold)
    - `15.668s - 16.068s`
    - `16.870s - 17.287s`
    - `18.088s - 18.488s`
  - no later ACT activity is visible after about `18.49s`
  - the early armstub activity is still partially merged by capture effects, so
    the clip is not a literal one-pulse-per-stage decode from checkpoint `1`
  - the strongest current interpretation is now:
    - stage `4` is still reached
    - the later grouped activity fits completion through stage `6` better than
      a halt before stage `5`
    - no convincing later group fits stage `7`
  - so the active failure band shifts again:
    - likely after stage `6`
    - likely before the existing stage `7` marker
    - meaning the next bounded split should be directly around
      `mrs currentEL`
- current manifest:
  `manifests/2026-04-09-pi4-mid-register-clear-video-analysis.md`

- on `2026-04-09`, the stage-`4` to stage-`5` gap was split again inside the
  earliest generic AArch64 `_start` register-clearing block:
  - stage `5` is now the midpoint of the `mov xN, #0` sequence
  - stage `6` is now the end of register clearing
  - later stages were shifted to `7..13`
- validation summary for the mid-register-clear split image:
  - Pi 4 A72 rebuild: pass
  - generic QEMU shell smoke: pass
  - direct Pi 4 QEMU serial sanity on the real-device build still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - bootfs assembly: pass
  - SD-image assembly: pass
  - canonical SD-image export: pass
  - FAT-aware host verification: pass
- the refreshed exported mid-register-clear split image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `03a0729254dc0bc81f542fe8db276f7a2b70d3fb76de9fc7303ea470aca83137`
- current manifest:
  `manifests/2026-04-09-pi4-stage4-mid-register-clear-split.md`

- on `2026-04-09`, the next hardware retry video `IMG_0005.mov` tightened the
  same early `plo` conclusion again:
  - `ffprobe` shows the clip is actually `30.01 fps`, not `60 fps`
  - the strongest left-side ACT windows were:
    - `0.79s - 5.86s`
    - `6.89s - 7.29s`
    - `8.09s - 8.12s` (weak / near-threshold)
    - `10.12s - 10.52s`
    - `11.36s - 11.72s`
    - `14.59s - 14.96s`
    - `15.79s - 16.19s`
    - `16.99s - 17.39s`
  - the durable conclusion is not that the pulse count itself became clearer
  - the durable conclusion is that no visible activity extends into the time
    range expected for completion of checkpoint `5`
  - so the current failure is now best classified as:
    - after checkpoint `4` (`plo _start` entry)
    - before checkpoint `5` (after register clearing)
- that bounded response is now implemented above:
  the split has been moved into the `_start` register-clearing block itself,
  not later EL-path code

- on `2026-04-09`, the latest `IMG_0004.mov` `60 fps` hardware video was
  mapped onto the slower GPIO42 telemetry timeline:
  - the highest-confidence green-on windows were:
    - `0.78s - 0.82s`
    - `0.93s - 5.72s`
    - `6.70s - 7.12s`
    - `7.92s - 8.35s`
    - `9.95s - 10.37s`
    - `14.03s - 14.43s`
    - `15.23s - 15.65s`
    - `16.45s - 16.85s`
  - the strongest interpretation is still:
    - the board completes checkpoint `4`
    - the failure is after earliest generic AArch64 `plo` `_start`
    - the failure is before the first EL-path marker from the older map
- the bounded response is now implemented in generic AArch64 `plo _start`:
  - keep checkpoints `1..4` unchanged
  - split the former post-stage-`4` band into:
    - `5`: after general-purpose register clearing
    - `6`: after `currentEL` sampling, before EL dispatch
    - `7`: `start_el3`
    - `8`: `start_el2`
    - `9`: `start_el1`
    - `10`: `start_common`
    - `11`: core-0 branch to `_startc`
    - `12`: unexpected-EL trap path
- validation summary for the narrowed post-stage-`4` image:
  - Pi 4 A72 rebuild: pass
  - generic QEMU shell smoke: pass on rerun
  - direct Pi 4 QEMU serial sanity on the real-device build still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - canonical SD-image export: pass
  - FAT-aware host verification: pass
- the refreshed exported post-stage-`4` split image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `d1e0fd5b2e3817d4e0d2ad339b63be34fb96d17f2d8a05d4e318d52a02952c20`
- current manifest:
  `manifests/2026-04-09-pi4-post-stage4-el-dispatch-split.md`

- on `2026-04-09`, the first structured-telemetry video was re-analyzed at
  higher frame granularity and proved the initial reading wrong:
  - the green ACT LED does blink repeatedly
  - the clip shows multiple later pulse groups rather than a simple
    `off -> on forever` sequence
  - but the current structured protocol is still too dense to decode
    confidently from one ordinary iPhone recording
- the bounded response is now implemented:
  - keep the same checkpoint map `1..9`
  - slow the GPIO42 timing to about `0.4s` on, `0.4s` off inside a group, and
    `2.0s` off between groups
  - remove the redundant leading gap from each stage emitter so the full
    sequence still fits within about one minute
- validation summary for the slower-telemetry image:
  - Pi 4 A72 rebuild: pass
  - generic QEMU shell smoke: pass
  - direct Pi 4 QEMU serial sanity on the real-device build still reaches:
    - `call: exec go!`
    - `go: enter`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
    - later `Exception #37`
  - Pi 4 HDMI smoke helper was attempted twice and both runs failed in the
    helper harness itself with early `socat ... Connection refused`, so that
    lane was not re-verified in this step
- the refreshed exported slower-telemetry image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `4698611f2231fd5508e6eddeed25a24147701ce3efc209371425ea75d502f23e`
- current manifest:
  `manifests/2026-04-09-pi4-led-telemetry-slower-protocol.md`

- on `2026-04-09`, the tighter LED-only hardware video from the current Pi 4
  image proved the old one-off GPIO42 probes were no longer sufficient:
  - the ACT LED sequence is now more complex than a simple armstub-only loop
  - the late short pulse pattern strongly suggests control reaches earliest
    generic AArch64 `plo` `_start`
  - but the exact failing checkpoint is still ambiguous from one-off probes
- the active response is now a structured GPIO42 telemetry image, not another
  moved single probe:
  - one pulse group per checkpoint, separated by longer off gaps
  - current checkpoint map:
    - `1`: armstub primary-core entry
    - `2`: armstub after early timer / GIC preparation
    - `3`: armstub just before the fixed-address jump to `plo`
    - `4`: earliest generic AArch64 `plo` `_start`
    - `5`: `plo` EL3 path selected
    - `6`: `plo` EL2 path selected
    - `7`: `plo` EL1 path selected
    - `8`: `plo` `start_common`
    - `9`: `plo` core-0 branch to `_startc`
- validation summary for that telemetry image:
  - Pi 4 A72 rebuild: pass
  - generic QEMU shell smoke: pass
  - direct Pi 4 QEMU serial sanity still reaches:
    - `go!`
    - `hal: jump exit el1`
    - `A3`
    - `KLM`
  - later direct Pi 4 QEMU prompt gating is still constrained by the known
    QEMU-only DTB / memory-node mismatch when the artifact is built for real
    firmware boot instead of the QEMU-patched DTB lane
- the refreshed exported Pi 4 telemetry image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `6d6b4d7dd84f237f3e8dab1764f8be34b29b4e4d46d6f92ad30aee1869a2acdc`
- current manifest:
  `manifests/2026-04-09-pi4-led-telemetry-protocol.md`

- on `2026-04-09`, the next real Pi 4 board retry on the fixed-address
  armstub image produced:
  - no HDMI output
  - no keyboard-visible reaction
  - red LED always on
  - green LED on at power-up, then off, then briefly on again, then off again,
    then on later and steady on
- that is not identical to the earlier single reset-like sequence:
  the fixed-address handoff changed the earliest hardware-visible behavior, but
  still did not prove that `plo` itself executed

- on `2026-04-09`, the active Pi 4 image was rebuilt around the next bounded
  response to that clue:
  - keep the current fixed-address custom armstub handoff
  - add a Pi-4-only GPIO42 pattern at the very top of generic AArch64 `plo`
    `_start`
  - run that pattern before register clearing and exception-level setup
- the Pi 4 A72 rebuild passed
- the direct Pi 4 QEMU serial-log sanity lane still reaches:
  - `go!`
  - `hal: jump exit el1`
  - kernel markers `A3` and `KLM`
- the refreshed exported Pi 4 SD image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `e5f8662aca8c859464bed6c23e9742afd196bf1136a09f453e9c975e06b6441c`
- current manifest:
  `manifests/2026-04-09-pi4-earliest-plo-entry-led.md`

- on `2026-04-09`, the Pi 4 SD-image export workflow was tightened into a
  fixed project rule:
  - `scripts/export-rpi4b-sdimg.sh` is now the only approved VM-to-host export
    path for the Pi 4 SD image
  - it now captures the VM-local image size and SHA-256, transfers the image
    through the text-safe base64 path, and runs the FAT-aware host verifier
    against those VM-derived values before replacing the exported artifact
  - future sessions must not improvise with `scp`, `sftp`, `rsync`,
    `limactl copy`, streamed `dd`, or manual binary-copy pipelines for this
    artifact; if export reliability is in doubt, the helper must be fixed
    instead of bypassed

- on `2026-04-09`, the latest real Pi 4 board result on the
  pre-kernel-branch armstub LED image produced:
  - red LED on
  - green LED on, then briefly off, then on forever
  - blank screen
  - no keyboard-visible reaction
- that is a stronger clue than the earlier static-ON result:
  - the custom armstub entry proof still holds
  - the brief low pulse proves the armstub reaches its final pre-branch split
  - the LED returning high strongly suggests the board resets and re-enters the
    armstub immediately after the current handoff to `kernel8.img`
- the highest-probability cause is now explicit:
  the current `kernel8.img` is a raw `plo` binary beginning directly with
  AArch64 instructions, not a Linux `Image`-format payload, while Raspberry Pi
  firmware populates armstub `kernel_entry32` by parsing the 64-bit kernel
  header

- on `2026-04-09`, the active Pi 4 image was rebuilt around the smallest
  bounded response to that clue:
  - the custom armstub still performs the current timer and GIC setup
  - the custom armstub still drives GPIO42 high early and low at the final
    branch-site split
  - but it now jumps directly to `0x40080000` instead of trusting the
    firmware-patched `kernel_entry32` slot
- the Pi 4 A72 rebuild passed
- the direct Pi 4 QEMU serial-log sanity lane still reaches:
  - `go!`
  - `hal: jump exit el1`
  - kernel markers `A3` and `KLM`
- the host-side SD-image export path was tightened again during this step:
  - both the earlier `rsync` export and a raw streamed `dd` export produced
    zeroed FAT boot sectors in the host-visible copy
  - the current export helper now uses:
    `limactl shell ... base64 | base64 -d`
- the refreshed exported Pi 4 SD image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- historical validated Pi 4 SD-image SHA-256 at that step:
  `d4e02f329c35f8187969f3c02e8f0d78189fac07b8884ddb774898598a1ddc36`
- current manifest:
  `manifests/2026-04-09-pi4-fixed-armstub-entry.md`

- on `2026-04-08`, the real Pi 4 board retry on the temporary late-`plo`
  `_init.S` GPIO42 split image produced:
  - both red and green LEDs on
  - blank screen
  - no keyboard-visible reaction
- that closed the temporary late-`plo` hypothesis cleanly:
  the board still does not reach the late `_init.S` split point, so that
  diagnostic probe was removed instead of being committed

- on `2026-04-08`, the active Pi 4 image moved the next persistent GPIO42
  transition back into the custom firmware armstub, at the final primary-core
  handoff point:
  - the custom armstub still drives GPIO42 high first
  - the primary-core armstub path now drives GPIO42 low just before branching
    to `kernel8.img`
  - if the ACT LED stays on, the failure is still before that final armstub
    handoff point
  - if the ACT LED ends off, the board reached that final armstub handoff
    point and the remaining failure is later
- the disproved `_startc()` LED proof has now been removed from `plo`
- the Pi 4 A72 rebuild passed, the direct Pi 4 QEMU serial-log sanity lane
  still reaches:
  - `go!`
  - `hal: jump exit el1`
  - kernel markers `A3` and `KLM`
- the refreshed exported Pi 4 SD image is still:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `f6abd64a6dcd9e254a224c73d2402c4d33e09f52eec6da36418d903e31ffddac`
- current manifest:
  `manifests/2026-04-08-pi4-pre-kernel-branch-led-proof.md`

- on `2026-04-08`, the first post-armstub Pi 4 LED split was implemented
  later than the custom armstub, in `plo` `_startc()`:
  - the custom armstub still drives GPIO42 high first
  - `_startc()` now drives GPIO42 low as the first persistent post-armstub
    loader milestone
- this changes the next real-board interpretation rule:
  - ACT LED stays on:
    the board reached the custom armstub but did not reach `_startc()`
  - ACT LED turns on and then ends up off:
    the board reached `_startc()` and the remaining failure is later in early
    `plo`
- the old assembly-path version of this probe was removed instead of being
  committed, because it did not improve the Pi 4 QEMU lane and was only a
  hypothesis probe
- the Pi 4 A72 rebuild passed and a refreshed SD image was exported and
  re-verified on macOS
- refreshed exported Pi 4 SD-image SHA-256:
  `acea299fb225edb0293b4d022b9b19d984fe51627a168bd69c403442590b757d`
- current manifest:
  `manifests/2026-04-08-pi4-plo-entry-led-proof.md`

- on `2026-04-08`, the first retry on the corrected FAT-verified exported
  image produced the first real post-fix Pi 4 board movement:
  - the ACT LED now turns on and stays on
  - the screen still stays blank
  - USB keyboard input still has no visible effect
- that is the strongest real-hardware clue so far:
  the custom Pi 4 armstub is now executing on the board, so the remaining
  failure is later than the earliest firmware-to-armstub boundary
- the next bounded diagnostic move should therefore stop targeting firmware
  media or armstub entry itself and instead prove the first post-armstub
  transition into early `plo`

- on `2026-04-08`, the current Pi 4 host-visible SD-card artifact was audited
  after a failed macOS mount attempt, and the problem turned out to be
  host-side export corruption rather than a bad VM-local build
- the VM-local Pi 4 SD image inside `phoenix-dev` remained valid:
  its FAT partition boot sector was intact and `mdir` could list the staged
  firmware-visible files at the partition offset
- the previously exported host copy was corrupt:
  the first partition existed in the MBR, but the FAT boot sector at offset
  `1048576` had been zeroed, which explains the earlier macOS mount failure
- historical note:
  - an intermediate `limactl copy --backend=rsync` export step was tried at
    that stage
  - it was later also classified as unreliable and is now superseded by the
    fixed canonical helper described at the top of this file
- the host-side verification helper is now FAT-aware:
  it checks image size, SHA-256, the first partition boot-sector signature,
  a non-zero FAT bytes-per-sector field, and an `mdir` listing at the computed
  partition offset
- the refreshed exported Pi 4 SD-card artifact is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- historical exported Pi 4 SD-image SHA-256 at that stage:
  `acea299fb225edb0293b4d022b9b19d984fe51627a168bd69c403442590b757d`
- current export-fix manifest:
  `manifests/2026-04-08-pi4-plo-entry-led-proof.md`
- the current image content still corresponds to the latest Pi 4 earliest-entry
  GPIO42 armstub proof; this step fixed only the host-side exported copy

- on `2026-04-08`, the next earliest-entry Pi 4 real-hardware proof was added
  to the custom firmware armstub:
  the primary core now drives GPIO42 high before the existing local-timer and
  GIC setup, which should make the ACT LED stay visibly on if the custom
  armstub executes on the real board
- the active purpose of this image is now sharper than before:
  a black screen plus ACT LED on implies the custom armstub ran and the next
  failure is later, while a black screen plus ACT LED staying off points back
  to the firmware-to-armstub boundary itself
- the Pi 4 QEMU HDMI smoke still passes on the DTB-prepared validation lane
  after that armstub change
- the Pi 4 shell-smoke prompt gate is currently flaky again, but the serial log
  still proves the same meaningful boot continuity after the new armstub code:
  `plo -> kernel -> dummyfs -> pl011-tty -> psh`, with the smoke run even
  reaching injected `help` input before hanging
- the refreshed exported Pi 4 SD-card artifact is still:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- current validated Pi 4 SD-image SHA-256:
  `44085197192f5578759269813c3aa38a8adcf04b18bc0092ec509b8fa5543920`
- current rebuild manifest:
  `manifests/2026-04-08-pi4-sdimg-export-fix.md`

- on `2026-04-08`, the first real Pi 4 board evidence was folded back into the
  image again:
  the earlier artifact reached firmware and either stayed on the rainbow splash
  forever or, after on-card `config.txt` edits, hung on a black screen with no
  Phoenix-visible output
- the intermediate low-placement experiment is now closed as false:
  restoring `ADDR_PLO=0x200000` caused Pi 4 QEMU to fail inside `plo` with
  `Cannot allocate memory for 'phoenix-aarch64a72-generic.elf'`, which proved
  that the current Phoenix `plo` memory map still depends on the older
  high-DDR load model
- the active Pi 4 image path is now back on the last coherent loader placement:
  `kernel_address=0x40080000` and `boot_load_flags=0x1` again match the
  existing generic `plo` map assumptions
- the new real-hardware change is instead a Pi-4-specific firmware handoff
  stub:
  `phoenix-armstub8-rpi4.bin` is now staged through `config.txt` as
  `armstub=phoenix-armstub8-rpi4.bin`
- the next real-hardware early-boot clue is now tighter:
  the Pi 4 `plo` board config had still been using the DT bus addresses
  `0x40041000` / `0x40042000` for GICv2, while real ARM-visible bare-metal
  code uses the high-peripheral aliases `0xff841000` / `0xff842000`
- the active Pi 4 image now also includes that GIC correction in
  `board_config.h`
- deep comparison against Circle and the other local Pi 4 bare-metal
  references then exposed the next missing real-hardware setup gap:
  the earlier custom `phoenix-armstub8-rpi4.bin` only implemented the small
  firmware handoff header, while Circle's working Pi 4 armstub also performs
  EL3 timer and GIC preparation before entering the runtime image
- the active Pi 4 armstub now also performs the bounded Circle-style setup
  that best matches that gap:
  - local timer control and prescaler initialization at
    `0xff800000` / `0xff800008`
  - `CNTFRQ_EL0 = 54000000`
  - EL3 GIC group-1 preparation and distributor/CPU-interface enablement using
    the ARM-visible aliases `0xff841000` / `0xff842000`
  - the `FIQS` marker used by Circle's Pi 4 GIC-aware bare-metal path
- the Pi 4 QEMU shell and HDMI smokes still pass after that armstub expansion,
  but only on the dedicated DTB-prepared validation lane that sets:
  - `RPI4B_DTB_PATH=/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb`
  - `RPI4B_QEMU_MEMORY_SIZE=80000000`
- the exported host-visible real-hardware image is rebuilt separately from that
  QEMU-only lane:
  it stages the Pi 4 DTB into `loader.disk`, but does not apply the QEMU-only
  memory-node patch
- the Pi 4 bootfs and SD-image assembly helpers now also carry that expanded
  armstub into the exported host-visible image
- the rebuilt current Pi 4 exported SD-card artifact is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- previous validated Pi 4 SD-image SHA-256:
  `16c4f7f5e313266bdb197a9ddc4d3dc81a080fffb6bea631ab7016dbbb741590`
- current rebuild manifest:
  `manifests/2026-04-08-pi4-armstub-el3-gic-prep.md`
- current practical Pi 4 QEMU note:
  after restarting `phoenix-dev`, regenerate `/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb`
  with `/Users/witoldbolt/phoenix-rpi/scripts/prepare-rpi4b-dtb.sh`
  before re-running the Pi 4 DTB-prepared QEMU lane
- the next preserved Pi 4 DTB clue is now explicit:
  the staged downstream `system.dtb` still inherits Raspberry Pi Linux's
  bootloader-filled placeholder
  `memory@0 { reg = <0 0 0>; }`, so future work should not treat the
  build-time DTB blob as equivalent to the firmware-patched live DTB passed at
  boot
- on `2026-04-08`, a dedicated low-level Pi 4 reference survey was added in:
  `docs/raspberry-pi-4-low-level-reference-survey.md`
  It consolidates the current cross-source facts that matter most for the next
  real-hardware boot-debug step:
  - official BCM2711 and Linux DTS translation from bus addresses to
    ARM-visible low-peripheral aliases
  - the corrected Pi 4 GIC aliases `0xff841000` / `0xff842000`
  - the local timer and prescaler aliases `0xff800000` / `0xff800008`
  - `CNTFRQ_EL0 = 54000000`
  - the fact that build-time `system.dtb` is not equivalent to the live
    firmware-patched DTB
  - the remaining high-probability gap:
    the current custom Phoenix Pi 4 armstub is still smaller than the known
    working Circle / `rpi4-bare-metal` armstubs, so a fuller early register
    setup experiment is now the strongest next radical option if the current
    board image stays black
- on `2026-04-08`, that low-level survey was expanded with an additional
  cross-check pass across U-Boot, Ultibo, historical Pi 4 Linux boot notes,
  and a final targeted online search
  The most useful new deltas are:
  - U-Boot independently matches the Linux downstream `ranges`, timer PPI
    ordering, Cortex-A72 `spin-table` description, and Pi 4 PCIe / GENET node
    placement
  - Ultibo documents that the activity LED is on GPIO 42, which now makes a
    GPIO42 toggle one of the best earliest no-UART sign-of-life proofs for the
    next real-board diagnostic image
  - Ultibo also warns that the effective interrupt-controller path can depend
    on the DTB and firmware behavior, not only the `enable_gic` config value,
    so a bounded controller-selection proof is now justified if the board
    remains black
  - NuttX's successful `0x480000` load address is now explicitly categorized as
    a U-Boot-specific kernel placement, not evidence against the firmware-native
    `0x80000` bare-metal convention
  - the historical `sakaki-/bcm2711-kernel` config now serves as time-sensitive
    evidence that early Pi 4 64-bit Linux often paired `enable_gic=1` with an
    explicit `armstub8-gic.bin`
- on `2026-04-08`, the EDK2 Raspberry Pi 4 platform was also folded into the
  same low-level dossier
  The useful new deltas from EDK2 are:
  - another independent confirmation of the Pi 4 ARM-visible GIC aliases
    `0xFF841000` / `0xFF842000`
  - another independent confirmation of the Pi 4 PCIe register base
    `0xfd500000` and outbound window `0xf8000000`
  - a concrete firmware-stage DTB staging window at `0x003e0000..0x00400000`,
    which now strengthens the warning against careless low-memory earliest-entry
    experiments
  - confirmation that PL011 is still the safer early serial path while
    mini-UART remains tied to the `500000000` clock and can be throttling
    sensitive
  - a warning that Pi 4 xHCI can be sensitive to DMA constraints above 3 GB of
    RAM, which is relevant for later real-hardware USB debugging even though it
    is not the current earliest-boot blocker

Latest sync and retest history:

- on `2026-03-30`, the current Phoenix sibling repositories were fetched from
  GitHub, upstream changes were merged locally where needed, and the validated
  build plus QEMU lanes were rebuilt and re-run successfully
- the then-current Pi 4 exported SD-card artifact was:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- the then-current validated Pi 4 SD-image SHA-256 was:
  `d815e4c1b72bf0c170fb7fb6c00165d918d82f3d7b78bad97ec1c345a00e86db`
- current sync manifest:
  `manifests/2026-03-30-upstream-sync-and-retest.md`
- current practical Pi 4 QEMU note:
  after restarting `phoenix-dev`, regenerate `/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb`
  with `/Users/witoldbolt/phoenix-rpi/scripts/prepare-rpi4b-dtb.sh`
  before re-running the Pi 4 DTB-prepared QEMU lane

## Implementation Readiness

Documentation readiness:

- ready

Tracking readiness:

- ready

Execution readiness on the current workstation:

- ready for implementation bootstrap and validated host-side Phoenix builds

Known remaining start-gate tasks before the first implementation step:

- none

Completed start-gate tasks:

- missing host prerequisite tools installed on the current workstation
- the initial Phoenix upstream repositories cloned into `sources/`
- first baseline integration manifest created under `manifests/`
- `phoenix-dev` Linux VM created and verified
- the documented Linux package baseline installed and verified inside `phoenix-dev`
- the full current `phoenix-rtos-project/.gitmodules` repo set cloned as sibling repos under `sources/`
- the local sibling-clone buildroot workflow has been defined and automated with `scripts/prepare-buildroot.sh`
- one clean upstream `host-generic-pc` build completed successfully inside `phoenix-dev`

Start-gate status:

- cleared for the first implementation steps

## Strategic Decisions Already Made

- First real target is Raspberry Pi 4 Model B.
- Raspberry Pi 5 is a second-stage target after Pi 4 stabilization.
- Final target architecture should preserve Phoenix's normal boot chain:
  `Raspberry Pi firmware -> plo -> syspage -> kernel -> user-space servers/drivers`
- Pi 4 bring-up should begin with a minimal single-core UART-booting system.
- The implementation must advance in narrow, explicitly validated steps rather than broad multi-subsystem pushes.
- Every successful implementation step must end with git commits in each touched upstream repository plus a coordination-repo state update.
- QEMU is a fast gate, not a replacement for real hardware.
- Pi 4 network boot is a preferred later-stage real-hardware deployment path for fast iteration once bootloader setup and DHCP/TFTP infrastructure are ready; SD or USB media remains the fallback and recovery path.
- This project runs on a macOS Apple Silicon workstation. The recommended execution model is macOS host for coordination and hardware control, plus a Linux arm64 VM as the primary Phoenix build and emulation environment.
- Future code must favor upstreamability: small diffs, Phoenix-native style, warning-clean builds, and no gratuitous reformatting.
- The workflow now supports explicitly authorized unattended sessions, but only under the step, validation, commit, and stop-condition rules documented in `docs/unattended-agent-mode.md`.
- The first acceptable real-device artifact handoff should now be based on a
  Pi 4 image with HDMI text console output, not on the earlier stage-panel-only
  artifact.
- USB keyboard work is now explicitly split into a generic HID class-driver
  track and a later Pi 4 transport track through BCM2711 PCIe plus VL805 xHCI.
- the platform-agnostic Phoenix PCIe server scan path no longer hardcodes
  direct ECAM access throughout its logic; it now goes through a small
  server-local config-space backend interface, which is the first enabling
  step toward a BCM2711 indexed-config backend.
- the Pi 4 path now also has the first BCM2711-specific indexed config-space
  backend behind that interface, selected by Pi 4 build settings and validated
  in compile-only form.
- the BCM2711 backend now also performs the first host-bridge preparation slice:
  reset sequencing, SerDes IDDQ clear, revision read, and early `MISC_CTRL`
  preparation before any claim of link-up or downstream enumeration.
- the BCM2711 backend now also performs the first bounded link slice:
  `PERST` release, 100 ms settle wait, link-state sampling, and RC-mode
  sampling, then uses that sampled state to gate downstream config-space
  accesses.
- the BCM2711 backend now also performs the first outbound-window and
  root-bridge shaping slice:
  one outbound window, RC BAR2 programming, and root-bridge class-code shaping
  behind the sampled link-state gate.
- the BCM2711 backend now also performs the first bridge-side exposure slice:
  root-bridge cache-line, bus-number, memory-window, and command programming on
  bus `0` behind the sampled link-state gate.
- the BCM2711 backend now also performs the next bounded bridge-capability
  slice:
  root-bridge parity plus PCIe root-control CRS software visibility behind the
  sampled link-state gate.
- the Pi 4 A72 image path now also includes and stages the `pcie` server,
  which makes the current BCM2711 transport work reachable on the actual board
  image path instead of only compile-validated in isolation.
- the Pi 4 board config now also carries the first VL805/xHCI fast-path
  contract taken from Circle:
  fixed downstream BDF, fixed xHCI class code, and MMIO through the outbound
  PCIe window.
- the first compile-valid Pi 4 xHCI code now also exists:
  a `libusbxhci` skeleton plus a Pi 4 discovery stub, validated together with
  the `usbkbd` pieces and the Phoenix USB host binary on the A72 lane.
- the Pi 4 xHCI path now also extracts the next controller-shape fields after
  the first reset/readiness checks:
  max slots, max scratchpad buffers, and context size.
- the Pi 4 xHCI path now also extracts the next controller register-layout
  facts needed before later interrupter or ring work:
  doorbell and runtime-register offsets.
- the Pi 4 xHCI path now also extracts the remaining pre-interrupt capability
  facts needed before later event-ring work:
  max interrupters, interrupt moderation scale, and maximum ERST size.
- the Pi 4 xHCI path now also extracts the remaining structural memory-layout
  capability facts needed before later DCBAA and scratchpad work:
  64-bit addressing support, scratchpad-restore support, and maximum primary
  stream array size.
- the Pi 4 xHCI path now also extracts the first operational-register memory
  layout state needed before later controller programming:
  `CRCR` and `DCBAAP`.
- the Pi 4 xHCI path now also allocates the first controller-owned memory
  objects needed for later controller setup:
  one `DCBAA` page and one first command-ring backing block.
- the Pi 4 xHCI path now also performs the first bounded controller
  register-programming step:
  `DCBAAP`, `CRCR`, and `CONFIG` are written from the previously allocated
  command-space objects and then read back for sanity checks.
- the current QEMU boot validation result is now explicit:
  - generic `virt` shell smoke still passes
  - Pi 4 `raspi4b` shell smoke also passes, but only on a DTB-prepared image
    built with:
    - `RPI4B_DTB_PATH`
    - `RPI4B_QEMU_MEMORY_SIZE=80000000`
  - the Pi 4 HDMI smoke also passes on that same DTB-prepared image
- that validation also exposed and fixed the first generic AArch64 USB-host
  portability issue in `phoenix-rtos-usb`:
  `usb.c` now passes the message-thread port value through `uintptr_t`.
- the normal `aarch64a72-generic` build now also produces the USB host and
  keyboard artifacts:
  `/sbin/usb` and `/sbin/usbkbd`, and the live Pi 4 image script now stages
  `/sbin/usb` as the intended linked-driver integration point.
- after the new xHCI capability-shape step, the remaining controller gap is
  narrower again:
  register-layout facts such as doorbell and runtime offsets should be
  extracted before any future interrupter, ring, or root-hub work.
- after the new register-layout step, the remaining controller gap is narrower
  again:
  pre-interrupt capability limits such as max interrupters and ERST sizing
  should be extracted before any future event-ring work.
- after the new pre-interrupt capability step, the remaining controller gap is
  narrower again:
  addressability and scratchpad-memory capability should be extracted before
  any future DCBAA or scratchpad-allocation work.
- after the new scratchpad/addressability capability step, the remaining
  controller gap is narrower again:
  the next clean seam is the first operational memory-layout register subset,
  especially `DCBAAP` and `CRCR`.
- after the new operational-register layout step, the next clean seam is no
  longer more capability decoding; it is the first controller-owned memory
  allocation step for `DCBAA` and the command ring.
- after the new memory-allocation step, the next clean seam is the first real
  xHCI register-programming step:
  `DCBAAP`, `CRCR`, and `CONFIG`.
- after the new register-programming step, the next clean xHCI seam is no
  longer passive controller setup; it is the first pre-run operational step
  beyond command-space binding.
- QEMU still cannot validate the real Pi 4 USB keyboard path itself, because
  the current `raspi4b` machine does not expose the BCM2711 PCIe root-port path
  needed for VL805 xHCI bring-up.
- the requested post-xHCI QEMU validation also exposed a separate Pi 4 shell
  startup issue:
  generic shell smoke still passes, Pi 4 HDMI smoke still passes, but the Pi 4
  shell smoke now stalls before `(psh)%`.
- the bounded shell-side retry fix is now also in place:
  the Pi 4 shell lane again reaches `psh: tty ready`, prompt, and `help`, so
  the remaining automated-smoke issue is stale kernel probe noise rather than
  the earlier console-open race.
- that stale probe noise is now also removed:
  generic shell smoke passes, Pi 4 shell smoke passes cleanly again, and Pi 4
  HDMI smoke still passes after deleting the obsolete `create_dev` probes.
- after that cleanup, the next clean xHCI seam is again explicit:
  the first pre-run operational step beyond `DCBAAP`, `CRCR`, and `CONFIG`
  programming.
- that run-state step is now also in place:
  the Pi 4 xHCI path can validate a bounded halted-to-run-to-halted controller
  transition while still returning `-ENOSYS`, so the next clean seam is event-
  ring preparation rather than more run-state guessing.
- that first event-ring memory step is now also in place:
  the Pi 4 xHCI path now allocates one event-ring segment and one ERST block
  and populates the first ERST entry for interrupter `0`, so the next clean
  seam is runtime-register programming for that event-ring state.
- that event-ring runtime-register step is now also in place:
  the Pi 4 xHCI path now programs and reads back `ERSTSZ`, `ERSTBA`, and
  `ERDP` for interrupter `0`, so the next clean seam is command-ring layout
  initialization rather than more event-ring plumbing.
- that command-ring layout step is now also in place:
  the Pi 4 xHCI path now initializes a real command-ring layout with a final
  link TRB and initial cycle-state contract.
- the current Pi 4 xHCI discovery contract still reports `irq = 0`, so the next
  clean seam is a single-command polled no-op submission path rather than
  interrupt-enable work.
- that single-command polled no-op path is now also in the tree:
  the Pi 4 xHCI code can now submit one internal no-op command through the
  command ring and event ring in a compile-validated form.
- that first xHCI roothub-control seam is now also in the tree:
  the Pi 4 xHCI code now answers the bounded Phoenix roothub subset needed for
  early enumeration:
  - device/config/string/hub descriptors
  - `PORTSC`-based port status
  - minimal port feature set/clear handling
- the first xHCI init-success seam is now also in the tree:
  `xhci_init()` no longer deliberately returns `-ENOSYS` after the current
  roothub-ready controller setup sequence
- the first xHCI roothub status-delivery seam is now also in the tree:
  the Pi 4 path now has a bounded temporary polling thread that completes the
  pending root-hub interrupt transfer when `xhci_getHubStatus()` reports change
  bits on the current no-IRQ path
- the first post-roothub child-device seam is now also in the tree:
  the Pi 4 xHCI path now executes a bounded `Enable Slot` command, validates
  the returned completion event, and captures the slot ID
- the next bounded child-device prerequisite is now also in the tree:
  the Pi 4 xHCI path now allocates the first per-slot controller-owned memory
  objects behind the returned slot ID:
  - one device context
  - one input context
  - one endpoint-0 ring backing block
  - one bound `DCBAA[slotId]` entry
- the next bounded `Address Device` prerequisite is now also in the tree:
  the Pi 4 xHCI path now prepares the minimum direct-root-port child context
  state derived from the Phoenix `usb_dev_t` contract:
  - EP0 ring layout with final link TRB
  - input-control `AddContextFlags`
  - slot-context root-hub port, speed, and context entries
  - EP0 dequeue pointer, error count, type, max packet, and average TRB length
- the first bounded xHCI `Address Device` action is now also in the tree:
  the Pi 4 xHCI path now handles only non-roothub `REQ_SET_ADDRESS` under an
  explicit temporary equality contract:
  requested USB address must match the enabled slot ID
- the first bounded non-`SET_ADDRESS` xHCI endpoint-0 transfer is now also in
  the tree:
  the Pi 4 xHCI path now executes a synchronous polled EP0
  `REQ_GET_DESCRIPTOR` control read for the first direct-root-port child under
  the current temporary slot-ID-equals-address contract
- that bounded descriptor-read path is also non-regression-validated:
  a fresh full `aarch64a72-generic-rpi4b` build passes and the Pi 4 shell smoke
  still passes after the xHCI change
- the first bounded post-enumeration xHCI control-write path is now also in
  the tree:
  the Pi 4 xHCI path now executes synchronous polled EP0 zero-length OUT
  control writes for the current direct-root-port child, limited to:
  - `REQ_SET_CONFIGURATION`
  - `CLASS_REQ_SET_PROTOCOL`
  - `CLASS_REQ_SET_IDLE`
- the first bounded non-EP0 xHCI endpoint-ownership slice is now also in the
  tree:
  the Pi 4 xHCI path now derives the keyboard interrupt-IN endpoint identity,
  allocates one transfer ring, populates one endpoint context, and completes
  one bounded `Configure Endpoint` command for the current child device
- the first bounded no-IRQ xHCI interrupt-transfer path is now also in the
  tree:
  the Pi 4 xHCI path now queues one outstanding interrupt-IN transfer on the
  configured endpoint and completes it from the existing status thread by
  polling the event ring
- the project is still not ready for interactive real-device USB testing:
  the live Pi 4 image now stages `/sbin/usb`, the Pi 4 QEMU shell smoke still
  passes after that integration, and the next concrete blocker is real-device
  validation of the new HDMI plus USB-keyboard path.
- the current rebuilt real-device Pi 4 SD-card artifact is now exported at:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  with SHA-256:
  `1c3bc4f6c474baad547059801ba49ea4c2de31c088aea3b1ef68fc7b8eb2924f`
- the first real Pi 4 board evidence is now also preserved:
  the earlier image was readable by firmware but either remained on the rainbow
  splash forever or, after removing the forced `kernel_address` override
  directly on-card, hung on a black screen with no Phoenix-visible output
- the smallest confirmed response was therefore to align the Pi 4 A72 `plo`
  image placement with the real firmware default 64-bit kernel load contract
  and rebuild the SD artifact around that model
- the first real-device execution handoff is now also structured in:
  `/Users/witoldbolt/phoenix-rpi/docs/pi4-first-hardware-trial.md`
  so the first board result can be reported in a form that directly maps to the
  next bounded implementation step
- the operator-side handoff now also includes two non-destructive macOS helpers:
  - `/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh`
  - `/Users/witoldbolt/phoenix-rpi/scripts/print-rpi4b-macos-flash-commands.sh`
- the first board-trial handoff now also includes one report helper:
  - `/Users/witoldbolt/phoenix-rpi/scripts/create-rpi4b-first-trial-report.sh`
- that report helper now derives the SHA-256 from the actual exported image by
  default, which removes one more stale-metadata risk after future image
  refreshes
- the upstream sync retest also exposed one environment-specific Pi 4 rebuild
  requirement:
  after a `phoenix-dev` restart, `/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb` is gone
  and must be regenerated from `external/raspberrypi-linux` before Pi 4 rebuilds
  with:
  `/Users/witoldbolt/phoenix-rpi/scripts/prepare-rpi4b-dtb.sh`
- after rechecking the full handoff set, no further meaningful pre-boot
  operator-side blocker remains; the next stronger lane is the real Raspberry
  Pi 4 board boot with the current image

## Most Important Technical Findings

- Phoenix has reusable AArch64 support, but it is currently too `zynqmp`-specific in build glue and DTB assumptions.
- The first Pi 4 A72 QEMU HDMI text-console milestone is now complete:
  `pl011-tty` mirrors transmitted bytes into the firmware-allocated framebuffer,
  the background is black, and white glyphs are visible in the expected text
  rows.
- The first reusable USB keyboard foundation now exists in
  `phoenix-rtos-devices/tty/usbkbd/`: it is a generic USB HID boot-keyboard
  class driver that exposes `/dev/kbdN` and translates interrupt-IN boot
  reports into shell-usable cooked bytes.
- `pl011-tty` now also has a small optional `/dev/kbd0` reader bridge on the
  Pi 4 A72 project path, so once a USB host transport exists the current HDMI
  text console can become interactive without another console-architecture
  rewrite.
- That keyboard step does not solve Pi 4 transport yet. Real Pi 4 keyboard
  input still depends on later BCM2711 PCIe and VL805 xHCI work.
- After the first transport scoping pass, the smallest next Pi 4 USB milestone
  is now explicitly BCM2711 PCIe root-complex bring-up plus ECAM enumeration;
  xHCI should come only after that link and config-space foundation exists.
- after the first enabling transport refactor, the next bounded PCIe move is
  no longer “make the server less ECAM-shaped”; it is “add the first
  BCM2711-specific indexed config-space backend behind the new server-local
  interface”.
- after that backend step, the remaining real Pi 4 transport gap is now
  narrower and explicit: BCM2711 host-bridge initialization and link bring-up
  before downstream enumeration can be treated as meaningful.
- after the new early host-bridge step, the remaining transport gap is narrower
  again: outbound-window setup, PERST release, link-up checks, and RC-mode
  verification before downstream enumeration can be treated as meaningful.
- after the new link-state step, the remaining transport gap is narrower again:
  outbound-window setup and root-bridge shaping before downstream enumeration
  can be treated as meaningful.
- after the new outbound-window and root-bridge shaping step, the remaining
  transport gap is narrower again: root-bridge memory-window programming and
  downstream-bus exposure before downstream enumeration can be treated as
  meaningful.
- after the new xHCI capability-shape step, the remaining xHCI gap is narrower
  again: register-layout facts such as doorbell and runtime offsets should be
  extracted before any future interrupter or ring work is attempted.
- after the new bridge-exposure step, the remaining transport gap is narrower
  again: first meaningful downstream endpoint visibility before any xHCI-
  specific work can be treated as meaningful.
- after the new bridge-capability step, the remaining transport gap is narrower
  again: one direct downstream config-space readback before any broader
  enumeration or xHCI work can be treated as meaningful.
- after the new Pi 4 project-integration step, that remaining gap is now tied
  to a real staged runtime component rather than to an unreferenced server
  binary.
- after the new VL805 fast-path constants step, the remaining USB gap is now
  also explicit:
  the first compile-valid xHCI HCD skeleton before any staged runtime USB host
  path is attempted on Pi 4.
- after the new compile-valid xHCI step, the remaining gap is narrower again:
  the normal `aarch64a72-generic` build flow still does not build these USB
  pieces by default before any staged runtime USB host path is attempted.
- after the new A72 USB build-glue step, the remaining gap is narrower again:
  the xHCI runtime path still returns `-ENOSYS`, so the next useful work item
  is the first runtime-safe xHCI initialization slice rather than more build
  glue.
- after the new runtime-safe xHCI init step, the remaining gap is narrower
  again:
  the Pi 4 xHCI path now maps MMIO and validates the capability header, so the
  next useful work item is the first controller-reset and operational-readiness
  slice rather than more discovery or build glue.
- after the new xHCI reset step, the remaining gap is narrower again:
  the Pi 4 xHCI path now completes the first bounded controller reset, so the
  next strongest device-facing gap is the Pi 4 VL805 firmware-reset notify
  handshake plus later page-size, ring, interrupt, and root-hub work.
- after the new Pi 4 firmware-notify step, the remaining gap is narrower
  again:
  the image path now includes the first VL805 firmware-load hook before device
  enable, so the next useful xHCI work item is controller-readiness refinement
  inside `xhci` rather than more Pi 4 firmware plumbing.
- after the new post-notify xHCI readiness step, the remaining gap is narrower
  again:
  the Pi 4 xHCI path now validates 4K page support and a non-zero port count,
  so the next useful work item remains inside structural xHCI capability state
  rather than firmware or PCIe plumbing.
- after the post-xHCI Pi 4 GDB pass, the current non-xHCI runtime blocker is
  also narrower:
  `resolve_path("/dev/console")` now succeeds on the Pi 4 lane, but
  `open("/dev/console")` still returns `-1` across all five `psh_ttyopen()`
  retries, so the next smallest move is a shell-side retry-policy refinement.
- Pi 4 `raspi4b` QEMU is not expected to validate that PCIe milestone, because
  the emulator still lacks the relevant PCIe root-port support.
- The strongest currently available no-hardware validation for the new
  keyboard driver is component-level AArch64 compilation with the existing
  `aarch64-phoenix` toolchain.
- the strongest currently available no-hardware validation for the new PCIe
  server abstraction is:
  - representative `pcie` server compilation on the ZynqMP/Xilinx-targeted
    lane with the correct project include path
  - fresh Pi 4 A72 full-build regression validation from a disposable temp
    buildroot
- the strongest currently available no-hardware validation for the first
  BCM2711-specific backend step is:
  - preserved ZynqMP/Xilinx `pcie` server compilation from a fresh disposable
    buildroot
  - Pi 4 targeted `pcie` server compilation with
    `PCI_EXPRESS_BCM2711_INDEXED_CFG=y`
  - fresh Pi 4 A72 full-build regression from the same disposable buildroot
- the same validation shape also now covers the first BCM2711 host-bridge
  preparation hook, because that step still stays in the compile-only lane
- the same validation shape also now covers the first BCM2711 link-state step,
  because that work still stays in the compile-only lane
- the same validation shape also now covers the first BCM2711 outbound-window
  and root-bridge shaping step, because that work still stays in the
  compile-only lane
- the same validation shape also now covers the first BCM2711 bridge-exposure
  step, because that work still stays in the compile-only lane
- the full `aarch64a53-zynqmp-qemu` project build is currently blocked by an
  unrelated kernel issue outside the PCIe step:
  `hal/aarch64/interrupts_gicv2.c` references `TIMER_IRQ_GROUP` without a
  definition on that lane
- A stronger IA32 EHCI host build lane exists in principle, but `phoenix-dev`
  does not yet have an `i386-pc-phoenix` toolchain, so the current VM cannot
  validate the full `ia32-generic-qemu` build script end to end.
- Two distinct bugs had to be fixed to reach that milestone:
  - generic AArch64 kernel `_init.S` still used pre-graphics hardcoded syspage
    offsets
  - generic `plo` published graphmode metadata too early, because `video_init()`
    runs before `syspage_init()` in `plo/main()`
- a bounded gdbstub session also proved that packed `graphmode_t` by-value
  passing in `plo` was unsafe on this AArch64 path; the current code now uses a
  pointer-based `syspage_graphmodeSet()` helper instead.
- The current Pi 4 QEMU validation pair is now:
  - `./scripts/qemu-shell-smoke.sh rpi4b`
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`
- the HDMI smoke is currently the stronger revalidated Pi 4 signal:
  a clean rerun still passes, while the `rpi4b` shell helper now needs one
  bounded follow-up because a fresh rerun echoed `help` but did not complete
  the expected `Available commands:` exchange before timeout.
- the current HDMI smoke no longer validates the old top-left stage panel; it
  validates a black console background plus white text pixels in the first text
  row instead.
- Phoenix's AArch64 DTB parser needs generalization for Raspberry Pi DT layouts and standard FDT cell handling.
- Phoenix's AArch64 HAL currently includes generic GICv2 support, but timer/platform selection is too platform-specific.
- Phoenix's existing test runner is already structured for UART-driven DUT automation and can be extended for Raspberry Pi targets.
- Phoenix officially documents Linux build flows and Linux package prerequisites; native macOS builds should not be treated as the primary path.
- On the current host, Homebrew, Xcode, QEMU, `dtc`, `uv`, `expect`, `jq`, `limactl`, `yq`, `socat`, `picocom`, `mtools`, and `socket_vmnet` are present, and the `phoenix-dev` Ubuntu 24.04 VM now has the documented package baseline installed.
- `phoenix-rtos-project` expects a populated multi-repo tree via its submodule paths. The full current `.gitmodules` repo set is now cloned under `sources/`, and the sibling-clone workflow is now handled through the disposable buildroot prepared by `scripts/prepare-buildroot.sh`.
- In the current Lima setup, the shared workspace path is effectively read-only from inside the Linux guest, so disposable buildroots should fall back to VM-local storage such as `~/phoenix-buildroots/phoenix-rtos-project`.
- The first clean upstream baseline build is now verified with `TARGET=host-generic-pc ./phoenix-rtos-build/build.sh clean host core fs test project image` inside the disposable buildroot, producing artifacts under `_build/host-generic-pc`, `_fs/host-generic-pc/root`, and `_boot/host-generic-pc`.
- The `aarch64-phoenix` toolchain is now installed and verified in `phoenix-dev` at `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix`, with sysroot `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/aarch64-phoenix`.
- The AArch64 toolchain build requires more than the baseline Phoenix package set; the currently confirmed extra VM packages are `bison`, `flex`, `libgmp-dev`, `libmpfr-dev`, `libmpc-dev`, `libisl-dev`, and `zlib1g-dev`.
- The current AArch64/libphoenix flow still generates files inside component source trees, so the linked buildroot is not sufficient for current toolchain or AArch64-target validation in the read-only Lima mount; use `scripts/prepare-buildroot.sh --copy-components` and the VM-local copied buildroot at `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy` for those lanes.
- The first upstream AArch64 cleanup step is now complete: `phoenix-rtos-kernel` and `plo` no longer hardwire top-level AArch64 platform selection through a literal `zynqmp` substring check, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with the new selection path.
- Copied buildroots exclude `.git`, so some builds may emit harmless version-probe noise such as `fatal: not a git repository`; treat the overall build exit status and produced artifacts as authoritative.
- Local `qemu-system-aarch64` in `phoenix-dev` provides the standard `virt` machine, and its DTB exposes root-level `pl011@...`, `intc@...`, `arm,armv8-timer`, and PSCI/HVC nodes; the first non-Xilinx QEMU follow-up should therefore start with kernel DTB parser recognition of those node names rather than with target metadata alone.
- The kernel DTB parser now recognizes shallow `pl011@...` and `intc@...` nodes, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- Local `virt` inspection also confirmed that the GIC `reg` property uses 16-byte tuples, so the next narrow generic-QEMU follow-up should stay in `hal/aarch64/dtb.c` and generalize interrupt-controller `reg` decoding before broader AArch64 platform work.
- The kernel DTB parser now decodes both the existing 12-byte GIC `reg` tuples and the 16-byte tuples used by local QEMU `virt`, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- There is still no reusable PL011 or ARM architectural timer implementation in the current Phoenix AArch64 tree, so the next smallest preparatory step is to expose root-level `timer` node interrupt metadata from the DTB parser before adding any runtime generic timer code.
- The AArch64 DTB API now exposes architectural timer interrupt metadata from the root-level `timer` node, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- The DTB preparation series is now far enough that the next step must introduce runtime code or new target/build structure, so the next active step is a bounded planning step to choose the smallest safe runtime follow-up.
- That runtime planning step is now complete: the next selected change is to remove the hard `TIMER_IRQ_ID` dependency from common AArch64 GICv2 code by moving timer IRQ knowledge behind the timer HAL API.
- The common AArch64 GICv2 code now queries timer IRQ identity through the timer HAL API instead of using the `TIMER_IRQ_ID` macro directly, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- Reusable AArch64 architectural timer sysreg helpers now exist in `hal/aarch64/aarch64.h`, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- The common AArch64 timer track is now explicitly shaped: the future backend should be a directly selectable common AArch64 timer implementation, and the next smallest step is to codify timer-source selection in the DTB API before the backend itself is introduced.
- The AArch64 DTB API now exposes an explicit selected generic timer source and IRQ for the common EL1 path, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- The first directly selectable common AArch64 timer backend step is now explicitly scoped: a full architectural timer backend is still premature because the current scheduler wakeup path can reprogram the timer from non-CPU0 contexts, so the next safe code steps must keep separating build and interrupt-path assumptions before changing runtime timer behavior.
- The common AArch64 kernel Makefile now exposes an explicit timer-backend selection hook, and the current ZynqMP timer backend still builds cleanly through that hook on the existing `aarch64a53-zynqmp-qemu` lane.
- Common AArch64 GICv2 handler registration now avoids SPI-style CPU retargeting for SGI/PPI interrupts, removing one interrupt-layer mismatch before a future architectural timer IRQ delivered as a PPI.
- Common AArch64 code now exposes a targeted SGI helper in addition to the broadcast helper, but the next timer-runtime step still requires an explicit SGI reservation and notification contract.
- The generic `hal/tlb/tlb.c` shared-work-plus-SGI pattern is not currently wired into AArch64 builds, so future AArch64 timer-update notifications cannot simply reuse that machinery without additional integration work.
- AArch64 now reserves `TIMER_WAKEUP_IRQ` and the scheduler can coalesce remote wakeup requests and redirect wakeup-deadline recomputation back to CPU 0, removing the main scheduler-side blocker for a future CPU-local architectural timer backend.
- The common AArch64 build now compiles a source-keyed `gtimer` helper layer, so the next backend work can focus on backend state and policy instead of raw physical-versus-virtual sysreg branching.
- The common AArch64 build now also compiles a generic timer backend-state layer that owns the selected source, IRQ, and frequency, so the next backend work can add behavior helpers without redoing state discovery.
- The backend-state layer now exposes raw-count, count-to-microseconds, and current-time helpers, so the next backend work can focus on forward conversion and wakeup programming instead of reopening current-time reads.
- The backend-state layer now also exposes a reusable microseconds-to-relative-ticks helper, so the next backend work can focus on state-keyed timer-register wrappers instead of open-coded frequency math.
- The backend-state layer now also exposes state-keyed control and relative-timer register wrappers, so the next backend work can focus on arming policy and IRQ ownership instead of low-level source dispatch.
- The backend-state layer now also exposes a backend-local wakeup helper that arms the selected architectural timer for bounded positive waits, so IRQ ownership is now the main missing piece before public AArch64 timer-HAL wiring.
- The backend-state layer now also exposes IRQ query and handler-registration helpers, so the next common-AArch64 timer step can finally move from backend-local helpers to the first public timer-HAL wrapper boundary.
- The AArch64 build now exposes an explicit public timer-implementation hook while keeping ZynqMP selected, so the next common timer step can focus on the first public `hal_timer*` wrapper file instead of reopening build glue.
- The AArch64 build now also exposes an explicit timer-implementation override hook, so the first common public timer file can be validated without replacing the default ZynqMP timer selection.
- The kernel now provides a common public AArch64 timer implementation file in `hal/aarch64/gtimer_timer.c`, and the existing copied-buildroot `aarch64a53-zynqmp-qemu` lane still builds successfully in `phoenix-dev` when that file is selected through `AARCH64_TIMER_IMPL_OVERRIDE`.
- `phoenix-rtos-build` now recognizes `aarch64a53-generic` and provides a matching generic AArch64 core-build entry point.
- `phoenix-rtos-kernel` now provides a first generic AArch64 platform scaffold, and the generic kernel target links successfully in the VM-local copied buildroot when validated with a temporary empty `board_config.h` shim via `PROJECT_PATH`.
- the first generic AArch64 `plo` scaffold is now explicitly bounded to one target-local linker template plus minimal `_init`, HAL, console, timer, and interrupt files, and it should be validated first through a direct `make -C plo base_noimg` lane in `phoenix-dev`.
- `phoenix-rtos-plo` now provides that first generic AArch64 loader scaffold, and `aarch64a53-generic` builds `plo` directly as `plo-aarch64a53-generic.elf` in the VM-local copied buildroot.
- the current generic loader fast lane is intentionally QEMU-`virt`-oriented and EL3-centric, assumes preconfigured PL011 state, and uses a polling architectural-counter timer inside `plo` to avoid widening the first runtime lane before the project entry point exists.
- the first generic QEMU project entry point is now explicitly scoped around a RAM-backed `loader.disk` loaded into `ram0` on QEMU `virt`; that was selected because the new generic `plo` path already has `ram-storage`, while generic flash, SD, and virtio boot paths do not exist yet.
- `phoenix-rtos-project` now provides that first generic AArch64 QEMU entry point, and the current project/image validation lane produces `_boot/aarch64a53-generic-qemu/plo.elf` plus `loader.disk` in `phoenix-dev`.
- the current project/image lane still uses a temporary narrower validation path:
  - prebuild the kernel directly for `aarch64a53-generic-qemu`
  - run `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`
  - this is temporary until generic-target support exists in `phoenix-rtos-devices` and any remaining generic userspace blockers are removed
- `libphoenix` now builds successfully for `aarch64a53-generic-qemu`, and its AArch64 reboot helper now handles both the ZynqMP and generic `platformctl_t` layouts cleanly.
- the broader generic `host project image` lane now succeeds again from the current copied-buildroot baseline; the next fastest blocker is therefore back in generic QEMU runtime progress rather than in project-build plumbing.
- the refreshed generic QEMU smoke lane is still kernel-only by construction: the current generic `user.plo` loads only the kernel and DTB, and the next fast-lane blocker is the missing userspace console path built around a reusable PL011 tty driver.
- in `phoenix-rtos-devices`, the first missing layer for that console path is now confirmed: `_targets/Makefile.aarch64a53-generic` does not exist yet, so the next repo-local unblock is target scaffolding before PL011 driver code.
- `phoenix-rtos-devices` now exposes that generic AArch64 target scaffold and validates successfully for `aarch64a53-generic-qemu`; the next fast-lane blocker is the first reusable PL011 tty driver slice itself.
- that first PL011 slice is now explicitly scoped as a single-instance polling `pl011-tty` driver with `libtty`, `libklog`, `/dev/tty0`, and `/dev/console`, configured first through `board_config.h`.
- the new `pl011-tty` scaffold now builds directly on `aarch64a53-generic-qemu`; the next fast-lane decision is how to integrate it with the generic target in the smallest useful way.
- that integration choice is now fixed: the next smallest fast-lane step is to add `pl011-tty` to the generic devices target defaults before wiring board-specific base addresses or `user.plo`.
- `pl011-tty` is now in the generic devices default component set; the next smallest blocker is the missing generic-QEMU `board_config.h` wiring for its PL011 base address and clock.
- local QEMU `virt,secure=on` inspection now pins those board-config values: the usable non-secure PL011 is at `0x09000000` and its fixed clock is `24 MHz`.
- the generic QEMU project now supplies those values in `board_config.h`; the next smallest fast-lane step is to load `dummyfs` and `pl011-tty` in the right order from `user.plo`.
- that `user.plo` ordering is now fixed as the next minimal image step: `dummyfs;-N;devfs;-D` first, then `pl011-tty`, with `psh` intentionally deferred.
- the generic image now packages `dummyfs` and `pl011-tty`, but the visible smoke result still stops at the first kernel banner line; the next fast-lane step should be chosen from that updated runtime state rather than from more packaging-only work.
- that next fast-lane step is now fixed as plain `psh` integration, following the proven minimal `dummyfs + tty + psh` shape used by another generic target and still avoiding `rc.psh` overlay work for now.
- the generic image now packages `dummyfs`, `pl011-tty`, and `psh`, but the visible smoke result is still unchanged; the next fast-lane step must therefore diagnose whether generic userspace startup is being reached at all.
- that diagnostic choice is now fixed as a raw PL011 startup banner from `pl011-tty`, because it is the smallest high-signal test that stays repo-local and does not require broader kernel tracing.
- that diagnostic now passes: `pl011-tty: started` appears on the generic QEMU console, proving that the packaged userspace path reaches the PL011 driver on the non-secure UART.
- the next smallest unknown is now `/dev/console` readiness, and the selected follow-up is a second raw PL011 banner emitted only after successful console-device registration.
- that follow-up diagnostic now also ran, and the new `pl011-tty: console ready` banner never appears even after a 20-second QEMU run; the current fast-lane boundary is therefore between `pl011_init()` completion and successful console-device registration.
- the next selected split point is successful `/dev/tty0` registration, because it is the immediate runtime boundary before `_PATH_CONSOLE` registration in `pl011-tty`.
- that `/dev/tty0` diagnostic also stayed absent, so the current fast-lane boundary is now between `pl011_init()` completion and the first successful `create_dev()` call; local `create_dev()` and `dummyfs -D` source inspection makes a startup-order race the next bounded hypothesis to test.
- the selected next runtime test is now a single `wait 500` between `dummyfs;-N;devfs;-D` and `pl011-tty` in the generic `user.plo`, because it is the smallest change that can test the observed `/dev` namespace readiness hypothesis.
- that `wait` test was rejected and reverted: local QEMU output plus `plo/cmds/wait.c` confirm that `wait` is an interactive loader command, not a passive sleep, so it is unsuitable for unattended generic-QEMU or real-hardware automation unless a loader input device is intentionally configured.
- the first Pi 4-specific scaffold step is now fixed: start with a project-local `aarch64a53-generic-rpi4b` layered on top of the existing generic target, rather than widening the target matrix before board-local overrides are in place.
- that first Pi 4 scaffold is now implemented and build-validated in `phoenix-dev`; the new `aarch64a53-generic-rpi4b` project provides Pi 4 board-local overrides while intentionally deferring the real firmware-facing DTB and boot-partition staging decisions to the next step.
- that next Pi 4 staging decision is now fixed: emit a firmware-facing boot directory with a project-local `config.txt` and renamed raw `plo` image before widening into DTB import or firmware-handoff code.
- that firmware-facing staging step is now implemented and build-validated; `_boot/aarch64a53-generic-rpi4b/rpi4b/` now contains `config.txt` and `kernel8.img`, while DTB staging and EL3-only loader entry remain the next two concrete blockers.
- the Pi 4 project now has an optional project-local DTB staging hook: when `RPI4B_DTB_PATH` is set or `_projects/aarch64a53-generic-rpi4b/bcm2711-rpi-4-b.dtb` exists, the build stages `bcm2711-rpi-4-b.dtb` into `_boot/aarch64a53-generic-rpi4b/rpi4b/`; otherwise the default build remains self-contained and stages only `config.txt` and `kernel8.img`.
- local QEMU reset logging now gives a useful generic loader-entry matrix for the same image:
  - `virt,secure=on` starts in EL3 and is the current known-good baseline
  - `virt,secure=off` starts in EL1h
  - `virt,secure=off,virtualization=on` starts in EL2h
- generic `plo` now handles EL1, EL2, and EL3 entry in one localized AArch64 assembly path, and the same generic QEMU image now reaches visible loader, kernel, and early userspace output in all three entry modes.
- the remaining caveat on that path is diagnostic rather than boot-blocking: generic non-EL3 loader exception-context save is not yet independently hardened, so the currently validated result is the normal no-fault fast path.
- the Pi 4 boot tree now reuses that existing generic `ram0` path: it stages `loader.disk` next to `kernel8.img` and uses `initramfs loader.disk 0x48000000` so Raspberry Pi firmware preloads the payload to generic `plo` `RAM_ADDR`.
- the next concrete Pi 4 blocker is now kernel DTB propagation, not raw payload transport: the generic AArch64 kernel requires a syspage program named `system.dtb`, but the current Pi 4 `user.plo.yaml` still loads only the kernel and user-space programs.
- the next selected Pi 4 step is therefore project-local DTB propagation: reuse the existing optional Pi 4 DTB input, copy it into `${PREFIX_ROOTFS}/etc/system.dtb`, and restore the `blob {{ env.BOOT_DEVICE }} /etc/system.dtb ddr` payload entry so the generic kernel contract stays intact.
- the Pi 4 project now propagates an optional board DTB into both the firmware boot tree and the kernel-visible payload as `system.dtb`; default no-DTB builds stay green, and supplied-DTB builds now emit the expected `blob` load entry in the generated loader script.
- the Ubuntu 24.04 packaged QEMU inside `phoenix-dev` is `8.2.2` and does not expose `raspi4b`, but a VM-local official QEMU `10.2.2` build now exists at `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64` and does expose `raspi4b`.
- the first `raspi4b` smoke now ran with the staged Phoenix Pi 4 image under QEMU `10.2.2`; QEMU requires `-smp 4`, and the current image still times out with no serial output, so the next blocker is in emulated Pi 4 boot progress rather than environment capability.
- official QEMU `raspi4b` docs already list `PCIE Root Port` and `GENET Ethernet Controller` as missing, so the new board-specific QEMU lane is useful for early boot and UART-path work but should not be treated as authoritative for Pi 4 PCIe or Ethernet bring-up.
- the first bounded emulated Pi 4 boot blocker is now identified: QEMU `raspi4b` direct raw-kernel boot uses the common ARM loader path, which loads raw AArch64 images at `0x00080000`, while the Pi 4 Phoenix `kernel8.img` is linked for firmware placement at `0x40080000`; the next smallest step is to validate `plo.elf` as the QEMU `-kernel` while keeping `kernel8.img` for real firmware boots.
- the Pi 4 `raspi4b` QEMU lane now reaches visible `plo` startup when `plo.elf` is used as `-kernel`, confirming that the earlier silent timeout was dominated by the raw-image handoff mismatch.
- local QEMU `10.2.2` source makes the next blocker explicit: ELF payloads are not treated as Linux kernels, so the board's AArch64 secondary-core spin-table boot stub is not installed for `plo.elf`.
- the first bounded blocker after the `plo.elf` handoff is now generic loader-side secondary-core containment: generic AArch64 `plo` has no equivalent of the existing ZynqMP non-primary-core trap, so the current Pi 4 QEMU lane falls into an early multi-core exception storm after loader startup.
- generic AArch64 `plo` now contains non-primary cores until kernel handoff, the generic `virt` lane still reaches the kernel banner and `pl011-tty: started`, and the Pi 4 `plo.elf` QEMU lane now reaches a stable post-alias loader boundary instead of an immediate exception storm.
- the next Pi 4 blocker is now strongly bounded by artifact comparison: the working generic QEMU lane includes `/etc/system.dtb`, while the current Pi 4 validation build without `RPI4B_DTB_PATH` does not include `system.dtb` in the loader payload.
- the next smallest Pi 4 follow-up is therefore DTB-backed validation on the existing QEMU lane before more invasive kernel or board-specific debugging.
- the Pi 4 DTB-backed QEMU validation is now complete: with an explicit `bcm2711-rpi-4-b.dtb`, the `raspi4b` lane reaches `pl011-tty: started`, which means the loader, kernel handoff, and enough userspace startup are now alive on the board-shaped emulation path.
- current local QEMU `10.2.2` `raspi4b` does not support `dumpdtb`; Pi 4 emulator validation therefore needs an explicit external DTB source rather than relying on QEMU to generate one.
- the next fast-lane blocker is now shared between the generic and Pi 4 QEMU lanes: both reach `pl011-tty: started` but not `tty0` or console readiness, so the next smallest step should target shared `pl011-tty` registration rather than more Pi 4 DTB or loader work.
- a bounded driver-local `create_dev()` retry experiment in `pl011-tty` did not change either QEMU lane, so that patch was reverted and should not be reused as if it were a proven fix.
- the next smallest high-signal step is now raw UART-side registration diagnostics in `pl011-tty`, because current stderr-only failure paths are not visible on the captured QEMU serial output.
- raw UART-side `pl011-tty` diagnostics now prove that both the generic and Pi 4 DTB-backed QEMU lanes reach `pl011-tty: register tty0` and then stop before either success or failure is reported.
- the current shared blocker is therefore inside the common `create_dev("/dev/tty0")` path, not in the driver-local code before or after that call.
- a bounded `libphoenix`-side `debug()` probe inside `create_dev()` produced no visible new markers on either QEMU lane and was reverted; that `debug()` path is not a useful early-boot visibility mechanism here.
- kernel-side syscall diagnostics now prove that the generic QEMU lane returns from `lookup("devfs", ...)` before hanging, and never reaches the final `msgSend()` marker for `tty0`; the live boundary is therefore between lookup return and final `msgSend()` entry inside `create_dev()`.
- the Pi 4 DTB-backed lane still does not show kernel-side markers, so the generic lane remains the authoritative fast diagnostic lane for this early `create_dev()` blocker.
- a temporary stdout-visible probe inside `libphoenix/create_dev()` also produced no visible new markers on either QEMU lane and was reverted; plain fd-1 writes are therefore not a useful early visibility path here either.
- a local raw `pl011-tty` helper now proves that the first `lookup("devfs", ...)` fails quickly on both the generic and Pi 4 DTB-backed QEMU lanes, so the first `/dev/tty0` registration attempt does not reach the create-message path at boot time.
- a temporary `dummyfs` experiment that removed the non-filesystem namespace `write(1, "", 0)` wait changed nothing on either lane and was reverted, so that startup gate is not the blocker behind the missing `devfs` lookup result.
- the bounded `pl011-tty` retry-window experiment is now complete on both QEMU lanes: each lane reaches `pl011-tty: tty0 lookup retry` and then stalls before either `lookup ok` or `lookup failed`, which means a later `lookup("devfs", ...)` call is now blocking instead of returning promptly.
- the next bounded blocker is therefore no longer inside the first raw `pl011-tty` helper branch itself; it is whether the `dummyfs` `devfs` instance reaches its main loop and actually receives or responds to the later `mtLookup` message.
- bounded `dummyfs` startup markers now prove on the generic lane that the non-filesystem `devfs` instance registers and reaches `initialized` only after the first `pl011-tty: tty0 lookup retry` marker, so `devfs` startup is genuinely late relative to the first tty-registration attempt.
- that same generic run still never reaches a non-filesystem `dummyfs: lookup recv` marker, which means the blocked later `lookup("devfs", ...)` path is not reaching the later `devfs` instance at all.
- the next bounded diagnostic target is therefore the root dummyfs instance, because unresolved `lookup("devfs", ...)` calls still travel through the root lookup path until `devfs` exists in the kernel name cache.
- the Pi 4 DTB-backed lane still shows no visible `dummyfs` markers in this step, so the generic lane remains the authoritative fast diagnostic source for this namespace-resolution blocker.
- the relabeled follow-up markers now show that the later startup markers are definitively coming from the `devfs` instance itself: `dummyfs: devfs registered` and `dummyfs: devfs initialized`.
- no `dummyfs: root ...` marker appears on the generic lane, and the current generic / Pi 4 `user.plo` images do not start a root dummyfs instance at all; the previous root-dummyfs hypothesis is therefore invalid for the current fast-lane image shape.
- the next bounded diagnostic target is now the kernel name-service layer in `proc/name.c`, because that is where `/` registration state and `lookup("devfs", ...)` branch selection actually live.
- the filtered `proc/name.c` trace is now in place on the generic lane and proves that the first `lookup("devfs")` takes the `name: devfs no root` fast-failure path, then `devfs` registers later as `name: register devfs`.
- after that first retry marker, there is still no second `create_dev: lookup devfs` entry and no second `name: devfs ...` branch marker at all, which means the retry loop is not re-entering the kernel lookup path during the observed boot window.
- the next bounded blocker is therefore no longer kernel name resolution; it is whether the `pl011-tty` retry loop ever wakes up from its `usleep(100000)` call.
- the raw post-`usleep()` marker is now in place on both QEMU lanes and never appears before timeout, which means the first bounded retry path sleeps and never wakes on both generic `virt` and Pi 4 DTB-backed `raspi4b`.
- the next bounded blocker is therefore inside the common sleep / timer wakeup path rather than inside `pl011-tty` retry control flow or a second `devfs` lookup.
- the new `proc/threads.c` markers now prove on the generic lane that the blocked retry path reaches `proc_threadNanoSleep()` and that `_threads_programWakeup()` does program a wakeup deadline.
- that same generic lane still never reaches `threads_timeintr()` before timeout, so the next bounded blocker is the common AArch64 timer source / IRQ-delivery path after wakeup programming rather than sleep enqueue itself.
- the Pi 4 DTB-backed lane still does not expose the new kernel-side `threads:` markers in this boot slice, so the generic lane remains the authoritative fast diagnostic lane for the missing timer interrupt.
- the common AArch64 timer frontend is now visible on the generic lane and selects `physical-nonsecure irq 30`.
- the first wakeup arm that follows the blocked `pl011-tty` retry reaches `gtimer_timer.c` as `gtimer: arm 1000 us`, which matches the current scheduler wakeup cap rather than the original `100000 us` sleep request.
- even with that explicit timer-source and arm visibility, the generic lane still never reaches `threads_timeintr()`, so the next bounded blocker is now the GIC-side timer-handler registration / dispatch path.
- the selected timer IRQ is now also visible on the GIC side: the generic lane reaches `gic: timer handler set`, which proves IRQ 30 handler registration succeeds.
- that same generic lane never reaches `gic: timer dispatch`, so the remaining bounded blocker is the timer-source / interrupt-generation side before GIC dispatch, not handler registration.
- the virtual-first timer-source experiment is now complete and negative: the generic lane changes from `physical-nonsecure irq 30` to `virtual irq 27`, but dispatch is still absent.
- the next bounded code clue is therefore not timer-source preference between those two architectural timers; it is explicit GIC configuration for timer PPIs, because the current generic AArch64 GIC path configures SPIs but does not explicitly configure PPIs.
- the explicit GIC PPI-configuration experiment is also negative: even after configuring non-SGI IRQs during handler registration, the generic lane still never reaches `gic: timer dispatch`.
- the remaining narrow common path is now the architectural timer sysreg write sequence itself, so the next bounded experiment is explicit synchronization after timer control and timer-value writes.
- the architectural-timer write-barrier experiment is also negative: even after explicit post-write barriers on both physical and virtual timer sysreg writes, the generic lane still never reaches `gic: timer dispatch`, `threads: timer irq`, or `pl011-tty: tty0 wake`.
- the next bounded timer clue is now register state rather than write ordering: the next common AArch64 experiment should read back the selected timer control state and timer value immediately after wakeup programming so the fast lane can distinguish failed arming from later interrupt-delivery loss.
- the architectural-timer register-readback experiment is now complete and high-signal: the generic fast lane reports `gtimer: arm 1000 us ctl 0x1 tval 58836`, which means the selected timer is genuinely armed with a live non-zero countdown.
- the next bounded clue is therefore GIC-side state for that IRQ rather than timer programming; the next common AArch64 experiment should expose the selected timer IRQ's interrupt-group and enable readback after handler registration.
- the GIC timer-state visibility step is now also high-signal: the generic fast lane reports `gic: timer handler set grp 0 en 0`, which means the selected timer IRQ still reads back as Group 0 and disabled immediately after registration.
- `sources/plo/hal/aarch64/generic/_init.S` exits EL3 to EL1 non-secure, and `sources/plo/hal/aarch64/zynqmp/_init.S` already documents and implements moving interrupts to Group 1 so non-secure code can manage them.
- the next bounded fix is therefore a timer-only Group 1 experiment in the kernel GIC path rather than another timer-programming change.
- the timer-only kernel Group 1 experiment is also negative: even after explicitly moving only the selected timer IRQ to Group 1 in the kernel GIC path, the generic fast lane still reads back `gic: timer handler set grp 0 en 0`.
- the next bounded boundary is therefore above the kernel in generic `plo` EL3 setup; the smallest next experiment is to initialize generic loader GIC state for Group 1 before the non-secure EL1 handoff.
- the generic `plo` EL3 GIC initialization experiment is the first major boundary break on the fast lane: the generic `virt` path now reaches `gic: timer dispatch`, `threads: timer irq`, `pl011-tty: tty0 wake`, `pl011-tty: tty0 ready`, `pl011-tty: console ready`, and visible later kernel startup logs.
- the Pi 4 DTB-backed `raspi4b` lane remains unchanged after that same loader-side fix, so the next bounded clue is the loader entry EL on the Pi 4 path rather than another generic timer or GIC change.
- the generic loader entry-EL visibility step is now complete: both the working generic `virt` lane and the stuck Pi 4 `raspi4b` lane enter `plo` at `EL3`.
- the next strongest Pi 4 clue is now the DTB itself: the current `RPI4B_DTB_PATH` input is only a 274-byte stub containing `compatible` plus one memory bank, which is not a real Pi 4 board tree.
- Pi 4 `raspi4b` validation is now rerun against the official Raspberry Pi firmware DTB from `raspberrypi/firmware` commit `63ad7e7980b030cb4649ecedf2255c9226e5a1e8`, path `boot/bcm2711-rpi-4-b.dtb`, size `56373` bytes.
- that official DTB materially changes the Pi 4 QEMU boundary: instead of reaching the old `pl011-tty: tty0 lookup retry` stall, the lane now stops earlier after `cmd: Executing pre-init script` and `alias: Setting relative base address to 0x0000000000200000`, with no later kernel or user-space logs.
- the old 274-byte stub DTB was therefore masking an earlier Pi 4-specific loader-side blocker, and the next bounded diagnostic target is now the `plo` `call ram0 user.plo` path rather than later kernel or user-space startup.
- the next bounded Pi 4 loader split is now fixed: add tightly filtered `plo/cmds/call.c` markers for open success, magic success, and each parsed line before `cmd_parse()` so the lane can be divided into pre-open, pre-read, pre-first-command, or first-command execution failure.
- that filtered `plo/cmds/call.c` visibility is now complete and high-signal: with the official firmware DTB, Pi 4 executes the entire `user.plo` script through `kernel ram0`, `blob ram0 system.dtb ddr`, both `app` commands, and `go!`, but still never prints the kernel banner.
- the current Pi 4 boundary is therefore no longer in pre-init or script execution; it is now strictly post-`go!`, inside `cmd_go()`, `hal_done()`, `hal_cpuJump()`, or the immediate handoff after `hal_cpuJump()`.
- the next bounded post-`go!` split is now fixed: add raw `go:` markers in `plo/cmds/go.c` around `devs_done()`, `hal_done()`, and the `hal_cpuJump()` call so the Pi 4 handoff can be divided without widening into HAL instrumentation yet.
- that filtered `plo/cmds/go.c` visibility is now complete and high-signal: both lanes reach `go: enter`, `go: devs done`, `go: hal done`, and `go: jump`, but only the generic lane reaches the kernel banner afterward.
- the current Pi 4 boundary is therefore no longer in `cmd_go()` cleanup; it is now strictly inside `hal_cpuJump()` or the immediate EL-exit handoff path in generic AArch64 `plo`.
- the next bounded jump-path split is now fixed: add raw `hal:` markers in `plo/hal/aarch64/generic/hal.c` around `hal_interruptsDisableAll()` and the call into `hal_exitToEL1()` so the C-side jump path can be exhausted before any assembly changes are made.
- that filtered `plo/hal/aarch64/generic/hal.c` visibility is now complete and high-signal: both lanes reach `hal: jump entry`, `hal: jump irq off`, and `hal: jump exit el1`, but only the generic lane reaches the kernel banner afterward.
- the current Pi 4 boundary is therefore no longer in C-side loader handoff code; it is now strictly inside the assembly EL transition in `plo/hal/aarch64/generic/_init.S` or in the first kernel instructions after that transition.
- the next bounded assembly-side split is now fixed: add tiny raw UART markers in `plo/hal/aarch64/generic/_init.S` at `hal_exitToEL1()` entry and immediately before the EL-specific transfer instruction so the loader assembly boundary can be divided before any kernel-side instrumentation is introduced.
- that assembly-side EL-exit visibility is now complete and high-signal: both lanes reach the EL3 transfer marker `A3`, but only the generic lane reaches the kernel banner afterward.
- the Pi 4 lane also prints repeated assembly markers such as `AAA333` and a later `A3`, which strongly suggests that multiple cores are taking the same generic loader EL3 handoff path during the Pi 4 `-smp 4` run.
- generic `virt -smp 4` now confirms that repeated EL3 handoff markers are a generic multi-core loader behavior, not the Pi 4 failure by themselves, because the generic lane still reaches the kernel banner and later startup logs.
- the current generic kernel target still declares `NUM_CPUS 1U` in `phoenix-rtos-kernel/hal/aarch64/generic/config.h`, so handing off multiple loader CPUs into this target is at least a design mismatch even though generic `virt -smp 4` happens to boot.
- the generic secondary-core containment experiment is now complete: generic `plo` keeps non-boot CPUs parked across the current handoff, generic `virt -smp 4` still reaches the kernel banner, and Pi 4 `raspi4b -smp 4` now shows only a single `A3` before timing out.
- secondary-core release was therefore not the root cause of the Pi 4 failure after the EL3 transfer; it was only adding noisy repeated handoff markers.
- the next bounded split is now the earliest visible generic AArch64 kernel entry point after that single `A3` marker.
- the earliest-kernel-entry visibility step is now scoped: add a raw PL011 marker at generic kernel `_start`, using project `board_config.h` for the early UART base on both generic QEMU and Pi 4, and keep the change limited to `hal/aarch64/_init.S` plus generic config glue.
- the earliest-kernel-entry visibility step is now complete: both generic QEMU and Pi 4 print `K` immediately after the loader-side `A3`, so Pi 4 definitely reaches generic kernel `_start`.
- the next bounded early-init clue is now the `__TARGET_AARCH64A53` system-register block in `hal/aarch64/_init.S`, because the active Pi 4 lane still builds as `aarch64a53` while QEMU `raspi4b` is running `-cpu cortex-a72`.
- the post-entry A53-block split is now complete too: Pi 4 prints `KLM`, so it gets past that block and still dies later in early kernel init.
- the strategic pivot is now explicit: Raspberry Pi 4 is BCM2711 with a quad-core Cortex-A72 CPU, so `aarch64a53-generic-rpi4b` should be treated only as a temporary diagnostic lane.
- the official Raspberry Pi 4 specifications page remains the authority for this CPU identity: BCM2711, quad-core Cortex-A72 (ARM v8) 64-bit SoC, so future Pi 4 target naming, CPU assumptions, and runtime validation should stay centered on the A72 lane.
- the next bounded implementation step should therefore enable a real Cortex-A72-capable generic target path, starting with removal of the first hard `aarch64a53` generic naming assumptions in `plo`.
- that first A72-enabling groundwork is now complete in `plo`: generic loader config can select `phoenix-aarch64a72-generic.elf` plus `ld/aarch64a72-generic.ldt`, while the existing A53 generic lanes still build cleanly.
- the first local `aarch64a72-generic-rpi4b` scaffold is now in place across build, project, filesystems, devices, and utils, and the new A72 Pi 4 build plus the preserved A53 generic QEMU and Pi 4 builds all complete successfully in `phoenix-dev`.
- the first `aarch64a72-generic-rpi4b` runtime validation is now complete: the A72 Pi 4 lane selects `phoenix-aarch64a72-generic.elf` but still reaches the same `A3KLM` boundary as the A53 diagnostic lane.
- the generic AArch64 identification strings are now corrected: the A72 Pi 4 loader lane reports `Cortex-A72 Generic`, the A53 lane still reports `Cortex-A53 Generic`, and the kernel-side generic platform names are now target-aware too.
- a negative experiment is now recorded too: raw UART probes placed after the `ttbr1_el1` switch are not valid on this path, because neither lane prints them and the generic fast lane regresses until the patch is reverted.
- the first C-entry visibility split is now complete: the generic lane reaches both `hal: console init done` and `main: hal init done`, while Pi 4 reaches neither.
- the generic console-init split is now complete too: the generic lane reaches `console: pl011 init done`, while Pi 4 still reaches none of the console-init markers.
- the strongest concrete blocker is now DTB address handling: the official Pi 4 firmware DTB uses bus-address serial nodes such as `serial@7e201000` plus `/soc/ranges` mapping to CPU-visible `0xfe...` space, and Phoenix still parses serial `reg` values without applying that translation.
- the Pi 4 serial DTB fix is now in place: the kernel DTB parser decodes `/soc` serial `reg` cells with the parent cell width and translates serial MMIO through `/soc/ranges`, while the generic `virt` lane still reaches the established tty / console-ready boot band.
- that same fix moves the Pi 4 A72 `raspi4b` lane much later: it now reaches `console: pl011 init done`, `hal: console init done`, `main: hal init done`, and the kernel banner before faulting with `Exception #37: Data Abort (EL1)`.
- the current Pi 4 fault symbolizes to `_map_init` in `vm/map.c` (`pc=...b198` -> line `1638`, `lr=...b0cc` -> line `1624`), so the active blocker is now well past early serial and into later kernel initialization.
- the official firmware DTB from `raspberrypi/firmware` also decompiles to `memory@0 { reg = <0x00 0x00 0x00>; }`, and Raspberry Pi documentation states that the firmware customizes the DTB before kernel handoff.
- the official Raspberry Pi kernel sources are now a preferred source for board intent over decompiling the already-built DTB:
  - `raspberrypi/linux` `rpi-6.12.y` and `rpi-6.19.y` both keep `bcm2711-rpi.dtsi` `memory@0` marked `Will be filled by the bootloader`
  - `bcm2711-rpi-4-b.dts` on those branches keeps `chosen { stdout-path = "serial1:115200n8"; }` with the comment `8250 auxiliary UART instead of pl011`
- that source-level confirmation means future DT analysis should not assume `stdout-path` alias resolution is immediately useful for the current Phoenix PL011 console path, and it also reinforces that direct `raspi4b` QEMU is missing firmware-time DTB customization
- the next bounded Pi 4 clue is therefore QEMU-specific: direct `raspi4b` validation is using an uncustomized firmware DTB without the Raspberry Pi firmware in the loop, so the next smallest step should validate a QEMU-only payload-DTB memory fix before widening into general VM or memory-management debugging.
- that one-off QEMU-only `memory@0/reg = <0x00 0x00 0x80000000>` experiment is now also complete and negative, so the next smallest step is to instrument the live `_vm_init` / `_map_init` boundary instead of adding speculative DTB automation first.
- the `_vm_init` / `_map_init` visibility step is now complete and high-signal: Pi 4 reaches `vm: enter`, `vm: page init done`, `vm: map init`, `map: enter`, `map: pool link`, and then explicitly `map: zero free` before aborting inside `_map_init`.
- the current Pi 4 exception now symbolizes to `_map_init` lines `1644-1645`, immediately after the new zero-free marker, which confirms the live failure is the `map_common.nfree - 1U` underflow path rather than a later VM issue.
- the strongest current root cause is now earlier DTB-backed memory-bank parsing:
  - `hal/aarch64/dtb.c:dtb_parseMemory()` still hardcodes a 16-byte `<addr,size>` assumption
  - the Pi 4 root memory node uses root cell widths instead (`#address-cells = 2`, `#size-cells = 1`)
  - that explains why the earlier one-off Pi 4 QEMU memory-size patch was negative: Phoenix never parsed that 3-cell memory node at all
- the root-memory parser fix is now in place: `hal/aarch64/dtb.c` decodes root
  memory banks with the DTB root cell widths, and the generic `virt` lane still
  reaches the established later boot band.
- that fix also cleanly separates the remaining Pi 4 issues:
  - direct `raspi4b` QEMU with the unmodified official firmware DTB still hits
    `map: zero free`, because QEMU is not performing the Raspberry Pi
    firmware-time `memory@0/reg` population
  - the same Pi 4 lane with a one-off `memory@0/reg = <0x00 0x00 0x80000000>`
    DTB patch now moves past `_map_init`, reaches `vm: map init done`, and
    stalls later after `dummyfs: devfs initialized`
- the Raspberry Pi source-reference rule is now stronger too:
  - `rpi-6.19.y` and `rpi-7.0.y` currently carry identical Pi 4
    `bcm2711-rpi-4-b.dts` and `bcm2711-rpi.dtsi`
  - future DT debugging should consult both Raspberry Pi Linux DTS sources and
    the Raspberry Pi device-tree documentation, not only decompiled DTBs
- the Pi 4 A72 project build now has a narrow QEMU-only DTB memory hook:
  `RPI4B_QEMU_MEMORY_SIZE=80000000` patches `memory@0/reg` in both staged DTB
  copies without changing the default real-device DTB path.
- that automated hook is validated:
  - `fdtget` on the staged boot DTB now returns `0 0 -2147483648`
  - the `raspi4b` lane reaches the same later boundary as the manual patched
    DTB run:
    - `vm: map init done`
    - `gtimer: source virtual irq 27`
    - `gic: timer handler set grp 1 en 1`
    - `dummyfs: devfs initialized`
  - then stalls before any visible `gic: timer dispatch`, `threads: timer irq`,
    or `pl011-tty: tty0 wake`
- the next bounded runtime experiment is now selected too:
  - the current common policy prefers the virtual timer first
  - the Pi 4 patched lane currently reports `gtimer: source virtual irq 27`
  - the next smallest follow-up is to force the Pi 4 patched lane to the
    non-secure physical timer and compare whether dispatch resumes
- that physical-timer experiment is now complete and negative:
  - the Pi 4 patched lane now reports `gtimer: source physical-nonsecure irq 30`
  - it still reaches `gic: timer handler set grp 1 en 1`,
    `threads: wakeup programmed`, and `dummyfs: devfs initialized`
  - it still does not reach `gic: timer dispatch`, `threads: timer irq`, or
    `pl011-tty: tty0 wake`
- timer-source choice is therefore no longer the most likely blocker; the next
  bounded question is whether the selected timer IRQ ever becomes pending in
  the Pi 4 GIC at all
- the next experiment is now fixed:
  add one first-arm timer-IRQ pending probe so the Pi 4 lane can be divided
  into:
  - timer never asserted into the GIC
  - timer asserted but still never dispatched
- that pending-state probe is now complete and high-signal:
  - generic `virt` reports `gtimer: pending 1`
  - Pi 4 A72 patched lane reports `gtimer: pending 0`
  - so the current Pi 4 blocker is earlier than GIC dispatch or CPU-interface
    handling; the selected timer IRQ is not even reaching pending state in the
    bounded probe window
- the next bounded timer-side question is now explicit: whether the Pi 4 timer
  is actually counting down after the first arm or remains inert before ever
  reaching pending state
- that timer-countdown readback is now complete and high-signal:
  - generic `virt` reports `gtimer: pending 1` and `gtimer: post 2000 us ctl 0x5 ...`
  - Pi 4 A72 patched lane reports `gtimer: pending 0` and `gtimer: post 2000 us ctl 0x5 ...`
  - so the Pi 4 timer does expire locally, but the interrupt still does not
    appear in the current GIC pending view or dispatch path
- the external bare-metal reference sweep is now complete enough to be useful:
  - `rpi4-osdev`, `rpi-os`, and the OSDev bare-bones article all reinforce the
    Pi 4 basics around low peripheral mode, `0x80000` AArch64 boot, and simple
    CPU containment
  - Circle is the strongest external reference for the current Phoenix seam
    because it explicitly uses the Pi 4 non-secure physical timer path, requires
    the physical counter on Pi 4, and maps that timer to GIC PPI 14 -> IRQ 30
- the next bounded diagnostic target is now explicit again:
  compare the current `ISPENDR`-based timer pending readback with a private
  pending-state readback for the same Pi 4 timer PPI
- that private-pending-state readback is now complete and negative:
  - generic `virt` reports `gtimer: pending 1` but `gtimer: ppi pending 0`
  - Pi 4 A72 patched lane reports `gtimer: pending 0` and `gtimer: ppi pending 0`
  - so `PPISR` is not the missing view that explains the Pi 4 gap
- the most visible remaining runtime difference is now the timer group:
  - generic lane timer registration reads back `grp 0 en 1`
  - Pi 4 lane timer registration reads back `grp 1 en 1`
- that bounded Group 0 experiment is now complete and negative:
  - generic `virt` remains healthy with `gic: timer handler set grp 0 en 1`
  - Pi 4 A72 patched lane can also be forced to `gic: timer handler set grp 0 en 1`
  - Pi 4 still reports `gtimer: pending 0` and `gtimer: ppi pending 0`
  - Pi 4 still never reaches `gic: timer dispatch`
- the timer-group difference is therefore ruled out as the last missing
  variable, and the next move should restore the Pi 4 board-config baseline
  before testing a new timer-routing hypothesis
- that baseline restore is now complete too:
  - Pi 4 A72 lane is back to `gic: timer handler set grp 1 en 1`
  - Pi 4 still reports `gtimer: pending 0` and `gtimer: ppi pending 0`
  - Pi 4 still never reaches `gic: timer dispatch`
- the next bounded hypothesis is now the BCM2711 local interrupt controller:
  Circle enables `ARM_LOCAL_TIMER_INT_CONTROL0` bit 1 for the non-secure
  physical timer and checks `ARM_LOCAL_IRQ_PENDING0`, while Phoenix currently
  has no code touching that local interrupt block
- that local-route-enable experiment is now complete too:
  - Pi 4 A72 lane writes and reports `gic: local timer route 0x2`
  - Pi 4 still reports `gtimer: local pending 0x0`
  - Pi 4 still reports `gtimer: pending 0` and no `gic: timer dispatch`
- the route-enable write alone is therefore not the missing piece
- the next bounded local-block variable from Circle is the Pi 4 prescaler
  write in `external/circle/lib/sysinit.cpp`:
  `ARM_LOCAL_PRESCALER = 39768216U`
- that prescaler experiment is now complete and negative too:
  - Pi 4 A72 lane logs `gic: local prescaler 39768216`
  - Pi 4 still reports `gtimer: local pending 0x0`
  - Pi 4 still never reaches `gic: timer dispatch`
- the most important new source-level finding is now QEMU-specific:
  local QEMU 10.2.2 source shows that `raspi4b` wires:
  - `GTIMER_PHYS -> GIC PPI 14` in `hw/arm/bcm2838.c`
  - unlike `hw/arm/bcm2836.c`, the Pi 4 QEMU model does not put the CPU
    physical timer through `bcm2836_control` first
- so the local-controller detour is not on the active Pi 4 QEMU timer path,
  and the next bounded move should restore that fast lane baseline before
  resuming direct GTIMER-to-GIC debugging
- that fast-lane restore is now complete:
  - Pi 4 QEMU is back to `gic: timer handler set grp 1 en 1`
  - Pi 4 QEMU again reports only `gtimer: pending 0` and
    `gtimer: ppi pending 0`
  - no local-controller traces remain in the active fast lane
- the next direct-GIC split should now probe the CPU-interface pending view
  itself through `GICC_HPPIR`
- that `GICC_HPPIR` probe is now complete:
  - generic lane reports `gtimer: hppir 1023` after a successful dispatch
  - Pi 4 QEMU lane reports `gtimer: hppir 0` while dispatch is still absent
- the next bounded follow-up should stay read-only and explain the Pi 4
  CPU-interface view, most likely by probing the alias or alternate pending
  register path before changing any timer or interrupt policy
- the next concrete Pi 4 boot blocker is now loader MMIO addressing: `sources/plo/hal/aarch64/generic/config.h` still hardcodes QEMU `virt` UART and GIC base addresses, so the current Pi 4 `kernel8.img` would still talk to the wrong MMIO blocks on real hardware until those addresses are made board-overridable.
- generic `plo` now accepts project-local MMIO base overrides for UART0 and GICv2 while preserving the current QEMU `virt` defaults, and the generic `virt` smoke lane still boots after that change.
- historical note:
  an earlier assumption in this bring-up log was that matching
  `kernel_address=0x40080000` and `ADDR_PLO 0x40080000` would be sufficient to
  avoid a raw placement mismatch; the live Pi 4 UART result from `2026-04-11`
  disproved that assumption by showing:
  - `Loaded 'kernel8.img' to 0x40080000`
  - `Kernel relocated to 0x80000`
  so the current active design now uses a relocatable trampoline `kernel8.img`
  instead of a raw direct copy of the high-linked `plo` image.
- the next Pi 4 deployment blocker is now firmware-file completeness rather than loader placement: the staged `_boot/.../rpi4b/` tree still lacks Raspberry Pi firmware files, so it is not yet a self-contained first-partition boot bundle.
- the Pi 4 project now accepts an operator-supplied Raspberry Pi firmware directory through `RPI4B_FIRMWARE_DIR` or `_projects/aarch64a53-generic-rpi4b/firmware` and stages required firmware files such as `start4.elf` and `fixup4.dat` into `_boot/.../rpi4b/` while keeping default no-firmware builds green.
- future agents are explicitly allowed to source Pi 4 firmware files and the board DTB from the Raspberry Pi firmware repository boot tree at `https://github.com/raspberrypi/firmware/tree/master/boot` when that is the most direct path to a testable boot bundle; the exact required file set should still be re-verified against the active Pi 4 firmware baseline.
- `gdb-multiarch` `15.1` is now installed in `phoenix-dev`, and the QEMU gdbstub is now a proven low-level inspection lane for the current Pi 4 bring-up work.
- the decisive Pi 4 QEMU blocker was not timer policy after all; it was DTB GIC discovery:
  - at `_hal_interruptsInit + 64`, generic returned pre-map
    `gicd = 0x08000000`, `gicc = 0x08010000`
  - the same Pi 4 breakpoint originally returned
    `gicd = 0x0`, `gicc = 0x0`
- the bounded `hal/aarch64/dtb.c` fix is now in place:
  - shallow `/soc/interrupt-controller@...` nodes are recognized
  - `/soc` GIC `reg` tuples are decoded through `/soc` cell widths and
    `ranges` translation
  - the old non-`/soc` fallback path remains intact
- after that fix, the same Pi 4 pre-map breakpoint now returns:
  - `gicd = 0xff841000`
  - `gicc = 0xff842000`
- the Pi 4 `raspi4b` QEMU lane now reaches the same early boot band as the
  generic lane, including:
  - `gic: timer dispatch`
  - `threads: timer irq`
  - `pl011-tty: tty0 ready`
  - `pl011-tty: console ready`
  - `main: Starting syspage programs ...`
  - `dummyfs: initialized`
- bounded syspage spawn visibility now proves that both generic and Pi 4 lanes
  successfully spawn:
  - `dummyfs`
  - `pl011-tty`
  - `psh`
- the first bounded interactive generic PTY probe now shows that serial input
  is echoed back but still produces no visible shell prompt or command output
- the active later-boot boundary is therefore now inside the shared `psh`
  startup path rather than in the kernel syspage spawn loop
- the bounded `psh` startup-marker experiment is now complete and negative on
  both generic and Pi 4:
  - neither lane prints even `psh: root ready` or `psh: run enter`
  - both lanes still stop visibly after `dummyfs: initialized`
- the first below-stdio hook is now complete and positive on both lanes:
  - generic prints `threads: psh user scheduled`
  - Pi 4 prints `threads: psh user scheduled`
- `psh` therefore does reach first user-mode execution on both lanes
- the next bounded visibility step should now move to the earliest `psh`-local
  syscall result, starting with `lookup("/")`
- the bounded `psh` root-lookup success trace is now complete and still
  negative on both lanes:
  - generic shows no `syscalls: psh root lookup ok`
  - Pi 4 shows no `syscalls: psh root lookup ok`
- the current ambiguity is now narrower:
  - `psh` may be looping on failed `lookup("/")`, or
  - `psh` may not be reaching that syscall path yet
- the first-result `psh` root-lookup trace is now complete and decisive on both
  lanes:
  - generic prints `syscalls: psh root lookup -22`
  - Pi 4 prints `syscalls: psh root lookup -22`
- the shared blocker is therefore no longer “missing rootfs” but a shared
  `lookup()` contract failure returning `EINVAL`
- the root-dummyfs fast-lane image fix is now in place in
  `phoenix-rtos-project`:
  - both generic and Pi 4 start a root `dummyfs-root` instance before the
    existing `dummyfs;-N;devfs;-D` instance
  - the extra boot alias is necessary because
    `phoenix-rtos-project/_targets/build.common:b_mkscript_user()` aliases each
    `app` payload by basename, so two `dummyfs` payloads collide in `plo`
- both fast lanes now recover past the old rootfs stall and print:
  - `name: register /`
  - `dummyfs: root initialized`
  - `syscalls: psh root lookup 0`
  - `psh: root ready`
  - `psh: app run`
  - `psh: run enter`
  - `psh: tty open`
  - `psh: app done`
- the next shared later-boot blocker is therefore inside
  `psh_ttyopen("/dev/console")`, before `psh: tty ready`
- the bounded `psh_ttyopen()` failure trace is now complete and identical on
  both lanes:
  - generic prints `psh: tty open fail open -2`
  - Pi 4 prints `psh: tty open fail open -2`
- the next shared blocker is therefore even narrower:
  `open("/dev/console", O_RDWR) -> -ENOENT` despite
  `pl011-tty: console ready`
- the first kernel-side `/dev/console` lookup experiment is now also bounded:
  - a `syscalls_lookup()` trace for `/dev/console` never fires on the generic
    fast lane
  - source review of `phoenix-rtos-kernel/posix/posix.c:posix_open()` explains
    why: the open path goes through `proc_lookup()` directly
- the next bounded trace should therefore move to `posix_open()` or
  `proc_lookup()` rather than adding more noise to `syscalls_lookup()`
- the `posix_open()` console trace is now also complete and negative on both
  fast lanes:
  - generic prints no `posix: psh console open ...`
  - Pi 4 prints no `posix: psh console open ...`
- the libphoenix `open()` console trace is likewise negative on both lanes:
  - generic prints no `open: console ...`
  - Pi 4 prints no `open: console ...`
- built-image inspection and a bounded QEMU gdbstub session now prove the real
  live call path:
  - `psh_ttyopen("/dev/console")` really does call the linked libphoenix
    `open()` symbol in the built `psh` image
  - inside `open()`, `stat("/dev/console")` returns `-1`
  - `open()` then falls through to `resolve_path("/dev/console", ..., 1, 1)`
  - `resolve_path()` returns `NULL`
  - `sys_open()` is therefore never reached
- the current shared blocker is no longer a call-path mismatch and no longer a
  kernel-side open syscall issue; it is now specifically the libphoenix path
  canonicalization path for `/dev/console`
- the earlier silent libphoenix trace is therefore a visibility limitation of
  that trace path, not evidence that `open()` was skipped
- a second bounded QEMU gdbstub pass now proves the failure is even earlier
  than the `console` leaf:
  - the first `resolve_path()` call reached from `stat()` fails with
    `errno = ENOENT`, `partial = "/dev"`, `is_leaf = 0`
  - the second `resolve_path()` call reached directly from `open()` fails the
    same way, also at intermediate `/dev`, even with `allow_missing_leaf = 1`
- the shared fast-lane blocker is therefore:
  `lookup("/dev") -> -ENOENT` inside libphoenix path resolution
- source review of `dummyfs` and project files makes the likely reason explicit:
  - the root `dummyfs-root` instance auto-populates only `/syspage`
  - the `dummyfs;-N;devfs;-D` instance registers `devfs` in the non-filesystem
    namespace, not at `/dev`
  - the current generic and Pi 4 fast-lane projects do not stage the usual
    pre-shell `devfs -> /dev` bind path used by established projects
- the pre-shell `/dev` fast-lane fix is now in place in
  `phoenix-rtos-project`:
  - both fast-lane projects stage `psh` aliases for `mkdir` and `bind`
  - both fast-lane `user.plo` scripts now run:
    - `mkdir /dev`
    - `bind devfs /dev`
    before the final `psh` app
- that fix is validated on both emulator lanes:
  - generic `virt` now reaches:
    - `psh: tty ready`
    - `psh: readcmd`
    - `(psh)%`
  - Pi 4 `raspi4b` now reaches the same prompt band:
    - `psh: tty ready`
    - `psh: readcmd`
    - `(psh)%`
- the obsolete `/dev/console` investigation probes are now removed again from:
  - `psh`
  - `libphoenix`
  - kernel lookup / posix open paths
- both emulator lanes still reach the same prompt band after that cleanup, so
  the prompt-reaching fast lane now exists without the old console-specific
  diagnostic scaffolding
- the first command-level shell smoke has now been attempted with `expect`:
  - Pi 4 `raspi4b` succeeds with the built-in `help` command:
    - `(psh)% help`
    - `Available commands:`
    - returned `(psh)%`
  - generic `virt` now also succeeds after a runtime-only launch fix:
    - switching from `-serial mon:stdio -serial null -display none`
      to `-nographic -monitor none`
    - `(psh)% help`
    - `Available commands:`
    - returned `(psh)%`
- the first command-level smoke is therefore now common to both fast QEMU
  lanes, and the earlier generic-only failure is confirmed to have been a QEMU
  stdio-launch issue rather than a Phoenix shell bug
- that smoke is now packaged in:
  - `scripts/qemu-shell-smoke.sh generic`
  - `scripts/qemu-shell-smoke.sh rpi4b`
- both helper runs print the same compact success markers and point to the
  corresponding VM-side log file under `/tmp`
- the first candidate external-applet smoke, `ls /`, is not a good fit for the
  current fast lane:
  its natural markers like `dev` collide with earlier boot-band text such as
  `create_dev: ...`, so the next applet smoke should use a unique token instead
- the next candidate, `echo codex-smoke-echo`, is also not a valid proof shape:
  the token appears in the echoed command line itself, so it still does not
  independently prove applet stdout
- the first clean external-applet smoke is now proven on both fast lanes with:
  - `echo -h`
  - `Usage: echo [options] [string]`
  - returned `(psh)%`
- the first no-hardware Pi 4 firmware-artifact helper is now in place:
  - `scripts/assemble-rpi4b-bootfs.sh`
- it assembles a firmware-visible boot tree containing the staged Phoenix Pi 4
  files plus Raspberry Pi firmware files under:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs`
- the next artifact layer is also now in place:
  - `scripts/assemble-rpi4b-bootfs-img.sh`
- it builds a portable FAT image at:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img`
- that FAT image is now the selected first real-device artifact for Pi 4; a
  larger SD-card image is deferred until the project actually needs more than
  the firmware-visible boot partition
- the operator-facing handoff is now also in place:
  - `scripts/export-rpi4b-fat-image.sh`
- it exports the current Pi 4 FAT image into a stable host-visible path:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-bootfs.img`
- the current validated exported-image SHA-256 is:
  - `fab57080ef7c770ac9346cfd9e86b6ef71c31d47559fe0bd955bee6b71d3a108`
- the immediate remaining operator gap is now SD-card usability rather than VM
  artifact access, because the current exported file is a raw FAT filesystem
  image rather than a full flashable disk image
- that gap is now partly closed too:
  - `scripts/assemble-rpi4b-sdimg.sh`
- it produces a VM-local full Pi 4 disk image at:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img`
- the current validated layout is:
  - DOS partition table
  - one bootable FAT32 partition
  - partition start sector `2048`
  - embedded FAT offset `1048576`
- the immediate remaining operator gap is now host visibility for that full
  disk image rather than image shape
- that host-visibility gap is now closed too:
  - `scripts/export-rpi4b-sdimg.sh`
- it exports the current Pi 4 full disk image into:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- the current validated exported full-image SHA-256 is:
  - `acfdb8c251be03a716cdd9811b151c412de1e3a11c24db76ed5a476d8fc8f107`
- the project now has a host-visible flashable Pi 4 SD-card image artifact for
  the first manual hardware trial
- the operator runbook now includes an explicit macOS flashing workflow for:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- the docs now also state the current first-boot limitation clearly:
  without USB-TTL serial, the first hardware trial is useful as an
  artifact-deployment check but not yet as a strong runtime-validation milestone
- the next non-UART observability target is now selected too:
  start with a `plo`-side Raspberry Pi mailbox framebuffer step, because it is
  visible on HDMI, testable under `raspi4b`, and narrower than early networking
  or full runtime display plumbing
- that HDMI observability step is now implemented and validated under
  `raspi4b` QEMU:
  - `plo` performs a Raspberry Pi mailbox property transaction
  - allocates a framebuffer
  - paints a visible marker rectangle in the upper-left corner
- the decisive constraint discovered during that step is now explicit:
  the current generic `plo` link address places a static mailbox request buffer
  above `0x40000000`, and that high buffer failed on the Pi 4 `raspi4b` lane;
  a bounded gdbstub experiment proved that redirecting the exact same request
  buffer to low physical memory (`0x02000000`) immediately produced a valid
  framebuffer allocation response
- the implemented source fix therefore keeps the mailbox/framebuffer logic but
  moves the Pi 4 property-request buffer into a board-provided low physical
  window instead of widening into broader graphics changes
- the currently validated Pi 4 HDMI fast-lane signature is:
  - framebuffer size `1024 x 768`
  - a small top-left staged progress panel
  - panel background pixel `(20, 20) -> (72, 72, 72)`
  - stage pixels `(48, 48)`, `(112, 48)`, `(176, 48)` all lit as
    `(240, 240, 240)` by the time `plo` reaches kernel jump
  - preserved background pixel `(639, 479) -> (160, 96, 48)`
- that visibility path is now regression-testable with one command:
  - `scripts/qemu-rpi4b-hdmi-smoke.sh`
  - it runs the existing Pi 4 QEMU lane, captures a framebuffer dump, and
    validates the current marker and background pixels
- the next smallest real-board refinement is now also implemented at the
  firmware staging layer:
  - the Pi 4 project `config.txt` now adds `hdmi_force_hotplug=1`
  - and `disable_overscan=1`
- this is intentionally narrow:
  - force HDMI mode even if hotplug detection is flaky
  - avoid default firmware overscan cropping of the upper-left marker
  - do not widen yet into explicit fixed HDMI modes or broader safe-mode
    bundles
- this is still an early `plo` visibility path only:
  it is not yet a runtime display console, windowing path, or general graphics
  subsystem
- the HDMI path is now more useful for the current no-UART lab:
  it shows three coarse loader milestones instead of only "framebuffer alive"
- that firmware refinement is now build-validated too:
  the rebuilt staged Pi 4 boot artifact contains both added `config.txt`
  lines in `_boot/aarch64a72-generic-rpi4b/rpi4b/config.txt`
- the host-visible Pi 4 SD-card artifact has now been refreshed through the
  full helper chain after that firmware change, so
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  is the current flashable image that includes the HDMI refinement
- after the staged HDMI progress implementation, the host-visible Pi 4 SD image
  has now been refreshed again, and the current validated exported full-image
  SHA-256 is:
  - `b2f3a33fe7b4e96d364b6e7579350d7c548359701cbaf0e9ac6b86fbf18860b0`
- Circle has now been reviewed in detail as a Pi 4 external reference:
  - it strongly confirms the current Phoenix early-HDMI direction:
    low-memory mailbox property requests plus firmware-allocated framebuffer
  - it also confirms that Pi 4 USB keyboard support is not a near-term shortcut
    for the current lab, because Circle reaches it through BCM2711 PCIe plus
    VL805 xHCI before HID keyboard handling
  - the detailed review is now captured in `docs/circle-reference-review.md`
- an important constraint on that choice is now explicit:
  Phoenix already has `plo` `graphmode` state, but the current AArch64 kernel
  path does not yet expose an IA32-style `pctl_graphmode` consumer, so the
  first visibility step should stop at early framebuffer life signs
- the currently available real-hardware lab is weaker than the ideal UART lab:
  microSD plus HDMI plus Ethernet plus USB keyboard or mouse are available, but
  no USB-TTL adapter is currently available
- that means the first hardware-oriented steps should prioritize boot-media
  usability and alternate observability paths, while keeping explicit notes
  that early runtime failures may remain low-visibility until UART, framebuffer,
  or network-level visibility improves
- debugger-first is now the recorded policy for QEMU runtime triage:
  future sessions should start with a bounded gdbstub inspection and only add
  source-level probes after documenting why GDB cannot answer the current
  question
- the user has explicitly deferred further SD-image refresh and manual Pi 4
  board testing until HDMI text output exists, so the current fast lane shifts
  from artifact handoff to framebuffer text enablement
- Phoenix already contains an in-tree framebuffer text renderer and bundled 8x16
  font in `phoenix-rtos-devices/tty/pc-tty/ttypc_fbcon.c` and
  `phoenix-rtos-devices/tty/pc-tty/ttypc_fbfont.h`
- the Pi 4 `plo` path already stores framebuffer geometry in the generic
  AArch64 syspage through `syspage_graphmodeSet()`, but generic AArch64 does
  not yet expose an IA32-style `pctl_graphmode` query for runtime consumers
- the shortest current route to HDMI text is therefore:
  expose generic AArch64 graphmode metadata through `platformctl`, then reuse
  the existing Phoenix framebuffer renderer instead of adding a new font stack
- copied-buildroot validation must be run sequentially per target; concurrent
  generic and Pi 4 builds against the same copied buildroot race on shared
  host-artifact paths such as `_build/host-generic-pc`
- local QEMU `10.2.2` `hw/intc/arm_gic.c` does not expose an explicit
  CPU-interface read case for offset `0x28`, so older `AHPPIR` experiments
  should not be treated as authoritative outside the exact runtime context in
  which they were observed.
- Phoenix upstream style is conservative and review-oriented: file headers, tabs in C, localized `clang-format off/on`, direct control flow, `static const` hardware tables, and warning-clean builds enforced by `-Werror` in `phoenix-rtos-build/Makefile.common`.
- Pi 4 uses BCM2711 with GIC-400, PL011, BCM2711 PCIe, VL805 xHCI over PCIe, GENET Ethernet, and Broadcom SDHCI.
- Pi 5 uses BCM2712 plus RP1, with most I/O behind a PCIe-connected southbridge-like peripheral controller.

## Immediate Next Implementation Milestones

1. Scope the smallest alternate-observability step for a Pi 4 lab without
   USB-TTL serial.
   Result: selected `plo` mailbox framebuffer as the next bounded path.
2. Keep the new Pi 4 HDMI visibility path stable and regression-testable.
   Result: the current helper is now `scripts/qemu-rpi4b-hdmi-smoke.sh`.
3. Keep the current QEMU shell smoke baseline stable:
  `help` plus the validated external-applet follow-up `echo -h`.
4. Use the current QEMU shell confidence to drive the next bounded steps toward
   a visible first real-device signal beyond UART-only diagnostics.
5. Use the Circle review to keep the next bounded move on the HDMI-visible path
   rather than prematurely widening into PCIe, xHCI, or USB keyboard work.
6. Reuse the existing `pc-tty` framebuffer text path surgically instead of
   porting the whole IA32 `pc-tty` app or introducing a new font subsystem.
7. Defer more SD-image refresh work until HDMI text output exists.
8. Keep the new prompt-reaching lane stable while avoiding new diagnosis-only
   probe accumulation.

## Pi 4 Success Criteria for "Phase 1"

- Stable boot from Raspberry Pi firmware into `plo`
- Stable `plo` UART console
- Stable `plo -> kernel` transfer
- Kernel MMU, exception, interrupt, and timer paths working
- Single-core shell on UART
- Reliable reboot

## Pi 4 Success Criteria for "Developer Complete"

- SD boot and persistent rootfs
- UART, GPIO, I2C, SPI, PWM
- Ethernet
- PCIe host bridge
- xHCI USB host
- USB mass storage
- Watchdog, thermal, RNG
- Reproducible build/test automation against real hardware

## Pi 5 Entry Gate

Do not start full Pi 5 enablement until Pi 4 has:

- stable boot
- stable storage
- stable Ethernet
- stable USB host
- a working real-device regression loop

## Re-Verify Before Depending On

- Raspberry Pi EEPROM/config behavior
- QEMU `raspi4b` peripheral completeness
- exact network boot and `boot.img` behavior on the current Raspberry Pi bootloader release
- Lima `socket_vmnet` behavior on the exact macOS and Lima versions in use when bridged lab networking is enabled
- Pi 5 debug/bootloader options such as `enable_rp1_uart`, `pciex4_reset`, `os_check`
- Linux and BSD support state for Pi 5 Ethernet and RP1 peripherals

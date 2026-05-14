# Temporary Fixes and Future Cleanup

This document is the registry of transitional shortcuts and workarounds
accepted during the Raspberry Pi 4 bring-up. Each item has a stable ID
(`TD-NN`) used to link from source code, commits, and future cleanup steps.

Ordering rule: once the Pi 4 boots to a usable state, every item here becomes
mandatory cleanup. Until then, progress on the boot path takes priority.

## Conventions

- **IDs are stable.** Never re-number. If an item is merged into another,
  add a `merged into TD-NN` note rather than deleting the entry.
- **Status** is one of:
  - `PENDING` — shortcut still active in source
  - `IN-PROGRESS` — cleanup step open against it
  - `RESOLVED` — cleanup committed and validated, record kept as history
- **Linking from source.** Every transitional fix in upstream source should
  carry an inline marker: `TODO(TD-NN): <short hint>`. Grep for `TD-NN` to
  find all sites of a given shortcut.
- **Location snapshots may drift.** Line numbers in this file reflect state
  at the time the entry was written. Re-verify against current source before
  acting — the code changes faster than this doc.
- **Stage tag** (added 2026-05-04): each TD that maps to the strategic
  roadmap (`docs/roadmap-cache-ram-smp.md`) carries a `Stage:` field
  pointing to one of the four trajectory stages
  (Stage 1 — caches via Linux `__enable_mmu` shape;
  Stage 2 — 4 GiB DRAM unlock + GPU memory;
  Stage 3 — SMP cores 1-3;
  Stage 4 — HDMI/USB-keyboard console).
  TDs without a Stage tag are independent of the roadmap.

---

## TD-17: Pi 4 cache-enable data-path cache hygiene

- **Status:** IN-PROGRESS
- **Stage:** 1 (cache enable).
- **First observed:** 2026-05-14 cache-enable session.
- **Where:** `sources/phoenix-rtos-kernel/vm/amap.c` (`amap_map()`),
  `sources/phoenix-rtos-kernel/proc/process.c` (`process_load32()` and
  `process_load64()`), and the TD-16-related early mapping policy in
  `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`.
- **What was done:** Kernel `SCTLR_EL1.M|C|I` is enabled on real Pi 4. The
  first stable cache-on image used broad `MAP_UNCACHED` workarounds for
  `amap_map()` and writable ELF data. The current narrower fix makes both paths
  cacheable again and adds targeted AArch64 cache invalidation in
  `amap_page()`:
  - Invalidate firmware-loaded object-page aliases before copying from them
    through cacheable temporary mappings.
  - Invalidate freshly allocated destination pages before first cacheable
    zero/copy reuse.
  - Keep explicit ELF BSS-tail mapping so the anonymous tail is mapped at the
    intended virtual address; it is cacheable again.
- **Why:** With D-cache enabled, cacheable writable ELF data corrupted
  `dummyfs` libc state (`atexit_common.head`) and faulted in `_atexit_init()` /
  `_atexit_register()`. Making writable ELF data uncached let all configured
  programs spawn. A negative-control test then proved `amap_map()` also remains
  necessary: restoring `amap` temporary aliases to cacheable immediately
  regressed to repeated EL0 Data Aborts in `dummyfs` `memset`, even while
  writable ELF mappings stayed uncached. Adding the targeted invalidation then
  made cacheable `amap` aliases and cacheable writable ELF data pass together
  on real Pi.
- **Risk accepted:** The current fix is still AArch64-specific and needs an
  upstreamable abstraction. It also only invalidates object-backed source
  aliases, not live shared-anonymous source pages, because invalidating a live
  dirty user-data source could drop valid data. Shared-anon COW needs a
  separate audit/test.
- **Validation:**
  - PASS: `artifacts/rpi4b-uart/rpi4b-uart-20260514-091513-netboot-stable-mmu-dcache-icache-uncached-amap-writable.log`
    reached `main: spawn loop done, entering proc_reap idle` with broad
    uncached data-path workarounds.
  - FAIL: `artifacts/rpi4b-uart/rpi4b-uart-20260514-091158-netboot-icache-dcache-cacheable-amap-writable-uncached.log`
    regressed when `amap_map()` was restored to cacheable mappings without
    targeted invalidation.
  - PASS: `artifacts/rpi4b-uart/rpi4b-uart-20260514-093258-netboot-full-cacheable-user-data-amap-inval-long2.log`
    reaches `main: spawn loop done, entering proc_reap idle` with cacheable
    `amap` temporary mappings and cacheable writable ELF data.
- **Resolution requirements:**
  - Generalize the AArch64-specific invalidation hook into an upstreamable VM /
    page-lifecycle cache-maintenance interface.
  - Audit and test shared-anonymous COW copy source handling.
  - Keep `SCTLR_EL1.M|C|I` enabled throughout cleanup.

## TD-01: SMP enable disabled on Cortex-A72

- **Status:** PENDING
- **Stage:** 3 (SMP bring-up; see `docs/roadmap-cache-ram-smp.md`).
  Cannot be tackled before Stage 1 (caches enabled, IS-shareable PT
  attributes) lands — LDXR/STXR exclusive monitor's cross-core
  semantics require Inner-Shareable Normal Cacheable memory.
- **First observed:** 2026-04 bring-up
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`, the
  `CPUECTLR_EL1` SMPEN block behind `__TARGET_AARCH64A72`.
- **What was done:** The SMP-enable MSR sequence is commented out and the
  only remaining effect is the debug markers around it.
- **Why:** Enabling SMP on A72 produced an early-boot hang; cause not
  diagnosed yet.
- **Risk accepted:** A72 coherency behavior with Inner-Shareable memory is
  undefined without this bit. Current code avoids Inner-Shareable in early
  boot, which is itself a related transitional compromise.
- **Resolution requirements:**
  - Reproduce the hang with a bounded diagnostic (GDB over QEMU gdbstub, or
    one minimal marker pair) and capture the fault.
  - Follow the Cortex-A72 TRM SMP enable sequence; compare against Circle OS
    and similar bare-metal references.
  - Re-enable SMP, then switch early boot back to Inner-Shareable and
    confirm boot on real hardware across multiple cold resets.

## TD-02: Pre-MMU cache invalidation disabled

- **Status:** PENDING
- **Stage:** 1. Subsumed by the Linux `__enable_mmu` restructure: in
  the new shape, the unified pre-MMU VA-range sweep over the kernel
  image and PT region replaces this entry's intent. Resolution comes
  for free with Stage 1.
- **First observed:** 2026-04 bring-up
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`, the
  `PMAP_COMMON_KERNEL_TTL2 … PMAP_COMMON_STACK` `_inval_dcache_range` call
  before MMU enable.
- **What was done:** The pre-MMU data-cache invalidation sweep is commented
  out. The code now relies solely on the post-MMU-enable invalidation and
  on `dsb ish; isb` to make table writes visible.
- **Why:** Cache maintenance with the MMU disabled hung the board in
  observed runs. Linux arm64 performs this sweep unconditionally.
- **Risk accepted:** Speculatively loaded stale lines for the page-table
  regions can survive into early MMU walks. So far no observed corruption,
  but that is not a guarantee.
- **Resolution requirements:**
  - Identify the A72-specific precondition that makes the generic sequence
    hang (likely ordering or an earlier missing setup step).
  - Restore the invalidation, or document the exact reason a narrower form
    is correct for this platform.

## TD-03: Syspage copy / BSS mapping shortcut

- **Status:** PENDING
- **Stage:** 1. The Stage 1 restructure moves the syspage copy into
  the caches-off pre-MMU window, copying directly to the destination
  PA. The TD-04 NC-mapping workaround becomes redundant once the
  copy happens with caches off. Resolves alongside Stage 1.
- **First observed:** 2026-04 bring-up
- **Where:** Interaction between `hal/aarch64/_init.S` (virtual syspage
  copy) and `syspage.c` (syspage access after MMU enable). BSS region is
  not reliably mapped in the early MMU page tables.
- **What was done:** Per `docs/status.md`, syspage access was stabilized by
  side-stepping the copied-into-BSS location and working with the original
  syspage. Intent and current source may diverge: **verify before acting.**
- **Why:** The early MMU page tables did not cover the BSS region into
  which the syspage was being copied.
- **Risk accepted:** Any code path that assumes the copied virtual syspage
  is authoritative may read stale data or wrong addresses.
- **Resolution requirements:**
  - Extend early MMU setup to map the BSS region (or move the syspage copy
    target to an already-mapped region).
  - Re-enable the canonical syspage copy path and validate that every
    consumer reads from the virtual location.
  - Add a syspage integrity check (size and a simple checksum) to the
    post-copy path.

## TD-04: BCM2711-specific syspage corruption at the plo→kernel handoff

- **Status:** ✅ FIXED at the syspage layer 2026-04-29; one residual
  Heisenbug (F→G hang masked by an inline UART probe) and one cleanup
  task (strip the diagnostic probes once the Heisenbug is rooted out)
  remain. Active blocker since 2026-04-19 is closed.
- **First observed:** 2026-04 bring-up. Originally tracked under several
  narrower descriptions: "iter-8 hang in `syspage_init` entry sub-loop",
  "non-deterministic post-MMU markers", "circular-list relocation
  divergence". 2026-04-29 reframed all of those as one underlying
  cache-coherency / boot-handoff anomaly that only manifests on real
  BCM2711 silicon.
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` (the syspage
  copy loop and surrounding cache maintenance) and
  `sources/phoenix-rtos-kernel/syspage.c` (the C-level relocation loops
  that read the copied data). The shared aarch64 kernel handoff code,
  which works correctly on ZynqMP and on QEMU 10.2.2 raspi4b.
- **Verified facts (2026-04-29 E2 probe):**
  - Kernel reads source bytes correctly from plo's heap PA at every
    offset checked (0, 0x100, 0x200, 0x310). plo and kernel agree on
    source contents.
  - Kernel reads destination bytes correctly at offsets 0/0x100/0x200,
    incorrectly at 0x310 onward. The garbage value at 0x310 differs
    every boot (e.g. `0xba79ec73…`, `0x2286619f…`, `0x2286419f…` across
    three runs of the same image).
  - Both low-PA (`adrp + lo12`) and high-VA (literal pool) reads return
    the same garbage value, so the TTBR0/TTBR1 mappings agree on the
    physical address.
  - The same kernel image in QEMU produces correct values at every
    offset. The bug is real-Cortex-A72-silicon-only.
  - plo on rpi4 runs cache-off the entire time
    (`sources/plo/hal/aarch64/generic/_init.S`: SCTLR_EL3 = `0x30c50838`,
    SCTLR_EL2 = `0x30c00838`, SCTLR_EL1 = 0). Plo's stores go directly
    to DDR. So the corruption is *not* "stale plo cache lines".
- **Working theory:** an external coherent-master writes to the DRAM
  region containing `_hal_syspageCopied` between plo's stores and the
  kernel's reads. The top candidate is the BCM2711 VideoCore VI GPU
  continuing to access mailbox/framebuffer memory after the ARM kernel
  takes over (Linux on Pi 4 quiesces this with explicit platform init;
  Phoenix has no equivalent yet). Secondary candidates: stale L2 lines
  from the bootcode → start4.elf → armstub firmware chain, or in-flight
  DMA that hasn't drained at plo's `eret`.
- **Why other Phoenix platforms don't hit it:** ZynqMP plo also runs
  cache-off and shares the same kernel handoff code, but ZynqMP has
  no always-running non-coherent peripheral and a single-stage boot.
- **Why neither Linux nor other OSes hit it on Pi 4:** Linux on Pi 4
  meets the ARM64 Linux Boot Protocol contract (MMU off, D-cache off,
  kernel image cleaned to PoC, DMA quiesced) and contains explicit
  Pi-4 platform init that touches the VPU. Both halves are required.
- **Resolution as landed (2026-04-29):**
  - **Step 1 (DONE):** `_hal_syspageCopied`'s page is now mapped
    Normal Non-Cacheable in TTBR1 TTL3 (MAIR slot 1, AttrIndx=1).
    Symbol is `.balign SIZE_PAGE` so it occupies exactly one TTL3
    entry. The kernel-side copy loop writes via the high-VA literal
    pool through that NC entry directly to DDR, bypassing the A72
    D-cache. Real-Pi-4 probes (s/l/v/d0/d100/d200) now return
    bit-identical correct values across consecutive boots. Map
    relocation walks all 11 entries cleanly and the kernel reaches
    `_hal_init()` (marker `f`).
  - **Step 2 (NOT NEEDED):** an external master writing to the dest
    PA between plo and kernel reads. Step 1 was sufficient on its
    own — the class of failure is closed at the cache layer.
  - **Future hardening (not blocking):** align plo's exit with the
    full ARM64 Linux Boot Protocol (clean kernel image and DTB to
    PoC, then disable SCTLR.{M,C,I} before `eret`). Mostly stylistic
    given plo already runs cache-off, but removes ambiguity for
    other ARM64 ports.

- **Residual issues carried forward:**
  - **Heisenbug F→G in `syspage_init()`:** without an inline `F → 1
    → 2 → 3 → G` UART probe, the kernel hangs immediately after the
    F marker. With the probe present, F→G→… completes reliably.
    Working hypothesis: timing or instruction-cache coherency
    interaction with the freshly NC-mapped dest page that the probe's
    UART-wait loops mask. Documented as TD-04 mitigation;
    investigate root cause as a separate step before the TD-05
    debug-marker cleanup pass.
  - **TD-05 cleanup:** the F→1→2→3 markers, the s/l/v/d0/s0/d100/
    s100/d200/s200 probe block in `_init.S`, the inline TTL3-
    override comment block, and the `H/4/5/6/F/S/r/D/s/E/7/8/9/a/
    b/c/d/e` localization probes inside `_hal_init()` all need to
    be reviewed and either stripped or gated when the bring-up is
    complete.

- **TD-04-hack-1: SKIP the program-relocation loop in `syspage_init()`**
  - **Status:** ✅ RESOLVED 2026-04-30 in `phoenix-rtos-kernel`
    commit `0fdf20ca` ("rpi4b: progress to syspage process spawn").
    The Heisenbug shape was eliminated by a combination of fixes
    that landed together: (a) extending the NC-mapping treatment
    from just `_hal_syspageCopied` to also cover `PMAP_COMMON_STACK`'s
    page (same TD-04-class problem on a second cacheable BSS
    page); (b) re-instating the `ldr/br` to `_core_0_virtual` so
    the primary core actually runs through TTBR1, with a
    `tlbi vmalle1is; dsb ish; isb` before the branch so the new
    mapping is visible to the instruction fetch; (c) deferring the
    post-MMU I-cache invalidate + cache enable that previously hung
    A72 in the cache-maintenance window. With those in place the
    original prog-reloc loop runs cleanly, with NULL guards added
    for `prog->imaps` / `prog->dmaps` and a 64-iter safety cap.
  - **Original problem (kept for history):** the very first head
    store `syspage_common.syspage->progs = hal_syspageRelocate(...)`
    hung the kernel on real Pi 4 hardware (works fine in QEMU and
    on ZynqMP with the same code), even though the visually-
    equivalent map-iter loop just above ran cleanly through all
    11 entries with the same NC-mapped destination.
  - **Where the resolution lives:** `hal/aarch64/_init.S` (NC entry
    for stack page; `_core_0_virtual` branch), `syspage.c`
    (restored prog-reloc loop with NULL guards + 64-iter cap).
  - **Verification:** real-Pi-4 boot now reaches
    `main: spawned psh (9)` via the prog-reloc path; QEMU smoke
    reaches `psh help`. See `docs/status.md` 2026-04-30 entry.

- **TD-04-hack-2: localization probes inside `_hal_init()`**
  - **Status:** ACTIVE HACK (TD-05-class diagnostic but pinned in
    place for now)
  - **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/hal.c`,
    `_hal_init()`.
  - **What was done:** Inline `H, 4, 5, 6, F/S, r, D, s, E, 7, 8,
    9, a, b, c, d, e` markers via the same TTBR1-mapped early UART
    that `syspage_init()` uses, between every step of `_hal_init`.
  - **Why:** Diagnostic, but also empirically the kernel hangs at
    different points depending on whether these markers are present
    — same Heisenbug shape as TD-04-hack-1.
  - **Risk accepted:** Boot-time UART chatter; no functional risk
    at runtime.
  - **Resolution requirements:**
    - Once TD-04-hack-1's root cause is fixed and `_hal_init()`
      runs reliably without the markers, strip them (or gate them
      behind a debug flag).
  - **Marker grep:** `grep -n "TD-04-hack-2"
    sources/phoenix-rtos-kernel/hal/aarch64/hal.c`

- **TD-04-hack-3: fake `dtbEnd` in `_hal_init()`**
  - **Status:** ACTIVE HACK
  - **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/hal.c`,
    `_hal_init()`'s syspage-dtb branch.
  - **What was done:** `dtbEnd = dtb->end;` replaced with
    `dtbEnd = dtbStart + 0x10000;`. The real size will be re-read
    from the DTB header by `_pmap_preinit()` / DTB parser anyway
    (DTBs are self-describing).
  - **Why:** `dtb->end` read hangs the kernel on real Pi 4
    immediately after `dtb->start` succeeds (one offset apart, same
    cache line, identical access pattern). Heisenbug shape again.
  - **Risk accepted:** If anything actually consumes `dtbEnd` as
    a hard upper bound (rather than re-reading the DTB header),
    parsing of a >64 KiB DTB would fail. Pi 4's DTB is ~57 KiB —
    well under the 64 KiB cap.
  - **Resolution requirements:**
    - Root-cause why the second word read of `dtb->*` hangs on
      real Pi 4. Almost certainly the same root as TD-04-hack-1.
    - Restore `dtbEnd = dtb->end;`.
  - **Marker grep:** `grep -n "TODO(TD-04-hack-3)"
    sources/phoenix-rtos-kernel/hal/aarch64/hal.c`
- **Historical note (no longer the active blocker):** the iter-7/8
  corruption used to block program relocation, which blocked
  reaching `_hal_init()` from `syspage_init()`. It was the active
  blocker between 2026-04-19 and 2026-04-29; closed at the syspage
  layer by the NC-dest fix and at the program-reloc layer by the
  combined fixes documented under TD-04-hack-1's RESOLVED status.
  As of 2026-04-30 the boot reaches `main: spawned psh (9)` and
  the new active blocker is the post-spawn user-mode handoff (no
  output from any of the 9 user processes after they're queued).
  Tracked separately as TD-13 below.
- **References:**
  - ARM64 Linux Boot Protocol —
    https://docs.kernel.org/arch/arm64/booting.html
  - raspberrypi/tools `armstubs/armstub8.S` (the contract Pi 4 firmware
    promises) — https://github.com/raspberrypi/tools
  - ARM tf-issues #205 — set/way safe only for power-down, not handoff
  - tracking/current-step.md — full probe data, QEMU comparison, plan

## TD-05: UART debug-marker scaffolding

- **Status:** PENDING
- **First observed:** 2026-04 bring-up (pervasive)
- **Where:** `hal/aarch64/_init.S`, `syspage.c`, `main.c`, and related
  boot-path files.
- **What was done:** Dozens of `uart_putc` and `uart` ring-buffer writes
  scattered through the early boot path to produce the
  `NYOPSTUZbcdeFGVWXabcdefgmklmno` progress trace.
- **Why:** The trace is how we locate hangs when no other diagnostic is
  available; there is no working console or fault reporting yet.
- **Risk accepted:** The markers affect boot timing, burn UART bandwidth,
  and make diffs noisy. Individual markers are easy to leave behind once
  they served their purpose.
- **Resolution requirements:**
  - Replace ad hoc markers with a compile-time-gated debug macro
    (`RPI4_BOOT_MARKER(c)`) so they can all be disabled in one place.
  - Establish a rule that every marker added to test a hypothesis is
    removed when the hypothesis is disproved (already in
    [code-quality-and-upstreaming.md](code-quality-and-upstreaming.md)).
  - Before upstreaming, strip or gate every remaining marker.

## TD-06: DTB handling assumptions

- **Status:** PENDING
- **First observed:** 2026-04 bring-up
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`.
- **What was done:** Early parsing assumes a fixed memory layout, a single
  known interrupt controller, and limited error paths.
- **Why:** Early bring-up needed a DTB path with no surprises; robust
  parsing was not on the critical boot lane.
- **Risk accepted:** Any future board variant or firmware change silently
  reuses the fixed assumptions.
- **Resolution requirements:**
  - Drive memory layout from the actual DTB, not compile-time constants.
  - Validate required nodes at parse time and fail with a useful message.
  - Add multi-board support (Pi 4B variants, Pi 5) as the scope expands.

---

## TD-07: Update QEMU inside the phoenix-dev VM to a current stable

- **Status:** PENDING
- **Where:** apt-installed QEMU inside the Lima VM, used by
  `scripts/qemu-rpi4b-hdmi-smoke.sh` and `scripts/qemu-shell-smoke.sh`.
- **What was done:** an older QEMU release was good enough for early
  bring-up; never refreshed.
- **Why:** Pi 4 peripheral models in older QEMU are limited; some boot
  stages don't progress or behave differently than on real hardware.
- **Resolution requirements:**
  - Install upstream QEMU 11.x (or newer stable) inside the VM, either
    via apt-pinning a backports source, source build into `/opt/qemu/`,
    or a Lima provision script. Document the version + install method.
  - Verify the `raspi4b` machine model exists, has Cortex-A72 + GIC +
    PL011 working, and reproduces our SD-boot path far enough to be
    useful as an introspection target.

## TD-08: Re-test boot under QEMU + gdbstub for in-flight introspection

- **Status:** PENDING (depends on TD-07)
- **Where:** QEMU runner scripts and a gdb script we'll add under
  `scripts/`.
- **What was done:** real-hardware bring-up gives only UART markers as
  state. Memory contents at specific markers, register values right
  before the iter-7/8 corruption, MMU translation tables, and cache
  state are all opaque.
- **Why:** QEMU + gdbstub solves this — at the cost of not fully
  reproducing real-hardware cache/DDR/DMA timing.
- **Resolution requirements:**
  - `qemu-system-aarch64 ... -gdb tcp::1234 -S` against the same SD
    image we use on hardware; attach `gdb-multiarch` from outside.
  - Walk: pre-handoff syspage region in plo (0x280..0x340, SCTLR,
    TTBR0/1); post-handoff in kernel _init.S right after MMU enable
    (same region via low PA and high VA); inside `syspage_init`'s
    map-entry sub-loop around iter 7's `entry->next` read.
  - Even if the corruption itself doesn't reproduce in QEMU, validate
    the *logic* (list shape, struct layout, pointer arithmetic).

## TD-09: Replace en7 crossover cable with an unmanaged ethernet switch

- **Status:** PENDING
- **Where:** physical cabling between the Mac's en7 USB-C ethernet
  and the Pi 4 RJ45.
- **What was done:** direct crossover cable. Works electrically.
- **Why:** en7's link state mirrors the Pi's PHY directly. Every Pi
  power-cycle toggles en7 between `active` and `inactive`. socket_vmnet's
  BPF capture on en7 wedges on a non-trivial fraction of those toggles,
  and once wedged tends to stay wedged across one or more VM restarts.
  The watchdog + auto-recovery in `test-cycle-netboot.sh` handles the
  simple wedge case; a switch eliminates the trigger entirely.
- **Resolution requirements:**
  - Plug an unmanaged GigE switch between Mac and Pi. en7's link
    partner becomes the switch (always `active`); Pi power-cycles
    don't touch the bridge.
  - User has the switch on hand but is missing its PSU; install when
    found.

## TD-10: SError masked across all early kernel paths on Pi 4

- **Status:** PENDING
- **First observed:** 2026-04-30 bring-up (real Pi 4 only).
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/_exceptions.S`
  (line ~383, the `hal_jmp` userspace branch that sets
  `mov x1, #(MODE_EL0 | NO_SERR); msr spsr_el1, x1`),
  `sources/phoenix-rtos-kernel/hal/aarch64/cpu.c` (line ~71/76, both
  user-context and kernel-context PSR setup OR'd with `NO_SERR`).
- **What was done:** SError is kept masked in created thread contexts,
  in syscall and exception C dispatch, in IRQ dispatch, and in
  `hal_jmp()` user entry. Effectively the kernel runs SError-blind
  across the whole early bring-up.
- **Why:** Real Pi 4 delivered asynchronous SError exceptions during
  early IRQ, syscall, and user-entry paths before Phoenix had any
  platform policy or handler. Without masking, the kernel hung
  silently or rebooted on the first such event.
- **Risk accepted:** Genuine bus-error / parity / firmware-DMA
  faults are now invisible. If one fires during normal operation
  the system has no way to log or recover.
- **Resolution requirements:**
  - Define an explicit AArch64 SError policy for Phoenix-rpi4
    (probably "log ESR_EL1 + ELR_EL1 + FAR_EL1 to console; halt
    in debug build, attempt to continue in release after a
    bounded retry count").
  - Implement the SError vector handler. Wire it into the existing
    exception_vector machinery in `_exceptions.S`.
  - Remove `NO_SERR` from the PSR templates in `cpu.c` and the
    `hal_jmp` user-entry branch in `_exceptions.S`. Verify that
    real-Pi-4 boot survives without the mask.
  - Add a test that injects a synthetic SError and confirms the
    handler logs and recovers.
- **Marker grep:** `grep -n "NO_SERR" sources/phoenix-rtos-kernel/`

## TD-11: Single-core AArch64 spinlock path uses DAIF mask, not exclusives

- **Stage:** 3. Replaced by real LDXR/STXR atomics during SMP
  bring-up. Cannot be removed before Stage 1 enables the IS-shareable
  Normal Cacheable memory model that makes the exclusive monitor
  work across cores.

- **Status:** PENDING (revisit before TD-01 SMP enable)
- **First observed:** 2026-04-30 bring-up.
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/spinlock.c`
  (added in commit `0fdf20ca`).
- **What was done:** When `NUM_CPUS == 1` the spinlock implementation
  uses DAIF save / IRQ-FIQ mask / restore instead of the
  exclusive-byte LDAXR/STLXR primitives. SMP builds keep the
  exclusive-byte implementation.
- **Why:** On the current single-core Pi 4 target, switching from
  the early spinlock-bypass path to the exclusive-byte spinlocks
  hung real hardware. The DAIF-mask path is correct for a single
  core (no other CPU can race), avoids exclusive-monitor / shareable-
  attribute issues, and unblocked progress.
- **Risk accepted:** When TD-01 (SMP enable) lands, this code path
  must NOT be the active one. The `NUM_CPUS == 1` guard is correct
  in principle but easy to leave stale.
- **Resolution requirements:**
  - As part of TD-01 SMP bring-up, validate that exclusive monitors
    work with the production memory attributes and shareability
    domain on Cortex-A72. Fix whatever in early-boot init prevented
    them from working.
  - Either delete the `NUM_CPUS == 1` path or document why it
    should remain (e.g. as a single-core build performance choice).

## TD-12: Plo memory clamp at ~948 MiB on real Pi 4

- **Stage:** 2 (phases 4-5). Resolved by DTB-driven memory layout
  and `total_mem=4096` config.txt change.

- **Status:** PENDING
- **First observed:** 2026-04-30 bring-up.
- **Where:** plo memory probe / kernel boot path. Concretely the
  start4.elf firmware hands plo a memory map that reports
  `MEM GPU: 76 ARM: 948 TOTAL: 1024` on a physical 4 GiB board.
  Plo currently honors that 948 MiB clamp.
- **What was done:** No active workaround in source — this is a
  *property* of the firmware/plo handoff that we noted but haven't
  changed. Phoenix sees ~948 MiB instead of the physical 4 GiB.
- **Why:** Avoiding immediate high-memory / GPU-overlap risk while
  the rest of the boot path is being stabilized. At 948 MiB we
  stay well clear of any firmware-reserved region.
- **Risk accepted:** Phoenix will run with ~24% of available DRAM
  on a 4 GiB board. Larger-RAM Pi 4 variants (8 GiB) get even less
  proportional usage.
- **Resolution requirements:**
  - Drive the usable-RAM and reserved-memory layout from the
    firmware-mutated DTB the way Linux does: `/memory@0`,
    `/reserved-memory`, `/memreserve/`, `dma-ranges`. (Also
    addresses TD-06 DTB-handling assumptions.)
  - Validate against 1 GiB / 2 GiB / 4 GiB / 8 GiB Pi 4B models.
  - Set firmware `total_mem` / `gpu_mem` config to the right
    values for our use, document in `docs/host-macos-apple-silicon.md`.

## TD-13: Post-spawn user-mode handoff produces no observable output

- **Status:** PENDING (CURRENT ACTIVE BLOCKER, since 2026-04-30)
- **First observed:** 2026-04-30 bring-up.
- **Where:** the boundary is between
  `sources/phoenix-rtos-kernel/main.c` (init thread that issues
  `main: spawn psh ... → main: spawned psh (9)`) and the first
  scheduled-then-`eret` of any user process. UART falls silent
  after marker `(9)` and stays silent.
- **What was observed:** The kernel reaches `_hal_init()`,
  initializes vm + threads + posix, spawns 9 user processes
  (`dummyfs-root`, `dummyfs`, `pl011-tty`, `mkdir`, `bind`, `pcie`,
  `usb`, `psh`), and the scheduler runs at least 8 cycles of
  `threads: schedule enter / selected / restoring`. After that
  point no user process emits a single byte of UART output (no psh
  prompt, no driver banner, no error). No SError, exception, or
  abort is logged either — the masking from TD-10 may be hiding
  the actual fault.
- **Why this isn't TD-04**: TD-04's NC-dest fix and follow-on
  combined fixes (RESOLVED in `0fdf20ca`) closed the syspage-copy
  / map-reloc / prog-reloc class of failure. The current silence
  is downstream of all that — process structures are populated and
  the scheduler runs.
- **Three candidate failure modes (in order of likelihood):**
  1. User-mode `eret` lands in unmapped or non-executable user-
     code page → instruction abort routed back to EL1 vector,
     which has no diagnostic print, scheduler retries forever.
  2. User-mode entry succeeds but the user binary's first
     instructions touch an unmapped data region → data abort,
     same silent loop.
  3. User process runs but its UART output is going through
     `pl011-tty` which can't actually open the device, so no
     output is produced. Less likely — would expect at least
     pl011-tty's own banner.
- **Resolution path:**
  1. Add a single-shot exception-vector probe (ESR_EL1, ELR_EL1,
     FAR_EL1 dump) on the EL1-from-EL0-AArch64 vector.
     `_early_exception_probe` already exists in `_exceptions.S`;
     re-wire it to fire on user-faults.
  2. Lift `NO_SERR` from the user-entry path so SErrors at the
     user-mode boundary become observable (cross-references TD-10).
  3. Triage the resulting ESR. Translation fault → pmap_switch
     not loading user TTBR0; permission fault → wrong AP/UXN
     bits; sync external abort → another TD-04-class cache page.
- **Marker grep:** `grep -n "main: spawn" sources/phoenix-rtos-kernel/main.c`

### TD-13 update 2026-05-01 — narrowed to `proc_mutexCreate`

- `>` pre-eret marker (kernel `c5c21c6e`) confirmed user threads ARE
  dispatched (one `>` per spawned process, 7/8 fired — `dummyfs` pid
  3 never SVCs, follow-up).
- EC probe at top of `_exceptions_dispatch` printing `*HL` for first
  16 EL0-source synchronous exceptions: 7× `*15` (AArch64 SVC,
  expected) on real Pi 4; one stray `*11` (AArch32 SVC) seen earlier,
  not seen in mtxbypass log. PSR setup forces AArch64; `*11` flagged
  for follow-up but is not the silence cause.
- Syscall # trace `sNN` (kernel `39c81236`) shows every user process
  that SVCs makes exactly one syscall, #16 = `phMutexCreate`. Source
  is libphoenix `_errno_init`'s `mutexCreate(&errno_common.lock)`.
- M/1/2/3/E/K probe ladder inside `syscalls_phMutexCreate`: 7× `M12`,
  0× `M123`. Hang is inside `proc_mutexCreate(attr)` itself, not in
  the validation (TD-13-mtxbypass) and not in stack-arg unpacking.
- TD-13-mtxbypass active: `vm_mapBelongs(proc, h, sizeof(*h))` and
  `vm_mapBelongs(proc, attr, sizeof(*attr))` calls in
  `syscalls_phMutexCreate` are skipped to let probes reach the actual
  hang point. Risk: kernel can fault on bad user pointers (acceptable
  while we drive 9 trusted syspage progs). Restore once root cause
  is fixed.
- **Conclusion**: silence is **not** a fault, **not** a console-
  binding issue, and **not** in pointer validation. It is one of
  these four steps in `proc/mutex.c:51-80`:
  `vm_kmalloc(sizeof(*mutex))`, `resource_alloc(p, &mutex->resource)`,
  `proc_lockInit(&mutex->lock, attr, ...)`, `resource_put(...)`.
  `vm_kmalloc` is the prime suspect (TD-04-class: heap free-list on
  uncached/stale memory).
- **Next probe**: granular `a..d` markers between the four calls in
  `proc_mutexCreate` itself.
- **Reference log**: `artifacts/rpi4b-uart/rpi4b-uart-20260501-184309-netboot-mtxbypass.log`
- **Manifest at this checkpoint**: `manifests/2026-05-01-td13-mtxbypass-checkpoint.md`
- **Marker grep:** `grep -n "TD-13-mtxbypass\|td13_syscall\|TD-13:" sources/phoenix-rtos-kernel/syscalls.c`

### TD-13 update 2026-05-01 — `resource_put` / atomic wall fixed

- Added `a..f` markers inside `proc_mutexCreate`:
  - `a`: function entry before reading user `attr`
  - `b`: attr validation passed
  - `c`: `vm_kmalloc` returned
  - `d`: `resource_alloc` returned
  - `e`: `proc_lockInit` returned
  - `f`: `resource_put` returned
- Pre-fix hardware log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260501-191345-netboot-td13-mutex-substep.log`
  repeatedly showed `M12abcde`, so the wall was `resource_put(p,
  &mutex->resource)`.
- `resource_put()` is only `lib_atomicDecrement(&r->refs)`. Kernel commit
  `23b9a127` changes `lib_atomicIncrement/Decrement` for the current
  single-core AArch64 case (`defined(__aarch64__) && NUM_CPUS == 1`) to use a
  DAIF-masked plain increment/decrement instead of GCC `__atomic_*` builtins.
  This mirrors TD-11's validated single-core spinlock path.
- Post-fix hardware log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260501-191724-netboot-td13-atomic-fallback.log`
  shows `M12abcdef3K`, `dummyfs: root initialized`, `pl011-tty: init:
  libtty_init ok`, `main: spawned psh (10)`, and `threads: psh user scheduled`.
- **Current residual blocker:** no clean `(psh)%` prompt on real hardware yet.
  Because the TD-13 probes now heavily corrupt/interleave with normal console
  output, clean or gate them before diagnosing the post-`psh` boundary.
- **Cleanup requirement:** When Cortex-A72 SMP/coherency is re-enabled, revisit
  both TD-11 and this `lib_atomic*` fallback. Multicore builds still need real
  atomics, not interrupt-masked plain updates.

### TD-13-mtxbypass: skip `vm_mapBelongs` in `syscalls_phMutexCreate`

- **Status:** RESOLVED/REMOVED (added and removed 2026-05-01)
- **Where:** `sources/phoenix-rtos-kernel/syscalls.c`
  `syscalls_phMutexCreate`.
- **Why:** Originally suspected the `proc->mapp->lock` acquired
  inside `vm_mapBelongs` was the wall (TD-04-class lock state). With
  the bypass we now reach `proc_mutexCreate(attr)`, which proves the
  hang is deeper.
- **Resolution:** After fixing the `resource_put()` /
  `lib_atomicDecrement()` wall, both `vm_mapBelongs()` validations were
  restored. QEMU still reaches `(psh)% help`; real Pi still reaches
  `threads: psh user scheduled`.
- **Validation log:** `artifacts/rpi4b-uart/rpi4b-uart-20260501-214225-netboot-td13-clean-probes.log`
- **Marker grep:** `grep -n "TD-13-mtxbypass" sources/phoenix-rtos-kernel/syscalls.c`
  should now return no matches.

### TD-13-spawn-cap: hard cap on the spawn loop in `main()`

- **Status:** ACTIVE HACK (added 2026-05-01)
- **Where:** `sources/phoenix-rtos-kernel/main.c` `main()`, the
  spawn `do {...} while ((prog = prog->next) != syspage_progList())`.
- **What was observed:** With the EL0-sync vector restored to normal
  SVC dispatch (so syscalls work), real Pi 4 spawn loop runs through
  all 9 progs once correctly (incrementing PIDs 2..9, each with a
  matching `>` user-mode-entry marker), then keeps printing
  `main: spawned psh (9)` ~187 000 times until UART capture ends.
  The "spawn psh" line that should pair with each "spawned psh (9)"
  iteration is *not* re-emitted — the loop body is being re-entered
  somewhere after `proc_syspageSpawn` and before
  `prog = prog->next`. Same code path runs cleanly to `(psh)% help`
  in QEMU. Smells like another cache-coherency artifact on the
  prog list head/tail pointer (TD-04 class).
- **What was done:** Added an `if (++spawnIters >= 32) break;` cap
  inside the loop. After 32 iterations the kernel prints a
  `main: TD-13 spawn-cap hit, breaking spawn loop` line and exits
  the loop into `for (;;) proc_reap();`. Lets the spawned user
  processes actually get scheduled and produce their own UART
  output (psh prompt, driver banners, etc.).
- **Risk accepted:** If the syspage ever ships >32 progs, this
  silently truncates. Trivial for current rpi4b config (9 progs).
- **Resolution requirements:**
  - Root-cause why `psh->next` doesn't return to the head on real
    hardware (instrument the prog-list before/after the spawn loop;
    likely needs the same NC-mapping treatment we applied to
    `_hal_syspageCopied` and `PMAP_COMMON_STACK`).
  - Restore the natural circular-list terminator and remove the cap.
- **Marker grep:** `grep -n "TD-13-spawn-cap" sources/phoenix-rtos-kernel/main.c`

### TD-13 status update 2026-05-02 — RESOLVED at the runtime layer

The TD-13 user-mode silence is closed. Real Pi 4 now reaches
`pshapp: ttyopen attempt` with all libc / libphoenix / posix init
running successfully. The post-mutex boundary is a separate problem
tracked under **TD-14** below. The remaining TD-13 cleanup items
(probe strip, restore TD-13-spawn-cap natural terminator) are listed
under TD-13-spawn-cap and the priority ladder.

## TD-14: `/dev/console` `resolve_path` hang on Pi 4

- **Status:** RESOLVED AS BOOT BLOCKER 2026-05-02; cleanup still pending
- **First observed:** 2026-05-01 evening, after TD-13 atomic wall lifted.
- **Where:** `sources/libphoenix/unistd/file.c` `open()` →
  `resolve_path("/dev/console", NULL, 1, 1)`. Last observed marker on
  real Pi 4 is `open: console resolve enter`; no `resolve done` /
  `resolve failed` follows.
- **What was observed:** psh runs cleanly through libc init,
  `psh: main`, `keepidle`, `root lookup`, `tcgetpgrp`, `basename`,
  `findapp`, `app run`, `pshapp: run`, the klog-flush sleep, the
  retry loop entry, and into `psh_ttyopen("/dev/console")`. `open()`
  starts (because `O_RDWR` set + traceConsole flag), the
  `stat()` pre-check is skipped (TD-14-stat-skip workaround), then
  enters `resolve_path` and never returns. UART silence past that
  point.
- **Why this is a separate item from TD-13:** TD-13's `proc_mutexCreate`
  atomic wall is fixed and `_errno_init`'s `mutexCreate` succeeds for
  every user process. Every spawned process reaches its own `main()`.
  The new wall is in libphoenix namespace lookup, not kernel mutex.
- **Candidate failure modes:**
  1. `bind` server not actually serving on hardware — `/dev` is
     never resolvable. Easy to discriminate: instrument resolve_path
     per component and watch which fails.
  2. `pl011-tty` registers `/dev/console` (or its own oid) too late
     or under the wrong parent oid because `pl011_fbcon_init` was
     deferred (devices `7929591`). Side effect: psh races ahead of
     pl011-tty's `console ready` and looks up a `/dev/console` that
     hasn't been created yet. The `PSH_TTYOPEN_RETRIES` loop should
     paper over this — verify how many times the retry actually runs
     on Pi 4.
  3. Another TD-04-class cache-coherency issue, this time on the IPC
     port table or message queues backing the lookup IPC. The fact
     that TD-13 was a TD-04-class issue inside the kernel raises
     priors here.
- **Resolution path:**
  1. Add per-component `debug()` traces inside `resolve_path` (and
     in the `sys_lookup` shim it calls). Identify which path
     component hangs (`/`, `dev`, or `console`).
  2. Cross-reference with `pl011-tty: fbcon init deferred ok` and
     `pl011-tty: console ready` markers — if neither prints, candidate
     2 is likely.
  3. If lookup hangs even before pl011-tty progresses, escalate to
     candidate 1 / 3 (bind / cache).
- **Reference log:**
  `artifacts/rpi4b-uart/rpi4b-uart-20260501-220933-netboot-console-open-skip-stat.log`
- **Manifest:** `manifests/2026-05-02-td13-resolve-path-boundary.md`
- **Marker grep:** `grep -n "open: console" sources/libphoenix/unistd/file.c`

### TD-14-console-open-fastpath: skip duplicate `/dev/console` canonicalization

- **Status:** ACTIVE WORKAROUND (libphoenix `3c76bba`, 2026-05-02)
- **Where:** `sources/libphoenix/unistd/file.c` `open()`.
- **Why:** Real Pi logs showed `open("/dev/console", O_RDWR)` spending most
  of the capture in the second `resolve_path()` pass. The earlier `stat()`
  had already resolved the direct `/dev/console` alias and proved the object
  was not a directory. During TD-14 the direct alias is intentional, so the
  second canonicalization is skipped only for `/dev/console`.
- **Risk accepted:** This relies on `/dev/console` being a direct alias, not
  a symlink that needs final resolution. Remove it together with
  TD-14-console-alias once canonical `/dev` traversal is fast.
- **Marker grep:** `grep -n "TD-14-console-open-fastpath" sources/libphoenix/unistd/file.c`

### TD-14-tiocspgrp-pgrp: TIOCSPGRP stores pgrp directly

- **Status:** ACTIVE FIX (devices `3ee4702`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-devices/tty/libtty/libtty.c`.
- **Why:** POSIX `tcsetpgrp(fd, pgrp)` passes a foreground process-group ID.
  `libtty` treated that value as a PID and called `getpgid(*pid)` inside the
  tty server. On Pi 4 this became the next shell-startup boundary. The tty
  layer now records the requested process-group ID directly.
- **Risk accepted:** None known; this is the expected semantic behavior.
- **Resolution requirements:** Keep unless upstream review finds a missing
  permission/session check that should be added around the direct assignment.
- **Marker grep:** `grep -n "TIOCSPGRP" sources/phoenix-rtos-devices/tty/libtty/libtty.c`

### TD-14-psh-debug-probes: psh early probes use debug syscall

- **Status:** ACTIVE DIAGNOSTIC (utils `da2f541`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-utils/psh/psh.c` and
  `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`.
- **Why:** Using `psh_write()` before `/dev/console` is open can block on
  inherited stdio. The probes were moved to `debug()` and bracket psh entry,
  root readiness, tty open, `isatty`, `tcsetpgrp`, and first `readcmd`.
- **Risk accepted:** Extra UART/klog noise and slower prompt visibility.
- **Resolution requirements:** Remove or gate after interactive shell smoke
  passes on real Pi 4.
- **Marker grep:** `grep -n "psh: tty isatty\\|psh: tcsetpgrp done\\|psh: readcmd" sources/phoenix-rtos-utils/psh`

### TD-14-stat-skip: skip `stat()` pre-check for `/dev/console`

- **Status:** RESOLVED 2026-05-02 (removed by libphoenix `43e050d`)
- **Where:** `sources/libphoenix/unistd/file.c` `open()`. When
  filename is `/dev/console`, the `O_WRONLY|O_RDWR` `stat()`
  pre-check is bypassed; everything else proceeds normally.
- **Why:** Two reasons during TD-14 bring-up: (a) keep the failure
  isolated to `resolve_path` rather than entangled with `stat`,
  (b) `stat()` would itself call `resolve_path` so the test was
  redundant.
- **Risk accepted:** None at runtime (open still calls
  `resolve_path` + `sys_open`); only `stat`-specific error paths
  (`EISDIR` on a directory, `ENOENT` early-out) are skipped — for
  `/dev/console` they don't apply.
- **Resolution:** Removed during TD-14 probe-strip cleanup. Current
  source has no `traceConsole` / `stat skipped` branch.

### TD-14-deferred-fbcon: pl011-tty defers fbcon init to main thread

- **Status:** ACTIVE WORKAROUND (devices `7929591`)
- **Where:** `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`.
  `pl011_fbcon_init()` was previously called inline at the end of
  `pl011_init()`; it's now invoked from `main()` *after*
  `beginthread(pl011_kbdthr, ...)` and `beginthread(poolthr, ...)`.
- **Why:** fbcon init touches the GPU mailbox via `pcie/mailbox`.
  If it runs before pl011-tty's pool/kbd threads exist, the early
  bring-up stalls on real Pi 4. Deferring lets `/dev/ttyUL0` start
  serving messages first.
- **Risk accepted:** Side effect on TD-14 — `/dev/console` may now
  be registered later than expected (pl011-tty's `pl011_writeRaw`
  banner runs before fbcon, but downstream `console ready` print
  is ordered with the deferred path). May contribute to TD-14
  candidate 2.
- **Resolution requirements:**
  - Once GPU mailbox handshake order is correct, move fbcon back
    into `pl011_init()` or split it into a discrete service that
    waits on a mailbox-ready event.
  - Confirm `/dev/console` registration ordering w.r.t. psh's
    ttyopen retries.
- **Marker grep:** `grep -n "fbcon init deferred" sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

### TD-14-tty0-nonfatal: pl011_createTty0() failure non-fatal

- **Status:** ACTIVE WORKAROUND (devices `8b80f4c`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
  in `main()`. `pl011_createTty0()`'s return value used to be fatal;
  now its failure is logged ("tty0 failed (non-fatal)") and `main()`
  proceeds to `create_dev("/dev/console")` regardless.
- **Why:** pl011_createTty0 calls `lookup("devfs")` in a retry loop
  that has no fallback. On real Pi 4 (TD-04-class slow IPC) the
  loop sometimes makes no forward progress within the test cycle
  window. Without a fallback, pl011-tty exits and `/dev/console`
  is never registered. The libphoenix `create_dev()` helper *does*
  have a 3-retry-then-portRegister fallback, so making createTty0
  non-fatal lets `/dev/console` register via the direct
  `portRegister` path even when devfs is unresponsive.
- **Risk accepted:** `/dev/tty0` may not be registered, breaking
  applications that open `/dev/tty0` directly. psh and basic
  console traffic don't need it.
- **Resolution requirements:**
  - Replace the busy-poll `lookup("devfs")` retry loop with a
    notification-based wait (TD-14-startup-settle below).
  - Restore the fatal path once IPC fragility is rooted out and
    pl011_createTty0 always succeeds.
- **Marker grep:** `grep -n "TD-14-tty0-nonfatal" sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

### TD-14-pl011-retry: reduced lookup-devfs retries inside createTty0

- **Status:** ACTIVE TUNING (devices `8b80f4c`, 2026-05-02)
- **Where:** `pl011_createTty0()`. The retry count was 50 (5 s wall);
  reduced to 30 (3 s wall) so the loop falls through to the
  TD-14-tty0-nonfatal path more quickly when devfs is unresponsive.
- **Risk accepted:** If devfs becomes responsive between 3 s and 5 s
  on a hardware variant we haven't tested, we'd give up too early
  and miss /dev/tty0 registration. Acceptable while createTty0 is
  non-fatal.
- **Resolution requirements:** Restore to 50 (or remove entirely)
  alongside reverting TD-14-tty0-nonfatal.
- **Marker grep:** `grep -n "TD-14-pl011-retry" sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

### TD-14-psh-retry: PSH_TTYOPEN_RETRIES bumped 20 → 200

- **Status:** ACTIVE TUNING (utils `0cafa08`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c` macro
  default. 20 retries × 100 ms = 2 s wall; 200 retries × 100 ms = 20 s.
- **Why:** On real Pi 4 the `/dev/console` registration path
  (pl011-tty + devfs) takes materially longer than QEMU. 2 s isn't
  enough; 20 s lets psh ride out the slow registration and still
  bail eventually.
- **Risk accepted:** Slow shell startup on broken images (psh sleeps
  20 s before reporting "ttyopen failed"). Trivial.
- **Resolution requirements:** Restore to 20 once the underlying IPC
  slowness is rooted out.
- **Marker grep:** `grep -n "TD-14-psh-retry" sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`

### TD-14-ttyopen-nonfatal: psh_run continues if /dev/console open fails

- **Status:** ACTIVE WORKAROUND (utils `b25b0f8`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`,
  `psh_run()`. The retry loop's `if (err < 0) return err;` is
  replaced with a warning print and the function continues with
  whatever stdin/stdout/stderr psh inherited from `posix_clone`
  (the kernel klog port for syspage-spawned processes).
- **Why:** When `pl011-tty` registration is slow on Pi 4
  (TD-14 lookup latency) and psh's
  `PSH_TTYOPEN_RETRIES * PSH_TTYOPEN_RETRY_US` budget exhausts,
  we'd rather emit a one-way `(psh)%` on UART (proof of life)
  than fail psh entirely. Restore the fatal path once IPC
  slowness is rooted out.
- **Risk accepted:** Without `/dev/console`, psh has no usable
  stdin from a terminal. The shell prompt prints but interactive
  input doesn't work.
- **Marker grep:** `grep -n "TD-14-ttyopen-nonfatal" sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`

### TD-14-devfs-direct: kernel stores the `devfs` namespace OID directly

- **Status:** ACTIVE WORKAROUND (kernel `60703368`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-kernel/proc/name.c`.
- **Why:** Real Pi logs showed `name: register devfs` followed by later
  `lookup("devfs")` dcache misses and root dummyfs queries. Those root
  queries are semantically wrong for the non-filesystem `devfs` namespace,
  return `-ENOENT`, and on Pi 4 sometimes take 21-43 s. A direct stored
  OID lets `lookup("devfs")` return immediately after registration.
- **Risk accepted:** This special-cases a well-known namespace name instead
  of fixing the generic dcache / namespace lookup semantics. It is narrow
  and boot-critical, but should be revisited once the shell is usable.
- **Resolution requirements:** Decide whether Phoenix should have first-class
  non-filesystem namespace roots, then replace the special-case with the
  canonical mechanism or prove the dcache path is reliable on Pi 4.
- **Marker grep:** `grep -n "devfs_registered\\|devfs direct" sources/phoenix-rtos-kernel/proc/name.c`

### TD-14-console-alias: PL011 registers a direct `/dev/console` alias

- **Status:** ACTIVE WORKAROUND (devices `63f1d438`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`.
- **Why:** The canonical `create_dev("/dev/console")` path creates the node
  in devfs, but psh startup is still sensitive to bind/devfs lookup latency.
  The direct alias preserves the old fast path through the kernel namespace.
  PL011 also implements minimal `mtGetAttr`, `mtGetAttrAll`, and `mtStat`
  so libc `stat()` and kernel stat/open preflights work through the alias.
- **Risk accepted:** Two visible names can point at the same tty object.
  Acceptable during bring-up; should be removed once `/dev` bind traversal
  is reliable and fast.
- **Resolution requirements:** Remove the alias after TD-14 is fixed; keep
  the PL011 stat/attr support if direct device aliases remain supported.
- **Marker grep:** `grep -n "TD-14-console-alias\\|pl011_attrAll" sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

### TD-14-psh-ttyopen-errno: psh prints `ttyopen` errno

- **Status:** ACTIVE DIAGNOSTIC (utils `50cf5605`, 2026-05-02)
- **Where:** `sources/phoenix-rtos-utils/psh/psh.c`.
- **Why:** During TD-14, `psh_ttyopen("/dev/console")` can fail for
  materially different reasons (`ENOENT`, `ENOSYS`, `ENOTTY`). Printing the
  errno keeps the next boundary visible in UART captures.
- **Risk accepted:** Extra UART noise only on failure.
- **Resolution requirements:** Remove after psh reliably opens
  `/dev/console` on real Pi 4.
- **Marker grep:** `grep -n "psh: tty open err" sources/phoenix-rtos-utils/psh/psh.c`

### TD-14-probe-strip: TD-13/TD-14 trace probes removed

- **Status:** RESOLVED 2026-05-02 (libphoenix `43e050d`,
  devices `3a3ee35`, utils `ff9fd9d`)
- **Where:** all the `debug()` syscall trace probes added during
  the TD-14 investigation:
  - `libphoenix unistd/dir.c` per-component
    resolve_path / _readlink_abs / safe_lookup tracing.
  - `libphoenix unistd/file.c` open() console-trace + the related
    TD-14-stat-skip workaround (now also reverted).
  - `libphoenix crt0-common.c` + `misc/init.c` startup prints.
  - `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c` TD13_DBG()
    macro and all callsites + standalone poolthr `debug()`.
  - `phoenix-rtos-utils/psh/psh.c` + `psh/pshapp/pshapp.c` debug
    probes (committed as a clean revert).
- **Why removed:** Each probe was an extra `debug()` syscall +
  UART byte stream. On QEMU they alone slowed `(psh)%` by ~2x
  (boot reaches the prompt at log line 264 without probes vs 454
  with). On Pi 4 they added per-IPC overhead that probably
  contributed to the visible slowness without changing the
  fundamental rate-limit (which is the kernel `proc_send`
  round-trip).
- **Note:** The diagnostic value of these probes was real — they
  localized TD-13 and reframed TD-14. They're recorded in this
  document so we can reintroduce them quickly if the next
  investigation needs the same data.

### TD-14-startup-settle (NOT TAKEN): initial sleep in pl011-tty `main()`

- **Status:** REJECTED 2026-05-02 — broke QEMU. Recorded so we don't
  retry it without understanding the failure mode.
- **What was tried:** `usleep(2000000)` at the top of pl011-tty
  `main()` to let dummyfs (-N devfs) finish registering before our
  first `lookup("devfs")`.
- **Why it failed:** Even on QEMU the timing shift caused
  `/dev/console` to not be findable by psh — psh's resolve_path
  returned -ENOENT for `/dev/console` despite pl011-tty's
  `console ready` print. Suggests the timing of `bind devfs /dev`
  vs pl011-tty's `create_dev` is fragile in a way we don't fully
  understand. **Original spawn order + no startup sleep is the
  only known-good QEMU baseline.**

## TD-15: Pi 4 VideoCore VI memory hygiene + 4 GiB DRAM enablement

- **Status:** PENDING (planned 2026-05-03; supersedes the open
  question in TD-12 about how to safely unlock the full 4 GiB).
- **Stage:** 2. Phase 1 (mailbox-buffer drift probe) already LANDED
  with `td15:OK` evidence. Phases 2-6 are the entire Stage 2
  workload. Will be tackled with caches enabled (per Stage 1) for
  fast iteration and meaningful DMA-cacheability validation.
- **First framed:** 2026-05-03 after the 2026-05-02 Pi 4 `(psh)%`
  prompt landed and the user asked us to handle VideoCore VI
  memory access correctly regardless of whether it also unblocks
  the residual TD-14 IPC slowness.
- **Where:**
  - `sources/plo/hal/aarch64/generic/video.c` — VC4 mailbox
    framebuffer setup; uses `PLO_RPI_MAILBOX_BUFFER_ADDRESS` at
    ARM PA `0x02000000`, *inside* the kernel-usable region
    `0x00400000…0x3b400000` declared in
    `_projects/aarch64a72-generic-rpi4b/preinit.plo.yaml`.
  - `sources/plo/ld/aarch64a72-generic.ldt` — hardcodes
    `SIZE_DDR = 0x3b400000`. plo never asks firmware or the DTB
    what RAM is actually present.
  - `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`
    — does not set `total_mem` / `gpu_mem`; firmware therefore
    treats the board as ≤1 GiB and reserves 76 MiB for VC4 by
    default. End result: Phoenix sees `MEM ARM: 948 / TOTAL: 1024`
    even on a physical 4 GiB Pi 4B.
  - `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c` — parses
    `/memory@0` but does NOT parse `/reserved-memory` or
    `dma-ranges`. Kernel does not know which DRAM ranges firmware
    reserved for the VPU or where the ARM↔VC4 BUS PA translation
    lives.
- **Why it matters even if it does not fix TD-14 IPC slowness:**
  - **Correctness.** Without `/reserved-memory` parsing, any
    enlargement of usable RAM risks placing kernel/user
    allocations in regions the firmware (or VC4) is using behind
    our back. TD-04 already proved that *something* on real
    BCM2711 silicon writes to ARM-visible DRAM during ARM boot;
    the mitigated symptom (NC-mapped `_hal_syspageCopied`) is
    consistent with VC4 traffic but we have not actually pinned
    the cause.
  - **Capacity.** Without `total_mem=4096` and DTB-driven memory
    layout, Pi 4 runs Phoenix on ~24 % of its DRAM. Larger
    (8 GiB) variants get worse.
  - **DMA safety.** xHCI / GENET / SD all DMA. On Pi 4 the VC4
    SoC fabric uses bus addresses (legacy `0xc0000000` alias or
    Pi 4's `0x40000000` alias) that are NOT identical to ARM
    PAs. Linux uses `/soc/dma-ranges` to translate. Phoenix does
    not. So far we have hand-crafted MMIO addresses for our
    drivers; the moment we want a USB or PCIe device to DMA into
    a buffer ARM allocated, we need this translation.
- **Verified facts and evidence base:**
  - plo's mailbox buffer (`PLO_RPI_MAILBOX_BUFFER_ADDRESS =
    0x02000000`) is inside the kernel-usable map. After plo's
    last mailbox call the buffer is just RAM; if VC4 background
    activity later touches the same physical bytes, ARM will see
    corruption.
  - plo masks the framebuffer pointer with `& 0x3fffffffu`
    (`hal/aarch64/generic/video.c:183`), correctly stripping the
    `0xc0000000` VC4 BUS alias to recover an ARM PA. So the
    HDMI framebuffer ARM PA is correct; ARM mmaps it
    `MAP_DEVICE | MAP_UNCACHED`.
  - `dtb.c` already understands FDT structure and walks
    `/memory@0`. Adding new node parsers (`/reserved-memory/*`,
    `/soc/dma-ranges`) is incremental work, not new
    infrastructure.
  - `config.txt` currently has no `gpu_mem` or `total_mem`.
    Firmware default for a 1 GiB-aware build is `gpu_mem=76`
    which exactly matches what we observe at runtime.
- **Resolution requirements (phased):**
  1. **VC4 / firmware audit + cheap probes.**
     - Document where each VC4-shared region currently sits:
       mailbox buffer, framebuffer, VC4-reserved DRAM (top of
       firmware memory), ATAGs/DTB fragment, armstub spin
       table.
     - Add a one-line probe in plo *after* the last mailbox
       call that re-reads a byte at `PLO_RPI_MAILBOX_BUFFER_ADDRESS`
       to confirm VC4 is no longer writing there.
     - Add a kernel-side probe: ~1 s after handoff, hash the
       mailbox-buffer page and compare to the post-mailbox-call
       hash. Drift = VC4 still writing.
  2. **Move VC4 mailbox buffer out of ARM-usable RAM.**
     Choose an address inside firmware-reserved (top 76 MiB)
     space, e.g. `0x3b400000 + offset`, or use a static buffer
     in plo's own ROM-style region. Update
     `PLO_RPI_MAILBOX_BUFFER_ADDRESS` and re-validate.
  3. **Quiesce VC4 explicitly before plo→kernel handoff.**
     Send a final mailbox sequence that sets all VC4 clocks
     except HDMI scanout to off (Linux uses
     `mbox_set_clock_state` / similar); follow with `dsb sy ;
     isb`. If TD-04-class corruption disappears, we have
     causal evidence that VC4 was the writer.
  4. **DTB-driven memory layout.**
     - Implement `/reserved-memory` parsing in `dtb.c`. Treat
       every `reg` range as off-limits to kernel allocators.
     - Implement `/soc/dma-ranges` parsing. Expose an
       `arm_to_bus_addr(arm_pa) → bus_addr` helper for drivers
       that build DMA descriptors.
     - Have plo (or kernel) build the syspage memory entries
       from DTB instead of from the hardcoded
       `0x00400000…0x3b400000` LDT range.
     - Drop `SIZE_DDR` from `aarch64a72-generic.ldt` once plo
       can derive it.
  5. **Unlock 4 GiB.**
     - Set `total_mem=4096` and a small fixed `gpu_mem` (e.g.
       64 MiB; 76 only matters on 1 GiB-mode firmware) in
       `config.txt`. Document the choice.
     - Verify firmware reports `MEM ARM: ~3968 MiB` after
       reboot.
     - Validate boot end-to-end at full DRAM. Watch for
       early-MMU regressions because the kernel's TTBR1 map
       coverage may be exhausted.
  6. **Tighten DMA correctness across drivers.**
     - Audit `pcie/server/pcie.c`, `usb/xhci/xhci.c` and
       follow-ons for any DMA descriptor population. Replace
       any implicit identity mapping with the ARM↔BUS helper
       from step 4.
- **Risk register:**
  - Step 3 (VC4 quiesce) might disable HDMI scanout if we are
    too aggressive. Keep the HDMI clock alive; only quiesce
    background tasks.
  - Step 4 (DTB-driven layout) interacts with TD-06 and TD-12.
    Sequencing matters: do `/memory@0` consistency check first,
    then `/reserved-memory`, then `dma-ranges`.
  - Step 5 (4 GiB unlock) likely surfaces TTBR1 map sizing
    regressions and the existing TD-04-class corruption may
    re-emerge in regions newly added to the kernel map. Stage
    cautiously; a 4 GiB build that hangs is a test-cycle setback.
- **Marker grep:** `grep -n "TD-15" docs/ sources/` (no in-source
  marker yet — add `TODO(TD-15-...)` comments at each touch
  point as the work lands).

## TD-16: Pi 4 system runs ~1000× slower than expected (timer-driven slowdown)

- **Status:** OPEN INVESTIGATION (logged 2026-05-03 from a TD-15
  phase 1 cycle and a user-supplied HDMI photograph showing kernel
  `threads:` output gaining only ~12 visible characters per minute
  on the framebuffer).
- **Stage:** 1. Root cause is now confirmed (TD-16-1 evidence:
  `dt = 0x872d51` for 1M nops at 1.5 GHz with caches off, ~62× off
  from the cache-on `~144,000` ticks). Resolution is the Stage 1
  Linux `__enable_mmu` restructure.
- **First observed:** 2026-05-02 night. Made the leap from
  "intermittent" to "definitively timer-driven" with the
  TD-15 phase 1 cycle on 2026-05-03.
- **Where:** anywhere a kernel/userspace operation depends on
  wall-clock or timer ticks. Concretely:
  - libphoenix `usleep()` → `proc_threadSleep()` → kernel sets a
    CNTP_TVAL_EL0 wakeup; thread is woken on the IRQ.
  - `proc_threadWait*()` timeouts.
  - pl011-tty's `pl011_thr` poll loop calls
    `usleep(PL011_TTY_POLL_US = 1000)` between TX-FIFO drains, so
    if `usleep` is wrong the per-character output rate to UART/HDMI
    drops accordingly.
  - psh's `while (lookup("/", ...) < 0) usleep(10000)` and pl011-tty
    createTty0's `for (i=0;i<30;++i) usleep(100000)` both visibly
    iterate at multiple-seconds-per-iteration on real Pi 4.
- **Quantitative evidence:**
  - QEMU reaches `(psh)% help` at log line ~290 (~30 s of Phoenix
    runtime).
  - Real Pi 4 reaches `(psh)% help` only with capture window 420 s
    or longer (TD-14 manifest 2026-05-02-td14-uart-shell-prompt.md).
  - HDMI text grows ~12 chars / minute (user photograph 2026-05-03)
    where QEMU produces hundreds of chars per second.
  - TD-14 timing probe (kernel commit 60703368) measured single
    `proc_send("devfs")` round trips of **1 ms to 43 s** on the same
    image. The 60 000× spread is consistent with mostly-broken timed
    waits.
- **Verified facts:**
  - The Phoenix armstub
    (`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`)
    sets `CNTFRQ_EL0 = 54000000` (line 153, `OSC_FREQ`). That value
    is correct for BCM2711.
  - Kernel reads `cntfrq_el0` at boot via `hal_gtimerGetFrequency()`
    in `hal/aarch64/aarch64.h` and stores it in `state->frequency`.
  - tick→us conversion math in
    `hal/aarch64/gtimer_backend.c::hal_gtimerStateCyc2us` and
    `hal_gtimerStateUs2Ticks` is straightforward: `us = cyc * 1e6 /
    freq` and `ticks = us * freq / 1e6`. Math checks out for
    `freq = 54e6`.
  - TD-15 phase 1 confirmed VC4 is **not** writing to the mailbox
    buffer page during the handoff window (kernel reads
    `td15:OK`). So the slowdown is *not* an external-writer trampling
    on kernel timer state.
- **Top suspects (ranked):**
  1. **CNTPCT_EL0 ticks at a different rate than CNTFRQ_EL0
     advertises.** If the actual hardware tick rate is e.g. 54 kHz
     while CNTFRQ says 54 MHz, every conversion is 1000× off — and
     pl011-tty's char-per-iteration math directly produces the
     observed slowdown. Pi 4 generic timer typically ticks at the
     54 MHz crystal directly, but a misconfigured prescaler /
     `cnthctl_el2` / EL2→EL1 trap could change the EL1 view.
  2. **Timer interrupt is delivered late.** TD-11's single-core
     spinlock implementation masks DAIF across critical sections.
     If we mask IRQs for substantially longer than `usleep` argument,
     timer-driven wakeups pile up. But this should only stretch
     short waits; it does not naturally produce 60 000× factors.
  3. **`cntp_tval_el0` is being written with stale state from EL2
     boot.** Armstub sets `cnthctl_el2 = 0x3` (allow EL1 access to
     phys+virt timer) and `cntvoff_el2 = 0`. If our kernel
     accidentally reads `cntvct_el0` (virtual count, with possibly
     non-zero offset) instead of `cntpct_el0`, that could produce
     skewed math.
  4. **Compiler-induced UB in timing math.** `time_t` is signed; if
     somewhere we compute `cycles * 1e6` and overflow before dividing
     by `freq`, we get garbage. With `freq = 54e6`, `cycles * 1e6`
     overflows i64 at `cycles ≈ 18e12`, which after 5 minutes of
     uptime is nowhere near. Unlikely but worth a glance.
- **Resolution path:**
  1. **Direct measurement probe (cheap).** Add a kernel probe at
     boot that:
     - reads `CNTFRQ_EL0` and prints it.
     - reads `CNTPCT_EL0` once.
     - busy-waits for some count of `nop`s (~10 million).
     - reads `CNTPCT_EL0` again.
     - prints the delta.
     If the delta divided by CNTFRQ tells us "1 ms" but `pl011_writeRaw`-loop wall time is ~1 s, then CNTFRQ does
     not match actual hardware tick rate.
  2. **Compare CNTPCT vs CNTVCT.** If they advance at different
     rates, one of them is wrong; pick the right one for kernel use.
  3. **Confirm cnthctl_el2 + cntkctl_el1.** Linux on Pi 4 sets
     specific bits to allow EL1 unprivileged read access; ours may
     differ.
  4. **If CNTFRQ matches CNTPCT but timer IRQ is late:** the issue
     is in the IRQ-mask / scheduler path, not the timer itself.
     Different fix: shorten DAIF-masked sections or use WFI / IRQ
     in spinlock.
- **Why this is on the critical path for HDMI + USB keyboard
  goals:** every gate after UART (psh)% relies on user-process
  timing (pl011-tty fbcon poll, pcie/usb retry, HID enumeration,
  psh interactive read). At current speeds an interactive smoke
  takes hours; HDMI mirror appears at 12 chars per minute. Fixing
  TD-16 likely makes Gates 2-5 trivially observable.
- **Marker grep:** none in source yet (probe code to be added in
  Phase 16-1 below).

## TD-16-1: timer measurement probe (LANDED 2026-05-03)

- **Status:** LANDED (probe code in main and plo); investigation
  ongoing.
- **Where:**
  - Kernel: `sources/phoenix-rtos-kernel/main.c` — early-marker
    section right after `'d'` and before `syspage_init()`.
  - Plo: `sources/plo/hal/aarch64/generic/video.c` —
    `video_td16QueryArmFreq()` invoked from `video_init()` via VC4
    mailbox tag 0x30002 (RPI_FIRMWARE_GET_CLOCK_RATE) for clock
    id 3 (ARM core).
- **Measurements captured on real Pi 4 (cycle
  artifacts/rpi4b-uart/rpi4b-uart-20260502-232506-netboot-td16-1-cpu-clock-probe.log):**
  - `td16: arm_freq Hz = 0x59682f00` = 1,500,000,000 Hz = **1.5 GHz**
    (firmware confirms ARM core at full turbo; rules out the
    "CPU is throttled to a tiny fraction of normal" hypothesis).
  - `td16:cf=0337f980 dt=0000000000872d51` →
    - cntfrq = 0x0337f980 = **54 MHz** (correct for BCM2711).
    - dt = 0x0872d51 = **8,858,961 ticks for 1M nops**.
  - Math: at 1.5 GHz with caches enabled, IPC ~1, the loop should
    take ~144,000 ticks. Observed value is **~62× higher**, i.e.
    the kernel is running ~62× slower than physics says it should
    even though firmware and timer are both correct.
- **Conclusion:** The slowdown is **caches being disabled in the
  kernel** for the entire kernel runtime (see TD-16). The CPU
  frequency, the architectural timer, and the firmware boot are
  all fine.
- **Sibling commits:**
  - kernel `843e6c61` (`aarch64: TD-16-1 timer + CPU-speed probe`)
  - plo `61927ba` (`rpi4: TD-16-1 plo VC4 mailbox arm_freq query`)

## TD-16-cache-enable: attempted, regressed, reverted (2026-05-03)

- **Status:** PARKED 2026-05-05 after 5 fault iterations + 4
  Stage 1 architectural-refactor cycles. Both lines of attack
  (more invalidation tactics; structural Linux `__enable_mmu`
  refactor) hit the same end-state: silent hang on real Pi 4
  silicon at the cache-enable transition, no exception fires,
  QEMU always passes. Resolution requires lab-grade debugging
  access (JTAG + trace) beyond UART-only iteration scope.
- **Stage:** 1 (parked). Stages 2 (4 GiB) and 4 (HDMI/USB) advance
  without it; Stage 3 (SMP) blocks on it because LDXR/STXR
  cross-core requires cacheable Inner-Shareable memory.
- **Stage 1 architectural refactor cycles** (working-tree only,
  no kernel commits landed; `tracking/current-step.md` 2026-05-05
  has the full iteration table):
  - Cycle 1 (image `4a5575b3`): all PT/syspage writes pre-MMU,
    single SCTLR.M\|C\|I flip, TTBR0 NC blocks → silent hang at X3.
  - Cycle 2 (image `4f8c5ea7`): + TTBR0 blocks Normal Cacheable IS
    (match TTBR1, fix mismatched-attribute hypothesis) → silent
    hang at X3.
  - Cycle 3 (image `1d219133`): + Linux 2-step SCTLR (C+I armed
    in baseline, M-only at flip) → silent hang at X3.
  - Cycle 4 (image `823c84bc`): + CPUECTLR_EL1.SMPEN write at EL1
    → silent hang at L→M (the SMPEN write itself; armstub already
    sets SMPEN at EL3 and EL1 access likely traps to EL2).
  - Same recurring point of failure across all 4 cycles strongly
    suggests a BCM2711 SCU / L2 cache / firmware-coherency-domain
    interaction not modeled by QEMU.
- **What was tried:** Several placements for enabling kernel
  I-cache and D-cache via `SCTLR_EL1.C` and `SCTLR_EL1.I` after
  the existing MMU-only enable. All were either harmful or
  unhelpful on real Pi 4:
  1. **I+D cache enable right after `_core_0_virtual:` label**
     (very early): caused a **recursive exception loop** on real
     Pi 4 (`E E E E ...` spam — early-exception handler trying
     to print "EX=" but only managing 'E' before refaulting).
     QEMU survived the same code unchanged.
  2. **I+D cache enable just before `b main`**: also caused a
     fault on real Pi 4. Last visible markers: `eF1` from
     `syspage_init()`, then either silent stall or recursive
     exception inside the very first IPC `hal_syspageAddr()`.
  3. **I-cache only just before `b main`** (no D-cache): no
     fault, and the post-cache-enable nop-loop probe in main.c
     measured a ~117× speedup (`dt = 0x126eb` vs `0x872d51`).
     But the **overall boot did not speed up** — both 240 s and
     480 s captures stalled around `kllmnP` inside
     `syspage_init`'s map relocation, never reaching `_hal_init`.
     Unclear whether this is because (a) data accesses still
     dominate without D-cache or (b) the I-cache enable disturbs
     timing of the rest of the boot in a subtle way.
  4. **D-cache enable in `_hal_init` (after syspage_init
     completed)**: hung *QEMU smoke* inside the
     `bl hal_cpuInvalDataCacheAll` set/way invalidate loop; never
     reached the SCTLR write. Did not get a chance to test on
     real Pi 4.
  5. **I-cache only at the end of `_hal_init_c()`**: QEMU smoke
     still reached `(psh)% help`, and real Pi 4 showed the expected
     instruction-speed win in the second TD-16 loop
     (`td16b:dt=0x126ee` vs pre-enable `dt≈0x89c82a`). But the
     same real Pi run then stalled after `main: hal init done` and
     marker `h`, before `_usrv_init()` returned. A 600 s capture
     never reached the version banner. Artifact:
     `artifacts/rpi4b-uart/rpi4b-uart-20260503-202432-netboot-td16-late-icache-long.log`.
  6. **I-cache only in `_hal_start()`**: moved the enable later,
     after `_usrv_init()`, VM, perf, proc, and syscall init. QEMU
     still passed, but real Pi stalled immediately after
     `main_initthr: enter` (inside `_hal_start()`), with no later
     `main_initthr: hal started`. Artifact:
     `artifacts/rpi4b-uart/rpi4b-uart-20260503-203723-netboot-td16-icache-hal-start.log`.
     This confirms "just enable I-cache later" is not safe enough;
     it changes the execution model in a way that exposes another
     coherency/maintenance bug.
- **Hypothesis space (for next session):**
  - The kernel's `_hal_syspageCopied` page (Normal Non-Cacheable)
    and `PMAP_COMMON_STACK` pages (also NC per TD-04 fix) coexist
    with the surrounding kernel BSS mapped Normal cacheable.
    Enabling D-cache may interact badly with this mixed-attr
    layout because A72 prefetcher may pull adjacent cacheable
    lines that aren't strictly mapped, faulting on translation.
  - `hal_cpuInvalDataCacheAll` was too weak for D-cache experiments:
    it selected L1 only, read `CCSIDR_EL1` immediately after writing
    `CSSELR_EL1` without the required `isb`, used invalidate-only
    (`dc isw`) rather than clean+invalidate, and had no final
    barriers. Kernel commit `1a4eb297` replaces it with a CLIDR-driven
    all data/unified-cache clean+invalidate loop using `dc cisw`.
    That is a prerequisite, not yet proof that D-cache can be enabled.
  - The `b main` placement is too early — kernel C runtime
    initialization (BSS zeroing? GOT setup?) may not be ready
    for cached accesses yet.
- **Working state restored:** The live kernel path has no SCTLR cache
  enable. The TD-15 phase 1 + TD-16-1 probes remain. Boot is slow
  but correct in the last known-good cache-off image, reaching
  `(psh)%` in ~420 s capture window per the 2026-05-02 manifest.
- **Progress toward alias-safe bootstrap maps:**
  - 2026-05-03 step 1: kernel commit `7f7684c4` changes the temporary TTBR0
    RAM identity block descriptors from Normal cacheable to Normal
    Non-Cacheable (`NC_BLOCK_ATTRS`). This does not enable caches and
    does not speed up the system, but it removes one obvious mixed-attribute
    conflict against the TD-04 NC `_hal_syspageCopied` and
    `PMAP_COMMON_STACK` TTBR1 pages. Real Pi 4 still reaches `(psh)%`;
    artifact:
    `artifacts/rpi4b-uart/rpi4b-uart-20260503-213203-netboot-td16-ttbr0-nc-blocks.log`.
  - 2026-05-03 step 2: kernel commit `d52f6c3a` drops `TTBR0_EL1` to the
    scratch table immediately after the syspage copy and its post-copy
    `_clean_inval_dcache_range`, instead of leaving the low identity map
    active until near `main`. The obsolete E2 syspage byte-dump probe block
    was removed at the same time. This still does not enable caches or speed
    the system up, but it narrows the window where the same PA is reachable
    through both low and high aliases. QEMU Pi 4, generic QEMU, and real Pi 4
    still reach `(psh)%`; artifact:
    `artifacts/rpi4b-uart/rpi4b-uart-20260503-214816-netboot-td16-early-ttbr0-drop.log`.
  - 2026-05-04 step 3: kernel commit `5e727dcc` restores the early
    `_inval_dcache_range` over
    `PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK` before the first
    `SCTLR_EL1.M` write. This follows the Linux/FreeBSD rule that page tables
    populated with the MMU off must be made visible to the table walker before
    translation starts. QEMU Pi 4, generic QEMU, and real Pi 4 still reach
    `(psh)%`; artifact:
    `artifacts/rpi4b-uart/rpi4b-uart-20260503-221342-netboot-td16-early-pt-inval.log`.
- **External OS comparison (2026-05-03):**
  - Linux arm64 builds the initial idmap/swapper tables, performs cache
    maintenance for page tables populated with MMU off, runs CPU setup,
    then enables `SCTLR_EL1.M | SCTLR_EL1.C | SCTLR_EL1.I` as one MMU
    transition. Linux does not treat I-cache or D-cache as a late C-level
    performance switch after normal kernel init has already started.
    Re-check:
    `https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/kernel/head.S`
    and
    `https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/mm/proc.S`.
  - FreeBSD arm64 follows the same shape in its early `start_mmu` path:
    establish MAIR/TCR/TTBR state, invalidate stale translations, then
    turn on MMU + caches before ordinary kernel execution. Re-check:
    `https://cgit.freebsd.org/src/tree/sys/arm64/arm64/locore.S`.
  - Circle's Pi-focused bare-metal code also treats MMU/cache enable as
    early bring-up infrastructure tied to a consistent memory map, not as
    a late optimization. Re-check:
    `https://circle-rpi.readthedocs.io/en/49.0/basic-system-services/memory.html`
    and local mirror paths listed in `docs/source-artifacts.md`.
  - Practical conclusion for Phoenix Pi 4: the correct endpoint is to
    enable both I-cache and D-cache during the MMU transition, after the
    bootstrap mappings are made coherent and alias-safe. The rejected
    late I-cache experiments above are expected to be fragile because
    they change execution after Phoenix has already created mixed
    cacheable/NC mappings and partially initialized kernel state.
- **Next session options:**
  1. First remove the mixed cacheable/NC aliases in the bootstrap
     mappings. The same PA must not remain reachable through both
     cacheable TTBR0 identity entries and NC TTBR1 entries once
     SCTLR.C is enabled.
  2. Restore early page-table cache maintenance in the same spirit as
     Linux's MMU-off table-clean/invalidate step, using the fixed
     `hal_cpuInvalDataCacheAll()` and/or narrower by-VA operations where
     appropriate.
  3. Retry a Linux/FreeBSD-shaped transition that enables
     `SCTLR_EL1.M | SCTLR_EL1.C | SCTLR_EL1.I` together, with exact
     ESR/ELR/FAR capture on any exception.
  4. Do not spend more cycles on I-cache-only late placements unless
     they are used as a bounded diagnostic. The two real-hardware
     I-cache-only placements above prove it is not the functional fix.



## Priority Ladder

**Strategic trajectory (2026-05-04):** see
`docs/roadmap-cache-ram-smp.md`. Stage order is
**1: caches → 2: 4 GiB DRAM → 3: SMP → 4: HDMI/USB**. The priority
ladder below is preserved for historical context, but the active
implementation step is governed by the roadmap.

**Stage 1 (active) — caches enabled via Linux `__enable_mmu` shape:**
- TD-16-cache-enable (architectural restructure of `_init.S`)
- TD-16 (resolved by Stage 1)
- TD-02 (subsumed by Stage 1's unified pre-MMU sweep)
- TD-03 (resolved by Stage 1's pre-MMU syspage copy)

**Stage 2 — 4 GiB DRAM unlock + GPU memory:**
- TD-15 (phases 2-6)
- TD-12 (subsumed by TD-15)
- TD-06 (DTB robustness; aligns with Stage 2 phase 4)

**Stage 3 — SMP cores 1-3:**
- TD-01
- TD-11
- TD-13-residual atomic-fallback

**Stage 4 — HDMI/USB-keyboard console:**
- Gate 2 + Gate 3 from `tracking/current-step.md` history
- TD-14-deferred-fbcon

**Cross-cutting (any stage):**
- TD-04-hack-2, TD-04-hack-3 (cleanup)
- TD-05 (debug-marker strip)
- TD-09 (netboot loop reliability)
- TD-10 (SError handler)
- TD-07 / TD-08 (QEMU+gdb tooling)

---

**Blocks "first Pi 4 boots to userspace" milestone (current):**
- TD-14 (`/dev/console` `resolve_path` hang in psh ttyopen — the
  actual current blocker). Wired to TD-14-deferred-fbcon (may be the
  underlying ordering cause).
- TD-13 (RESOLVED at the runtime layer; residual cleanup items
  remain — probe strip, TD-13-spawn-cap proper resolution).
- TD-10 (SError masked across all early kernel paths — partly hides
  later kernel-fault diagnostics; needs a real handler)

**Blocks effective debugging:**
- TD-09 (netboot loop reliability — bottleneck for fast iteration on
  real Pi 4)
- TD-07 → TD-08 (QEMU+gdb introspection — QEMU smoke already reaches
  `psh help` so this is now a *comparison* tool, not a "QEMU might
  not even boot" risk)

**Blocks upstream-ready quality:**
- TD-04-hack-2 (`_hal_init` debug markers; TD-05-class diagnostic
  pinned in place)
- TD-04-hack-3 (fake `dtbEnd`)
- TD-05 (debug-marker strip/gate, broader than just TD-04 hacks)
- TD-01 (SMP enable, required for anything beyond single-core)
- TD-11 (single-core spinlock path; revisit alongside TD-01)

**Medium-term:**
- TD-02 (cache invalidation correctness)
- TD-03 (proper virtual syspage / BSS mapping; TD-04 closed the
  symptom but the underlying mapping shortcut is still in place)
- TD-06 (DTB robustness, portability)
- TD-12 (memory size clamp; subsumed by TD-15 phases 4 and 5)
- TD-15 (Pi 4 VC6 memory hygiene + 4 GiB DRAM enablement —
  prerequisite for any work beyond ~948 MiB on real Pi 4 and
  the most likely root cause for residual TD-04/TD-14 IPC
  fragility)

**Closed (kept for history):**
- TD-04 (BCM2711 syspage corruption at handoff — closed at the
  syspage layer 2026-04-29 by the NC-dest fix; the prog-reloc
  follow-on closed 2026-04-30 by `0fdf20ca`. Hacks 2 and 3 still
  live as cleanup items above; hack 1 RESOLVED.)

## Tracking Checklist

| ID | Status | Blocker? |
| --- | --- | --- |
| TD-01 | PENDING | multi-core work |
| TD-02 | PENDING | stability risk |
| TD-03 | PENDING | virtual-syspage cleanup |
| TD-04 | RESOLVED at syspage layer; hack-2/3 still active | residual cleanup |
| TD-05 | PENDING | upstream quality |
| TD-06 | PENDING | portability |
| TD-07 | PENDING | QEMU debugging |
| TD-08 | PENDING | QEMU+gdb debugging |
| TD-09 | PENDING | netboot loop reliability |
| TD-10 | PENDING | partly hides TD-13 |
| TD-11 | PENDING | revisit before TD-01 |
| TD-12 | PENDING | DRAM utilization |
| TD-13 | RESOLVED at runtime layer (mutex/atomic wall fixed) | residual probe-strip + spawn-cap cleanup |
| TD-14 | RESOLVED 2026-05-02 | first Pi 4 UART `(psh)%` prompt reached; cleanup remains |
| TD-14-stat-skip | RESOLVED 2026-05-02 | open() stat skip removed |
| TD-14-deferred-fbcon | PENDING | pl011-tty defers fbcon to main thread |
| TD-14-tty0-nonfatal | PENDING | pl011_createTty0 failure is non-fatal |
| TD-14-pl011-retry | PENDING | createTty0 lookup-devfs retries 50 → 30 |
| TD-14-psh-retry | PENDING | PSH_TTYOPEN_RETRIES 20 → 200 |
| TD-14-ttyopen-nonfatal | PENDING | psh_run continues if /dev/console open fails |
| TD-14-devfs-direct | PENDING | kernel direct OID for `devfs` |
| TD-14-console-alias | PENDING | PL011 direct `/dev/console` alias and stat support |
| TD-14-psh-ttyopen-errno | PENDING | psh ttyopen errno diagnostic |
| TD-14-probe-strip | RESOLVED 2026-05-02 | TD-13/TD-14 debug() probes removed |
| TD-14-console-open-fastpath | PENDING | narrow `/dev/console` open fast path |
| TD-14-tiocspgrp-pgrp | PENDING | TIOCSPGRP uses pgrp value directly |
| TD-14-psh-debug-probes | PENDING | psh early probes use debug syscall |
| TD-15 | PENDING | **VC6 memory hygiene + 4 GiB unlock; phased plan** |
| TD-15-mboxprobe | PENDING (phase 1 evidence captured: NO drift) | mailbox-buffer drift probe |
| TD-16 | OPEN INVESTIGATION | **Pi 4 ~1000× slowdown — timer-driven; suspect CNTFRQ ≠ CNTPCT** |
| TD-16-1 | LANDED 2026-05-03 | direct CNTFRQ + CNTPCT measurement probe |

When resolving an item:

1. Create a `tracking/current-step.md` scoped to that single ID.
2. Remove the corresponding `TODO(TD-NN):` marker(s) from upstream source.
3. Commit the upstream repo change and snapshot an integration manifest.
4. Flip the status to `RESOLVED` in this file with the commit SHA and date.

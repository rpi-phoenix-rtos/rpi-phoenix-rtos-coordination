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

---

## TD-01: SMP enable disabled on Cortex-A72

- **Status:** PENDING
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

### TD-13-mtxbypass: skip `vm_mapBelongs` in `syscalls_phMutexCreate`

- **Status:** ACTIVE HACK (added 2026-05-01)
- **Where:** `sources/phoenix-rtos-kernel/syscalls.c`
  `syscalls_phMutexCreate` — both `vm_mapBelongs(proc, h, ...)` and
  `vm_mapBelongs(proc, attr, ...)` calls commented out.
- **Why:** Originally suspected the `proc->mapp->lock` acquired
  inside `vm_mapBelongs` was the wall (TD-04-class lock state). With
  the bypass we now reach `proc_mutexCreate(attr)`, which proves the
  hang is deeper. Bypass kept in place as a stepping stone so the
  deeper investigation is not blocked by re-introducing the lock.
- **Risk accepted:** Kernel will fault on bad user pointers passed
  to `phMutexCreate`. Acceptable during bring-up where the only
  callers are the 9 trusted syspage progs.
- **Resolution requirements:**
  - Fix the deeper TD-13 root cause (`proc_mutexCreate` hang).
  - Re-enable both `vm_mapBelongs` validations.
  - Verify QEMU smoke and real Pi 4 boot still reach `(psh)% help`.
- **Marker grep:** `grep -n "TD-13-mtxbypass" sources/phoenix-rtos-kernel/syscalls.c`

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

## Priority Ladder

**Blocks "first Pi 4 boots to userspace" milestone (current):**
- TD-13 (post-spawn user-mode handoff — the actual current blocker;
  kernel reaches `main: spawned psh (9)` and then nothing). Wired
  to TD-10 because lifting SError mask is part of the diagnostic.
- TD-10 (SError masked across all early kernel paths — partly hides
  the TD-13 root cause; needs a real handler)

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
- TD-12 (memory size clamp; tied to TD-06 + firmware config)

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
| TD-13 | PENDING | **current active blocker** |

When resolving an item:

1. Create a `tracking/current-step.md` scoped to that single ID.
2. Remove the corresponding `TODO(TD-NN):` marker(s) from upstream source.
3. Commit the upstream repo change and snapshot an integration manifest.
4. Flip the status to `RESOLVED` in this file with the commit SHA and date.

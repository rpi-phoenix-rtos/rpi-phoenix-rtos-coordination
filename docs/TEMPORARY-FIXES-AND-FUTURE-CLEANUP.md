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
  - **Status:** ACTIVE HACK
  - **Where:** `sources/phoenix-rtos-kernel/syspage.c`, `syspage_init()`
    immediately after the map-relocation loop.
  - **What was done:** Replaced the entire `if (...progs != NULL) {
    syspage_common.syspage->progs = hal_syspageRelocate(...); ... }`
    block with a no-op (just emits a `P` marker and skips). The
    progs list pointers in the copied syspage stay as raw plo PAs.
  - **Why:** The very first head store
    `syspage_common.syspage->progs = hal_syspageRelocate(...)` hangs
    the kernel on real Pi 4 hardware (works fine in QEMU and on
    ZynqMP with the same code), even though the visually-equivalent
    map-iter loop just above runs cleanly through all 11 entries
    with the same NC-mapped destination. Heisenbug-class — slight
    code-layout changes shift the hang point.
  - **Risk accepted:** Userspace launch is broken — `prog->start`,
    `prog->end`, `prog->argv` etc. still hold plo PAs and need
    relocation before they can be used. Anything that walks the
    progs list will read wrong data once TTBR0's stale-TLB low-PA
    coverage is invalidated.
  - **Resolution requirements:**
    - Root-cause the Heisenbug. Likely candidates: residual cache
      coherency on cacheable BSS pages adjacent to the NC dest page;
      instruction-cache prefetch interaction with the new TTL3
      mapping; or speculative load via TTBR0 SCRATCH_TT racing with
      the NC-mapped TTBR1 access.
    - Restore the prog-reloc loop with whatever pre-store fence /
      attribute fix unblocks it.
    - Add a regression test that parses the relocated progs list
      and validates strings + addresses.
  - **Marker grep:** `grep -n "TODO(TD-04-hack-1)"
    sources/phoenix-rtos-kernel/syspage.c`

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
- **Risks of doing nothing:** the iter-7/8 corruption blocks every
  attempt to validate program relocation, which blocks reaching
  `_hal_init()` from `syspage_init()`, which blocks the first full
  boot to userspace. This is the active blocker.
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

## Priority Ladder

**Blocks "first Pi 4 boots to userspace" milestone:**
- TD-04 (currently active — BCM2711 cache-coherency at plo→kernel handoff;
  next move: re-map `_hal_syspageCopied` as Normal Non-Cacheable)
- TD-03 (unblocks proper virtual syspage access)

**Blocks effective debugging:**
- TD-09 (netboot loop reliability — is the bottleneck right now)
- TD-07 → TD-08 (QEMU+gdb introspection of the iter-7/8 corruption)

**Blocks upstream-ready quality:**
- TD-05 (debug-marker strip/gate)
- TD-01 (SMP enable, required for anything beyond single-core)

**Medium-term:**
- TD-02 (cache invalidation correctness)
- TD-06 (DTB robustness, portability)

## Tracking Checklist

| ID | Status | Blocker? |
| --- | --- | --- |
| TD-01 | PENDING | multi-core work |
| TD-02 | PENDING | stability risk |
| TD-03 | PENDING | milestone |
| TD-04 | PENDING | active step |
| TD-05 | PENDING | upstream quality |
| TD-06 | PENDING | portability |
| TD-07 | PENDING | QEMU debugging |
| TD-08 | PENDING | QEMU+gdb debugging |
| TD-09 | PENDING | netboot loop reliability |

When resolving an item:

1. Create a `tracking/current-step.md` scoped to that single ID.
2. Remove the corresponding `TODO(TD-NN):` marker(s) from upstream source.
3. Commit the upstream repo change and snapshot an integration manifest.
4. Flip the status to `RESOLVED` in this file with the commit SHA and date.

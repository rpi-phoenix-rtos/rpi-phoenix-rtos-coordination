# Step History

## Completed Steps

### 2026-04-29 (later): TD-04-hack-{1,2,3} pushes boot past `_hal_init()` entry, exposes a residual Heisenbug class ✅ (hacks landed; root cause deferred)
- **Kernel commit**: `59c58644` ("aarch64: TD-04-hack-{1,2,3} — skip
  prog-reloc, fake dtbEnd, _hal_init probes").
- **Coordination-repo commit**: `4ae3a86` (TD-04-hack-N entries
  added to TEMPORARY-FIXES).
- **Result**: Kernel now reliably reaches `_hal_init()` entry on
  real Pi 4 hardware via three documented hacks, AND surfaces a
  second class of bug that the TD-04 NC-dest fix does not cover.
- **What was tried in `_hal_init`**:
  1. Added inline `H/4/5/6/F/S/r/D/s/E/7/8/9/a/b/c/d/e` markers
     between every step of `_hal_init()` to localize where the boot
     stops.
  2. Discovered the kernel hangs at the very first program-reloc
     head store in `syspage_init()` —
     `syspage_common.syspage->progs = hal_syspageRelocate(...)` —
     even though the visually-identical map-iter loop just above
     runs cleanly through 11 entries with the same NC-mapped
     destination.
  3. Discovered `dtb->end` reads inside `_hal_init` hang
     immediately after `dtb->start` reads succeed (one offset
     apart, same cache line, identical access pattern).
- **Hacks applied to push past these and reach `_hal_init()` entry**:
  - **TD-04-hack-1**: SKIP the entire prog-reloc loop in
    `syspage_init()`. Risk accepted: progs list still holds raw plo
    PAs; userspace launch will break later. Resolution required:
    re-enable once root cause is understood.
  - **TD-04-hack-2**: localization probes inside `_hal_init()`.
    TD-05-class diagnostic but pinned because the markers also act
    as Heisenbug insurance.
  - **TD-04-hack-3**: fake `dtbEnd = dtbStart + 0x10000` instead of
    `dtb->end`. Risk: only matters if DTB > 64 KiB (Pi 4's is
    ~57 KiB).

  All three carry inline `TODO(TD-04-hack-N)` markers in the source,
  full block comments explaining what / why / risk / resolution,
  and corresponding entries in
  `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` so future sessions
  can find and revert them.

- **Why iterating with more probes doesn't work here**: three
  consecutive rebuilds produced **different end markers**:

  ```
  build A   (commit cff18d49, no hacks)        → lmnYf
  build B   (cff18d49 + hal.c probes + skips)  → lmnPYfH456SRrDsE
  build C   (build B + slight reordering)      → lmnPYf  (no H!)
  ```

  Each layout shift moves the hang. Adding a marker sometimes masks
  one hang and creates a new one. Canonical Heisenbug signature.
  Continuing to add UART markers on real hardware has hit a wall.

- **Working theory for the second-class bug**: residual
  cache-coherency on cacheable BSS pages adjacent to the NC-mapped
  dest page. The NC fix only covers `_hal_syspageCopied`'s one
  page; `hal_common`, `schedulerLocked`, `syspage_common.syspage`,
  the kernel stack base, etc. are all in *cacheable* BSS and may
  share the same firmware-leftover-cache class of failure.

- **What this unblocks**: the actual source of the second class of
  bug remains for the next step to root-cause. With the hacks in
  place, the kernel boot on real Pi 4 reliably reaches the
  `_hal_init` entry sequence, which is the new launchpad for
  further investigation.

- **Next step (planned, see `tracking/current-step.md`)**: stand up
  QEMU + gdbstub introspection (TD-08, requires TD-07 QEMU upgrade
  first) to validate the *logic* of the relocation walks against
  the unhacked source, and then try broader fixes on real hardware
  (extend NC mapping to all kernel BSS, OR full inner-shareable
  D-cache clean+invalidate at `_hal_init` entry, OR VPU quiesce via
  mailbox).

### 2026-04-29: TD-04 NC-dest fix — kernel reaches `_hal_init()` ✅
- **Kernel commit**: this session.
- **Result**: The active blocker since 2026-04-19 ("kernel stuck at
  marker `o` in `syspage_init()`'s map-entry sub-loop") is closed.
  The kernel now walks the entire entry list (11 entries on Pi 4),
  exits the map loop via the natural `entry == original_entries`
  terminator, returns from `syspage_init()`, and enters `_hal_init()`.
  Three consecutive bit-identical real-hardware runs.
- **Last working markers**:
  `NYOPSTUZbcdeF123GHIJKs{...}p{...}r{...}q{...}VWXabcdefgB{...}T{...}O{...}`
  `h{...}ijR{...}kl × 11 (terminates correctly) lmnYf`
  — past `o` (program-relocation entry) and into `f` (_hal_init).
- **Fix shape (in `hal/aarch64/_init.S`)**:
  1. New `NC_ATTRS = 0x707` descriptor (AttrIndx=1, MAIR slot 1 =
     Normal Non-Cacheable inner+outer).
  2. After `_fill_page_descr` populates TTL3 with cacheable entries
     across the kernel's 2 MB window, an inline override re-writes
     the single TTL3 entry covering `_hal_syspageCopied`'s page with
     NC attrs. Page index computed at runtime from the symbol VA so
     the override survives any future linker layout shift.
  3. `_hal_syspageCopied` is now `.balign SIZE_PAGE` so the symbol
     fits exactly into one TTL3 entry's worth of address space.
  4. The syspage copy loop's destination is now loaded from the
     literal pool (`ldr x1, =VADDR_SYSPAGE`) — high VA — so str
     instructions write directly through the NC TTL3 entry to DDR,
     bypassing the A72 D-cache entirely. The pre-fix `adrp + lo12`
     low-PA destination was dropped.
  5. The post-copy `_clean_inval_dcache_range` over the dest range
     was kept (empirically required: removing it regresses the boot
     to "no kernel UART output at all after plo's `hal: jump exit
     el1`"). Working theory in the comment block: speculative
     prefetch via the still-cacheable LOW-PA mapping populates dest
     cache lines from DDR before the copy completes; the flush
     invalidates them so later cacheable reads of dest re-fetch from
     DDR, which now has plo's correct bytes via the NC writes.
- **Companion `syspage.c` changes**:
  - Bumped entry-loop safety cap from 10 to 64 (list legitimately
    has 11+ entries; the cap=10 break was hiding the natural
    terminator).
  - Added a small `F → 1 → 2 → 3 → G` localization probe inside
    `syspage_init()`. **It is not just diagnostic** — empirically
    the kernel hangs between F and G *without* the extra
    `uart_putc` calls between them. Heisenbug: probe's UART-wait
    loops introduce just enough microsecond delay (and instruction-
    stream layout changes) to mask whatever timing/cache-coherency
    issue is hiding there. Documented as "TD-04-mitigation, do not
    remove without re-testing on real hardware". Real fix
    investigation deferred.
- **QEMU vs HW**: same kernel image works correctly under QEMU
  10.2.2 raspi4b, including all probe markers and the iter trace.
  HW now matches QEMU bit-for-bit through the verified probe
  region. The cache-coherency class of failure is closed at this
  layer.
- **What this unblocks**: the next implementation step can drive
  `_hal_init()` further. Other BSS regions that share the same
  cache-coherency-with-firmware-leftover environment may need the
  same NC-mapping treatment; that's a follow-up if/when they
  surface as the next blocker.

### 2026-04-29: E2 probe + QEMU comparison reframes iter-7/8 as a class-of-problem ✅
- **Coordination-repo commits**: this session (AGENTS.md probe-parity rule,
  docs/testing-automation.md probe-parity workflow, TD-04 reframing,
  tracking updates).
- **Kernel sibling commit**: this session (E2 probe + reverted-flush
  comment marker in `hal/aarch64/_init.S`).
- **Result**: The corruption that was tracked for several sessions as
  "iter-8 sub-loop hang inside `syspage_init`" is now understood as a
  cache-coherency / boot-handoff anomaly *specific to the BCM2711
  environment*, not a bug in the shared aarch64 kernel handoff code.
  The same code runs correctly on QEMU 10.2.2 raspi4b and on ZynqMP.
- **Method**:
  1. Added asm probe markers in kernel `_init.S` that read 8 bytes at
     offsets 0/0x100/0x200/0x310 from both source PA (saved x14) and
     destination (low PA via adrp+lo12, high VA via literal pool).
  2. Captured probe data on real Pi 4 (3 consecutive boots): copy is
     correct at offsets 0/0x100/0x200, garbage at 0x310 with the
     garbage value differing every boot. Mappings agree (l == v).
  3. Captured probe data on QEMU same image: copy correct at every
     offset including 0x310. Same compiled instruction stream,
     divergent behaviour.
  4. Tried a pre-copy `_clean_inval_dcache_range` over the dest range:
     hangs the kernel on real hardware at marker `V` (line 247);
     boots fine in QEMU. Reverted with a do-not-redo comment.
  5. Verified plo's actual SCTLR state by reading source: plo runs
     **cache-off the entire time** (SCTLR_EL3 = `0x30c50838`,
     SCTLR_EL2 = `0x30c00838`, SCTLR_EL1 = 0). Earlier sessions'
     mental model "plo writes are stuck in plo's cache" was wrong —
     plo writes go directly to DDR.
  6. Cross-referenced the ARM64 Linux Boot Protocol, the upstream
     Pi 4 armstub8 source, and ZynqMP's plo + kernel code.
- **What this rules out**: code bug in the copy logic, relOffs
  arithmetic, TTBR0/TTBR1 mappings, or probe code itself (all proven
  correct by QEMU). Plo's writes being stuck in plo's cache (proven
  by SCTLR reading).
- **What this points to**: an external coherent-master writing to the
  `_hal_syspageCopied` PA on real Pi 4 between plo's stores and the
  kernel's reads. Top candidate is the VideoCore VI GPU continuing
  to DMA into firmware-reserved DRAM regions across the handoff;
  secondary candidates are stale L2 lines from the bootcode →
  start4.elf → armstub firmware chain, or in-flight DMA that hasn't
  drained when plo takes over.
- **Side-effect output**: a hard-won project rule, now codified —
  every new probe must be tested in QEMU first, then on real HW,
  with both outputs diffed in the active step's tracker file.
  Recorded in `AGENTS.md` and `docs/testing-automation.md`.
- **Markers reached**: kernel boots through the full asm sequence
  (Z, K, [, L, S, T, U, M, V, X1, X2, X3, N, Y, O, P) and into the
  syspage probes; iter trace runs to ~iter 8 with the same corruption
  pattern as before, but now we know *why* it corrupts.

### 2026-04-28: Netboot test cycle infrastructure + UART picocom --noinit fix ✅
- **Coordination-repo commit**: (this commit)
- **Result**: Build → boot → log loop is now ~30 s, fully automated, with
  bridge-wedge auto-recovery. UART captures are clean at 115200 — the E1
  probe data flowed through end-to-end on the first run after the fix.
- **Netboot infra details**:
  - dnsmasq (DHCP+TFTP) runs **inside** the phoenix-dev Lima VM; the host's
    en7 USB-C ethernet is bridged into the VM by socket_vmnet, so macOS
    never binds DHCP/TFTP ports and the Pi sees no upstream lease.
  - TFTP serves directly out of the buildroot bootfs tree — no copy step.
  - `scripts/test-cycle-netboot.sh` runs one full power-cycle + UART
    capture, with a DHCP watchdog that detects bridge wedges (en7 link
    cycling on Pi power events, USB-C unplug events) and runs
    `scripts/netboot-bridge-recover.sh` (full VM restart, Pi-stays-on)
    once before failing.
  - EXIT/INT/TERM/HUP trap unconditionally calls `pi_power_off.sh`.
  - Doc: `docs/netboot-test-cycle.md`. Recommends a switch between Mac
    and Pi rather than a crossover cable; with a switch, en7 stays
    "active" across Pi power events and the bridge essentially never
    wedges from normal test cycles.
- **UART fix details**: picocom invocation in
  `scripts/capture-rpi4b-uart.sh` was passing `--noinit`, which tells
  picocom *not* to configure the TTY. The macOS USB-UART driver retains
  the previous `stty` baud across opens; mine was at 9600. picocom
  silently captured the Pi's 115200 transmission at 9600 → ~95 bytes of
  framing-error garbage per minute. Removed `--noinit`; `--baud` is now
  honored. Verified by inducing the bug (`stty 9600`) then capturing —
  picocom now resets the TTY to the requested baud on open.
- **Markers**: First clean netboot capture
  (`artifacts/rpi4b-uart/rpi4b-uart-20260428-215524-netboot-uart-fix-115k.log`,
  15.5 KiB) shows the full plo → kernel handoff: plo's `probe: pre-jump
  read#1`/`no diff (DDR stable)`, plo's syspage-region byte snapshot,
  kernel `NYOPSTUZbcdeFGHIJKs{...}p{...}r{...}q{...}VWXabcdefgB{hash}`
  followed by 8 iterations of the map-entry sub-loop trace. Iter 7's
  `entry->next` reads as `0xabb988f1` (outside any plausible mapping);
  iter 8 reads wild values (`0x759ecdc4`, `0xc6a91328`). Analysis
  pending — see current-step.md.

### 2026-04-26: Iter-8 Q-marker safety break in map-entry sub-loop ✅
- **Kernel commit**: (committed in this session)
- **Result**: Map-entry sub-loop in `syspage_init()` is now bounded —
  if it runs more than 32 iterations, it emits marker `Q` and breaks.
  Previously the boot would just hang at iteration 8 with no
  observability beyond the last-printed marker.
- **Details**: Companion to the E1 probe — the Q break gives us a
  deterministic exit so we can observe what comes *after* the bad
  loop rather than always stalling on the same byte.
- **Also fixed**: removed the double-relocation of `map->entries` at
  the pre-existing `syspage.c:252` (was harmless to the loop because
  the loop snapshots `original_entries`, but was a real bug).

### 2026-04-26..28: E1 probe instrumentation in plo + kernel ✅
- **plo commits** (this session): pre-jump self-read of the
  emitted syspage region, byte-diff between two reads, optional
  4-word dump at offsets 0x310/0x318/0x320/0x328.
- **Kernel commits** (this session): immediately-post-handoff
  SHA-256 of the syspage region (marker `B{hash}`), per-iteration
  entry-pointer trace through the map-entry sub-loop (markers
  `T{}/O{}/h{}ij/R{}kl`).
- **Result**: First end-to-end probe trace captured 2026-04-28 via
  netboot. Plo confirms DDR stable across two reads; kernel confirms
  the syspage hash is reproducible; the iter-7/8 corruption is real
  and is the same hang point as the prior step. Next step is to
  bound which side of the handoff is authoritative — see
  current-step.md.

### 2026-04-23: Fix flaky debug-marker UART address in syspage.c ✅
- **Kernel commit**: (worktree-local — not yet committed in sibling repo)
- **Result**: Eliminated non-determinism in post-MMU C-level markers. Two consecutive hard-reboot runs produced bit-identical 5643-byte UART logs.
- **Details**: `syspage.c:193-194` used physical UART address `0xfe201000` as a virtual pointer. Post-MMU, TTBR0 had been overwritten with a zeroed scratch page, so those writes depended on stale TLB entries from the pre-MMU identity map — explaining the earlier F/G/o hang variance. Switched to `PL011_TTY_EARLY_VADDR = 0xffffffffffe00000` (the TTBR1-mapped VA used by `_init.S`'s `uart_putc_virt`).
- **Markers**: Flaky F→o → deterministic `NYOPSTUZbcdeFGVWXabcdefghijklhijklhijklhijklhijklhijklhijklh` across two consecutive runs
- **New hang point**: Inside the map-entry sub-loop of `syspage_init()`, mid-iteration 8 (right after marker `h` on iter 8, before `entry->next` read completes)
- **SD image SHA256**: b0b67ccb932aa883c817d2dd2c3c4e9cce804af2abab6d62f69fffced84a344d

### 2026-04-19: Complete Map Relocation in Syspage Initialization ✅
- **Commits**: 1bb7f806, 1c6a5267, d1996d8f, aff01622, 2f0b391f
- **Result**: Kernel completes all map relocation and reaches program relocation phase
- **Image**: bb7861c314ca675eeee1f98e7744df29c123efa0533f3d007bc0c49b5d469531
- **Details**: Fixed infinite loop in entry relocation, implemented workaround to skip entry relocation, completed all map processing
- **Markers**: NYOPSTUZbcdeFGVWXf → NYOPSTUZbcdeFGVWXabcdefgmklmno (map relocation completed)

### 2026-04-19: Fix Syspage Access Crash After MMU Enable ✅
- **Commits**: 448c5e9c, de3e7e33, 3615bc1f, 43c4a20b, 2ac28beb
- **Result**: Kernel progresses from syspage_init() crash to _hal_init()
- **Image**: 2f166572e5f2380748317e2128f5633cd4367c07a5d3baf3bb280b6e3a17b991
- **Details**: Identified BSS region not mapped in MMU, implemented temporary fix to use original syspage
- **Markers**: NYOPSTUZbcde → NYOPSTUZbcdeFGVWXf (progress to _hal_init)

### 2026-04-19: Fix UART Corruption After MMU Enable ✅
- **Commit**: 6a0bdd06
- **Result**: Clean UART output after MMU enable, boot progresses to kernel entry point
- **Image**: 991e51d4bdafdbf7f5cc13ddff070654ee274ba886b05d5a47989a2878305e69
- **Details**: Replaced physical UART calls with virtual address macro after MMU enable

### 2026-04-18: Inline Critical Setup Functions
- **Commit**: (previous commit)
- **Result**: Progressed from NYO hang to NYOPSTUZb markers
- **Details**: Moved stack setup after MMU enable and inlined critical functions

### 2026-04-17: Separate MMU and Cache Enable
- **Commit**: (previous commit)
- **Result**: Progressed from X3 hang to NYO markers
- **Details**: Separated MMU enable from cache enable to avoid Cortex-A72 issues

### 2026-04-16: Fix CPACR_EL1 FPU/SIMD Setup
- **Commit**: (previous commit)
- **Result**: Progressed from early hang to X3 markers
- **Details**: Fixed CPACR_EL1 to enable (not disable) FPU/SIMD

### 2026-04-15: Initial SMP Enable for A72
- **Commit**: (previous commit)
- **Result**: Identified SMP enable issues
- **Details**: Added basic SMP enable using direct register writes

## Next Steps

### Immediate Next Step
1. **Debug program relocation hang**
   - Add strategic debug markers to program relocation section
   - Identify exact failure point (NULL pointer vs circular list issue)
   - Implement temporary workaround if needed
   - Goal: Reach marker `Y` (end of syspage_init())

### Short Term Goals
1. **Complete kernel initialization**
2. **Achieve console output**
3. **Test basic device drivers**
4. **Reach user-space entry**

### Longer Term Goals
1. **Implement proper MMU mapping for BSS/data region**
2. **Restore syspage copy operation**
3. **Full device driver support**
4. **Networking stack**
5. **Filesystem support**
6. **Multi-core SMP**

## Current Status

**Current Position**: HAL initialization entry (marker 'f')
**Blockers**: None - syspage access issue resolved
**Next Focus**: HAL initialization completion and console output

**Progress Summary**:
- ✅ Early boot sequence working
- ✅ MMU enable working
- ✅ Virtual memory transition working
- ✅ Syspage access working
- ✅ Kernel entry working
- 🔄 Current: HAL initialization

**Technical Debt**:
- Temporary fix for syspage access (skip copy)
- Need proper BSS region MMU mapping
- Need to restore syspage copy operation

Last updated: 2026-04-19
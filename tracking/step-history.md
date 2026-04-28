# Step History

## Completed Steps

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
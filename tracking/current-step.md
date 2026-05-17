# Current Implementation Step

## Step: Investigate non-cache root causes — boot artifacts and other Phoenix-RTOS early-boot code

**Status**: IN PROGRESS — four parallel investigation agents running.
Kernel cache experiments are PAUSED per user directive.

**Date**: 2026-05-17

**Phase**: post-Phase-Z pivot. The cache-enable code paths are now
known-equivalent to Linux/BSD canonical patterns and STILL fail with
the same `ESR=0x02000000 / FAR=0x0 / PC=0x400498` exception. Root
cause must be elsewhere.

### Why the pivot

The previous several sessions exhaustively iterated on cache-enable
strategies:

- C-3 series: deferred D-cache enable, deferred I-cache enable, every
  variant of single-shot vs staged SCTLR writes.
- Phase Z: align the kernel + plo with the canonical zynqmp aarch64a53
  pattern (single-shot M|C|I in kernel, plo cache-on with `dc civac`
  teardown). plo cache-on hangs at the MSR on A72 r0p3 + BCM2711 —
  reverted. Kernel-only Phase Z (M|C|I + drop TD-04 NC override + drop
  post-copy clean_inval) still crashes with the same exception.
- Defensive `dc isw` set/way invalidate immediately before M|C|I (per
  Linux/U-Boot/ATF/A72-microarch research recommendation): same crash.

Subagent comparisons (Linux/ATF/U-Boot/FreeBSD/NetBSD on A72 vs A53,
imx6ull-armv7a, zynqmp-aarch64a53, x86) all concluded our cache-enable
code matches the canonical pattern. None of the other working OS code
paths branch on MIDR for A72 vs A53. There is no known cache-side
delta that would make our kernel fail where Linux succeeds.

User directive (verbatim, 2026-05-17 early hours):

> Clearly poking around with different settings of the cache itself is
> a waste of time (in the longer run) if we do exactly the same things
> as other OSes and A53 architecture. The problem hides somewhere
> else! Maybe this is still a GPU related topic — knowing that GPU /
> VPU on this SoC is very active and accesses the same memory as CPU.
> Maybe this is related to Ethernet controller memory (we do netboot —
> so ethernet needs to touch memory from the very beginning). Maybe
> this is related to some misconfiguration, misalignment of the
> kernel file, wrong sizing, padding, formatting of the boot
> artifacts or elements, wrong order of files being loaded, wrong
> early stage boot loader behavior. Maybe the kernel gets loaded in
> a broken state from the very beginning and anything we do in the
> kernel will always be broken. Think outside the box on this.

### Current failure state (defensive `dc isw` test, 2026-05-17 00:12)

UART log:
`/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260517-001210-netboot-phase-z-defensive-dcisw.log`

- Firmware boot OK
- plo console_init / sctlr-M / hal_init done / banner all reached
- plo→kernel handoff message printed cleanly
- Kernel emits markers `X2`, `X3` (post-MMU/cache-enable diagnostic
  markers in `_init.S`)
- Then immediately: `EX=4, ESR=0x02000000, ELR=0x400498, FAR=0x0`

Interpretation: synchronous EL1h exception from current EL with
SP_EL1 active. EC=0 (Unknown Reason). ELR points to a PA in the
kernel image (loaded at low PA ~0x400000 before TTBR1 takes over).
FAR=0 — not a data-abort or instruction-abort with a recoverable
fault address.

### Active investigation (parallel subagents, launched 2026-05-17)

1. **Boot artifact correctness** — config.txt, armstub, kernel image
   format / alignment / padding, plo+kernel concatenation,
   Pi-firmware version (start4.elf, fixup4.dat, bootcode.bin), DTB
   correctness.
2. **GPU/VPU + GENET concurrent bus masters** — does VC4 firmware
   retain DMA into ARM memory after handoff? Does GENET leave its
   descriptor ring armed from netboot? BCM2711 SLC (system L2)
   shared with VC4 — is it disabled or quiesced?
3. **Phoenix-RTOS early-boot code outside cache enable** — map
   PC=0x400498 to exact `_init.S` instruction. Audit vector table
   placement, identity map coverage of executing PA, stack
   validity, syspage construction by plo.
4. **Comparative analysis of working Pi 4 OSes** — Circle,
   rust-raspberrypi-OS-tutorials, U-Boot, NetBSD/evbarm, TF-A
   rpi4 platform — what do they all do that we may be missing
   (pre-MMU CPU config, SLC handling, MMU-enable barriers,
   stack/VBAR positioning, armstub specifics)?

### Hypotheses to test (ranked from agent-research-so-far)

H1. VC4 / VPU continues to DMA into ARM memory after the firmware
    hands off. The BCM2711 SLC retains coherency state that A72's
    `dc isw` cannot reach.

H2. GENET netboot leaves DMA descriptor ring active; subsequent
    incoming frames (ARP, multicast) DMA into stale buffers that
    overlap kernel image PA.

H3. armstub8 misconfigures A72 CPU control registers (CPUACTLR,
    CPUECTLR) — our armstub uses `S3_1_C15_C2_2` for
    CPUACTLR2_EL1; ATF uses `S3_1_C15_C0_4`. Even if 1319367 is
    irrelevant to boot, there may be other A72 r0p3 erratum
    workarounds we're missing.

H4. Phoenix's `_init.S` identity-map TTL1 setup doesn't cover the
    LOW PA where the kernel is currently executing, so the first
    page-table walk after M|C|I goes off the rails.

H5. The kernel image itself is misaligned or has wrong padding
    relative to the plo loader contract — bytes at the
    instruction position after the MSR-sctlr-write are not what we
    think they are.

### What's already DONE (so we don't redo)

- USB merge: phoenix-rtos-devices commit `b5cc6b0`, project commit
  `fb771c4` — BCM2711 PCIe bridge bring-up + VL805 BAR0
  programming folded into the xhci library (canonical Phoenix
  pattern, single-process bus owner). Standalone `pcie` daemon
  removed from `user.plo.yaml`.

### What stays committed but disabled (do not redo)

- Kernel `main.c`: `hal_cpuEnableICache` call wrapped in `#if 0`
  (the BCM2711 SLC non-determinism issue we kept hitting). Do not
  re-enable until the SLC question is settled.

### What needs cleanup AFTER agent results

- Kernel `hal/aarch64/_init.S` Phase Z2/Z3/Z4 changes (single-shot
  M|C|I, deleted TD-04 NC override, deleted post-copy
  clean_inval, defensive `dc isw`). These all failed identically.
  Whether to revert to M-only baseline or keep some pieces depends
  on what the agents find — wait for their reports.

- plo `hal/aarch64/generic/hal.c` is already reverted to M-only
  baseline with documentation of the failed Phase Z1 experiment.
  Leave as-is.

### Exit criteria for this investigation step

- One of H1-H5 (or a new hypothesis surfaced by the agents) is
  confirmed by a targeted test that does NOT involve cache code
  changes.
- A documented next-action plan with concrete code/config edits
  that target the confirmed root cause.

### Rollback

- Worktree `dazzling-joliot-cd9889`.
- Kernel branch `agent/rpi4-program-reloc` (ahead of last known-good
  tag).
- plo branch `codex/upstream-sync-20260516`.
- All sibling source changes are uncommitted at the time of writing
  this step (except the USB merge commits above), so a clean revert
  is `git restore .` in each repo.

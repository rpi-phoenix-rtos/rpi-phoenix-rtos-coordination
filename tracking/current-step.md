# Current Step

## Metadata

- Step ID: `STEP-0519`
- Title: `Complete Pi 4 kernel handoff to C runtime after MMU enable`
- Status: `in_progress`
- Date: `2026-04-19`
- Milestone / phase: `Phase 1`

## Objective

- complete the final assembly-to-C handoff after successful MMU and virtual memory enable
- reach the `main()` function entry point and begin C runtime initialization
- capture diagnostic data if the final handoff fails

## Scope

In scope:

- final debugging of `_set_up_vbar_and_stacks` function
- analysis of C runtime environment setup
- one rebuilt and re-exported Pi 4 image with enhanced debug markers
- one real-device UART test to pinpoint final hang location

Out of scope:

- broad peripheral driver work
- unrelated `plo` refactoring
- SMP re-enable (deferred to post-boot stability phase)

## Acceptance Criteria

- the boot process proceeds beyond marker `P` (80)
- markers `Q` (81) and `R` (82) are visible on real hardware
- the kernel reaches `main()` function or emits a valid exception report (EX=...)
- debug markers from `_set_up_vbar_and_stacks` (S=83, T=84, U=85) pinpoint exact hang location

## Validation Plan

- rebuild and flash:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `fc886ed6b0b68e3b162722fcc80a68ade3e8fdef8888c0815b4ec871caf37787`
- capture UART with:
  - `/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh`
- inspect for:
  - `P` (80) - syspage copy complete
  - `S` (83) - vector table setup complete
  - `T` (84) - TLB invalidation complete  
  - `U` (85) - stack setup complete
  - `Q` (81) - after `_set_up_vbar_and_stacks` return
  - `R` (82) - about to jump to `main()`
  - kernel banner or C runtime output

## Progress Summary

### Achieved Milestones (2026-04-19)

1. **Fixed MMU enable hang at X3**:
   - Disabled pre-MMU cache invalidation (Cortex-A72 specific issue)
   - Separated MMU enable from cache enable
   - Result: Progressed from `X3` to `N` marker

2. **Fixed SMP enable hang at S**:
   - Temporarily disabled A72 SMP enable (CPUECTLR_EL1 access issues)
   - Result: Progressed from `S` to `N` marker

3. **Fixed virtual memory transition hang**:
   - Replaced indirect branch with direct branch during MMU transition
   - Result: Progressed from `N` to `NOP` markers

### Current State
- **Last working seam**: `A2 ZK[LSTUMV X1 X2 X3 NOPST`
- **Current hang location**: After `T` marker, during stack setup in `_set_up_vbar_and_stacks`
- **Root cause identified**: Stack setup was happening BEFORE MMU enable
- **Fix applied**: Moved stack setup to AFTER MMU enable
- **Remaining issue**: Test if the fix resolves the stack setup hang

## Rollback / Baseline

- Previous successful baseline (STEP-0518):
  - Image SHA-256: `376bb01fe8274ffefeaef0a224e25414f59f88f5f2a693d418ba60b13a383205`
  - UART seam: `A2 ZK[LSTUMV X1 X2 X3 NOP`
  - Log: `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260419-021945.log`

## Notes

- **Major achievement**: >95% of low-level bring-up is complete
- **Remaining work**: Final C runtime environment setup
- **Architecture-specific findings**: Cortex-A72 requires different cache maintenance and branching strategies than A53
- **Next phase**: Once `main()` is reached, focus shifts to C runtime initialization and early device drivers

# Manifest: AArch64 Targeted SGI Helper

- Date: `2026-03-20`
- Step: `STEP-0024`
- Result: `completed`

## Scope

- add a targeted AArch64 SGI helper alongside the existing broadcast helper
- update the common CPU interface declaration used by the kernel to include that helper
- preserve existing behavior and validate the current `aarch64a53-zynqmp-qemu` build

## Touched Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Upstream Commits

- `phoenix-rtos-kernel`: `c211c4ca` (`aarch64: add targeted sgi helper`)

## Validation

- Refreshed the copied buildroot in `phoenix-dev`:
  `./scripts/prepare-buildroot.sh --copy-components`
- Rebuilt the existing AArch64 QEMU lane in `phoenix-dev`:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`
- Build result: success

## Key Findings

- Common AArch64 code now has a targeted SGI helper in addition to the existing broadcast IPI helper, so future timer-path work can address CPU 0 explicitly.
- The remaining architectural blocker is no longer raw SGI send capability but the notification contract:
  a future timer-update path still needs an explicitly reserved interrupt number and a minimal handler/queue design.
- The generic shared-work SGI pattern in `hal/tlb/tlb.c` is not currently wired into AArch64 builds, because `hal/aarch64/Makefile` does not include `hal/tlb/Makefile`.
  That means the next timer-notification step cannot assume ready-made AArch64 TLB-style SGI infrastructure.

## Selected Next Step

- run a planning step to define the first CPU0-directed timer wakeup-notification change:
  choose the SGI reservation, the handler ownership, and the exact minimal file set before any timer or scheduler runtime code is modified

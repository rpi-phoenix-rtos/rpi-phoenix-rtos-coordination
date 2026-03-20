# Manifest: AArch64 CPU0 Wakeup Timer Notification

- Date: `2026-03-20`
- Step: `STEP-0026`
- Result: `completed`

## Scope

- reserve `TIMER_WAKEUP_IRQ` in the AArch64 interrupt definitions
- add a guarded remote wakeup-notification path in `proc/threads.c`
- coalesce remote wakeup requests and handle them on CPU 0 under `threads_common.spinlock`
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

## Touched Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Upstream Commits

- `phoenix-rtos-kernel`: `4e89a0ce` (`aarch64: add cpu0 wakeup timer notification`)

## Validation

- Refreshed the copied buildroot in `phoenix-dev`:
  `./scripts/prepare-buildroot.sh --copy-components`
- Rebuilt the existing AArch64 QEMU lane in `phoenix-dev`:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`
- Build result: success

## Key Findings

- The scheduler can now coalesce remote wakeup requests and redirect timer wakeup recomputation to CPU 0 through a dedicated SGI.
- This removes the main scheduler-side blocker for a future CPU-local architectural timer backend.
- The next backend-preparation step should stay out of runtime policy and focus on eliminating the remaining physical-vs-virtual timer sysreg split from future backend code.

## Selected Next Step

- define the first source-agnostic common AArch64 architectural timer helper step

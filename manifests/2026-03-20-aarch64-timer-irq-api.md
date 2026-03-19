# Manifest: AArch64 Timer IRQ HAL API Step

- Date: `2026-03-20`
- Step: `STEP-0015`
- Result: `completed`

## Scope

- add a timer IRQ query to the timer HAL API
- switch the common AArch64 GICv2 code to use that timer HAL query instead of the `TIMER_IRQ_ID` macro
- preserve the current ZynqMP AArch64 runtime behavior

## Touched Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Upstream Commits

- `phoenix-rtos-kernel`: `84f95eb8` (`aarch64: query timer irq via hal`)

## Validation

- Refreshed the copied buildroot in `phoenix-dev`:
  `./scripts/prepare-buildroot.sh --copy-components`
- Rebuilt the existing AArch64 QEMU lane in `phoenix-dev`:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`
- Build result: success

## Key Findings

- The common AArch64 GICv2 code no longer depends directly on the platform macro `TIMER_IRQ_ID`.
- Timer IRQ knowledge now sits behind the timer HAL API, which is a cleaner seam for a future DTB-backed generic ARM timer backend.
- This was a smaller and safer first runtime step than introducing a full generic timer implementation or a broader generic target split.

## Selected Next Step

- add reusable AArch64 architectural timer sysreg helpers in `hal/aarch64/aarch64.h`

## Rationale For The Next Step

- There are still no AArch64 helpers in the current tree for `cntfrq_el0`, `cntpct_el0`, `cntp_tval_el0`, `cntp_ctl_el0`, or their virtual-timer counterparts.
- Adding those helpers is a tiny kernel-only step, it compiles in the current build lane, and it prepares the first generic ARM timer backend without requiring target wiring yet.

# 2026-04-17 - Pi 4 rollback to the last better post-MMU UART baseline and Linux MMU fix

## Trigger

The real-board UART log
`/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-223918.log`
still ended at:

- `A2`
- `KLM`
- `X1`
- `X2`
- `X3`

while earlier logs `213826` and `215745` had objectively reached `... X3NO`.
That meant the recent kernel `_init.S` work had mixed a real regression with
probe churn, and the current line no longer provided a trustworthy baseline.

## Decision

Stop iterating on the regressed `X3`-only line. Restore the last objectively
better post-MMU UART baseline, then add only one primary-source-backed MMU
transition fix on top of it.

## Applied changes

### `phoenix-rtos-kernel`

- rolled back the effective `_init.S` / `main.c` state from the later
  post-`ttbr1` / syspage experiments to the earlier `c0fd7ff7`-era baseline
  that had already produced `... X3NO` on real hardware
- restored the temporary post-MMU PL011 virtual UART seam:
  - `N`
  - `O`
  - `P`
  - `Q`
  - `R`
  - `S`
- added one Linux-derived MMU transition fix:
  - after `msr sctlr_el1, x0`, execute:
    - `isb`
    - `ic iallu`
    - `dsb nsh`
    - `isb`

### `phoenix-rtos-project`

- restored `PL011_TTY_EARLY_VADDR` in the Pi 4 board config so the proven
  post-MMU UART seam is available again

## Research basis

Primary reference:

- Linux arm64 `arch/arm64/kernel/head.S`
  [raw upstream file](https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/kernel/head.S)

The relevant observation is the generic arm64 `__enable_mmu` sequence:

- load TTBRs
- `isb`
- `msr sctlr_el1, x0`
- `isb`
- `ic iallu`
- `dsb nsh`
- `isb`
- branch to the virtual address

Phoenix did not perform the local I-cache invalidation after enabling the MMU.

## Validation

- `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
- canonical export: pass
- FAT-aware verify: pass
- no compiler or assembler warnings in the touched source repos

## Exported image

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `5eb05cc13844cf6628b1334753e112c59e90303c45feedc9a294bc1760051700`

## Expected next hardware result

The next retry should tell whether the rollback baseline plus the Linux MMU fix
restores the previously proven late seam:

- `N`
- `O`
- `P`
- `Q`
- `R`
- `S`

If the next real-board log still does not recover `N`, stop adding source
probes and move the next step to QEMU gdbstub correlation around the
MMU-to-`_core_0_virtual` transition.

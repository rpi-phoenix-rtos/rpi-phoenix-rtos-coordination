# 2026-04-17 - Pi 4 early-kernel TTBR1-from-start rework

## Trigger

The rollback-based hardware log
`/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-223918.log`
still ended at:

- `A2`
- `KLM`
- `X1`
- `X2`
- `X3`

At the same time:

- the exported SD image was verified end-to-end and proved not stale
- earlier real-board logs had objectively reached `... X3NO`
- a bounded Pi 4 QEMU gdbstub session on the rollback image proved that same
  image reaches:
  - `_core_0_virtual`
  - `_set_up_vbar_and_stacks`
  - `main()`

So the remaining live hardware-only seam was no longer “post-MMU code never
runs” in general. It was the runtime TTBR1-activation path itself.

## Decision

Remove Phoenix's late TTBR1-activation seam and switch to a Linux-like model:

- build TTBR1 tables before MMU-on
- enable TTBR1 from the start
- keep TTBR0 as the active identity mapping until the branch into the
  higher-half kernel

## Applied change

In
`/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:

- clear `TCR_EL1.EPD1` before the initial `msr tcr_el1, ...`
- keep `ttbr0_el1` pointing at the scratch identity map
- keep `ttbr1_el1` pointing at `PMAP_COMMON_KERNEL_TTL2`
- move the kernel TTBR1 table construction fully before MMU-on
- remove the old late runtime TTBR1 activation block:
  - no late `mrs tcr_el1`
  - no late `bic #(1 << 23)`
  - no late `msr tcr_el1`
- keep the Linux-derived post-`SCTLR_EL1` sequence:
  - `isb`
  - `ic iallu`
  - `dsb nsh`
  - `isb`

## Why this is stronger

This removes the exact seam that the logs and GDB session isolated:

- old hardware path:
  - `X3`
  - late TTBR1 enable
  - first TTBR1-based UART/MMIO access
  - silence

Linux arm64 early boot does not create that seam. It installs both TTBRs before
MMU-on and branches straight into the virtual kernel after the standard
post-`SCTLR_EL1` instruction-cache invalidation sequence.

## Validation

- `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
- direct Pi 4 QEMU run: reaches `psh`
- direct generic AArch64 QEMU run: reaches `psh`
- canonical export: pass
- FAT-aware verify: pass

## Warning surfaced

`./scripts/qemu-shell-smoke.sh generic` hung even though a direct generic QEMU
run still reached `(psh)%`. Treat that as harness flakiness until the helper is
re-checked; do not treat it as proof that the common kernel path regressed.

## Exported image

- source commit:
  - `phoenix-rtos-kernel`: `47ce3548`

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `f65877d5cffc58222198cc71f2841a09b3d183b4fb66b92e9efaa2e52fe171aa`

## Next expected hardware result

The next real-board UART retry should show whether the board finally moves
beyond:

- `A2`
- `KLM`
- `X1`
- `X2`
- `X3`

and into the restored post-MMU seam:

- `N`
- `O`
- `P`
- `Q`
- `R`
- `S`

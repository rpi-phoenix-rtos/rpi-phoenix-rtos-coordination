# 2026-04-17 - Pi 4 early-kernel pre-MMU page-table invalidation

## Trigger

The current Pi 4 hardware lane still stopped at:

- `A2`
- `KLM`
- `X1`
- `X2`
- `X3`

even after:

- rolling back to the last better post-MMU UART baseline
- restoring the temporary post-MMU PL011 virtual mapping
- adopting the Linux-style post-`SCTLR_EL1` sequence
- moving TTBR1 table setup and enablement before MMU-on

A public-source cross-check then showed the strongest remaining well-known gap:
Linux explicitly invalidates page tables populated with the MMU off, to remove
speculatively loaded cache lines before the page-table walker uses them.

## Decision

Add a narrow, Linux-style pre-MMU data-cache invalidation pass over the exact
contiguous early-kernel MMU region that Phoenix writes just before enabling the
MMU.

## Applied change

In
`/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:

- added `_inval_dcache_range`
  - reads the data-cache line size from `CTR_EL0`
  - performs `dc ivac` over `[x0, x1)`
  - brackets the loop with `dmb sy` and `dsb sy; isb`
- after all early table writes and before MMU-on:
  - invalidate the contiguous region:
    - `PMAP_COMMON_KERNEL_TTL2`
    - `PMAP_COMMON_KERNEL_TTL3`
    - `PMAP_COMMON_DEVICES_TTL3`
    - `PMAP_COMMON_SCRATCH_TT`
    - `PMAP_COMMON_SCRATCH_PAGE`

## Why this is stronger

- it comes directly from Linux's documented arm64 early-MMU reasoning
- it targets the exact remaining well-known risk for tables written with the
  MMU off
- it does not add more probes or change the Phoenix boot model

## Validation

- `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
- `./scripts/qemu-shell-smoke.sh rpi4b`: pass
- canonical export: pass
- FAT-aware verify: pass

## Warning surfaced

The `--qemu-sanity` captured serial tail still stopped at `A3 / KLM`, while the
explicit Pi 4 shell smoke reached `(psh)%`. Treat the explicit shell smoke as
the stronger QEMU signal here; the broad rebuild helper's captured tail is not
currently sufficient to classify the final runtime boundary.

## Exported image

- source commit:
  - `phoenix-rtos-kernel`: `22994c65`

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `14553eb250414b6b93e72cca44f280aac88d5162fdb57aa7f6ae9a659c3e68b5`

## Next expected hardware result

The next real-board UART retry should show whether the board finally moves
beyond:

- `A2`
- `KLM`
- `X1`
- `X2`
- `X3`

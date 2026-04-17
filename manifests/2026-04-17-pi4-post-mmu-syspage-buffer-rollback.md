# 2026-04-17: Pi 4 post-MMU syspage buffer rollback

## Trigger

- real-board UART log:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-221842.log`
- unchanged raw tail:
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `X3`

## Interpretation

- the earlier `NO` seen in the `213826` and `215745` logs is real output after
  `X3`, not a substring-analysis mistake
- after reverting the pre-MMU copy regression, the only remaining material
  change before the missing `N` marker was `_hal_syspageCopied = 16 * SIZE_PAGE`
- so the larger syspage backing buffer itself must be treated as regressing the
  live boundary until proven otherwise

## Change

- kept the restored original post-MMU syspage copy seam in:
  `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
- kept the fine UART split:
  - `U`
  - `V`
  - `W`
  - `Z`
  - `Y`
  - `P`
- reverted `_hal_syspageCopied` from `16 * SIZE_PAGE` back to `SIZE_PAGE`

## Validation

- `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`
- canonical export via:
  `/Users/witoldbolt/phoenix-rpi/scripts/export-rpi4b-sdimg.sh`
- FAT-aware verify via:
  `/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh`

## Resulting image

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `51d4f610d6bbc7778e5de165add6ff0be908879396da859f75323aef14fb6d8c`

## Source commit

- `phoenix-rtos-kernel`: `bd55b727`

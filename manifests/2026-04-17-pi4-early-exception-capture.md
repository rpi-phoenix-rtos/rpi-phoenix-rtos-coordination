# Pi 4 Early Exception Capture At The Post-`3C` MMU Seam

Date: `2026-04-17`

## Trigger

The latest real-board UART log:

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-234142.log`

still stopped at:

- `A2`
- `KLM`
- `X1`
- `X2`
- `3C`

That confirmed the refreshed image was live on hardware, but the board still
went silent immediately after the `SCTLR_EL1` enable sequence.

## Strategy Change

The previous iteration strategy relied on missing progress markers to infer
where the CPU died. That had become too weak:

- older logs had already reached `...X3NO`
- the post-MMU UART probes were then removed on purpose
- after that removal, the project lost direct visibility into the first
  hardware-only fault after MMU-on

So this step switched from “more progress markers” to “capture the first
exception”.

## Code Changes

### `phoenix-rtos-kernel`

File:

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/hal/aarch64/_init.S`

Changes:

- added an early exception vector table `_early_vector_table`
- installed `VBAR_EL1 = _early_vector_table` before MMU-on
- extended the TTBR0 identity map with the 1 GB block containing
  `PL011_TTY_BASE`
- added a compact early exception path that prints:
  - `EX=`
  - `ESR=`
  - `ELR=`
  - `FAR=`
  via the physical PL011 path after MMU-on

Key intent:

- if the CPU is taking an exception immediately after `3C`, the next UART log
  should stop being silent and identify the fault class and address context

## Validation

Validated on the strongest no-hardware lanes available for this step:

- `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`
- `./scripts/qemu-shell-smoke.sh rpi4b`
- canonical export
- FAT-aware SD-image verification

Result:

- pass

## Warnings Surfaced

- the broad `--qemu-sanity` helper still only surfaced the short `A3 / KLM`
  tail
- the explicit Pi 4 shell smoke still reached `(psh)%`

Current interpretation:

- the explicit Pi 4 shell smoke remains the stronger QEMU runtime signal
- the rebuild helper’s captured tail is still not authoritative enough to
  classify the live runtime boundary by itself

## Exported Image

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `4e873f294f07e6d636390816aac318b51f3ceb55ed85ab4ea9ac594e0fc06204`

## Next Recommended Step

Flash `4e873f29...`, boot the Pi 4, capture UART, and classify the next log as:

- still silent after `3C`
- or an early exception report containing:
  - `EX=`
  - `ESR=`
  - `ELR=`
  - `FAR=`

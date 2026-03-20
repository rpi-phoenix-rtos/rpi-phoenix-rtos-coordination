# Manifest: `psh` Root-Lookup First-Result Visibility

- Date: `2026-03-21`
- Step: `STEP-0239`
- Status: `completed`

## Goal

- distinguish a failed `psh` `lookup("/")` loop from “no root lookup reached”

## Implementation

Changed file:

- `sources/phoenix-rtos-kernel/syscalls.c`

Bounded change:

- updated the existing `psh` root-lookup trace in `syscalls_lookup()` so it now
  prints once on the first `psh` lookup of `/`, regardless of success or
  failure, including the integer result code

Marker shape:

- `syscalls: psh root lookup <err>`

## Validation

### Build guardrails

- generic build:
  `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 build:
  `RPI4B_DTB_PATH=.../bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- both succeeded in `phoenix-dev`

Validation note:

- these builds were rerun sequentially, not in parallel, because the shared
  copied buildroot races on host artifact paths when two targets build at once

### Runtime verification

#### Generic `virt`

- within the 30-second QEMU window, now prints:
  - `syscalls: psh root lookup -22`

#### Pi 4 `raspi4b`

- within the same 30-second QEMU window, now prints:
  - `syscalls: psh root lookup -22`

## Result

- the root-lookup blocker is now sharply bounded and shared by both lanes
- `psh` does reach `lookup("/")`
- the first observed result is `-22` (`EINVAL`), not `-ENOENT`

## Next Step

- scope the smallest source-level review of the `lookup()` calling convention
  and root-path expectations that can explain the shared `EINVAL`

# Manifest: `psh` Root-Lookup Success Visibility

- Date: `2026-03-21`
- Step: `STEP-0237`
- Status: `completed`

## Goal

- prove whether `psh` gets past its `lookup("/")` wait loop

## Implementation

Changed file:

- `sources/phoenix-rtos-kernel/syscalls.c`

Bounded change:

- added a one-time marker in `syscalls_lookup()` when:
  - the calling process path is `psh`
  - the lookup name is `/`
  - `proc_portLookup()` returns success

Marker:

- `syscalls: psh root lookup ok`

## Validation

### Build guardrails

- generic build:
  `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 build:
  `RPI4B_DTB_PATH=.../bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- both succeeded in `phoenix-dev`

### Runtime verification

#### Generic `virt`

- within the 30-second QEMU window:
  - still prints `threads: psh user scheduled`
  - does not print `syscalls: psh root lookup ok`

#### Pi 4 `raspi4b`

- within the same 30-second QEMU window:
  - still prints `threads: psh user scheduled`
  - does not print `syscalls: psh root lookup ok`

## Result

- the current evidence still does not prove that `psh` ever sees `/`
- the remaining ambiguity is now:
  - `psh` repeatedly looks up `/` and keeps failing, or
  - `psh` never reaches the root-lookup syscall path at all

## Next Step

- scope the smallest first-attempt `psh` root-lookup trace that can distinguish
  “failed lookup loop” from “no lookup reached”

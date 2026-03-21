# Manifest: `/dev/console` Lookup Trace via `syscalls_lookup()`

- Date: `2026-03-21`
- Step: `STEP-0245`
- Status: `completed`

## Goal

- check whether the failing `open("/dev/console")` path reaches the existing
  kernel `syscalls_lookup()` seam

## Implementation

Changed repository:

- `sources/phoenix-rtos-kernel`

Changed file:

- `sources/phoenix-rtos-kernel/syscalls.c`

Upstream commit:

- `phoenix-rtos-kernel ab1e2f89`

Bounded change:

- extended the existing one-shot `psh` lookup trace so it can also print the
  first `psh` lookup of `/dev/console`

## Validation

### Build guardrails

- refreshed the copied buildroot with:
  `./scripts/prepare-buildroot.sh --copy-components`
- generic build:
  `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 build was not rerun in this exact step because the negative result was
  already explained by source review before a second full rebuild was needed

### Runtime verification

#### Generic `virt`

- the 30-second QEMU window still shows:
  - `psh: tty open`
  - `psh: tty open fail open -2`
- it shows **no** `syscalls: psh console lookup ...` marker

## Result

- the negative result is still useful: the failing `/dev/console` open path is
  not reaching `syscalls_lookup()`
- source review of `phoenix-rtos-kernel/posix/posix.c:posix_open()` explains
  why:
  - `open()` reaches `posix_open()`
  - `posix_open()` calls `proc_lookup()` directly

## Next Step

- move the bounded `/dev/console` trace from `syscalls_lookup()` to the
  `proc_lookup()` or `posix_open()` path where the failure actually occurs

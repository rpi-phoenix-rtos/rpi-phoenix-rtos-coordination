# BCM2711 PCIe Config-Space Abstraction

Date: `2026-03-21`

## Step

- `STEP-0312` Implement PCIe server config-space access abstraction for BCM2711 follow-up

## Repositories

- `phoenix-rtos-devices` `f8540f0`
- coordination repo

## Summary

- refactored the platform-agnostic PCIe server scan path to use a small
  server-local config-space access interface instead of hardcoding direct ECAM
  memory access throughout the scan logic
- preserved the existing Xilinx ECAM behavior through an ECAM-backed
  implementation of that interface
- left BCM2711-specific host-bridge work explicitly for the next step

## Key Files

- `sources/phoenix-rtos-devices/pcie/server/pcie.c`

## Design Notes

- the new `pcie_cfgio_t` interface keeps the current scan logic structurally
  platform-agnostic without widening the public Phoenix PCIe API
- the new `pcie_ecam_ctx_t` context owns the current mmap'ed ECAM window, so
  cleanup now lives next to the ECAM-specific backend state instead of being
  spread across `main()`
- the current implementation still supports only 32-bit config-space accesses,
  which matches the existing scan logic and is sufficient for the next BCM2711
  indexed backend slice

## Validation

Validated in `phoenix-dev` with the strongest available touched-target lanes:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
make -C phoenix-rtos-devices TARGET=aarch64a53-zynqmp-qemu \
  CPPFLAGS="-I$PWD/_projects/aarch64a53-zynqmp-qemu" pcie
```

Observed result:

- the touched `pcie` server now compiles and links successfully on the current
  Xilinx-targeted lane when built with the representative project include path

Additional regression validation:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
tmpdir=$(mktemp -d ~/phoenix-buildroots/pi4-pcie-refactor.XXXXXX)
./scripts/prepare-buildroot.sh --copy-components "$tmpdir"
cd "$tmpdir"
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Observed result:

- a fresh Pi 4 A72 full build still succeeds from a disposable temp buildroot,
  so the current HDMI text-console and keyboard-foundation baseline is
  preserved

## Important Caveat

- the full `aarch64a53-zynqmp-qemu` project build is currently blocked by an
  unrelated kernel issue outside this step:
  `hal/aarch64/interrupts_gicv2.c` references `TIMER_IRQ_GROUP` without a
  definition on that lane
- that unrelated tree-wide failure does not invalidate the touched PCIe server
  compile result above

## Next Logical Step

- scope the first BCM2711-specific indexed config-space backend step behind the
  new server-local abstraction

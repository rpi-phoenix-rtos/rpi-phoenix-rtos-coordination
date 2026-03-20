# Current Step

## Metadata

- Step ID: `STEP-0114`
- Title: Refresh the Linux VM QEMU lane to a `raspi4b`-capable stable release
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- replace the current Ubuntu-packaged QEMU-only Pi 4 emulation limitation with a documented VM-local QEMU lane that can emulate `raspi4b`

## Scope

In scope:

- verify the current Ubuntu 24.04 packaged QEMU limitation in `phoenix-dev`
- build and install a newer QEMU side-by-side in VM-local storage
- prefer the latest stable QEMU release that is practical on `2026-03-20`
- validate that the newer QEMU exposes a `raspi4b` machine
- attempt one first Pi 4-oriented no-hardware smoke invocation with the new QEMU binary
- update the host/testing/manual docs with the exact QEMU strategy and binary path

Out of scope:

- replacing the Ubuntu package with mixed third-party apt repositories
- broad Pi 4 peripheral-debug work beyond a first smoke attempt
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration
- changing the existing packaged QEMU-based generic `virt` lane

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/host-macos-apple-silicon.md`
- `docs/testing-automation.md`
- `docs/manual-operator-instructions.md`
- `docs/source-artifacts.md`
- manifests and tracking updates for this environment step

## Acceptance Criteria

- the documented `phoenix-dev` packaged QEMU limitation is confirmed with concrete version and machine-list evidence
- a newer VM-local QEMU stable build exists at a documented path
- the newer binary exposes `raspi4b` in `-machine help`
- at least one first Pi 4-oriented no-hardware smoke invocation is run with the new binary and its result is documented, even if the boot is incomplete

## Validation Plan

- Review:
  inspect the selected QEMU strategy for minimal blast radius and confirm the packaged Ubuntu QEMU path remains available as fallback
- Build:
  build and install the newer QEMU in VM-local storage
- Emulator:
  run `qemu-system-aarch64 --version`
  run `qemu-system-aarch64 -machine help`
  attempt one Pi 4 boot or smoke invocation with the staged Phoenix Pi 4 artifacts
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-firmware-file-staging.md`

## Notes

- Risks:
  keep the new QEMU side-by-side and VM-local; do not destabilize the existing packaged QEMU lane or widen this step into unrelated Pi 4 bootloader design changes
- Dependencies:
  completed `STEP-0113`
- User-visible control point before next step:
  after this step lands, the next bounded decision should come from the first `raspi4b` smoke result: either trim the first emulated boot blocker or, if the smoke is already useful, turn it into a repeatable documented QEMU lane

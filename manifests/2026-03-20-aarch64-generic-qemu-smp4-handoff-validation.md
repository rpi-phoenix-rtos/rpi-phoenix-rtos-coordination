# Manifest: Generic `virt` Multi-Core Loader Handoff Validation

- Date: `2026-03-20`
- Step: `STEP-0173`
- Status: `completed`

## Goal

- determine whether the repeated EL3 handoff markers observed on the Pi 4 lane are a generic multi-core loader behavior or a Pi 4-specific anomaly

## Changes

No code changes.

The validation reused the already-built generic `virt` artifacts from `STEP-0172` and reran QEMU with:

- `-smp 4`

## Validation

Environment:

- `phoenix-dev`
- reused buildroot:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0172-generic`
- QEMU `10.2.2`

Runtime validation:

1. Generic `virt` with `-smp 4`

   - shows repeated assembly handoff markers:
     - `AA33`
     - `A3`
     - another `A3`
   - but still reaches:
     - `Phoenix-RTOS microkernel v. 3.3.1`
     - later kernel and user-space startup logs

## Conclusion

- repeated EL3 handoff markers are a generic multi-core loader behavior, not a Pi 4-specific failure by themselves
- the Pi 4 failure therefore remains after the EL3 transfer point and is not explained solely by multiple cores traversing the generic handoff path
- the next bounded step should now target the earliest visible kernel entry on Pi 4 so the boundary can be divided into:
  - failure before kernel `_start`
  - failure inside the first kernel instructions after `_start`

## Selected Next Step

- scope the earliest kernel-entry visibility step for Pi 4

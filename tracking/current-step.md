# Current Step

## Metadata

- Step ID: `STEP-0095`
- Title: Add a direct PL011 `/dev/tty0` registration banner
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest direct diagnostic that can prove whether `pl011-tty` reaches successful `/dev/tty0` registration

## Scope

In scope:

- update `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
- emit a raw PL011 banner immediately after successful `/dev/tty0` registration
- rebuild the needed generic artifacts and rerun the generic QEMU smoke lane

Out of scope:

- broader `pl011-tty` refactoring
- failure-path diagnostics
- `psh` or script-behavior changes
- Pi 4 board-specific code
- Raspberry Pi-specific code
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_targets/aarch64a53/generic/user.plo.yaml`
- comparable QEMU `user.plo` files
- `phoenix-rtos-devices/tty/pl011-tty/*`
- `docs/status.md`
- tracking files and manifest updates for this step
- generic QEMU smoke output
- generic utils packaging expectations

## Acceptance Criteria

- `pl011-tty` emits a raw banner only after successful `/dev/tty0` registration
- the needed artifacts are rebuilt and repackaged
- the generic QEMU smoke lane is rerun from the refreshed image

## Validation Plan

- Review:
  inspect the `pl011-tty` diagnostic change and keep it minimal and localized
- Build:
  rebuild `phoenix-rtos-devices all` and the generic `host project image` lane in `phoenix-dev`
- Emulator:
  rerun `timeout 12s ./scripts/aarch64a53-generic-qemu.sh`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-tty0-diagnostic-scope.md`

## Notes

- Risks:
  the result must stay as one localized diagnostic step and must not silently turn into broader console-driver or shell refactoring
- Dependencies:
  completed implementation step `STEP-0094`
- User-visible control point before next step:
  after the diagnostic lands, the next follow-up should be chosen from the new smoke output

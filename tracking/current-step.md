# Current Step

## Metadata

- Step ID: `STEP-0254`
- Title: Implement the pre-`psh` `/dev` bind fast-lane fix
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- make `/dev` visible in the filesystem namespace before the shell opens
  `/dev/console`

## Scope

In scope:

- stage `psh` aliases for `mkdir` and `bind` in the fast-lane project images
- invoke those aliases before the final `psh` app in:
  - generic QEMU fast lane
  - Pi 4 fast lane
- keep the change inside project-local startup composition

Out of scope:

- kernel name-resolution changes
- libphoenix path-resolution changes
- `psh` runtime policy changes
- new utilities or new applets
- unrelated Pi 4 bring-up work
- real hardware work

## Expected Repositories

- `sources/phoenix-rtos-project`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/build.project`
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/user.plo.yaml`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- both fast-lane projects stage the required `mkdir` and `bind` aliases
- both fast-lane projects run pre-shell `/dev` namespace setup before the final
  `psh` app
- generic QEMU validation moves past `psh: tty open fail open -2`
- Pi 4 QEMU validation is rerun against the same startup shape

## Validation Plan

- Build:
  copied-buildroot generic and Pi 4 project rebuilds in `phoenix-dev`
- Emulator:
  generic QEMU first, then Pi 4 `raspi4b`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-pre-psh-dev-bind-scope.md`

## Notes

- Risks:
  keep the fix project-local and avoid dragging shell or libc changes into the
  same step
- Dependencies:
  completed `STEP-0253` fix scoping
- Source reminder:
  `mkdir` and `bind` already exist as `psh` applets and do not require
  `psh_ttyopen()`
- User-visible control point before next step:
  after this implementation lands, the next step depends on whether the shell
  moves past `/dev/console` open

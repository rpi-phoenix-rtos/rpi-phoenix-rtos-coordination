# Current Step

## Metadata

- Step ID: `STEP-0076`
- Title: Define the next generic full-build unblock after `libphoenix`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the next smallest blocker in the broader generic `host project image` lane after the `libphoenix` reboot unblock

## Scope

In scope:

- refresh the copied buildroot in `phoenix-dev`
- rerun the broader generic `host project image` lane
- record the first remaining blocker and select the next smallest safe step

Out of scope:

- all upstream source changes
- Pi 4 board-specific code
- Raspberry Pi-specific code
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/status.md`
- tracking files and manifest updates for this step
- generic `host project image` build output

## Acceptance Criteria

- the broader generic `host project image` lane is rerun from the current baseline
- the first remaining blocker is identified from real build output
- the next code step is selected with one-repo-local scope where possible

## Validation Plan

- Review:
  inspect the failing build output and keep the selected follow-up step narrow
- Build:
  rerun the broader generic `host project image` lane in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-libphoenix-reboot.md`

## Notes

- Risks:
  the result must stay as a discovery-and-scoping step and must not silently turn into multi-repo implementation work
- Dependencies:
  completed implementation step `STEP-0075`
- User-visible control point before next step:
  after the next blocker is identified, the follow-up implementation step should stay repo-local and validation-driven

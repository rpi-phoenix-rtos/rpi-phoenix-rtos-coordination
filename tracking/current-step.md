# Current Step

## Metadata

- Step ID: `STEP-0270`
- Title: Implement the Pi 4 firmware-boot-tree assembly helper
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest helper that assembles a firmware-visible Pi 4 boot
  tree from the current staged artifacts

## Scope

In scope:

- add one helper script for Pi 4 firmware-boot-tree assembly
- take as input:
  - the staged `rpi4b/` output tree
  - a firmware directory provided by the operator or environment
- produce one assembled boot-tree directory without flashing media

Out of scope:

- changing Phoenix source code
- SD-card writing
- network boot setup
- boot-media work
- real hardware work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- QEMU runtime validation flow in `phoenix-dev`
- existing generic and Pi 4 smoke logs in `/tmp`
- `scripts/qemu-shell-smoke.sh`
- Pi 4 firmware staging references
- Pi 4 `_boot/aarch64a72-generic-rpi4b/rpi4b/` outputs
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the helper assembles a Pi 4 boot tree from staged outputs plus firmware files
- the output location and required inputs are documented by the helper itself
- no Phoenix upstream repo changes are introduced

## Validation Plan

- Artifact validation:
  run the helper against a known firmware directory and inspect the assembled
  output tree
- Matching:
  confirm the output contains both firmware files and the staged Phoenix Pi 4
  files
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-firmware-artifact-scope.md`

## Notes

- Risks:
  avoid widening into general deployment or storage work
- Dependencies:
  completed `STEP-0269` firmware-artifact scoping
- Source reminder:
  the next step should leverage the current shell confidence, not revisit it
- User-visible control point before next step:
  after this helper lands, the next step can validate the assembled output or
  move toward media/image generation

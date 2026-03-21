# Current Step

## Metadata

- Step ID: `STEP-0272`
- Title: Implement the Pi 4 FAT firmware-image helper
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest helper that turns the assembled Pi 4 boot tree into a
  portable FAT firmware image

## Scope

In scope:

- add one helper script for Pi 4 FAT-image assembly
- use the assembled `rpi4b-bootfs` directory as input
- build one image artifact without flashing media

Out of scope:

- changing Phoenix source code
- SD-card writing
- network boot setup
- real hardware work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- QEMU runtime validation flow in `phoenix-dev`
- existing generic and Pi 4 smoke logs in `/tmp`
- `scripts/qemu-shell-smoke.sh`
- Pi 4 firmware staging references
- Pi 4 `_boot/aarch64a72-generic-rpi4b/rpi4b/` outputs
- assembled `rpi4b-bootfs` tree
- FAT-image tools available in `phoenix-dev`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the helper builds one FAT firmware image from the assembled boot tree
- the output path is printed or otherwise easy to inspect
- no Phoenix upstream repo changes are introduced

## Validation Plan

- Artifact validation:
  run the helper and inspect the created image plus its file listing
- Matching:
  confirm the image contains the expected firmware and Phoenix boot files
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-fat-image-scope.md`

## Notes

- Risks:
  avoid widening into general deployment or storage work
- Dependencies:
  completed `STEP-0271` FAT-image scoping
- Source reminder:
  the next step should leverage the current shell confidence, not revisit it
- User-visible control point before next step:
  after this helper lands, the next step can validate or reuse the image
  artifact instead of a loose directory tree

# Current Step

## Metadata

- Step ID: `STEP-0275`
- Title: Implement the Pi 4 FAT-image export helper
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest helper that makes the current Pi 4 FAT image directly
  usable from the macOS host side

## Scope

In scope:

- add one host-side export helper
- copy the current VM-local `rpi4b-bootfs.img` into a stable host-visible
  workspace artifact path
- keep the step no-hardware and operator-facing

Out of scope:

- changing Phoenix source code
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
- assembled `rpi4b-bootfs.img`
- `docs/status.md`
- host-visible artifact directory under this repo
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the helper exports the FAT image into a host-visible workspace location
- the destination path is documented
- no Phoenix upstream repo changes are introduced

## Validation Plan

- Helper validation:
  run the export helper from the host workspace
- Matching:
  confirm the exported file exists and matches the VM-local source size or hash
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-fat-image-handoff-scope.md`

## Notes

- Risks:
  avoid widening into general deployment or storage work
- Dependencies:
  completed `STEP-0274` FAT-image handoff scoping
- Source reminder:
  the next step should leverage the current shell confidence, not revisit it
- User-visible control point before next step:
  after this helper lands, the next step can add checksum metadata or actual
  SD-card-writing guidance

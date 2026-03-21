# Current Step

## Metadata

- Step ID: `STEP-0273`
- Title: Scope the smallest post-FAT-image real-device artifact step
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest next artifact step after the new Pi 4 FAT boot image

## Scope

In scope:

- review the current FAT image plus loose boot tree outputs
- decide whether the next artifact should be:
  - a full SD-card image
  - a documented direct-use FAT artifact
- keep the step artifact- and workflow-focused, not hardware-execution-focused

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
- assembled `rpi4b-bootfs.img`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- one concrete next artifact decision is selected
- the reasons are tied to the current boot chain and operator workflow
- the next step remains no-hardware and artifact-focused

## Validation Plan

- Artifact review:
  compare the assembled boot tree and the FAT image against the intended first
  real-device workflow
- Documentation review:
  update the operator guidance to reflect the selected next artifact path
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-fat-image-helper.md`

## Notes

- Risks:
  avoid widening into general deployment or storage work
- Dependencies:
  completed `STEP-0272` FAT-image helper implementation
- Source reminder:
  the next step should leverage the current shell confidence, not revisit it
- User-visible control point before next step:
  after this scope lands, the next step should either build one larger media
  image or explicitly bless the current FAT image as the first device-facing
  artifact

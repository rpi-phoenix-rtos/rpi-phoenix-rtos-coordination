# Manifest: First Buildable `aarch64a72-generic-rpi4b` Scaffold Scope

- Date: `2026-03-20`
- Step: `STEP-0182`
- Status: `completed`

## Goal

- define the smallest actual A72-capable Pi 4 target scaffold step after the loader alias groundwork

## Changes

No code changes.

## Review Basis

Reviewed:

- `sources/phoenix-rtos-build/makes/include-target.mk`
- `sources/phoenix-rtos-project/_targets/aarch64a53/generic/*`
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/*`
- `manifests/2026-03-20-aarch64-generic-a72-loader-aliases.md`

## Findings

- target-family admission is centrally enforced in `include-target.mk`
- the generic target payload under `_targets/aarch64a53/generic/` is small and board-agnostic
- the Pi 4 project payload under `_projects/aarch64a53-generic-rpi4b/` is also small and board-specific rather than A53-specific
- after `STEP-0181`, the generic loader is now ready to reference an `aarch64a72-generic` kernel image and linker script

## Conclusion

- the next bounded implementation step should:
  - add `aarch64a72-generic` to the target allowlist
  - create `_targets/aarch64a72/generic/` as the first generic A72 target scaffold
  - create `_projects/aarch64a72-generic-rpi4b/` as the first Pi 4 A72 project scaffold
  - validate that `TARGET=aarch64a72-generic-rpi4b` builds

## Selected Next Step

- implement the first buildable `aarch64a72-generic-rpi4b` scaffold

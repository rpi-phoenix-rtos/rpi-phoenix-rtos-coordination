# Manifest: Pi 4 Full SD-Card Image Export Scope

- Date: `2026-03-21`
- Step: `STEP-0278`
- Focus: select the smallest next host-facing handoff step for the new VM-local
  Pi 4 disk image

## Decision

- the next bounded step should be a dedicated host-side export helper for:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img`

## Rationale

- the current disk-image shape is now suitable for normal SD-card flashing
- the remaining practical gap is that the operator still cannot use it from the
  macOS host without manual VM file copying
- keeping export as a separate helper preserves the current small-step pattern
  already used for `rpi4b-bootfs.img`

## Next Bounded Move

- add one host-side helper that exports the VM-local `rpi4b-sd.img` into a
  stable workspace artifact path and validates that the host-visible image
  matches the VM-local source by size or hash

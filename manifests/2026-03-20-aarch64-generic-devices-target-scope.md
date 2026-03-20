# Manifest: Generic AArch64 Devices Target Scope

- Date: `2026-03-20`
- Step: `STEP-0078`
- Result: `completed`

## Scope

- inspect the current `phoenix-rtos-devices` target layout
- confirm the smallest missing target-layer piece before any PL011 driver work
- keep the result scoped to one repo-local follow-up step

## Upstream Repositories

- none

## Validation

- reviewed the `phoenix-rtos-devices` top-level target include path
- reviewed existing generic target files in `_targets/`
- validated the current generic failure directly with:
  `TARGET=aarch64a53-generic-qemu PROJECT_PATH=/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_projects/aarch64a53-generic-qemu make -C phoenix-rtos-devices all`

## Validation Evidence

- the generic devices build fails immediately because `_targets/Makefile.aarch64a53-generic` does not exist
- there is no narrower pre-driver blocker inside `phoenix-rtos-devices` than that missing target file
- existing generic targets in this repo follow the same simple target-file pattern already used in earlier filesystem and utils unblock steps

## Notes

- this confirms that the first repo-local devices change should be target scaffolding, not driver code
- the file can stay intentionally minimal so the PL011 driver lands in a later step without coupling the two changes

## Selected Next Step

- add the generic AArch64 devices target makefile and validate the repo on the generic target

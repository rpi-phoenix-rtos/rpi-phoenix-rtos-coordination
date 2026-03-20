# Manifest: Generic AArch64 QEMU PHFS Pre-Init Fix Scope

- Date: `2026-03-20`
- Step: `STEP-0064`
- Result: `completed`

## Scope

- inspect the generic target pre-init scripts and NVM layout after `plo` reached visible output
- compare the failing `ram0` user-script path with existing RAM-backed PHFS setups in other Phoenix targets
- choose the smallest fix that should let `plo` open `user.plo`

## Upstream Repositories

- none

## Findings

- the generic target pre-init currently maps DDR and then tries to `call` `user.plo` from `ram0` at `nvm.loader.kernel.offs`
- unlike existing RAM-backed targets such as:
  - `riscv64/generic`
  - `sparcv8leon/generic`
  the generic AArch64 pre-init does not first run `phfs ram0 4.0 raw`

## Selected Fix

- add `phfs ram0 4.0 raw` to `phoenix-rtos-project/_targets/aarch64a53/generic/preinit.plo.yaml` before the `call user.plo` action

## Notes

- this is preferred over changing offsets, device names, or `plo` code because the current failure is consistent with the raw PHFS view never being established on `ram0`
- if `user.plo` still fails after this fix, the next step should inspect the script offset or artifact placement rather than widening into loader internals

## Selected Next Step

- mount the RAM-backed loader image as raw PHFS in generic pre-init and rerun the unchanged smoke command

# 2026-03-30 Fork README AI Warning

## Scope

- add a visible warning to the top-level README of each published Phoenix fork
  carrying Raspberry Pi port work
- create a minimal top-level README in `phoenix-rtos-usb`, which previously had
  no root README

## Result

Updated top-level README files:

- `sources/libphoenix/README.md`
- `sources/phoenix-rtos-build/README.md`
- `sources/phoenix-rtos-devices/README.md`
- `sources/phoenix-rtos-filesystems/README.md`
- `sources/phoenix-rtos-kernel/README.md`
- `sources/phoenix-rtos-project/README.md`
- `sources/phoenix-rtos-utils/README.md`
- `sources/plo/README.md`

Created new top-level README:

- `sources/phoenix-rtos-usb/README.md`

Applied warning text:

> Fork warning:
> This fork contains AI-generated changes for the Phoenix RTOS Raspberry Pi
> port. These changes have not been fully reviewed and have not been fully
> tested.

## Validation

- verified that the warning text appears at the top of each touched README
- `git diff --check` should remain clean in the touched repositories

## Publishing Consequence

After the corresponding commits are pushed, each public fork page will show a
clear warning near the top of the default GitHub README rendering.

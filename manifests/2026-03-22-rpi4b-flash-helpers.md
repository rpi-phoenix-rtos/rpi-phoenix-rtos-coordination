# 2026-03-22: Pi 4 SD-image verification and macOS flash helpers

## Scope

Close `STEP-0401` by adding the smallest remaining operator-side helpers before
the first Pi 4 board run.

## Changes

Added:

- `/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh`
- `/Users/witoldbolt/phoenix-rpi/scripts/print-rpi4b-macos-flash-commands.sh`

Updated:

- `/Users/witoldbolt/phoenix-rpi/docs/manual-operator-instructions.md`
- `/Users/witoldbolt/phoenix-rpi/docs/pi4-first-hardware-trial.md`
- `/Users/witoldbolt/phoenix-rpi/docs/status.md`
- `/Users/witoldbolt/phoenix-rpi/docs/source-artifacts.md`

## Validation

Verified the current exported image:

```sh
/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh
```

Observed result:

- image path resolved
- size `69206016`
- SHA-256 `475d8d21cdc00d2c2fc79819fe02bdcc946b5ee75329b503198dda7ac16877c3`
- `Verification: OK`

Printed flash commands in dry-run form:

```sh
/Users/witoldbolt/phoenix-rpi/scripts/print-rpi4b-macos-flash-commands.sh disk4
```

Observed result:

- expected `diskutil`, `dd`, `sync`, and `eject` command sequence printed
- no disk writes performed

## Outcome

The first real Pi 4 trial now has:

- the exported SD image
- the focused trial checklist
- a verification helper for the image artifact
- a non-destructive helper that prints the exact macOS flashing commands

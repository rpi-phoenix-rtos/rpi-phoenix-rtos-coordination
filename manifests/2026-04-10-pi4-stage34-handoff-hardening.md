# 2026-04-10: Pi 4 stage-`3 -> 4` handoff hardening

## Summary

`IMG_7135.mov` decoded cleanly through stage `3` but not stage `4`, which
shifted the active failure band from the `currentEL` seam back to the armstub
handoff itself. The bounded response in this step was to harden that handoff
and make the first generic `plo` proof independent of the stage-emitter helper
call.

## Implemented Change

Repositories:

- `phoenix-rtos-project`
- `plo`

Files:

- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
- `sources/plo/hal/aarch64/generic/_init.S`

The custom Pi 4 armstub now:

- preserves the primary-path argument registers instead of clearing `x0..x3`
  before the fixed-address branch
- executes:
  - `dsb sy`
  - `ic iallu`
  - `dsb sy`
  - `isb`
  immediately before `br 0x40080000`

Earliest generic AArch64 `plo _start` now:

- emits stage `4` inline through direct GPIO writes and local delays
- no longer depends on `bl actled_stage_emit` for the first proof
- keeps the existing compact stage-code protocol for later stages unchanged

## Validation

Build:

- rebuilt generic AArch64 QEMU image in `phoenix-dev`: pass
- rebuilt Pi 4 A72 image in `phoenix-dev`: pass

Emulator:

- generic QEMU shell path still reaches runtime and `help`
- direct Pi 4 QEMU serial sanity on the real-device build still reaches:
  - `call: exec go!`
  - `go: enter`
  - `hal: jump exit el1`
  - `A3`
  - `KLM`
  - later `Exception #37`

Artifact:

- assembled bootfs: pass
- assembled FAT image: pass
- assembled SD image: pass
- exported SD image through canonical helper: pass
- FAT-aware host verifier: pass

## Current Artifact

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `4b9c967c9381e8935998a19eb1a976c43b440dd57da4c5fab489763f729a6835`

## Commits

- `phoenix-rtos-project`: `1868568`
- `plo`: `0e625b0`

## Next Step

Run the next real Pi 4 board retry on this refreshed image and use the next LED
video to answer whether the inline stage `4` proof now appears. If it still
does not, the problem is likely the branch target / entry contract itself, not
the old helper-call telemetry path.

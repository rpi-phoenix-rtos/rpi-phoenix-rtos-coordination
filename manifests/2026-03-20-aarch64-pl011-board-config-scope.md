# Manifest: Generic PL011 Board-Config Scope

- Date: `2026-03-20`
- Step: `STEP-0084`
- Result: `completed`

## Scope

- inspect the current generic-QEMU `board_config.h`
- inspect the `pl011-tty` board-config contract
- identify the smallest board-config step that gives the driver real runtime parameters

## Upstream Repositories

- none

## Validation

- inspected the generic-QEMU project board config
- inspected the `pl011-tty` driver contract
- dumped the current local QEMU `virt,secure=on` DTB and inspected:
  - `chosen.stdout-path`
  - the PL011 nodes
  - the shared fixed clock node

## Validation Evidence

- `chosen.stdout-path` points to the non-secure PL011 at `/pl011@9000000`
- the secure UART remains the separate `/pl011@9040000`
- the shared fixed clock node reports `clock-frequency = <0x16e3600>`, which is `24000000`

## Notes

- the smallest useful board-config step is now explicit:
  - `PL011_TTY_BASE  = 0x09000000`
  - `PL011_TTY_CLOCK = 24000000`
- this step should stay in the generic QEMU project board config and avoid `user.plo` or smoke-lane changes

## Selected Next Step

- wire the generic QEMU PL011 base and clock into `board_config.h`

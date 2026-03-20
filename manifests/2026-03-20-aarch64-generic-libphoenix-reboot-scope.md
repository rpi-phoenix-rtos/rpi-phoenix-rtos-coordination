# Manifest: Generic AArch64 libphoenix Reboot Scope

- Date: `2026-03-20`
- Step: `STEP-0074`
- Result: `completed`

## Scope

- inspect the current generic AArch64 `libphoenix` reboot blocker
- decide whether the non-ZynqMP guard can be removed cleanly
- choose the smallest safe `libphoenix` change

## Upstream Repositories

- none

## Findings

- `libphoenix/arch/aarch64/reboot.c` currently includes a ZynqMP header only under `__CPU_ZYNQMP`
- the file does not actually use any symbol from that header
- the implementation only uses generic `platformctl_t`, `platformctl()`, and reboot magic handling

## Selected Fix

- remove the unused ZynqMP-only include guard and `#error "Unsupported TARGET"` from `libphoenix/arch/aarch64/reboot.c`

## Notes

- this is preferred over touching generic device support first because it removes a hard generic AArch64 build failure with a single-file change and no board-policy commitment
- after this step, the next major decision remains whether to define a minimal generic-device target or to use the improved generic lane to expand project-level builds further

## Selected Next Step

- make `libphoenix` AArch64 reboot support generic and validate `libphoenix` directly on the generic target

# Manifest: Scope First Post-`go!` Pi 4 Handoff Visibility Step

- Date: `2026-03-20`
- Step: `STEP-0167`
- Status: `completed`

## Goal

- define the smallest next visibility patch that will split the current Pi 4 silence after `call: exec go!`

## Reviewed Paths

- `sources/plo/cmds/go.c`
- `sources/plo/hal/aarch64/generic/hal.c`

## Findings

- Pi 4 now executes the full `user.plo` script and reaches `call: exec go!`
- `cmd_go()` currently emits only `log_info("\nRunning Phoenix-RTOS\n")`, which is gated by echo mode and not visible in the current loader path
- after that invisible message, `cmd_go()` performs:
  - `devs_done()`
  - `hal_done()`
  - `hal_cpuJump()`
- `hal_cpuJump()` disables interrupts, sets `hal_coreJumpFlag`, and calls `hal_exitToEL1()`
- on the working generic lane, the next visible output is the kernel banner, so the narrow unresolved split is:
  - block inside `devs_done()`
  - block inside `hal_done()`
  - silent handoff inside or immediately after `hal_cpuJump()`

## Decision

The next implementation step should change only `sources/plo/cmds/go.c` and add raw `lib_printf()` visibility markers for:

- entry to `cmd_go()`
- after `devs_done()`
- after `hal_done()`
- immediately before `hal_cpuJump()`
- unexpected return from `hal_cpuJump()`

## Why This Step

- it is the smallest patch that can divide the current post-`go!` silence into loader cleanup versus jump-path failure
- it avoids widening into HAL instrumentation before the `cmd_go()` boundary is split
- it keeps the generic `virt` lane as a clean regression gate

## Explicitly Deferred

- changes to `hal_cpuJump()`
- changes to EL handoff code in `_init.S`
- kernel-side instrumentation

## Acceptance Criteria

- the next patch touches only `plo/cmds/go.c`
- the generic lane still reaches the kernel banner after the new `go:` markers
- the Pi 4 lane shows one of:
  - no `go:` marker at all after `call: exec go!`
  - `go: enter` only
  - `go: devs done`
  - `go: hal done`
  - `go: jump`
  - `go: jump returned ...`

## Selected Next Step

- implement filtered post-`go!` visibility in `plo/cmds/go.c`

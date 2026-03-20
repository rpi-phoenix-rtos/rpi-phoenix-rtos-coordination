# Manifest: Scope `dummyfs` Main-Loop Visibility Step

- Date: `2026-03-20`
- Step: `STEP-0133`
- Status: `completed`

## Goal

- define the smallest next diagnostic step that can prove whether the `devfs` dummyfs instance reaches its main loop and receives the first `mtLookup`

## Decision

The next step is bounded to:

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`
- add narrow `debug()` markers around:
  - successful non-filesystem namespace `portRegister()` for `devfs`
  - the existing `initialized` boundary before the main loop
  - the first `mtLookup` receive / response path

## Why This Step

- `proc_portLookup()` and `proc_send()` now explain the current stall shape: once a server port is registered, a later `lookup("devfs", ...)` can block waiting for that server to receive and respond
- the current `pl011-tty` retry-window result has already shown that a later lookup no longer returns promptly
- the next honest split point is therefore whether `dummyfs` reaches its message loop and handles the first lookup at all

## Explicitly Deferred

- service-order changes in `user.plo`
- broad `dummyfs` refactoring
- changes to shared `create_dev()` behavior
- Pi 4 real-hardware work

## Acceptance Criteria

- at least one lane gains a new visible marker that distinguishes:
  - `devfs` never reached main-loop readiness
  - `devfs` reached main-loop readiness but did not receive `mtLookup`
  - `devfs` received `mtLookup` but did not respond cleanly

## Selected Next Step

- implement the bounded `dummyfs` kernel-console visibility markers and rerun both the generic and Pi 4 DTB-backed QEMU lanes

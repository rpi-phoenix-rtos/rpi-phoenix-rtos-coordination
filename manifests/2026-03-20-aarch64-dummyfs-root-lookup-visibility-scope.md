# Manifest: Scope Root-Dummyfs Lookup Visibility Step

- Date: `2026-03-20`
- Step: `STEP-0135`
- Status: `completed`

## Goal

- define the smallest next diagnostic step that can prove whether the blocked later `lookup("devfs", ...)` call is still waiting on the root dummyfs instance

## Decision

The next step is bounded to:

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`
- preserve the existing non-filesystem `devfs` startup markers
- extend the marker set just enough to distinguish:
  - root dummyfs `initialized`
  - root dummyfs first `mtLookup` receive / response
  - non-filesystem `devfs` startup

## Why This Step

- the generic `STEP-0134` run proves that the non-filesystem `devfs` instance starts late and reaches `initialized`
- that same run also proves the blocked later lookup never reaches that non-filesystem instance
- `proc_portLookup()` explains the remaining gap: until `devfs` is in the kernel name cache, `lookup("devfs", ...)` still resolves through the root server path

## Explicitly Deferred

- service-order changes in `user.plo`
- broad `dummyfs` refactoring
- changes to shared `create_dev()` or name-cache semantics
- Pi 4 real-hardware work

## Acceptance Criteria

- the generic lane gains enough visibility to show whether the blocked later `lookup("devfs", ...)` reaches the root dummyfs instance
- the marker output clearly distinguishes root dummyfs activity from later non-filesystem `devfs` startup

## Selected Next Step

- implement the bounded root-dummyfs visibility markers and rerun both QEMU lanes

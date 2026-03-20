# Manifest: Scope Kernel Name-Service Visibility Step

- Date: `2026-03-20`
- Step: `STEP-0137`
- Status: `completed`

## Goal

- define the smallest next diagnostic step that can prove which kernel name-service path the later `lookup("devfs", ...)` call is actually taking

## Decision

The next step is bounded to:

- `sources/phoenix-rtos-kernel/proc/name.c`
- add tightly filtered markers only for:
  - `/` registration
  - `devfs` registration
  - `lookup("devfs", ...)` branch selection and return path

## Why This Step

- the current images do not start a root dummyfs instance, so dummyfs-side root tracing cannot explain the blocked later lookup
- `proc_portLookup()` owns the real split points now:
  - direct no-root failure when nothing is registered
  - direct cache hit when `devfs` is already registered
  - root-mediated lookup when `/` becomes registered later

## Explicitly Deferred

- changes to loader script order
- changes to `create_dev()` semantics
- broad kernel tracing outside `/` and `devfs`
- Pi 4 real-hardware work

## Acceptance Criteria

- the generic lane gains enough kernel visibility to distinguish:
  - no-root fast failure
  - cached `devfs` success
  - root-mediated lookup entry before blocking

## Selected Next Step

- implement the bounded `proc/name.c` visibility markers and rerun both QEMU lanes

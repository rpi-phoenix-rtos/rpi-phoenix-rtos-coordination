# Manifest: Pi 4 Optional DTB Hook Scope

- Date: `2026-03-20`
- Step: `STEP-0102`
- Result: `completed`

## Scope

- inspect the new Pi 4 boot-tree staging result
- choose the smallest project-local DTB staging hook that keeps builds reproducible without requiring a checked-in external DTB blob
- stop before implementing that hook

## Findings

- the staged Pi 4 boot tree is now useful, but it still lacks a board DTB
- importing a Linux DTB blob directly into the repo would widen provenance and maintenance scope too early
- the smallest useful next step is an optional build hook that stages a DTB only when it is explicitly supplied by the operator or future automation

## Selected Next Step

- update the Pi 4 project build to accept an optional DTB source path
- stage `bcm2711-rpi-4-b.dtb` into the boot tree only when it is available
- keep the default no-DTB build green

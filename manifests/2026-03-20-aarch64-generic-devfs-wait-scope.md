# Manifest: Generic `devfs` Wait Scope

- Date: `2026-03-20`
- Step: `STEP-0096`
- Result: `completed`

## Scope

- inspect the updated generic QEMU smoke evidence where the `/dev/tty0` and `_PATH_CONSOLE` success banners are both absent
- use local `create_dev()` and `dummyfs -D` behavior to choose the smallest startup-timing test
- stop before implementing that test

## Findings

- the current runtime path reaches `pl011_init()` but not the first confirmed successful `create_dev()` call
- `create_dev()` waits for `devfs` lookup and then sends synchronous create requests into the `/dev` namespace
- `dummyfs -D` daemonizes and signals readiness only after synchronous mounting, which makes the `/dev` namespace startup order a plausible small-step hypothesis

## Selected Next Step

- insert one short `wait 500` after `dummyfs;-N;devfs;-D` and before `pl011-tty` in the generic `user.plo`
- rebuild the needed generic artifacts and rerun the generic QEMU smoke lane

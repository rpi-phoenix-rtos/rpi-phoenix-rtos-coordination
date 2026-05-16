# Upstream Sync Note: 2026-05-16

## Purpose

This note is for the main Raspberry Pi 4 cache/MMU bring-up agent. A separate
upstream-following pass fetched all official Phoenix RTOS `origin` remotes and
merged upstream where it was safe to do so without touching the dirty kernel
cache worktree.

## What changed

Fast-forwarded local `master` branches that were only behind upstream:

- `sources/phoenix-rtos-doc`
- `sources/phoenix-rtos-hostutils`
- `sources/phoenix-rtos-lwip`
- `sources/phoenix-rtos-ports`
- `sources/phoenix-rtos-posixsrv`
- `sources/phoenix-rtos-tests`

Created `codex/upstream-sync-20260516` branches and merged `origin/master`
there for clean-diverged repos:

- `sources/libphoenix`
- `sources/phoenix-rtos-build`
- `sources/phoenix-rtos-devices`
- `sources/phoenix-rtos-filesystems`
- `sources/phoenix-rtos-project`
- `sources/phoenix-rtos-utils`
- `sources/plo`

The kernel merge was completed after checkpointing the active cache/MMU WIP.

## Kernel merge resolution

`sources/phoenix-rtos-kernel` has 44 incoming upstream commits on
`origin/master`. The real merge conflicted in:

- `proc/name.c`

Relevant local-only commits touching that path include:

- `70e561a0 proc: trace devfs lookup state`
- `60703368 rpi4b: stabilize devfs lookup during TD-14`

Relevant upstream incoming commits touching that path include:

- `447f8169 !syscalls: add sys_portUnregister syscall`
- `64f2e8eb !syscalls/portRegister: pass string len from userspace`
- `1c7fac5e posix: add proc_destroy() function`

Resolution:

- Checkpointed the dirty cache/MMU diagnostic WIP first as
  `phoenix-rtos-kernel ef3a0fda` (`rpi4b: checkpoint I-cache boundary diagnostics`).
- Merged `origin/master` into `agent/rpi4-program-reloc` as
  `phoenix-rtos-kernel 2193fc4b` (`Merge upstream master into rpi4 cache branch`).
- Kept upstream `proc_portRegister()` / `proc_portUnregister()` API changes,
  `proc_destroy()`, and upstream unlink behavior fixes.
- Preserved local TD-14/devfs diagnostics and direct devfs lookup fast path.
- Added cleanup so unregistering `devfs` clears the cached devfs state.

Validation after merge:

- `git diff --check` in `sources/phoenix-rtos-kernel`: clean.
- `./scripts/rebuild-rpi4b-fast.sh`: passed after one targeted stale-object
  cleanup in the VM-local Pi buildroot.
- Exported/verified image SHA256:
  `242d495bd67079b8e566735c506839e68a9d39d5f112afa5426d31449c883ffa`.

Warning/process note: the first post-merge fast rebuild failed with a
`portRegister` multiple-definition link error because the VM-local Pi-target
`libphoenix` build directory still contained stale generated syscall objects
from before the upstream `sys_portRegister` rename. The source tree was not
manually modified; only the target-specific VM build output for `libphoenix`
was cleaned with `make -C libphoenix clean`, then the canonical fast rebuild
completed. Future fast-rebuild automation should detect upstream syscall ABI
changes and clean/rebuild dependent libphoenix objects instead of surfacing a
stale-object linker failure.

## Upstream themes to account for

- Kernel/libphoenix: signal bounds, waitpid, spawn argv/env handling,
  port register/unregister, `destroy_dev()`, VM error handling.
- Build/project/ports: port-manager feature updates, LittleFS image helper,
  submodule pointer updates, CI/lint updates.
- Devices/lwIP: STM32N6 USB/client work, usbwlan and Wi-Fi/WHD updates,
  GRLIB GPIO/NAND, sensors, and half-duplex tty support.
- Plo: STM32U3/N6 support and stricter command/device validation.

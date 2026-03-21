# Manifest: Pre-`psh` `/dev` Bind Scope

- Date: `2026-03-21`
- Step: `STEP-0253`
- Focus: choose the smallest fast-lane fix for making `/dev` visible before the
  shell opens `/dev/console`

## Source Review Result

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`
  proves the root `dummyfs-root` instance only auto-populates `/syspage`
- the same file proves `dummyfs;-N;devfs;-D` registers `devfs` only in the
  non-filesystem namespace
- `sources/phoenix-rtos-utils/psh/Makefile`
  shows both `mkdir` and `bind` already exist as internal `psh` applets
- `sources/phoenix-rtos-utils/psh/mkdir/mkdir.c`
  and `sources/phoenix-rtos-utils/psh/bind/bind.c`
  show these applets do not rely on `psh_ttyopen()`

## Selected Smallest Fix

- keep the change project-local
- stage `psh` binary aliases for `mkdir` and `bind`
- run those aliases as syspage apps before the final `psh` launch:
  - `mkdir /dev`
  - `bind devfs /dev`

## Why This Is The Smallest Reasonable Move

- it does not change kernel name resolution
- it does not change libphoenix path resolution
- it does not change shell console policy
- it reuses existing Phoenix applets instead of inventing a new startup helper
- it operates exactly where the missing namespace wiring currently lives:
  project image/startup composition

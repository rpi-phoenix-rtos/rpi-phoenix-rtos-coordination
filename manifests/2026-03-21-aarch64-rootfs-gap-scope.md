# Manifest: Root Filesystem Gap Scope

- Date: `2026-03-21`
- Step: `STEP-0240`
- Status: `completed`

## Goal

- explain the shared `lookup("/") -> -22` result without adding more runtime
  probes

## Evidence Reviewed

Source paths:

- `sources/phoenix-rtos-utils/psh/psh.c`
- `sources/phoenix-rtos-kernel/proc/name.c`
- `sources/phoenix-rtos-project/_targets/aarch64a53/generic/user.plo.yaml`
- `sources/phoenix-rtos-project/_targets/aarch64a72/generic/user.plo.yaml`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`

Key findings:

- `psh.c:main()` waits in:
  - `while (lookup("/", NULL, &oid) < 0) { usleep(10000); }`
- `proc_portLookup()` returns `-EINVAL` for `"/"` when
  `name_common.root_registered == 0`
- the current generic and Pi 4 fast-lane `user.plo` trees start:
  - `dummyfs;-N;devfs;-D`
  - `pl011-tty`
  - `psh`
- they do **not** start a plain root `dummyfs` instance

## Conclusion

- `lookup("/", NULL, &oid)` is not the problem
- the problem is that `/` never becomes registered in the current fast-lane
  image shape
- `psh` is therefore looping exactly as designed, but against an image that
  lacks a root filesystem server

## Selected Next Step

- add the smallest root-filesystem server change to the fast lane:
  - start a plain `dummyfs` instance before the existing `devfs` instance
  - keep the rest of the generic and Pi 4 boot chain unchanged

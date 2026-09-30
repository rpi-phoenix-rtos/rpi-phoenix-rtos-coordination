# Integration State: build31-release-cleanup

## Summary

- Date: 2026-09-30
- Note: Release build 1: the release configuration (`--scope core --with-ports --with-showcase`, no `--with-tests`, rootfs wiped first). Test, diagnostic and demo programs build only with `--with-tests`. Dead ports weston and ffmpeg are removed, and the second shmsrv source is gone. lighttpd starts: its missing comma is fixed, and `/usr/www` and `/var/run` are in the root-skel. Build 09:24 PASS. rootfs COMPLETE 73/73 and the stack gate PASS. The 20 dropped programs are absent and the kept ones present (`RELABSENT`/`RELPRESENT`). The pristine NFS export was swapped in 09:25. Pi runs on this image: G5 experiments W1/W2/W3, C4 `q3dm7` (15 727 frames, 0 wedges).
- Generator: written by hand from the pre-merge SHAs (the upstream-sync, wl-unify and dropbear merges landed on master afterwards; this is the rollback point before them)

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | abd67df5f (clean) | https://github.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination.git |
| libphoenix | master | da58f77 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 73ffe0e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | d70d2ec (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 2d9030a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 17a47f39 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 494ac61 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | fba600e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | df2f604 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | d84b1c2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 74e018b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a15f3ed (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Restore

`./scripts/restore-integration-state.sh manifests/2026-09-30-build31-release-cleanup.md`, then
`./scripts/rebuild-rpi4b-fast.sh --scope full-clean --with-ports --with-showcase` (the syscall table
differs from the merged master, so nothing incremental).

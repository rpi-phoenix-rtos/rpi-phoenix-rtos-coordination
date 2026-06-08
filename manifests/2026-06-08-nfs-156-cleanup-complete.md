# Integration State: nfs-156-cleanup-complete

## Summary

- Date: 2026-06-08
- Note: NFS #156 cleanup COMPLETE: both socket-fallback twins reverted (kernel ba09865d + libphoenix 4b5cc61), all Gap-B DIAG removed, nfs_runRoot comment corrected; netboot+nfsroot regression PASS x2, 0 faults. Supersedes 2026-06-08-nfs-perf-lazyclose-plus-diag-cleanup.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | aaa5521 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 4b5cc61 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2a6aebb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 07bb181 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 272231e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | ba09865d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 8e7ec8a (dirty(5)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8f54b36 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | d65a95a (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 9a05bd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | aae3d35 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 68172c1 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	aaa55215b36e0a5aaee0bfb677991f0391baadfa	main
libphoenix	4b5cc61172234ec8b8a6187b06e42f389011578a	master
phoenix-rtos-build	2a6aebb3aef02ec87a5b8c273ad9b49567f9fe25	master
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	07bb181a7651429a06d3c0e82e693f001e5c0823	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	272231e60c6d16a5894ab96fcd84c2c48044bbc5	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	ba09865d6b1e3b6b3eab44be03897fb9c97d34f3	master
phoenix-rtos-lwip	8e7ec8ac5c4c306afb682921c64ad9a66a544caa	master
phoenix-rtos-ports	8f54b36cfef755d5f24b82250f83f15b88e6d56f	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	d65a95abe61c037cd82d2a45c447f6630e2228f7	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	9a05bd923b86828bfba2f76cc4045f478201f695	master
phoenix-rtos-utils	aae3d350fa05f3663f8b9bdfb75c201a66fdf218	master
plo	68172c11570d0d0f64fa416fadae47cb113dab2f	master
```

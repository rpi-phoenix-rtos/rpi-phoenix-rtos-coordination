# Integration State: sd-mmcblk0-works

## Summary

- Date: 2026-06-03
- Note: EMMC2 SD-card #119 resolved: /dev/mmcblk0 brings up under SD-boot (CMD7 R1b poll + IRQ lost-wakeup guard); devices c1aa946, project f8f9533
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 91f24c4 (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 91f5b3c (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | bedc672 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | c1aa946 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 6cdf217e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 0581be3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 4bd9ec8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f8f9533 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | dcc5ea7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 34f87c4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | ae05823 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	91f24c4407e5f2f16fb1c9e14563c62ada951bbd	main
libphoenix	91f5b3cba302dd092503680799ff0e483bba262f	master
phoenix-rtos-build	bedc672c5d634e614fba2bea3d87bbc370304acf	master
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	c1aa9461cb0334eab5313c16cc227223ecdf2558	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	6cdf217e6e6370030055c9f905c933a0c1ac98d1	master
phoenix-rtos-lwip	0581be3e145f171c1b1814d039f4ae0e4d196147	master
phoenix-rtos-ports	4bd9ec801d4c10e1cd4a2712d2a44810a891fd44	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	f8f953319ae371b29e18fa2b368b68a79786bf80	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	dcc5ea7419f53d3796aa4746a82177733dfd8c76	master
phoenix-rtos-utils	34f87c487fb09979ac5705cb1c4fc8a002508fe4	master
plo	ae05823587dc3a1540417920dd4b49d292d876b9	master
```

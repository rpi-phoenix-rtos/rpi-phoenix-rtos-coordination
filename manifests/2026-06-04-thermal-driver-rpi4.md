# Integration State: thermal-driver-rpi4

## Summary

- Date: 2026-06-04
- Note: rpi4-thermal driver (/dev/thermal,/dev/throttled) validated on HW via netboot (T=35986mC); two-variant build infra (netboot/sd)
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 9281541 (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 5674368 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 30f6867 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 97e38f0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 6cdf217e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 745b3a2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 4bd9ec8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 27f3913 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | b3e97dc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 34f87c4 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | ae05823 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	9281541d9f418b49e2c2b1402b8ba493a9853d67	main
libphoenix	567436842a062614e9d7a99368598c89c470b27b	master
phoenix-rtos-build	30f686733607104fbb850a70c4f5363e68fac78b	master
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	97e38f00aca2b3d15e9a4b9875406356cff169c3	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	6cdf217e6e6370030055c9f905c933a0c1ac98d1	master
phoenix-rtos-lwip	745b3a2dcf06b444ac2e7d53d00d5a4c1af414ff	master
phoenix-rtos-ports	4bd9ec801d4c10e1cd4a2712d2a44810a891fd44	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	27f391302a208162a2afc16449c2b4d0364f7134	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	b3e97dcb66245fc5f3e70fc4ed821af72299d8cd	master
phoenix-rtos-utils	34f87c487fb09979ac5705cb1c4fc8a002508fe4	master
plo	ae05823587dc3a1540417920dd4b49d292d876b9	master
```

# Integration State: usbmouse-hosted-in-daemon

## Summary

- Date: 2026-06-03
- Note: rpi4b: usbmouse linked into usb daemon (USB_HOSTDRV_LIBS), dead standalone HID launches dropped; /dev/mouse0 + /dev/kbd0 both bind in one boot (#126)
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | b88f567 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 91f5b3c (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 30f6867 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 854103f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 6cdf217e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 4269464 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 4bd9ec8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | d6c1356 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | b3e97dc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 34f87c4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | ae05823 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	b88f567700b0dfe047570398f3243001cad01a4f	main
libphoenix	91f5b3cba302dd092503680799ff0e483bba262f	master
phoenix-rtos-build	30f686733607104fbb850a70c4f5363e68fac78b	master
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	854103f0397055bf5d976117ec2431d6d84dbf8f	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	6cdf217e6e6370030055c9f905c933a0c1ac98d1	master
phoenix-rtos-lwip	4269464fd903bf877c9d19e5de9f21541cca04e2	master
phoenix-rtos-ports	4bd9ec801d4c10e1cd4a2712d2a44810a891fd44	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	d6c1356f04fc2d91780b5bfa99644acc262b0d11	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	b3e97dcb66245fc5f3e70fc4ed821af72299d8cd	master
phoenix-rtos-utils	34f87c487fb09979ac5705cb1c4fc8a002508fe4	master
plo	ae05823587dc3a1540417920dd4b49d292d876b9	master
```

# Integration State: 2026-06-06-sd-ext2-root-mounts

## Summary

- Date: 2026-06-05
- Note: HW-validated: bcm2711-emmc mounts SD ext2 partition as / by storage id (portRegister=0, 0 retries); ls works over live root; USB kbd+mouse enumerate. Open: executing binaries from /bin fails with sdcard Data CRC/End-Bit error (read-volume / data-path reliability, #120 layer 2).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 4ecc311 (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 5674368 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 30f6867 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | ebac8e4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 6cdf217e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a078a5c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 4bd9ec8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | cb4b216 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | b3e97dc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 34f87c4 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | ae05823 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	4ecc311d1e19c98a0939d4c9f9fd0434147add83	main
libphoenix	567436842a062614e9d7a99368598c89c470b27b	master
phoenix-rtos-build	30f686733607104fbb850a70c4f5363e68fac78b	master
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	ebac8e4d9747ff13a1cef4dece995bfa60592afd	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	6cdf217e6e6370030055c9f905c933a0c1ac98d1	master
phoenix-rtos-lwip	a078a5c7dd98f9a6fc84fc921c7e78b48fc3378d	master
phoenix-rtos-ports	4bd9ec801d4c10e1cd4a2712d2a44810a891fd44	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	cb4b2169ec91fe8eff8e023a13306ed7f8911339	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	b3e97dcb66245fc5f3e70fc4ed821af72299d8cd	master
phoenix-rtos-utils	34f87c487fb09979ac5705cb1c4fc8a002508fe4	master
plo	ae05823587dc3a1540417920dd4b49d292d876b9	master
```

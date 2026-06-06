# Integration State: fb0-framebuffer-device

## Summary

- Date: 2026-06-06
- Note: rpi4-fb /dev/fb0 framebuffer device; HW-validated netboot (pa=0x3e87c000 1024x768x32, read-only self-test, fbcon still OK, boot->psh, 0 faults)
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | fe2ae9f (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 5674368 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 30f6867 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 62888e3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 0fe0506 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 08a09d28 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | f0973b5 (dirty(5)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 4bd9ec8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | eec6e67 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 9a05bd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 993fe3c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 68172c1 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	fe2ae9f52868df2d8db867c10309f96e5685c585	main
libphoenix	567436842a062614e9d7a99368598c89c470b27b	master
phoenix-rtos-build	30f686733607104fbb850a70c4f5363e68fac78b	master
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	62888e31a753d137752f182277eb0c8919dc3477	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	0fe050684221ac918745aa0c315961f56627f2b4	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	08a09d28c700e676945c0a1db0eaf3df18755306	master
phoenix-rtos-lwip	f0973b560201fe7beba8d99a8c61391436f59fbb	master
phoenix-rtos-ports	4bd9ec801d4c10e1cd4a2712d2a44810a891fd44	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	eec6e674a1cc9c54c818ac1ef12514d09a4f36a7	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	9a05bd923b86828bfba2f76cc4045f478201f695	master
phoenix-rtos-utils	993fe3c82f8d25b707eaac8c8e40d2172724a15a	master
plo	68172c11570d0d0f64fa416fadae47cb113dab2f	master
```

# Integration State: 2026-06-09-usb121-stackfix-watchpoint

## Summary

- Date: 2026-06-09
- Note: #121 RESOLVED: usbkbd/usbmouse msgstack 1KB->8KB (devices f07b938) fixes the HID-attach stack overflow into hub_common.events. Root-caused via the new Route-A self-hosted watchpoint facility (kernel a67f6dac halt-first + 5160cd8d value-trap/emulate-resume). Validated: WP-armed boot silent (no overflow) + 6/6 clean netboot boots (psh, kbd0+mouse0, 0 faults) vs pre-fix ~3/11. Image d1e0c9e3.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 052857c (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 4b5cc61 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2a6aebb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | f07b938 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 19cd062 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 5160cd8d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 3d11426 (dirty(5)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8f54b36 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 7f866f2 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | e0911ce (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | aae3d35 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 68172c1 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	052857c419db186928ee04432ea1af2473732e8a	main
libphoenix	4b5cc61172234ec8b8a6187b06e42f389011578a	master
phoenix-rtos-build	2a6aebb3aef02ec87a5b8c273ad9b49567f9fe25	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	f07b938b4cfec8194f5b84a0e8df8c61a71ad949	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	19cd062dc373e74f523f666048fb51b23899a824	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	5160cd8d07c67ec26d139cb0488bdad5faf02e25	master
phoenix-rtos-lwip	3d11426d7cf9054a1bf81233a24c673abf04fbba	master
phoenix-rtos-ports	8f54b36cfef755d5f24b82250f83f15b88e6d56f	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	7f866f252db756d45b76c3da3b644c83f9177274	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	e0911ce7b0c8b063db2f3a5d13afba9d63b08eb6	master
phoenix-rtos-utils	aae3d350fa05f3663f8b9bdfb75c201a66fdf218	master
plo	68172c11570d0d0f64fa416fadae47cb113dab2f	master
```

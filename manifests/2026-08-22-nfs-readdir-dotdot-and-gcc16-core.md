# Integration State: nfs-readdir-dotdot-and-gcc16-core

## Summary

- Date: 2026-08-22
- Note: nfs-fs readdir . / .. synthesis (ee32037, test-libc-dirent 38/0 HW-verified) + the night's validated siblings: libphoenix semaphore lost-wakeup fix, phoenix-rtos-build -std=gnu17 pin, devices gcc-16 stringop fixes, coreutils stty fix, ncurses/nano/mc framework ports, test-libc/semaphore
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 2b4ed69 (dirty(5)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | e75c4fe (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 8b0c29c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | d026ff0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | eeb3590 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 3575089 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | ee32037 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 77d3a7f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 78a42efb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | e2bc887 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 7614905 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 50a8636 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f869ce7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | b64a8b1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 0033722 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	2b4ed6968343772eb15f63db8c267e5114406aa4	main
libphoenix	e75c4fe15c024f221c288f9536438fecbb1a6197	master
phoenix-rtos-build	8b0c29c187c8be34cece32e66c52cd76170a053a	master
phoenix-rtos-corelibs	d026ff096456eaaf70db4b6c2c93004a39c8bbf9	master
phoenix-rtos-devices	eeb3590c742e2eb73abf9e7657545c325e4b5fbf	master
phoenix-rtos-doc	35750898e4831cd61c7264b6d8e083ac08bb7a62	master
phoenix-rtos-filesystems	ee320370489bb92eec992ba8700298c8b0eaa5f1	master
phoenix-rtos-hostutils	77d3a7f737cf341618945b6cf5a9a3686dd867e3	master
phoenix-rtos-kernel	78a42efb608bc3118ec60401994ba6e4b267b0c4	master
phoenix-rtos-lwip	e2bc887f86faa8139b6af2a558ab64cec0efdc9b	master
phoenix-rtos-ports	7614905ba52a308061db09b6dbb90aff031abf0e	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	50a863652fdaa8ccb772d29b31f8e13e2efdeecb	master
phoenix-rtos-tests	f869ce76078805acb2ee34da341f0b4c31bc1f52	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	b64a8b16001fd6d974b74b5ce7761519e644b9dd	master
plo	003372296ee16f71d382ae10ed19ba934c8d5e48	master
```

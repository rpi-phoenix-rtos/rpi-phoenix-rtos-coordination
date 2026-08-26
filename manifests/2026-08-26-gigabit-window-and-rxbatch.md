# Integration State: 2026-08-26-gigabit-window-and-rxbatch

## Summary

- Date: 2026-08-26
- Note: TCP_WND 32->44*MSS (socket-recv 27.86->29.63, NFS 24.4->26.3 MB/s, bit-exact, project 0993e81 pushed) + optional RX-input core-lock batching (lwip 9db0d23 local, default-off; -21% input_us, gated for future socket-path gains). lib-lwip submodule 8f8335c8 (phoenix-pin-2) unchanged.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 66f9544 (dirty(7)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 033ee1f (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 643293e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | d026ff0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | eeb3590 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 3575089 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | b017513 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 77d3a7f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 78a42efb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 9db0d23 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | a99edc4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 0993e81 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | ca616da (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | b64a8b1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 0033722 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	66f95444dba2521116b5b520ef491accce07c55c	main
libphoenix	033ee1f307b43c11c36874569227f9e854edf0b5	master
phoenix-rtos-build	643293e4ca5ef96bbebf1001d30db6bc4ba2def1	master
phoenix-rtos-corelibs	d026ff096456eaaf70db4b6c2c93004a39c8bbf9	master
phoenix-rtos-devices	eeb3590c742e2eb73abf9e7657545c325e4b5fbf	master
phoenix-rtos-doc	35750898e4831cd61c7264b6d8e083ac08bb7a62	master
phoenix-rtos-filesystems	b017513f62009c577b2092a6cb5276bada304104	master
phoenix-rtos-hostutils	77d3a7f737cf341618945b6cf5a9a3686dd867e3	master
phoenix-rtos-kernel	78a42efb608bc3118ec60401994ba6e4b267b0c4	master
phoenix-rtos-lwip	9db0d23611c07ebdffe127e4a944bb3582744e3c	master
phoenix-rtos-ports	a99edc45ea9b3bfa0270f522f845f92d534d7f8c	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	0993e8163c67c7f69495317ea702527ec556b247	master
phoenix-rtos-tests	ca616da6d0257ea8af52ab30db7b4710eb84efbd	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	b64a8b16001fd6d974b74b5ce7761519e644b9dd	master
plo	003372296ee16f71d382ae10ed19ba934c8d5e48	master
```

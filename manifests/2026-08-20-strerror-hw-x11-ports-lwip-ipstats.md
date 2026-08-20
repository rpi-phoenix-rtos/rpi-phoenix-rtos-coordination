# Integration State: strerror-hw-x11-ports-lwip-ipstats

## Summary

- Date: 2026-08-20
- Note: strerror POSIX descriptions HW-verified (test-libc-string 9/9); X11 ports migrated to real framework (5 ports build-clean, gated if:false in ports.yaml); lwip /dev/ipstats diagnostic enabled+HW-validated
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 684cb0a (dirty(3)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | e71331d (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 4abd7a0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | d026ff0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 8d95c9b (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 3575089 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | bec497c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 77d3a7f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | ec58537b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 2323efd (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 3500a2a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | ff04a1b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 4f4c9c9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 56b1f7d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | b64a8b1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 0033722 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	684cb0a26e4fe4f9ea1f9bed2782fa90f909a5d1	main
libphoenix	e71331dbd812cbef911ea543e2fdf91fd4708f02	master
phoenix-rtos-build	4abd7a039319b9399356025b970ba365dd42b386	master
phoenix-rtos-corelibs	d026ff096456eaaf70db4b6c2c93004a39c8bbf9	master
phoenix-rtos-devices	8d95c9be1ee85f7e6cdd4ffd16f855636a15d1df	master
phoenix-rtos-doc	35750898e4831cd61c7264b6d8e083ac08bb7a62	master
phoenix-rtos-filesystems	bec497cbe53c2cbf4b9084c4fadb5fbe51076fbb	master
phoenix-rtos-hostutils	77d3a7f737cf341618945b6cf5a9a3686dd867e3	master
phoenix-rtos-kernel	ec58537b7b2007c043fb26f48d13ba4fc7b79543	master
phoenix-rtos-lwip	2323efda3e790a378587ac448d0d9257fcd0da3d	master
phoenix-rtos-ports	3500a2aa48479b40937049680aee2d2e63eeccfe	master
phoenix-rtos-posixsrv	ff04a1b3a669238147ef8c7c5bc28c2e3652f76d	master
phoenix-rtos-project	4f4c9c9704653b34d5882d90ee2d310e61537398	master
phoenix-rtos-tests	56b1f7d36d7413d3d567946967488efabbd93f19	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	b64a8b16001fd6d974b74b5ce7761519e644b9dd	master
plo	003372296ee16f71d382ae10ed19ba934c8d5e48	master
```

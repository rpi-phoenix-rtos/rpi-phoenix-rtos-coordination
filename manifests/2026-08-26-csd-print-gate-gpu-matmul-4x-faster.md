# Integration State: csd-print-gate-gpu-matmul-4x-faster

## Summary

- Date: 2026-08-26
- Note: Gate per-dispatch CSD UART print (v3d_gpu.c + winsys); reveals GPU GEMV 0.377ms vs CPU 1.644ms = 4.4x faster bit-exact. Overturns 'CNN-GPU ruled out' verdict.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 8a572f7 (dirty(9)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 033ee1f (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 643293e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | d026ff0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 95542eb (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 3575089 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | b017513 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 77d3a7f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 78a42efb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | bf34d89 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | a99edc4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 07ba705 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | ca616da (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | b64a8b1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 0033722 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	8a572f7040d014d5e64be1ab6bd095f225cf1fa4	main
libphoenix	033ee1f307b43c11c36874569227f9e854edf0b5	master
phoenix-rtos-build	643293e4ca5ef96bbebf1001d30db6bc4ba2def1	master
phoenix-rtos-corelibs	d026ff096456eaaf70db4b6c2c93004a39c8bbf9	master
phoenix-rtos-devices	95542ebd13d463b6c5763cf7e05909a0d4dda223	master
phoenix-rtos-doc	35750898e4831cd61c7264b6d8e083ac08bb7a62	master
phoenix-rtos-filesystems	b017513f62009c577b2092a6cb5276bada304104	master
phoenix-rtos-hostutils	77d3a7f737cf341618945b6cf5a9a3686dd867e3	master
phoenix-rtos-kernel	78a42efb608bc3118ec60401994ba6e4b267b0c4	master
phoenix-rtos-lwip	bf34d89f961705315e45a5e884b11d4380309623	master
phoenix-rtos-ports	a99edc45ea9b3bfa0270f522f845f92d534d7f8c	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	07ba705a73b56ce39da305629fb4b929102a9572	master
phoenix-rtos-tests	ca616da6d0257ea8af52ab30db7b4710eb84efbd	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	b64a8b16001fd6d974b74b5ce7761519e644b9dd	master
plo	003372296ee16f71d382ae10ed19ba934c8d5e48	master
```

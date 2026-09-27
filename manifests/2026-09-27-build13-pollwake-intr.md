# Integration State: build13-pollwake-intr

## Summary

- Date: 2026-09-27
- Note: kernel fb8b66ee: poll readiness wait always interruptible (eintr set=server 1000.1 ms); showcase 6/6
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 03fe2e412 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 156422a (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 57f4940 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 1924a42 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 2d9030a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | fb8b66ee (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 7c509c5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 33533d1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | df2f604 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 8cb5b62 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 926e1be (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a15f3ed (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	03fe2e412673f751a2bd5ec66ca259ea869329de	main
libphoenix	156422a2e95272a71393ddc5b6362ffcb31b2f42	master
phoenix-rtos-build	57f4940ae460ac1113ba3959a7005c86bb75a92a	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	1924a428a8827ce82961dea4c15272075ff47909	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	2d9030afcec16d2d1ccd5004a4fab23df0e10243	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	fb8b66ee7a9143f4fb54fa78310ae73b3b1a9272	master
phoenix-rtos-lwip	7c509c57893858ab790801f426e77a9a0b24841e	master
phoenix-rtos-ports	33533d1ae8b1cab66d5f2a75c7df644d2266093f	master
phoenix-rtos-posixsrv	df2f6049145503f584931a7abaf58a061106b845	master
phoenix-rtos-project	8cb5b62e7909f0efede9838d43414342ceee4443	master
phoenix-rtos-tests	926e1be00180fefe6b1c60f05a44b10f7eaf3932	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	a15f3ed82e3be5861aab785ae243ca8d7c6a1f12	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

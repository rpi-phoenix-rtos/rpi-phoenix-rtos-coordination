# Integration State: gpu-lane-build9

## Summary

- Date: 2026-09-26
- Note: Build 9: kernel object-tree fix (d0fb0ca9) + E1 memExport/memUnexport + port-death (dead server fails its parked clients; rid rotation) + libphoenix memExport prototypes + rpi4-vcmbox large-buffer calls. HW: M1 part-1 ping PASS, M1 P2-A GPU smoke PASS (serial/IRQ/pipeline), old-lane STK 7.30 fps 0 exceptions. E1/port-death/E3 probes pending (queue3b).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 4fa90886b (dirty(3)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 718b162 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2f0e5ad (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 79a4212 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 08d6a16 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 38ad32cf (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 7c509c5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8074f1b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | df2f604 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f5f5a29 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 7e6d891 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a15f3ed (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	4fa90886b4b28a5667b53e1f32ac0a3656660a2f	main
libphoenix	718b16282455df097a7ceee19d43d7c2dc5e2347	master
phoenix-rtos-build	2f0e5adf4203aafb3d2f323871313eca60cc1e61	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	79a42123f41274d2ddb2c87a5af111eb8f081c3d	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	08d6a16be7e3975b2ac05274dc5df3a27d61591c	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	38ad32cfbbed98352b3f0b8347629ad622d605ae	master
phoenix-rtos-lwip	7c509c57893858ab790801f426e77a9a0b24841e	master
phoenix-rtos-ports	8074f1bdaeb0a37fc70173583f67dc5145209f31	master
phoenix-rtos-posixsrv	df2f6049145503f584931a7abaf58a061106b845	master
phoenix-rtos-project	f5f5a29fca9cbcc9d77f5e0febe318ee4897b55d	master
phoenix-rtos-tests	7e6d891b00e3167f8b5ebf8b68fa6b16040b9e08	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	a15f3ed82e3be5861aab785ae243ca8d7c6a1f12	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

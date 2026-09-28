# Integration State: build27-threadinfo-cpuid

## Summary

- Date: 2026-09-28
- Note: Build 27b: kernel 17a47f39 (threadsinfo() reports cpuId again; dropped by sweep-10 merge 00dd500a) + tests 2230061 (sys/threadinfo: 3/3 FAIL on the old kernel -> 3/3 PASS); top -n 2 -d 1 shows CPU0..CPU3; gate X arm 0 faults. Also on this state: lwip 356ae98 (P13), devices 07c61c6 (WiFi merged), ports 086c35a (dbus SO_PEERCRED).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | ec633269e (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 00e6a64 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 73ffe0e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 07c61c6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 2d9030a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 17a47f39 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 356ae98 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 086c35a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | df2f604 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 8cb5b62 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 2230061 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a15f3ed (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	ec633269ee68781e9162f94d7916b0bfd916bf57	main
libphoenix	00e6a642a1fff916cae7d5f0c4e83772bbec2f07	master
phoenix-rtos-build	73ffe0efcb103296ca478c5deec81e9d6760440f	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	07c61c69f5e1fc447f26490c27273df53c74bb33	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	2d9030afcec16d2d1ccd5004a4fab23df0e10243	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	17a47f397eebcfdf583c26a72c616e6e41aba362	master
phoenix-rtos-lwip	356ae98b652cf2c684c7c93e8746690704d26563	master
phoenix-rtos-ports	086c35a71a3d59b63882f74c9b4be5bd3b4dea6a	master
phoenix-rtos-posixsrv	df2f6049145503f584931a7abaf58a061106b845	master
phoenix-rtos-project	8cb5b62e7909f0efede9838d43414342ceee4443	master
phoenix-rtos-tests	2230061c743413623e35b992eb1584714edb36c4	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	a15f3ed82e3be5861aab785ae243ca8d7c6a1f12	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

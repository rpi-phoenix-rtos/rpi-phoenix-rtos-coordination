# Integration State: libc-ten-defect-cluster

## Summary

- Date: 2026-09-25
- Note: 10 libc defects fixed+gated (setvbuf/freopen/strtol/strtod/printf/strftime/scanf x2 + UB + includes); D6 closed; all 21 libc suites green; 3 showcase gates 6/6 0 faults
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | f72ca308a (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | ad351a3 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2f0e5ad (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | aa3fe1e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 08d6a16 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | a6ddd2ca (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8074f1b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | df2f604 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 2f31e8e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | d0b75a9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | ce472cb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3e22b52 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	f72ca308aa3584606b785aee4c6c88aab49c4b12	main
libphoenix	ad351a38449d881e1812628152d398006840d8e0	master
phoenix-rtos-build	2f0e5adf4203aafb3d2f323871313eca60cc1e61	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	aa3fe1eee2479d46c5fd23a3b74280e401885f97	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	08d6a16be7e3975b2ac05274dc5df3a27d61591c	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	a6ddd2cae68cd04b11bffe6734864b74a9313fd8	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	8074f1bdaeb0a37fc70173583f67dc5145209f31	master
phoenix-rtos-posixsrv	df2f6049145503f584931a7abaf58a061106b845	master
phoenix-rtos-project	2f31e8e6c9b768d16a287d927cf6ef90ec567efd	master
phoenix-rtos-tests	d0b75a984e57d0c3ee02de4cb81fb67f5d483e7f	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	ce472cb9050260310c57bc466eb1e682561dcad9	master
plo	3e22b52515ce705233ee97f48ee579bd05406ca6	master
```

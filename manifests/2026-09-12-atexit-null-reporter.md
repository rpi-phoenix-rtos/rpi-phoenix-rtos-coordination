# Integration State: atexit-null-reporter

## Summary

- Date: 2026-09-12
- Note: libphoenix: _atexit_init's NULL-head recovery now reports instead of masking. ~3645 processes across 3 boots, 0 reports, 0 faults.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | c4b96a780 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 94a2d8e (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 95d9fca (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 4814fed (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 1292480 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 4f2d8fd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 6cd3adec (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 13d4a89 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 211d49a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | a9e6a66 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | d2e82b1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | bb33c9f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | e4dd587 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3e22b52 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	c4b96a780d9dd108ce6c7d1b22a48ad65de35a75	main
libphoenix	94a2d8e6ec324e61f8dec7cd33ea14fd0be17a43	master
phoenix-rtos-build	95d9fcaf5ee6df1077340705e02d573f2e1df47b	master
phoenix-rtos-corelibs	4814fedee83291da75b2c1d127e7ae68a3d99349	master
phoenix-rtos-devices	129248023ef1349a0d4bf815a829f662da63648d	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	4f2d8fdf2201785b9be4026523ea4fd39301deed	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	6cd3adecb0ecf37f0d78d9fc3da3905e1297091a	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	13d4a8905df8a06b8f067ab20e3c20ad74311afb	master
phoenix-rtos-posixsrv	211d49a7a3f54b736c3c03296492ce5b7cc45539	master
phoenix-rtos-project	a9e6a66dd62c20d12d4bcad804a3a41fbcdc6273	master
phoenix-rtos-tests	d2e82b1603638374d55e57bdcc8d955a6810b2f8	master
phoenix-rtos-usb	bb33c9ff825c0f2cd85fadc5b4ca749911ac5f6d	master
phoenix-rtos-utils	e4dd587195c5b7ad5745884e2aa5010ec8e3687a	master
plo	3e22b52515ce705233ee97f48ee579bd05406ca6	master
```

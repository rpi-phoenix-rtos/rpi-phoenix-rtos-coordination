# Integration State: 2026-06-22-glquake-render-fixes-ez-reenable

## Summary

- Date: 2026-06-22
- Note: GLQuake rendering correctness (color swap removal, alpha GL_SetupState, direct-render scanout, line-buffered log) + V3D Early-Z re-enabled with stock GL_LEQUAL. HW-validated: drops 265->1-2/boot, peak 51fps, all 5 issues correct, gun solid.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | e9b9389 (dirty(3)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 91cdbfd (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2a6aebb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 1821772 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 4b5acb4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 9c88a8e1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 0bb0123 (dirty(5)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8f54b36 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | ef6e39b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 2fb5a62 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | e0911ce (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 112c56b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | f8ed6aa (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	e9b93894a80eae6a53fd7b735642faeb8a709263	main
libphoenix	91cdbfd87ea6d526f8720a9cb57ff6c0b35bc57c	master
phoenix-rtos-build	2a6aebb3aef02ec87a5b8c273ad9b49567f9fe25	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	182177269a2767fa1220024dd83a0a3720abf2cb	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	4b5acb4a0d556bc036fc5394628e36d47e56fb27	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	9c88a8e129ae22adf692a52368d85805d4605599	master
phoenix-rtos-lwip	0bb0123446b304d95759dae7daeb2eb1bc5519cd	master
phoenix-rtos-ports	8f54b36cfef755d5f24b82250f83f15b88e6d56f	master
phoenix-rtos-posixsrv	ef6e39bd48e49e5ade990c67dc60f30d06b384ad	master
phoenix-rtos-project	2fb5a625afb3f43ec9939c17ffb300878efe2cd1	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	e0911ce7b0c8b063db2f3a5d13afba9d63b08eb6	master
phoenix-rtos-utils	112c56bbb31839ac5cd46422bb290c5916672de1	master
plo	f8ed6aaa69975e355e9e0d2b3c771a3f636c3480	master
```

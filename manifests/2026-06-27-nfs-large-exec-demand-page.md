# Integration State: 2026-06-27-nfs-large-exec-demand-page

## Summary

- Date: 2026-06-27
- Note: process_load demand-pages the ELF header map (kernel d30fd33a); large binaries (3MB X clients + startx) exec reliably from NFS, 0/~20 ENOMEM across 3 boots
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 4e10998 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | e9bb8c4 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | aad9a50 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 53383d1 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | fafa024 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | d30fd33a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 39309b9 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | ffa4214 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | ef6e39b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 08e4e41 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 12c4fe8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 83fa2f7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | f8ed6aa (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	4e10998c15bfcc462ef8b4c137d5010a7fb9347f	main
libphoenix	e9bb8c4b3bd30b14dfb73585844d91bba4b783df	master
phoenix-rtos-build	aad9a5039465aa037a4222835106201817c3ef04	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	53383d18feab43fb287b7691756ac6f63501f94d	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	fafa0249bfecbf6ca1a48c192f465a0ca4059978	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	d30fd33ac67f4dc8200d31b23a0f63a3884e4f61	master
phoenix-rtos-lwip	39309b9d88e84c11405939ca8aebb19085ea0a7f	master
phoenix-rtos-ports	ffa421430861848ff1d3eed5fb7caf8f542b1e12	master
phoenix-rtos-posixsrv	ef6e39bd48e49e5ade990c67dc60f30d06b384ad	master
phoenix-rtos-project	08e4e41289005ff11ea233010b7480ebc010c87a	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	12c4fe85b4a0d5731ffff110dda536a06905d35c	master
phoenix-rtos-utils	83fa2f7a1e34ceadb45a0c64735f593800ba9eea	master
plo	f8ed6aaa69975e355e9e0d2b3c771a3f636c3480	master
```

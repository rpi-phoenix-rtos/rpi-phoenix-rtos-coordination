# Integration State: 2026-06-26-quake-to-user-reorg

## Summary

- Date: 2026-06-26
- Note: Quake/vkQuake moved devices/misc -> phoenix-rtos-project/_user/; 6 GPU diagnostics+gl rungs dropped; GPU libs -> tools/.gpu-libs/; _user wired into rpi4b build. --scope core clean, rpi4-quake bundled.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 9381401 (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | da69de7 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | aad9a50 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 41d745b (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | fafa024 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | c3a118d2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 39309b9 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | ffa4214 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | ef6e39b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 08e4e41 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | e0911ce (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 83fa2f7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | f8ed6aa (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	93814019d762106ebb08110a69e02941744c6b2f	main
libphoenix	da69de7d3eda7341e509fc655be04e8aaedd41ce	master
phoenix-rtos-build	aad9a5039465aa037a4222835106201817c3ef04	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	41d745b2f6339ec70415fe07bd8e0dcf3a754e3b	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	fafa0249bfecbf6ca1a48c192f465a0ca4059978	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	c3a118d27182dd6bdac86871b40f0f2358f75ef6	master
phoenix-rtos-lwip	39309b9d88e84c11405939ca8aebb19085ea0a7f	master
phoenix-rtos-ports	ffa421430861848ff1d3eed5fb7caf8f542b1e12	master
phoenix-rtos-posixsrv	ef6e39bd48e49e5ade990c67dc60f30d06b384ad	master
phoenix-rtos-project	08e4e41289005ff11ea233010b7480ebc010c87a	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	e0911ce7b0c8b063db2f3a5d13afba9d63b08eb6	master
phoenix-rtos-utils	83fa2f7a1e34ceadb45a0c64735f593800ba9eea	master
plo	f8ed6aaa69975e355e9e0d2b3c771a3f636c3480	master
```

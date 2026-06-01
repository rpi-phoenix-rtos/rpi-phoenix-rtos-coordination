# Integration State: 2026-06-01-usb-merged-config-bringup

## Summary

- Date: 2026-06-01
- Note: Framework's own xHCI bring-up deterministic in merged config (no rig/DRIVE_ONLY); wall gone (eventsSeen=9,fix19Rc=0,bringupRc=0 3/3). Enumeration still pending.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | c303fef (dirty(4)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | codex/upstream-sync-20260516 | 85c3bb1 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | codex/upstream-sync-20260516 | 3e4028a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | codex/upstream-sync-20260516 | eacdd27 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | codex/upstream-sync-20260516 | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | bcb64610 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | agent/rpi4-genet | 1e0102f (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | a7f3b67 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | codex/upstream-sync-20260516 | 2213c18 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 7259b26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | codex/upstream-sync-20260516 | 34f87c4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | codex/upstream-sync-20260516 | 54bf7c3 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	c303fef654af8020d769110526c36c1747527b5d	main
libphoenix	85c3bb147655792cb673e9a03b3d370d23316fe0	codex/upstream-sync-20260516
phoenix-rtos-build	3e4028a6b4b5b5f414c31b1a44ec13d797201701	codex/upstream-sync-20260516
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	eacdd27b98a893846d8a29865d4447f606a15f50	codex/upstream-sync-20260516
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	codex/upstream-sync-20260516
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	bcb6461015e916240d66f6858a70278b772f2fa6	agent/rpi4-program-reloc
phoenix-rtos-lwip	1e0102ff397bab6ff4928f5fb1006e71aa7dcfa7	agent/rpi4-genet
phoenix-rtos-ports	a7f3b67daeb1aeb934a3dc6919101d4f4bf80c9c	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	2213c18498739692a908c60673b5952b45a48890	codex/upstream-sync-20260516
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	7259b262144dcc4fe7a6ca874cfb29fb857bfedf	master
phoenix-rtos-utils	34f87c487fb09979ac5705cb1c4fc8a002508fe4	codex/upstream-sync-20260516
plo	54bf7c33d7cd695fb1abc3ed77f6a9779c02b06f	codex/upstream-sync-20260516
```

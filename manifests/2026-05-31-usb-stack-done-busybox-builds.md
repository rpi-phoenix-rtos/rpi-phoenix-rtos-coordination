# Integration State: 2026-05-31-usb-stack-done-busybox-builds

## Summary

- Date: 2026-05-31
- Note: Working boot: USB stack (hub+kbd enumerate, stable) + posixsrv launched + busybox builds for aarch64. Rollback point before any devfs-root/rc.psh boot-structure change for #123.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 218a928 (dirty(3)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | codex/upstream-sync-20260516 | 85c3bb1 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | codex/upstream-sync-20260516 | 3e4028a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | codex/upstream-sync-20260516 | 8e9e563 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | codex/upstream-sync-20260516 | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | bcb64610 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | agent/rpi4-genet | dc4d0c2 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | a7f3b67 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | codex/upstream-sync-20260516 | 8f7858a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 7259b26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | codex/upstream-sync-20260516 | b188911 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | codex/upstream-sync-20260516 | 54bf7c3 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	218a92882f8182ecf72f14e86fa774b38280bbf1	main
libphoenix	85c3bb147655792cb673e9a03b3d370d23316fe0	codex/upstream-sync-20260516
phoenix-rtos-build	3e4028a6b4b5b5f414c31b1a44ec13d797201701	codex/upstream-sync-20260516
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	8e9e56314314c83edc154085409f878c156de543	codex/upstream-sync-20260516
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	codex/upstream-sync-20260516
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	bcb6461015e916240d66f6858a70278b772f2fa6	agent/rpi4-program-reloc
phoenix-rtos-lwip	dc4d0c245901b687350b88cac099c411a72b12b6	agent/rpi4-genet
phoenix-rtos-ports	a7f3b67daeb1aeb934a3dc6919101d4f4bf80c9c	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	8f7858a23e828c5f9f2ec2225b895bdc841b005d	codex/upstream-sync-20260516
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	7259b262144dcc4fe7a6ca874cfb29fb857bfedf	master
phoenix-rtos-utils	b188911bcd123097f3eb0f6ba482e6a274c6c140	codex/upstream-sync-20260516
plo	54bf7c33d7cd695fb1abc3ed77f6a9779c02b06f	codex/upstream-sync-20260516
```

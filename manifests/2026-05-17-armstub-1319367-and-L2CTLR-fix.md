# Integration State: 2026-05-17-armstub-1319367-and-L2CTLR-fix

## Summary

- Date: 2026-05-17
- Note: Pi 4 boot crash at PC=0x400498 RESOLVED: armstub now applies 1319367 to CPUACTLR_EL1[46] (was on undefined sysreg) + L2CTLR_EL1|=0x22 BCM2711 timing. Kernel boots through to psh shell + VL805 enumeration. Two-boot reproducibility verified.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| libphoenix | codex/upstream-sync-20260516 | d063d93 (clean) | https://github.com/phoenix-rtos/libphoenix |
| phoenix-rtos-build | codex/upstream-sync-20260516 | 3fd5c6b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs/ |
| phoenix-rtos-devices | codex/upstream-sync-20260516 | b5cc6b0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc |
| phoenix-rtos-filesystems | codex/upstream-sync-20260516 | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils/ |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | 2ca5df17 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | b63d44c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip |
| phoenix-rtos-ports | master | d3c6cf9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv |
| phoenix-rtos-project | codex/upstream-sync-20260516 | dde9bb5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 8676250 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 2a35b16 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb |
| phoenix-rtos-utils | codex/upstream-sync-20260516 | 7800484 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils/ |
| plo | codex/upstream-sync-20260516 | f6fa5f9 (dirty(1)) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
libphoenix	d063d9362d933e0160a5e51b16ea59f25060b79c	codex/upstream-sync-20260516
phoenix-rtos-build	3fd5c6b20cbf2d6c0caa9a36577753bf455dc5f5	codex/upstream-sync-20260516
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	b5cc6b069c87296f5a89e2ac2ebe88bd1cf25001	codex/upstream-sync-20260516
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	codex/upstream-sync-20260516
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	2ca5df1722fa04f400dffba6f3b658e89619a959	agent/rpi4-program-reloc
phoenix-rtos-lwip	b63d44c2fc998f63bde1c3e24d0faf5b0a188c46	master
phoenix-rtos-ports	d3c6cf99fbeba450cddff097765e1dfbd28ab33d	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	dde9bb5e0bda6a1db1550a5c68c15811efc8c82e	codex/upstream-sync-20260516
phoenix-rtos-tests	8676250f05f73fcc82192aa68e4dff4991a4c0a0	master
phoenix-rtos-usb	2a35b16927fb7e58329fe494e5176b374695fa87	master
phoenix-rtos-utils	780048412adbed1ece5e995fc5809135804deb24	codex/upstream-sync-20260516
plo	f6fa5f97fa933ab85207a4f7810e4b9362378bc6	codex/upstream-sync-20260516
```

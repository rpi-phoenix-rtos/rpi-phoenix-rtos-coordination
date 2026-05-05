# Integration State: stage2-phase5-total-mem-4096

## Summary

- Date: 2026-05-05
- Note: Stage 1 cache enable parked after 4 cycles; Stage 2 phase 5 sets total_mem=4096 + gpu_mem=64 in config.txt
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| libphoenix | master | 3c76bba (clean) | https://github.com/phoenix-rtos/libphoenix |
| phoenix-rtos-build | master | 044ae92 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs/ |
| phoenix-rtos-devices | master | 3ee4702 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 77d6931 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc |
| phoenix-rtos-filesystems | master | 1884043 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 3865813 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils/ |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | 49ca0c66 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 806d01c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip |
| phoenix-rtos-ports | master | fdabc22 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports |
| phoenix-rtos-posixsrv | master | 4525014 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv |
| phoenix-rtos-project | master | dd419e1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f7978fc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | ac09eca (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb |
| phoenix-rtos-utils | master | da2f541 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils/ |
| plo | codex/common-aarch64-platform-makefiles | 61927ba (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
libphoenix	3c76bba52fcfbb1cacf45eac6ff400caff9b882a	master
phoenix-rtos-build	044ae924ec5ca310e28d9037979df7e20cd3b2ae	master
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	3ee470267787252b1d602fb44ea6aef216b84ead	master
phoenix-rtos-doc	77d6931bc1bd20e651227cc5f4760f6b5228b3a2	master
phoenix-rtos-filesystems	18840437d6c72f78b9a697560c23f0e4681610b7	master
phoenix-rtos-hostutils	3865813e559bbd4c63b47e8e30ed490c3ef910ff	master
phoenix-rtos-kernel	49ca0c66e2637cf23818daf6f1fed6652bf6809e	agent/rpi4-program-reloc
phoenix-rtos-lwip	806d01cbefd46004f72685cab121bd6dd6424a62	master
phoenix-rtos-ports	fdabc2259a91fc9707d8557e0b70fc009be17868	master
phoenix-rtos-posixsrv	45250145e393e0aa468e6a9bbde349d903e9eef3	master
phoenix-rtos-project	dd419e173ccd7091726a0d3dd8fa763ee1e3432a	master
phoenix-rtos-tests	f7978fcc7bb9ba6a6b7958a49faee8c31aef3e1a	master
phoenix-rtos-usb	ac09eca6e9a3bf1423fc9e592b42ab8be9010cf0	master
phoenix-rtos-utils	da2f54157830637ede485523a392c5110c6062dd	master
plo	61927ba0fd9df124d508e7848e92a51bb6495510	codex/common-aarch64-platform-makefiles
```

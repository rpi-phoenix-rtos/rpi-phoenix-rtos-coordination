# Integration State: build52-demand-zero

## Summary

- Date: 2026-10-02
- Note: build 33: kernel demand-zero anonymous mmap (test-mmap-demand-zero 13/0; 7 cases FAIL on the eager kernel), jsc footprint LLInt 215->79 MB, JIT 386->141 MB; browser: patch 0018 Wayland read fix (start 0/7 -> 8/8, 15/16), child exit watchdog + LOG without stdio; libc suites 0 failures; showcase 7/7 0 faults; B6 persist PASS. Open: unreaped WebProcess per start (P8, branch proc-exit-msg-abandon)
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | dcd1e7c81 (dirty(24)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 5dee408 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 183e7ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 961544f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 22089bb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | fcae86d0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a7f63a4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | d201875 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 56cb63e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 907cc6a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 749e2ac (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	dcd1e7c815913a21a80a28f76b062cea8be64c03	main
libphoenix	5dee408a1428df6c674c4dea7374851b30a70edc	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	183e7ae6e2f047bf06b9d440c608c39532668204	master
phoenix-rtos-devices	961544fccd9d136b143a8b8b0a04f916a0e8d68a	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	22089bb7118ccf4a9af31f31defcd66c64f84df8	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	fcae86d003e267c2e9531ed14e7fd183f8f1bfc3	master
phoenix-rtos-lwip	a7f63a4d49dbe9211221f359a1a8a55e7e95860f	master
phoenix-rtos-ports	d20187519bbcae0738299854ae6201fa62be5230	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	56cb63e991d17f966705d8ff6a6549d9b81d7f62	master
phoenix-rtos-tests	907cc6a86587f0755e6678b6f8e699772f7fea7b	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	749e2ac314214b0211d88832f2dea19883f57008	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

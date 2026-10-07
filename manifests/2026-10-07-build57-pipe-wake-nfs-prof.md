# Integration State: build57-pipe-wake-nfs-prof

## Summary

- Date: 2026-10-07
- Note: builds 41+42 (2026-10-07): posixsrv pipe/pty pollNotify + try-lock fix; NFS C10 fix (libnfs PDU abandon, chunked nfs_ops I/O); profiler event classes, kernel entry reason per sample; perf memcpy link fix. Pi: pipe_poll_wake 9/0 (failed 9/9 on build 40), poll suite 20/0, test_bigwrite PASS, ipc-rtt 23->3 ms, Speedometer 0.377->0.432 (Perf-Dashboard 86->2.6 s), WebGL 18.4 fps no rAF pauses (was 8.8 + 4 s pauses), B6 starts 8/8, test-prof-sampling 4/0.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 8487ec1db (dirty(23)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | e834bd2 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 183e7ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 961544f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 94ea9be (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 821644ea (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a7f63a4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 15654a5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 84b2636 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | bd59b34 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 67344dc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 64ff3b6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	8487ec1db7ccaafa738a56fbdbba839e1f836170	main
libphoenix	e834bd2cf93d01f412ebf6e60faaf2d293f42f74	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	183e7ae6e2f047bf06b9d440c608c39532668204	master
phoenix-rtos-devices	961544fccd9d136b143a8b8b0a04f916a0e8d68a	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	94ea9be8a08792c6f156600955b5bbaa1c88f481	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	821644ea1299cfb50fee0e371d17a45e8f7f3e4f	master
phoenix-rtos-lwip	a7f63a4d49dbe9211221f359a1a8a55e7e95860f	master
phoenix-rtos-ports	15654a5ccce5819341711e99f1f5f1e39f99d663	master
phoenix-rtos-posixsrv	84b26367ed44b7096b9652619b3b3cf2f16a2c9b	master
phoenix-rtos-project	bd59b34446dee923b340785a02d0998973ce90e9	master
phoenix-rtos-tests	67344dc771adc44dfa25014aa1f0bb8fa45aa0b3	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	64ff3b669d63318766554ae9e22211ff94f66a2b	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

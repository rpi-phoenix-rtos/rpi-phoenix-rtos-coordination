# Integration State: build60-kpipes-gpu-default

## Summary

- Date: 2026-10-07
- Note: builds 47-49 (2026-10-07): GResource bundles linked; page-cache follow-up (unlink frees, early eviction, diagnostics); kernel-native anonymous pipes; CSS features (corner-shape, object-view-box, ident()); /bin/browser + bench default GPU raster + dma-buf (+WebGL). Pi: pipes 21/0 (31 us ping-pong, 1.3 GB/s), poll-wake 20/0, css3test 70%, starts 3-4 s, Speedometer 1.281, JetStream-ab 79.7. Open: C16 fragmentation (agent).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | b0e30ad17 (dirty(25)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 6bf27ab (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 183e7ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 961544f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 94ea9be (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 4f85ec30 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 6ae263b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 2a5e304 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 85ce3dd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | bd59b34 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 9c2c3a0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 64ff3b6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	b0e30ad17774ad83ec04bc609d222ed305db3d4f	main
libphoenix	6bf27ab124f2719f5810cbaa6e6d78774478b8f7	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	183e7ae6e2f047bf06b9d440c608c39532668204	master
phoenix-rtos-devices	961544fccd9d136b143a8b8b0a04f916a0e8d68a	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	94ea9be8a08792c6f156600955b5bbaa1c88f481	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	4f85ec300375befda15902f5bda0dec0e8f38b63	master
phoenix-rtos-lwip	6ae263bfe1d72ef6af7d4c8f42a43033876c128b	master
phoenix-rtos-ports	2a5e3045feb3bc288f106e445c93f811cb7e2b34	master
phoenix-rtos-posixsrv	85ce3dd9f4277b9531d949946235cb6b18c401dd	master
phoenix-rtos-project	bd59b34446dee923b340785a02d0998973ce90e9	master
phoenix-rtos-tests	9c2c3a0c7e761480ec583b5415fc610640b61966	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	64ff3b669d63318766554ae9e22211ff94f66a2b	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

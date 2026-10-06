# Integration State: build54-jit-video-webgl

## Summary

- Date: 2026-10-06
- Note: build 35: browser with JSC JIT (B9 page 1136 vs 2374 ms LLInt, same checks), <video> over FFmpeg (H.264 720p 30 fps 0 drops; HEVC 1080p on rpivid hw=1 30 fps; seek ok; but only ~30% of frames painted), WebGL compiled (W0 context=none OK; W1/W2 never finished: no window on HDMI) and dma-buf transport (texture-dmabuf AB24 UIF accepted by labwc); libm asinhf/acoshf/atanhf + test; B4 crc c3e96bf3 x2; b6 start 8/8; 0 faults
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 1f0514b0b (dirty(24)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | e834bd2 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 183e7ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 961544f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 22089bb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 28e93f4a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a7f63a4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 90c123e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | bd59b34 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f7f3d36 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 749e2ac (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	1f0514b0b80e49d43d10516ab39cdecbfafd2896	main
libphoenix	e834bd2cf93d01f412ebf6e60faaf2d293f42f74	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	183e7ae6e2f047bf06b9d440c608c39532668204	master
phoenix-rtos-devices	961544fccd9d136b143a8b8b0a04f916a0e8d68a	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	22089bb7118ccf4a9af31f31defcd66c64f84df8	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	28e93f4ae8a50ade9f76a09fb2a12aa874cfe119	master
phoenix-rtos-lwip	a7f63a4d49dbe9211221f359a1a8a55e7e95860f	master
phoenix-rtos-ports	90c123e18b730e0020b15285ebb81ab08c6d51a8	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	bd59b34446dee923b340785a02d0998973ce90e9	master
phoenix-rtos-tests	f7f3d36f920f6bafbea0dc06cbe3f87e970a46d8	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	749e2ac314214b0211d88832f2dea19883f57008	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

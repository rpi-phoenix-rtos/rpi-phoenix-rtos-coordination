# Integration State: build64-mse-gated-zerocopy-optin

## Summary

- Date: 2026-10-08
- Note: Builds 59-61: MSE stage 1 (patch 0032, USE mse; pre-play pool, eviction keeps the start, --mse=off honoured) GATED: stage1a/1b/1c pass; native HLS + hls.js HEVC on rpivid. Zero-copy (0033/0034, rpivid drm_prime, P31 drop-to-IRAP) merged opt-in (WPE_PHOENIX_MEDIA_ZERO_COPY=1); build 61 HLS 1080p30 99.2% painted.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 29b2c4d07 (dirty(24)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 6bf27ab (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 183e7ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 961544f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 393b5d6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | fdb93738 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 6ae263b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 1f1f818 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 85ce3dd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f6a1868 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | aad9527 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 64ff3b6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	29b2c4d07027681d978726a0230d19ce6703aaeb	main
libphoenix	6bf27ab124f2719f5810cbaa6e6d78774478b8f7	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	183e7ae6e2f047bf06b9d440c608c39532668204	master
phoenix-rtos-devices	961544fccd9d136b143a8b8b0a04f916a0e8d68a	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	393b5d68bee1d2730e1c1ac991d479783392a013	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	fdb9373837b30fae5cc7918effedacb0b1a4cdcd	master
phoenix-rtos-lwip	6ae263bfe1d72ef6af7d4c8f42a43033876c128b	master
phoenix-rtos-ports	1f1f818fb91fc3ed78337668cf8e3897cdefe80c	master
phoenix-rtos-posixsrv	85ce3dd9f4277b9531d949946235cb6b18c401dd	master
phoenix-rtos-project	f6a1868b01cbb822522076f71b835298fd3f8e90	master
phoenix-rtos-tests	aad95271a4065a9e3c69828232965b9f031ddbc0	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	64ff3b669d63318766554ae9e22211ff94f66a2b	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

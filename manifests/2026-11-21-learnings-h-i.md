# Integration State: learnings-h-i

## Summary

- Date: 2026-11-21
- Note: Gates H (2026-11-20 23:26-00:40) and I (2026-11-21 01:15-02:38), netboot, Pi: devices ea2524f3 WiFi defaults f2blk 512 + pioirq 1 + rxblk 1 (TX 4.94/4.86, RX 5.28/5.03 MB/s, rpi4-wifi CPU ~half); kernel 9ef9624e (only translation/access/permission aborts to the page-fault handler: an alignment fault no longer livelocks), 057e0d03 (MAP_CONTIGUOUS pages zeroed), 6a8f9d14 (DC ZVA in hal_memset on A72, TD-20 resolved; signal frame zeroed with plain stores); tests 88bc1f1 (kernelhard kh_vm). Gate I: 8/8 boots with kernelhard 10/0 each, signal 58/0, misc 247/0, pthread 69/0, unix-socket 72/0, showcase 7/7 0 faults torches (q3 16539 frames). HEVC pipelining stays opt-in (worse in the player).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 4075abd59 (dirty(27)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | ad42f63 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 909333b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | f39fa96 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | ea2524f3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | f699369 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 474e852 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 6a8f9d14 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a0d6ebb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | c55f982 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | df9694b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 88bc1f1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | a37cd21 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 4bed126 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3818c83 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	4075abd59d505081ae0016976671eb84c96f2183	main
libphoenix	ad42f63e6dc5f745d5820b9d1e7ba442f6b18f31	master
phoenix-rtos-build	909333b94a47364fb77673c209bd0c584a3f876b	master
phoenix-rtos-corelibs	f39fa964e500e18698335e94cb179f5a1aab4f21	master
phoenix-rtos-devices	ea2524f36e9803ef151d088a7d1e310f4265dd94	master
phoenix-rtos-doc	f6993698da817e6fa705713ea33a22581e83b6d8	master
phoenix-rtos-filesystems	474e852ccc114fa1b96e9eb18411f138ab60f94d	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	6a8f9d1456515cfa81f1150d11ebfa7791a347e8	master
phoenix-rtos-lwip	a0d6ebbf39cee29ab511becce139ea5358535bb6	master
phoenix-rtos-ports	c55f982021f43ceabe7c74dfffa41ceb21240344	master
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	df9694b3852b413b3e92cdf8b09db2a06c6915af	master
phoenix-rtos-tests	88bc1f1db28cc662fc0a31c07b3c1cf5bc0c4280	master
phoenix-rtos-usb	a37cd21280a14a2811138de7ed33842963a58578	master
phoenix-rtos-utils	4bed126d92e1617cc4e597d62068988ca2ddd9f1	master
plo	3818c83dfc4df981f1ec874cd9fa2130b6ac6ca3	master
```

# Integration State: learnings-g

## Summary

- Date: 2026-11-20
- Note: Image G landed (gate G 2026-11-20 18:36-20:40, netboot, Pi): mount table (libphoenix ad42f63 /dev/mtab + mount_record, filesystems/devices recorders, utils psh df/mount, tests mntent; df/mount correct on the NFS root, test-libc-misc 247/0); ext2 ENOSPC/holes/crash-order/symlink fixes (filesystems 43c00d7 28e26d7 9e1ea4e 13f4808 6c8c117; host harness ALL GREEN, Pi SD test pending); WiFi interrupt RX path + block-mode RX knobs (devices, defaults unchanged; A/B: pioirq 1 + rxblk 1 = RX 4.81->5.07 MB/s, driver CPU -50%); HEVC phase pipelining opt-in FFMPEG_RPIVID_PIPELINE=1 (32.5->34.7 fps, 0 mismatches); lwip GENET_RX_SYNC_FRAME knob (default 0). Showcase 7/7, 0 faults, torches.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 12c8ddbba (dirty(28)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | ad42f63 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 909333b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | f39fa96 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 23b82fdd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | f699369 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 474e852 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 4c00c81b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a0d6ebb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | c55f982 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | df9694b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f7ee0b4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | a37cd21 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 4bed126 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3818c83 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	12c8ddbbaaabcb912233b74c82993af106e28743	main
libphoenix	ad42f63e6dc5f745d5820b9d1e7ba442f6b18f31	master
phoenix-rtos-build	909333b94a47364fb77673c209bd0c584a3f876b	master
phoenix-rtos-corelibs	f39fa964e500e18698335e94cb179f5a1aab4f21	master
phoenix-rtos-devices	23b82fddf9622e6dc823b944208fed7512513fcd	master
phoenix-rtos-doc	f6993698da817e6fa705713ea33a22581e83b6d8	master
phoenix-rtos-filesystems	474e852ccc114fa1b96e9eb18411f138ab60f94d	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	4c00c81b1d1c67a833e5931e35ccaeea300c15b0	master
phoenix-rtos-lwip	a0d6ebbf39cee29ab511becce139ea5358535bb6	master
phoenix-rtos-ports	c55f982021f43ceabe7c74dfffa41ceb21240344	master
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	df9694b3852b413b3e92cdf8b09db2a06c6915af	master
phoenix-rtos-tests	f7ee0b4c2a287ad32111143e45662e80b51b5c4a	master
phoenix-rtos-usb	a37cd21280a14a2811138de7ed33842963a58578	master
phoenix-rtos-utils	4bed126d92e1617cc4e597d62068988ca2ddd9f1	master
plo	3818c83dfc4df981f1ec874cd9fa2130b6ac6ca3	master
```

# Integration State: learnings-j

## Summary

- Date: 2026-11-21
- Note: Image J landed 2026-11-21 (gates J1 10/10 boots, J3 4/4, J2 libc 0 failures + showcase 7/7, q3 tickless A/B n=3/arm: off 52.04 vs on 51.60 avg fps, within spread). Idle interrupts ~6 200 -> ~1 600/s: kernel 642f2752 (tickless idle on CPUs 1-3: reschedule SGI, 100 ms backstop, no kick from an idle waker, IPI-aware heartbeat), devices bf9c7d6c (pl011-tty RX interrupt, xHCI idle 8 ms poll, KMS whole-timeout vblank wait, audio firmware DMA mask with empty-mask fallback), project c0a5025 (PL011_TTY_IRQ), lwip 3606550 (GENET 50 ms idle backstop), ports 6b3fac9 (mesa patch 0004 V3D_NPOT_MIPGEN=tfu switch). NFS read ~88-99 MB/s by dd and the host nfsd counter. mipcheck: TFU NPOT 0/252 off vs render path 118/252.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 3ed7fd1b9 (dirty(29)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | ad42f63 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 909333b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | f39fa96 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | bf9c7d6c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | f699369 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 474e852 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 642f2752 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 3606550 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 6b3fac9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | c0a5025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 88bc1f1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | a37cd21 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 4bed126 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3818c83 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	3ed7fd1b9f33777d9fd26ca08e6bdfe9036edd03	main
libphoenix	ad42f63e6dc5f745d5820b9d1e7ba442f6b18f31	master
phoenix-rtos-build	909333b94a47364fb77673c209bd0c584a3f876b	master
phoenix-rtos-corelibs	f39fa964e500e18698335e94cb179f5a1aab4f21	master
phoenix-rtos-devices	bf9c7d6c37937970702c023cf42fe0a75a3b1cb3	master
phoenix-rtos-doc	f6993698da817e6fa705713ea33a22581e83b6d8	master
phoenix-rtos-filesystems	474e852ccc114fa1b96e9eb18411f138ab60f94d	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	642f27527abc54aa7ac4c8497d029c7e7aa32899	master
phoenix-rtos-lwip	36065507486556e266efd816effcb3cf3c104b8d	master
phoenix-rtos-ports	6b3fac98ab9ab6c0a6c1474bf497efb303614ba6	master
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	c0a5025ae9708ddd6f720142d7fcbbe4e2d4fd15	master
phoenix-rtos-tests	88bc1f1db28cc662fc0a31c07b3c1cf5bc0c4280	master
phoenix-rtos-usb	a37cd21280a14a2811138de7ed33842963a58578	master
phoenix-rtos-utils	4bed126d92e1617cc4e597d62068988ca2ddd9f1	master
plo	3818c83dfc4df981f1ec874cd9fa2130b6ac6ca3	master
```

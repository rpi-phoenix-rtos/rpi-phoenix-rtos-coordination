# Integration State: build36-malloc-fastpath

## Summary

- Date: 2026-10-01
- Note: Build 7 (core scope + ports relink + tests, 01:47 PASS): libphoenix single-thread malloc fast path (a74a01f beginthreadex wrapper + __libc_multithreaded flag, fcb13f1 heap lock skipped while single-threaded). Pi: bench-malloc 654 ns/pair single-threaded vs 9119 ns multithreaded; bash 200k loop 75.9 -> 7.1 s; test-libc-malloc-mt 4/0, pthread 41/0; stale 0/381; showcase 7/7 0 faults (HDMI looked at).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | f83f2a986 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | fcb13f1 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 938c32a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | c99da26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | fc21dab0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 83a56d5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 7879360 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 2eb001e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | c33fd40 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 99535a2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	f83f2a9861c5ca6fcb85395b50251495d623b655	main
libphoenix	fcb13f195b608ccc5555fd122b481d6bc6e41770	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	938c32a69faa97586fa652fc5ed6c1084f20f4ed	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	c99da26d730f23ac156690d9dbd6fbff5598e27f	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	fc21dab021df81961b2c55fcc76de7d1f6b4ef9a	master
phoenix-rtos-lwip	83a56d5fb901ebc4e6d4142c8166d1aee97321ec	master
phoenix-rtos-ports	787936020e14f356800a5ac86f563123cd1b2b2e	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	2eb001ed040f5921d303ebb29cc8120e057e7376	master
phoenix-rtos-tests	c33fd404fa3b8a3b5313c71ddf36151d4c31bf67	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	99535a200d1d7965cb7c32bd034217884561447f	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

# Integration State: build35-optimised-ports

## Summary

- Date: 2026-10-01
- Note: Build 6 (full-clean + tests, 23:45 PASS): -O2 for all ports (build 71b723d + ports 7879360), log-noise fixes (catalogue 1-16), P20 hostname/resolver, G7 = libphoenix EINTR retry on queued path queries, F5, stack-protector runtime (not enabled yet), batch 1 (redis 7.2.16, libjpeg NEON, python computed gotos + modules, micropython/mbedtls, openssl -O2, curl http/ftp/file only, ffmpeg formats, sqlite pread, busybox trim), lwip AF_INET6 fix 83a56d5. Pi: tests gai 11/0, mprotect 6/0, inet-socket 8/0, netdb 14/0, sa_restart 1/0, stack-protector 4/0; showcase 7/7 0 faults; every tracked noise class 0; xz 2.0x faster, NFS unchanged. One X-cycle boot hit C9 (USB cc 36).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | cf00d167e (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 90bf4af (clean) | https://github.com/phoenix-rtos/libphoenix.git |
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
| phoenix-rtos-tests | master | 6f4781a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 99535a2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	cf00d167e2099cc6417fa46f4b57e6110b80e510	main
libphoenix	90bf4afe113a1bf39bb7bacb16f014a44097ae25	master
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
phoenix-rtos-tests	6f4781a9e77c02a47e41f8bf9c80c7fa47cc5283	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	99535a200d1d7965cb7c32bd034217884561447f	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

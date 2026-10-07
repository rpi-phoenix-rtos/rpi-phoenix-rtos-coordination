# Integration State: build58-perf-correctness

## Summary

- Date: 2026-10-07
- Note: builds 43-45 (2026-10-07): page cache across exit; syscall-free pthread_self/getspecific; lwIP+posixsrv futex locks; posixsrv timed requests (C12); pthread lifetime (C13); P17 REALTIME default condvars; sem_timedwait multi-waiter; lwIP per-socket worker hand-off + loop poll backport (C14); msun libm (C15). Pi: Speedometer 1.159, JetStream-ab 76.8, stanford-crypto PASS, math 132/0, inet-loopback 9/0 (failed on 44), pthread 69/0, NFS read 60-63 MB/s, IPC-RTT 2 ms, starts 3-4 s, video ends, WebGL 18 fps. Open: page-cache memory-pressure ENOMEM (agent).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | b7aab159d (dirty(24)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 6bf27ab (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 183e7ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 961544f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 94ea9be (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 3db4d10e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 2d9fc47 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 15654a5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 85ce3dd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | bd59b34 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | c8f9748 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 64ff3b6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	b7aab159dee178f881712fd81b261faa6113e554	main
libphoenix	6bf27ab124f2719f5810cbaa6e6d78774478b8f7	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	183e7ae6e2f047bf06b9d440c608c39532668204	master
phoenix-rtos-devices	961544fccd9d136b143a8b8b0a04f916a0e8d68a	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	94ea9be8a08792c6f156600955b5bbaa1c88f481	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	3db4d10e6c966f419bea3a53a2368778d3cb4312	master
phoenix-rtos-lwip	2d9fc471c57f19befad8e8b3fb2a94aa372fd222	master
phoenix-rtos-ports	15654a5ccce5819341711e99f1f5f1e39f99d663	master
phoenix-rtos-posixsrv	85ce3dd9f4277b9531d949946235cb6b18c401dd	master
phoenix-rtos-project	bd59b34446dee923b340785a02d0998973ce90e9	master
phoenix-rtos-tests	c8f97483b6fe58b589f725fd1d566e0d8480c3b3	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	64ff3b669d63318766554ae9e22211ff94f66a2b	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

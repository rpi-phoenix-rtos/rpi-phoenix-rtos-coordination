# Integration State: upstream-sync-adopted

## Summary

- Date: 2026-10-08
- Note: Upstream Phoenix-RTOS sync of 2026-10-08 adopted (12 repos fast-forwarded to upstream-sync-2026-10-08) + ports 59de7e5 (quakespasm stub) + tests 57f2389 (times test). Toolchain rebuilt (gcc 16.2.0; c++config.h reviewed). Build: --scope full-clean then --scope core, --with-tests --with-ports --with-showcase. Census 0 stale (14 export probes quarantined). Pi gate: sync8a 22/23 programs (the 23rd = stale test, fixed after; not yet re-run), hwconc cxx=4 c=4; sync8b2 fs contract ok on NFS + /tmp; sync8c2 exec-overwrite ok; showcase 7/7 rc0 0 faults, HDMI looked at; bench smoke 1.270/78.7/96/70%. Note: tests 57f2389 is in this manifest but NOT in the image that was gated.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | c0d306fd1 (dirty(25)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 6a511fd (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | a8e8679 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 79f68b0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 440bcec (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 777a04b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 389dfe1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | a2ce03b8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 6ae263b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 59de7e5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | a4ff18f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 57f2389 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 4bfadfb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	c0d306fd1b8828acfa16390bf4fff6b294d99f6b	main
libphoenix	6a511fd867b46e19049e498432ea79c69e11f349	master
phoenix-rtos-build	a8e867922db225453a86d415a2d12e658e63cd49	master
phoenix-rtos-corelibs	79f68b013d248111c524868fde19ddd68c64c389	master
phoenix-rtos-devices	440bcec0acf3ba841abf53da9a2c77c108615d76	master
phoenix-rtos-doc	777a04bd3ac621cd64976749c00276ce93627131	master
phoenix-rtos-filesystems	389dfe16c749c499f8438240181fa0bf1bd5aa42	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	a2ce03b8a14d7318b339341dada5b2b0e1cc3ba0	master
phoenix-rtos-lwip	6ae263bfe1d72ef6af7d4c8f42a43033876c128b	master
phoenix-rtos-ports	59de7e50865f67ad419977ed7f2527f60fdb2aeb	master
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	a4ff18f681a67e6317a34150690d81800d0d1dbc	master
phoenix-rtos-tests	57f2389c888b2f3a42bb30fa5c2c56a349c5e9ca	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	4bfadfba183d170f612ffe9560ad19015c4142b1	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

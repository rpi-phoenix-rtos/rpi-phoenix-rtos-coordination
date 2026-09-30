# Integration State: build32-upstream-sync-g5

## Summary

- Date: 2026-09-30
- Note: Build 2 (full-clean + tests, 11:15 PASS): upstream sync 2026-09-30 (kernel sessions/pgroups, getsid/procExists/sessionCtty in upstream syscall order; libphoenix/libtty/posixsrv/psh/ext2), one Wayland provider (wayland_phoenix: libwayland 1.24, protocols 1.49, xkbcommon 1.13.2; wayland port deleted, labwc no private copy), dropbear 2026.94, G5 flip-gate fix (ports 38cb9c0), redis Dl_info stub removed. Gates: stale 0/384, uids 4/0, pthread 41/0, WiFi joined, drmprobe 50/0, SSH password login OK, G5 0/1266 reversals + labwc flips gated 1421/1422, showcase gate 7/7 0 faults (HDMI checked). Project 69a1df9 (lighttpd HTTPS block off) landed after the build, unverified.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | e724f7f94 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 8c5b359 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 73ffe0e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 1c89d23 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 8985a35 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 9b3fe074 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 494ac61 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 9b908c6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 69a1df9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 74e018b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | e15e6cd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	e724f7f94ca62364a42669de491aecc8237e02b4	main
libphoenix	8c5b35904f0a8b70d5324fa16e58dbbd2c6cd87d	master
phoenix-rtos-build	73ffe0efcb103296ca478c5deec81e9d6760440f	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	1c89d23f3027e0ec14d1aaeb871734757a635f67	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	8985a35ce6f09ab325978cd2b6a3fa511adb7dce	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	9b3fe07494cc239cf1d0effd7482023a145878fc	master
phoenix-rtos-lwip	494ac619b6a28213b19d24dd9a71f6e90cc94568	master
phoenix-rtos-ports	9b908c621e81b9472fa08566e4a5f6b57771f5b2	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	69a1df997c4f9780f9edae6b86bb15b0c8d4c10b	master
phoenix-rtos-tests	74e018b1470f0baf75bfb9ba273c120174ae2e50	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	e15e6cdc2a85f48f5af10c35c1493696d11a2d32	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

# Integration State: build37-fork-resethand-fionbio-d10

## Summary

- Date: 2026-10-01
- Note: Builds 8-10 gated: showcase 7/7 x3; F7 redis --daemonize PONG (fork pthread_self + SA_RESETHAND + bundle sync after core), F9 asyncio, P19 GIO poll monitor, F8 sysconfig, C10 ext2 timestamps, D10 XFCE session paths. Toolchain libc bundle now synced between core and ports.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 59f27bbd1 (dirty(4)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | c0f7e86 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 938c32a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 5c807d1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | e45263b2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 83a56d5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 54ef596 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 2eb001e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f179d8f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 99535a2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	59f27bbd176b161d952e9b5545a7895da1629538	main
libphoenix	c0f7e863f49d30fdf0aaa6f83cadddc255072818	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	938c32a69faa97586fa652fc5ed6c1084f20f4ed	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	5c807d15fa2d5ac9d7bcb12c9855819df0d59768	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	e45263b271034c67a57114ad8b2fbcbf1c494720	master
phoenix-rtos-lwip	83a56d5fb901ebc4e6d4142c8166d1aee97321ec	master
phoenix-rtos-ports	54ef5964dc467e87491ae2226e93aa49e6fd16f9	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	2eb001ed040f5921d303ebb29cc8120e057e7376	master
phoenix-rtos-tests	f179d8f67934af929f456933bf8e6e03d226ecba	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	99535a200d1d7965cb7c32bd034217884561447f	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

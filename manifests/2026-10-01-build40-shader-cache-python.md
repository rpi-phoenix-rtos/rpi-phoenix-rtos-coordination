# Integration State: build40-shader-cache-python

## Summary

- Date: 2026-10-01
- Note: Builds 16-17: python _curses in lib-dynload (no exec_prefix warning); Mesa on-disk shader cache (G4: vkQuake first present 77 s cold -> 2.8 s warm). C10 closed on SD. Showcase 7/7.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 881807d3e (dirty(8)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | c0f7e86 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 9299299 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 5c807d1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | b3040484 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a7f63a4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 69344b2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 38a5156 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 8176308 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 1e624b0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	881807d3e9291c9b0f8dbe6c513dc1a2e3cb4024	main
libphoenix	c0f7e863f49d30fdf0aaa6f83cadddc255072818	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	929929936b86a9d23c5945325d6a31ec0f4f63ce	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	5c807d15fa2d5ac9d7bcb12c9855819df0d59768	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	b3040484e2032f7b841e0a631e6ad1ac89545823	master
phoenix-rtos-lwip	a7f63a4d49dbe9211221f359a1a8a55e7e95860f	master
phoenix-rtos-ports	69344b24e1e3145c1b044a68f6f4243bac5b7661	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	38a51562f7ac9b20cdc77f63db3512a4b837d2f5	master
phoenix-rtos-tests	81763085cd389aa2178403f646329ce979b9977f	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	1e624b0a167d5d0c38d33b0b0e564fd012d49b22	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

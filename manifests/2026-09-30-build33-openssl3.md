# Integration State: build33-openssl3

## Summary

- Date: 2026-09-30
- Note: Build 3 (full-clean, RELEASE config, 13:59 PASS): OpenSSL 3.5.9 LTS replaces 1.1.1w (ports 85c21b1, build 066c6c3), lighttpd HTTPS off without a cert (project 69a1df9), port downloads retry 5xx (build b945e2e). Pi: openssl/python 3.5.9 TLS1.3, CA-verified HTTPS example.com + python.org 200, lighttpd clean start, 0 faults; test programs in image = 0.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 2b47c6915 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 8c5b359 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | b945e2e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 1c89d23 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 8985a35 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 9b3fe074 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 494ac61 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 85c21b1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | d8926fe (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 74e018b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | e15e6cd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	2b47c69150ff225cf969776aebf575bc0ce33296	main
libphoenix	8c5b35904f0a8b70d5324fa16e58dbbd2c6cd87d	master
phoenix-rtos-build	b945e2e3875ab260d32e1e37b171b795645cfcb1	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	1c89d23f3027e0ec14d1aaeb871734757a635f67	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	8985a35ce6f09ab325978cd2b6a3fa511adb7dce	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	9b3fe07494cc239cf1d0effd7482023a145878fc	master
phoenix-rtos-lwip	494ac619b6a28213b19d24dd9a71f6e90cc94568	master
phoenix-rtos-ports	85c21b148df1f1d44bda1b9154ee5c98664aae27	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	d8926fe8179df47580ecb6ad84b33c4a6dccd3d4	master
phoenix-rtos-tests	74e018b1470f0baf75bfb9ba273c120174ae2e50	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	e15e6cdc2a85f48f5af10c35c1493696d11a2d32	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

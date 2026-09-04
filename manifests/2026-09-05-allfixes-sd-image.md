# Integration State: 2026-09-05-allfixes-sd-image

## Summary

- Date: 2026-09-04
- Note: SD image rpi4b-sd-2part-allfixes-20260905.img (SHA256 8dc1f67b15...). Carries: Q2 GLES glInvalidateFramebuffer fix, libphoenix double-fclose/list fix, wmsetbg, glamor GL_BGRA readback, per-repo boot version banner, openssl 1.1.1w with TLS 1.3 ENABLED, mbedtls 2.28.10, sysroot-built GPU archives, binary.mk libc relink rule. HW: TLS 1.3 True, Q2 renders, 0 faults, 336 ELF / 0 dynamic / 15-15 complete.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | ba9ad227d (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 5fa3847 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 382d7dd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 1cb348f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | cd30ad2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | d9048511 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 779ebef (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 96f3088 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 83eda31 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 57cbf33 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	ba9ad227da0dc287bbfebe109d21fe96cff86a10	main
libphoenix	5fa3847e83397e9b862b5152c209c5a4afc49b88	master
phoenix-rtos-build	382d7dd7b862a6d6e3fdcfcd3039af6ac25d987c	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	1cb348fd8fdee1ee903d444f8c162c871584e0af	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	cd30ad2b286aa08c3b9b0cd44451e6126d26359c	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	d9048511d35f6d8ee285f55ddf9b5da88872ac84	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	779ebefbd4a9f7923c012088e58f84b90b6d8d30	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	96f30884c19d2e13a0156a5606e1fc41ce254e89	master
phoenix-rtos-tests	83eda318cddc91dfdc7ac428ab9520cb8dc20d3b	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	57cbf332e295b0a85baff0922ba081d2cbdf4bf6	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

# Integration State: nfs-fast-path-sd-image

## Summary

- Date: 2026-09-07
- Note: nfs-fs attr cache + readdir snapshot (stat 8.4x, open 10.2x, readdir 142x, 243-file asset read 32s->3s) + audit fixes; quake3e upstream f694bbbc merged; stk launcher setenv overwrite=0. Demo gate 5/5 on the pre-merge build, Q3 re-verified post-merge.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 14bc0a77f (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 5fa3847 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | a80c1fe (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | a4f21b0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | be68a10 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 6d8f40a5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | b36c396 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f257a5f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 5982203 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 57cbf33 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	14bc0a77f5a9805dc539d1e6b0c2d8d40581c435	main
libphoenix	5fa3847e83397e9b862b5152c209c5a4afc49b88	master
phoenix-rtos-build	a80c1fe7edd6f4cc4aa7f128c9e5c5caacd3fea6	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	a4f21b0763a700bf187ace61fd5049e769329dd0	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	be68a10c5f49a7e84eeeec255576994cd2bd2270	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	6d8f40a59434e4798f04d4386317dae637715903	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	b36c396f91cd695d93ebd9bb617280e4e334c0a4	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	f257a5f24890d66551ef180f5d4795f0b06baf40	master
phoenix-rtos-tests	5982203847d9ba12c952d6dc14652db289c04568	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	57cbf332e295b0a85baff0922ba081d2cbdf4bf6	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

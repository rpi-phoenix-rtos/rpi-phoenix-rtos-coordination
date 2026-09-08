# Integration State: stk-clock-fix-demo-image

## Summary

- Date: 2026-09-08
- Note: STK std::chrono 1s-resolution fix (ports 6f08c26); SD image 23f46860; STK gated 0 faults on --race-now, quakespasm/yquake2/vkquake relinked and need re-verification
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 2e7349481 (dirty(3)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 9029813 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | a80c1fe (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 45e3d06 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | be68a10 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 76e0adbc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 6f08c26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f257a5f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f54cdec (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 57cbf33 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	2e73494818ee9fe4fa85c279e44b6566239322a1	main
libphoenix	9029813b292a56691fce0806deab99ced6eb891f	master
phoenix-rtos-build	a80c1fe7edd6f4cc4aa7f128c9e5c5caacd3fea6	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	45e3d068549ca075c3c024fae8c761dfc0aac9ae	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	be68a10c5f49a7e84eeeec255576994cd2bd2270	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	76e0adbc71d428d5aa279688ea64072cd0853715	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	6f08c261a26c4a366702e5b0db87112e6d9b8ec8	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	f257a5f24890d66551ef180f5d4795f0b06baf40	master
phoenix-rtos-tests	f54cdec0bccd24fabd6a9b46d5e7e779dff41e7b	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	57cbf332e295b0a85baff0922ba081d2cbdf4bf6	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

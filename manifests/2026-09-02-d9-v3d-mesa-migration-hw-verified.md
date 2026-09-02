# Integration State: d9-v3d-mesa-migration-hw-verified

## Summary

- Date: 2026-09-02
- Note: V3D/Mesa glue moved to phoenix-rtos-devices/gpu/rpi4-v3d/mesa (D9 option A) - HW-verified on Pi4: GLQuake 37-43fps virt_h=3240 triple-buffer, startx_gpu deskapps desktop, gl-x11-window-daemon GPU window; 0 faults in all three cycles
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 3e43777e4 (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 8dc40bb (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 857a5e8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | b0c9cdb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | cd30ad2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 31045b7f (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | fcb4311 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 0d9de9a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | ca91eb9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f9e3102 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 5d19cda (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	3e43777e45468d2776fa798191d6042f0204747d	main
libphoenix	8dc40bbc18d4766754e90de39765ff5d77e4632b	master
phoenix-rtos-build	857a5e86f668b7e1e9b0e11bc3e3f86f2654744c	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	b0c9cdbd9c725267c1c46606efe5046f3d3d0b6f	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	cd30ad2b286aa08c3b9b0cd44451e6126d26359c	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	31045b7f0c79a84f177e167eba6dafe05f740523	master
phoenix-rtos-lwip	fcb4311b598d7cc29d4101f4b0cb5800cb80c2a2	master
phoenix-rtos-ports	0d9de9a39418f4b217e81a9fe285cccb84ccdc12	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	ca91eb97c50f5da2727af036130214befd0b12e5	master
phoenix-rtos-tests	f9e310207197c5359ccbf114638dd4f3525c0b4b	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	5d19cdab175eda6595f984421db8141b64540bf3	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

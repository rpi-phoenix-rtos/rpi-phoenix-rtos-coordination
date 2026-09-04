# Integration State: 2026-09-04-tls-bump-sysroot-archives

## Summary

- Date: 2026-09-04
- Note: openssl 1.1.1w + mbedtls 2.28.10 (dependents rebuilt via the new dependency-version staleness), GPU archives compiled against the build sysroot, all five engines relinked. HW: python3 reports OpenSSL 1.1.1w on the Pi, vkQuake torches 8/8 at the reference viewpoint, 0 faults; rootfs gates 15/15 and 333 ELF / 0 stale.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | f5a305c8f (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 8dc40bb (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 925550b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | abb4670 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | cd30ad2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | d9048511 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | fcb4311 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | e9181a9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 62b7fef (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 83eda31 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 57cbf33 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	f5a305c8f63414ca174ef0385c0561040d3268db	main
libphoenix	8dc40bbc18d4766754e90de39765ff5d77e4632b	master
phoenix-rtos-build	925550b9fc3ccc03e8468df75b7189c5a8e415de	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	abb4670bf6f2be7723b51b78d4595bcea031671e	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	cd30ad2b286aa08c3b9b0cd44451e6126d26359c	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	d9048511d35f6d8ee285f55ddf9b5da88872ac84	master
phoenix-rtos-lwip	fcb4311b598d7cc29d4101f4b0cb5800cb80c2a2	master
phoenix-rtos-ports	e9181a95df2222f59d5ff694dc06571f444701ad	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	62b7fef3cd5f6aeea5bad79a634fd9d419eda174	master
phoenix-rtos-tests	83eda318cddc91dfdc7ac428ab9520cb8dc20d3b	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	57cbf332e295b0a85baff0922ba081d2cbdf4bf6	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

# Integration State: drivable-demo-image

## Summary

- Date: 2026-09-07
- Note: 6/6 demo components gated 3 trials each (21 boots, 0 faults) PLUS reliable USB input: 6/6 boots with full enumeration and keyboard+mouse active. Adds the xHCI Disable-Slot recovery fix on top of the six-component image.
- Generator: scripts/snapshot-integration-state.sh

## Image artifact

| field | value |
| --- | --- |
| path | `artifacts/rpi4b/rpi4b-sd-2part.img` |
| size | 1680867328 bytes (1.57 GiB) |
| sha256 | `0ddcee676f46ac57f607e7f7bd9f26b5e575fa0621863adc9b6428a0baaaa91e` |
| variant | `sd` (2-partition: FAT boot + ext2 root) |

Verified still byte-intact 2026-09-08. Flash with:

```
sudo dd if=artifacts/rpi4b/rpi4b-sd-2part.img of=/dev/sda bs=4M conv=fsync status=progress
```

(`/dev/sda` is always the SD card on this host; `rpi4b-sd.img` is FAT-boot-only — do not flash
that one.)

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 4aafa8bef (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 2ec0497 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | a80c1fe (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | e371967 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | be68a10 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 76e0adbc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 4eed9f0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f257a5f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 291708a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 57cbf33 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	4aafa8bef5f65772682221b3a220e8b2bb5d86bb	main
libphoenix	2ec049762775c1f3041d5c7f2aa0a31fc1ce3e1a	master
phoenix-rtos-build	a80c1fe7edd6f4cc4aa7f128c9e5c5caacd3fea6	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	e371967642f48a7a8286005d0e663e0fa7f6354c	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	be68a10c5f49a7e84eeeec255576994cd2bd2270	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	76e0adbc71d428d5aa279688ea64072cd0853715	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	4eed9f046cd43aaaba635edcde414351000d2381	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	f257a5f24890d66551ef180f5d4795f0b06baf40	master
phoenix-rtos-tests	291708ac024431123ce46c8a1c1e7f1f1f91248a	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	57cbf332e295b0a85baff0922ba081d2cbdf4bf6	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

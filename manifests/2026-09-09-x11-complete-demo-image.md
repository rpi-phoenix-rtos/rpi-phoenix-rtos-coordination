# Integration State: x11-complete-demo-image

## Summary

- Date: 2026-09-09
- Note: SD image with the complete X11 work: AF_UNIX SO_RCVBUF (kernel ceiling 256 kB), glamor mirror fix, damage path fixed (25.6 presents/s vs 2.3), partial-X readback, xbill layout fix. Gates ALL PASS: contents + QEMU boot (SD variant) + the full 6-app HW re-gate the --scope core kernel relink owed. SHA256 7f3de5974b8f5fbade32759677d89763609825b599742844f561a9033561da7f
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | a6a311ce2 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | a595099 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | a80c1fe (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 7e58045 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | be68a10 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 137ec58f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 31e6097 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f257a5f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | f54cdec (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 02d733d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3e22b52 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	a6a311ce2a2eec7dfd5427888d8c1e0d7c74aacf	main
libphoenix	a5950994e84240a944e4c3d5fccb5efe552150a6	master
phoenix-rtos-build	a80c1fe7edd6f4cc4aa7f128c9e5c5caacd3fea6	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	7e5804581f0e251490a4a1a2871b75dd51de6a3b	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	be68a10c5f49a7e84eeeec255576994cd2bd2270	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	137ec58fe795e98855e8e61b7542137787821198	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	31e609730f21efab78bc98a0220fc28b501ddbc7	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	f257a5f24890d66551ef180f5d4795f0b06baf40	master
phoenix-rtos-tests	f54cdec0bccd24fabd6a9b46d5e7e779dff41e7b	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	02d733d5b11613d337caae27d781017c3e1e9e59	master
plo	3e22b52515ce705233ee97f48ee579bd05406ca6	master
```

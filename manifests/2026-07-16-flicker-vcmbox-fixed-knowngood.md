# Integration State: 2026-07-16-flicker-vcmbox-fixed-knowngood

## Summary

- Date: 2026-07-16
- Note: Quake flicker fixed via /dev/vcmbox (coord 3cc684c); SD HW-validated triple-buffer; known-good pre-upstream-pull rollback point
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 3cc684c (dirty(21)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 491618c (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 4ddabee (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 21d1d95 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | deb40ff (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 14d53ed (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | aa0c55a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 86ff8f4a (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 3526d62 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 5b1e0ea (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | ff04a1b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 83901ab (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 3b85994 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 3779d7d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 0766fcd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 98418d1 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	3cc684cd0438ac425552544bc99c8edc871355cd	main
libphoenix	491618cf5d29c81a417704c02da8b78ff9ff3d9e	master
phoenix-rtos-build	4ddabeef40778589374fa2c8078f06e7244d4847	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	21d1d958c3b216878fd790b2da4d69928aa669bd	master
phoenix-rtos-doc	deb40ffcf957bf72eaf0a4cedbb77922254c6439	master
phoenix-rtos-filesystems	14d53ed2b50f736530ebd29f4c248bc2b910a115	master
phoenix-rtos-hostutils	aa0c55a1bc12cbdf6a169bd3c15f6c636bd5b7be	master
phoenix-rtos-kernel	86ff8f4a180790378a2429ab2b077c8aac232281	master
phoenix-rtos-lwip	3526d6277e05080b5213733b48db7a2c1a4eb461	master
phoenix-rtos-ports	5b1e0ea53b0875d92fd598bd8066a7f575043113	master
phoenix-rtos-posixsrv	ff04a1b3a669238147ef8c7c5bc28c2e3652f76d	master
phoenix-rtos-project	83901ab775145ba0ff4a025e6115e247a6c9a368	master
phoenix-rtos-tests	3b859943839228ffcaec565dd23385f9721300a6	master
phoenix-rtos-usb	3779d7d19d9ed2e3d1ee9802609e27f1f6443e22	master
phoenix-rtos-utils	0766fcdd303e5bbb902d18b174fcc8171896a516	master
plo	98418d14753f21e46999acc5da3a118235a0ab3d	master
```

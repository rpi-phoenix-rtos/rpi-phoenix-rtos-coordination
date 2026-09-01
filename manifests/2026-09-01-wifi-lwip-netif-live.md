# Integration State: wifi-lwip-netif-live

## Summary

- Date: 2026-09-01
- Note: lwip wifi43455 netif obtains a DHCP lease over WiFi (wl2=10.43.0.89) alongside genet (en1=10.42.0.12); + strtok_r/copysign duplicate-stub fixes
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 91d2bd245 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 05afcd4 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 857a5e8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 4492373 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | fc2f62b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | d2e152ec (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 24aba77 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | c335352 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 0281848 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 4c170b8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a585fae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	91d2bd24567242c15aa8c58951650295b14fee2d	main
libphoenix	05afcd45b2157b63772d0e5b437cce83d6ce07c0	master
phoenix-rtos-build	857a5e86f668b7e1e9b0e11bc3e3f86f2654744c	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	4492373bcd815b3a779f1e5734a2433a92cd531b	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	fc2f62b0976a6aa33ebad27ca122b64c412daa04	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	d2e152ec836e2a2e7e562f29119ecce93779ccd9	master
phoenix-rtos-lwip	24aba77bb3ef5302ceeb0626ae8f9791f91cab9a	master
phoenix-rtos-ports	c335352372b1967cb2c1ff6e8baf49914e3cf0e4	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	0281848c037c9e1977d0cc459da7e7b1c07290b4	master
phoenix-rtos-tests	4c170b8cb4b0a0c7dbe905329ea2b17a7441bb01	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	a585faec974c981df7c07e83f365fcaf8c594d99	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

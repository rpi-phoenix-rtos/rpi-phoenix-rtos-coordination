# Integration State: lwip-detangled-on-upstream

## Summary

- Date: 2026-09-01
- Note: lwip fork rebuilt on upstream origin/master (11 clean commits, wi-fi/ restored); boot-proven: lwip+genet+IP+NFS-root takeover, 0 faults
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | cc08f8125 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | ac3baed (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 857a5e8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | b242f55 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | fc2f62b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 3844d204 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 95d55a6 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | c5812a1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 84a36fb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 92299b0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a585fae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	cc08f812541a4c48e08edbf309a43a1613b7ece9	main
libphoenix	ac3baedcf19d06374788c7f51614c3462c29f928	master
phoenix-rtos-build	857a5e86f668b7e1e9b0e11bc3e3f86f2654744c	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	b242f55ba6f9c4cdf513cf9c7fb50554a964f1fe	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	fc2f62b0976a6aa33ebad27ca122b64c412daa04	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	3844d2048309efc9cc537d9afb9b29daa56f3cf8	master
phoenix-rtos-lwip	95d55a6e8b6832dad4e8ab210e576a3fe9bca53b	master
phoenix-rtos-ports	c5812a16f717de1ee6256af5ae64ec9a54da320f	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	84a36fbd6bcf977d9a4ce9e9804f16deae35773e	master
phoenix-rtos-tests	92299b029622c04df2a7951c7ba9b9fd92520b67	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	a585faec974c981df7c07e83f365fcaf8c594d99	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

# Integration State: pthread-detached-stack-crash-fix

## Summary

- Date: 2026-09-01
- Note: libphoenix pthread 4c97a79: stop detached-thread exit crash (drop racy to_cleanup cross-thread stack free; leak detached stack interim) + fix pthread_join lock leak; HW-verified pthr burst clean + test-libc-semaphore 3/0
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 346b9274a (dirty(9)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 4c97a79 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 857a5e8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 5be4655 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | fc2f62b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 9a0593d0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | bf34d89 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | c76fcc4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 84a36fb (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 7c678ee (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a585fae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	346b9274ae9b13b74aff9158301f2900a28c0433	main
libphoenix	4c97a79adcd42b91851128e84f7e4f80058c2507	master
phoenix-rtos-build	857a5e86f668b7e1e9b0e11bc3e3f86f2654744c	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	5be46558f4cf413882076dfb4254a3b923f1e7e2	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	fc2f62b0976a6aa33ebad27ca122b64c412daa04	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	9a0593d0e7d14bd2a0f86e83cb4e7219bd891a83	master
phoenix-rtos-lwip	bf34d89f961705315e45a5e884b11d4380309623	master
phoenix-rtos-ports	c76fcc4e91c512bdfd5deb227c8759193072f897	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	84a36fbd6bcf977d9a4ce9e9804f16deae35773e	master
phoenix-rtos-tests	7c678eeac8e9d80d4f85e2339e4759880d666f9e	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	a585faec974c981df7c07e83f365fcaf8c594d99	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

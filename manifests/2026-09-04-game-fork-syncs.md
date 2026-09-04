# Integration State: 2026-09-04-game-fork-syncs

## Summary

- Date: 2026-09-04
- Note: All four game forks synced to upstream (quakespasm f5fe1786, quake3e 8dd27a54, yquake2 a9e88f6c, vkQuake 1aa13a56); each pin+patch bumped and HW-verified
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | a75921b15 (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 8dc40bb (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | fc3de5c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 6cdf597 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | cd30ad2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 5d8645f6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | fcb4311 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | a38880c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 2642335 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 83eda31 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 5d19cda (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	a75921b15383965d914c2074df87af6f5ae0d6d3	main
libphoenix	8dc40bbc18d4766754e90de39765ff5d77e4632b	master
phoenix-rtos-build	fc3de5cbd8d3fd0def127b62e4954125dc99ca33	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	6cdf597fdaae02c76dc7af40f81059dbca64928b	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	cd30ad2b286aa08c3b9b0cd44451e6126d26359c	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	5d8645f6d7b5fd5478f40602f76eff55ff4b8831	master
phoenix-rtos-lwip	fcb4311b598d7cc29d4101f4b0cb5800cb80c2a2	master
phoenix-rtos-ports	a38880ce43f719474e122976dcae98c0beec5238	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	26423354ba1bb3dade5a1f8a37a5cafbfb29839e	master
phoenix-rtos-tests	83eda318cddc91dfdc7ac428ab9520cb8dc20d3b	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	5d19cdab175eda6595f984421db8141b64540bf3	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```

# Integration State: 2026-06-14-quake-mapload-singleplayer

## Summary

- Date: 2026-06-14
- Note: Single-player 'map' loading works: registered loopback net driver in pl_phoenix_stubs.c (was net_numdrivers=0 stub from excluded net_bsd.c -> CL_Connect failed). map start spawns server + QuakeC VM + loopback connect + renders live 'start' hub (QUAKE logo room) textured on HDMI. QuakeC-VM pointer crash already resolved by prior work.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 4d9919f (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 4b5cc61 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2a6aebb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 8056e14 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | af30007 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 5160cd8d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | c4a7f46 (dirty(5)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8f54b36 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | edc9bd8 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | e0911ce (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | a3731ba (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 68172c1 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	4d9919f6816607aa1517d49a677c03ef6bea925b	main
libphoenix	4b5cc61172234ec8b8a6187b06e42f389011578a	master
phoenix-rtos-build	2a6aebb3aef02ec87a5b8c273ad9b49567f9fe25	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	8056e143d1530e312c46be71677e7b41ef4cb43e	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	af300072fc0a5a89a68809a5fc06ea2b2d0fa0df	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	5160cd8d07c67ec26d139cb0488bdad5faf02e25	master
phoenix-rtos-lwip	c4a7f46b0518fbfb32c0632aa332041c4c755667	master
phoenix-rtos-ports	8f54b36cfef755d5f24b82250f83f15b88e6d56f	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	edc9bd88ffe51669457b1e92327b7d385fd93cd5	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	e0911ce7b0c8b063db2f3a5d13afba9d63b08eb6	master
phoenix-rtos-utils	a3731ba39746b95da7458f193ba4ee4f29c31b63	master
plo	68172c11570d0d0f64fa416fadae47cb113dab2f	master
```

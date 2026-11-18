# Integration State: 2026-11-18-learnings-e

## Summary

- Date: 2026-11-18
- Note: L38 interrupt-driven SDIO blocks (opt-in: pioirq=1 / wifi pioirq 1; default polled); test-usb-dmapressure -H. Ports on stk-720-default (unverified).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | dbea44da2 (dirty(27)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 203a347 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 909333b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | f39fa96 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | de2867d6 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | f699369 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 7e6b5ba (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 4c00c81b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a09f9c2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | stk-720-default | 6e7a865 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 38e635c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 1165567 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 182d146 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 14da84c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3818c83 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	dbea44da21aa530c83c83c7fb0a1bbf8b542f45c	main
libphoenix	203a34758d737deeb8169600e27a2b368dc08616	master
phoenix-rtos-build	909333b94a47364fb77673c209bd0c584a3f876b	master
phoenix-rtos-corelibs	f39fa964e500e18698335e94cb179f5a1aab4f21	master
phoenix-rtos-devices	de2867d6481d1376bf294b6ec45325a37d857963	master
phoenix-rtos-doc	f6993698da817e6fa705713ea33a22581e83b6d8	master
phoenix-rtos-filesystems	7e6b5ba9284ec2106c0404b0e1889f71deab6b40	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	4c00c81b1d1c67a833e5931e35ccaeea300c15b0	master
phoenix-rtos-lwip	a09f9c246d4b630bca67a29ec948f00beb4ca5f8	master
phoenix-rtos-ports	6e7a865ea6c3974255ff3883b1e935163441db8f	stk-720-default
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	38e635c286cac9d719cbd250325c495c1ac62078	master
phoenix-rtos-tests	11655677c2a11ee592e01ba92c48ac05eecfe5b0	master
phoenix-rtos-usb	182d1460e83d66c9efb70217f66b2d72f274c408	master
phoenix-rtos-utils	14da84ced71983901842df6a277cf580b56519e2	master
plo	3818c83dfc4df981f1ec874cd9fa2130b6ac6ca3	master
```

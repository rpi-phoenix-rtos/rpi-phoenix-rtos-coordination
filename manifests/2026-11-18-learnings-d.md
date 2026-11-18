# Integration State: 2026-11-18-learnings-d

## Summary

- Date: 2026-11-18
- Note: Signal frame carries no kernel data; bounded PL011 console + optional per-CPU heartbeat (KERNEL_DIAG=-DKERNEL_HEARTBEAT); umass whole-disk node; L41 xHCI ghost-port + slot release, hub acks; plo poison command (PLO_DEBUG_POISON, off); jq 1.8.2; tests kh_sigframe + test-usb-dmapressure. Gates D (20/20 boots) + D3 (tests, USB 1 GiB PASS, WiFi profile, showcase 7/7)
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | e14042bf0 (dirty(27)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 203a347 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 909333b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | f39fa96 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 5f77897a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | f699369 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 7e6b5ba (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 4c00c81b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a09f9c2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | d5e26ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 38e635c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 7689d7f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 182d146 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 14da84c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3818c83 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	e14042bf011fe56684d55b9da9743553573f8d6e	main
libphoenix	203a34758d737deeb8169600e27a2b368dc08616	master
phoenix-rtos-build	909333b94a47364fb77673c209bd0c584a3f876b	master
phoenix-rtos-corelibs	f39fa964e500e18698335e94cb179f5a1aab4f21	master
phoenix-rtos-devices	5f77897af76af340eb1761204e4798424375decd	master
phoenix-rtos-doc	f6993698da817e6fa705713ea33a22581e83b6d8	master
phoenix-rtos-filesystems	7e6b5ba9284ec2106c0404b0e1889f71deab6b40	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	4c00c81b1d1c67a833e5931e35ccaeea300c15b0	master
phoenix-rtos-lwip	a09f9c246d4b630bca67a29ec948f00beb4ca5f8	master
phoenix-rtos-ports	d5e26ae2b9468888b8f440b092462c0babb5b3e6	master
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	38e635c286cac9d719cbd250325c495c1ac62078	master
phoenix-rtos-tests	7689d7f8f4b7b9e413a1d8345646b1e9078d4df5	master
phoenix-rtos-usb	182d1460e83d66c9efb70217f66b2d72f274c408	master
phoenix-rtos-utils	14da84ced71983901842df6a277cf580b56519e2	master
plo	3818c83dfc4df981f1ec874cd9fa2130b6ac6ca3	master
```

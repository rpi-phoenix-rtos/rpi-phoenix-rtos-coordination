# Integration State: 2026-11-18-upstream-sync-v3.4.0-adopted

## Summary

- Date: 2026-11-18
- Note: Upstream v3.4.0 merged into all 14 forks, full-clean + toolchain rebuild, post-sync Pi gate passed (showcase 7/7, libc 25 programs 0 failures), pushed to publish
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 53ce90403 (dirty(27)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 203a347 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 909333b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | f39fa96 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | b84ef164 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | f699369 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 7e6b5ba (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | d6fba79a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a09f9c2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | e8590d2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 38e635c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 62809a4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 3acc1f7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 14da84c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 9f3e72f (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	53ce904039b5cf21f18e7a6f979d0667433c1d3d	main
libphoenix	203a34758d737deeb8169600e27a2b368dc08616	master
phoenix-rtos-build	909333b94a47364fb77673c209bd0c584a3f876b	master
phoenix-rtos-corelibs	f39fa964e500e18698335e94cb179f5a1aab4f21	master
phoenix-rtos-devices	b84ef164328beabdd1e92e4d3731a282dd2f6928	master
phoenix-rtos-doc	f6993698da817e6fa705713ea33a22581e83b6d8	master
phoenix-rtos-filesystems	7e6b5ba9284ec2106c0404b0e1889f71deab6b40	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	d6fba79a3af9f974fa4b712b0a0d59f09c4bd0d7	master
phoenix-rtos-lwip	a09f9c246d4b630bca67a29ec948f00beb4ca5f8	master
phoenix-rtos-ports	e8590d207650fa8ff4aed80f42306e928ed8e071	master
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	38e635c286cac9d719cbd250325c495c1ac62078	master
phoenix-rtos-tests	62809a4ced5d20ef8f67e30a9cfeaae3f7b6caed	master
phoenix-rtos-usb	3acc1f78bc9954ff3d82eedd70ed2ee8f1e0ca76	master
phoenix-rtos-utils	14da84ced71983901842df6a277cf580b56519e2	master
plo	9f3e72f03d0f1a1ec62c6319432728e075884818	master
```

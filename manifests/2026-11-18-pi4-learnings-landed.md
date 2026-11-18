# Integration State: 2026-11-18-pi4-learnings-landed

## Summary

- Date: 2026-11-18
- Note: Pi 4 learnings L33 (plo .bss), L37 (PM-watchdog reboot), L10 (fault/sigreturn hardening + hal_jmp register scrub), L34 (USB DMA < 3 GiB), L35 (SD 512 KiB commands), L3/L39 (Mesa V3D 0019-0024) on top of v3.4.0; learnings gate B + master check passed
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 8a73f3dd5 (dirty(27)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 203a347 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 909333b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | f39fa96 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 4da28509 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | f699369 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 7e6b5ba (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 8d85230 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 0fc17d5e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a09f9c2 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 706f4bc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 5295e26 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 38e635c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | b6e640a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 1309693 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 14da84c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 9551073 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	8a73f3dd59fb8985ef2a18fb356f007ed6a6a134	main
libphoenix	203a34758d737deeb8169600e27a2b368dc08616	master
phoenix-rtos-build	909333b94a47364fb77673c209bd0c584a3f876b	master
phoenix-rtos-corelibs	f39fa964e500e18698335e94cb179f5a1aab4f21	master
phoenix-rtos-devices	4da2850934e885bd03c42a5b8714d4cf5e240aed	master
phoenix-rtos-doc	f6993698da817e6fa705713ea33a22581e83b6d8	master
phoenix-rtos-filesystems	7e6b5ba9284ec2106c0404b0e1889f71deab6b40	master
phoenix-rtos-hostutils	8d85230ccb56d620e74e4cd27cd676a6bde47a59	master
phoenix-rtos-kernel	0fc17d5e636d6efc3fe7354a9890fabbc1f492b3	master
phoenix-rtos-lwip	a09f9c246d4b630bca67a29ec948f00beb4ca5f8	master
phoenix-rtos-ports	706f4bcc9e552326fce9458517969a0699f97463	master
phoenix-rtos-posixsrv	5295e263674147b2a2af738d75eb671a128fec50	master
phoenix-rtos-project	38e635c286cac9d719cbd250325c495c1ac62078	master
phoenix-rtos-tests	b6e640a166485e8db86fb7a64081bbf2dfaadde1	master
phoenix-rtos-usb	13096931c6905ab517036b9af7c1e63547a3abe2	master
phoenix-rtos-utils	14da84ced71983901842df6a277cf580b56519e2	master
plo	955107389baf128afbaee9d3127357495fca9d33	master
```

# Integration State: 2026-05-25-wifi-tier2-cmd5-ocr

## Summary

- Date: 2026-05-25
- Note: WiFi Tier 2 CMD5 returns BCM43455 OCR = 0x30ffff00 (3 IO functions, no MP, voltage 2.0-3.6V). Root cause of prior all-zero responses: SDHCI command-issue encoding bug — RESPONSE_TYPE bits were going to TRANSFER_MODE instead of COMMAND. Fix: shift resp_type left by 16 bits.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 8576f6b (dirty(5)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | codex/upstream-sync-20260516 | bd61195 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | codex/upstream-sync-20260516 | 3e4028a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | codex/upstream-sync-20260516 | 14233e1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | codex/upstream-sync-20260516 | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | af171987 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | agent/rpi4-genet | 868e1f6 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | d3c6cf9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | codex/upstream-sync-20260516 | d1e77df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 9f99382 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | f7dea42 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | codex/upstream-sync-20260516 | b188911 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | codex/upstream-sync-20260516 | 54bf7c3 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	8576f6b8249b1290db6cdf5f341251ad5c805859	main
libphoenix	bd61195eb188d383c0163f5a22a461f7160c2fd8	codex/upstream-sync-20260516
phoenix-rtos-build	3e4028a6b4b5b5f414c31b1a44ec13d797201701	codex/upstream-sync-20260516
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	14233e1e284a9e569847034f0df75c9dac804868	codex/upstream-sync-20260516
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	codex/upstream-sync-20260516
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	af1719870cd640ef7f5ba981e2bca0f75cdd6cb1	agent/rpi4-program-reloc
phoenix-rtos-lwip	868e1f6851c09ef09eb11abe112e06deade89cc8	agent/rpi4-genet
phoenix-rtos-ports	d3c6cf99fbeba450cddff097765e1dfbd28ab33d	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	d1e77dfea83f59704f929afc6663c484311d5a52	codex/upstream-sync-20260516
phoenix-rtos-tests	9f99382f76a9a7a896b16d04351171945fe9256d	master
phoenix-rtos-usb	f7dea4236d1ac176d57c98a073a806baffaec299	master
phoenix-rtos-utils	b188911bcd123097f3eb0f6ba482e6a274c6c140	codex/upstream-sync-20260516
plo	54bf7c33d7cd695fb1abc3ed77f6a9779c02b06f	codex/upstream-sync-20260516
```

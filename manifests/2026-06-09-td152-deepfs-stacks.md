# Integration State: 2026-06-09-td152-deepfs-stacks

## Summary

- Date: 2026-06-09
- Note: #152: libstorage STORAGE_DEEPFS_STACKSZ constant + bcm2711-emmc use; genet irq/link stacks widened (8->16K, 4->8K). Netboot smoke clean (psh/lwip/genet/IP, 0 faults), image 2726cbfe. USB stack-bump lead REFUTED+reverted (see docs/inprogress/2026-06-09-usb-hid-attach-abort-localized.md).
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 9900308 (dirty(2)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 4b5cc61 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2a6aebb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 1741541 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 19cd062 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 2ea366be (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 3d11426 (dirty(5)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8f54b36 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 7f866f2 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 63951d7 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 9a05bd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | aae3d35 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 68172c1 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	99003083a52ca91750c228ce723428c062fd03f5	main
libphoenix	4b5cc61172234ec8b8a6187b06e42f389011578a	master
phoenix-rtos-build	2a6aebb3aef02ec87a5b8c273ad9b49567f9fe25	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	17415413c79b8ce0dd6fee52b44c6f6e15382389	master
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	19cd062dc373e74f523f666048fb51b23899a824	master
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	2ea366bed39e404087695c07c5bbd3377f0def7e	master
phoenix-rtos-lwip	3d11426d7cf9054a1bf81233a24c673abf04fbba	master
phoenix-rtos-ports	8f54b36cfef755d5f24b82250f83f15b88e6d56f	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	7f866f252db756d45b76c3da3b644c83f9177274	master
phoenix-rtos-tests	63951d7370d2e263c789311b8c040b4f38839e14	master
phoenix-rtos-usb	9a05bd923b86828bfba2f76cc4045f478201f695	master
phoenix-rtos-utils	aae3d350fa05f3663f8b9bdfb75c201a66fdf218	master
plo	68172c11570d0d0f64fa416fadae47cb113dab2f	master
```

## Note (restore caveat)

Validated image `2726cbfe` was built from this sibling state PLUS the parked,
**uncommitted** #154 SD deltas in the working tree (`devices`
`storage/bcm2711-emmc/sdcard.c` + `sdstorage_dev.c` — reset-on-timeout + gated-off
diag). Those are SD-only and do **not** affect the netboot deliverable validated
here (genet stacks + libstorage constant). A `restore-integration-state.sh` from
this manifest reproduces the committed tree but not those uncommitted SD deltas;
rebuild the netboot variant after restore for a byte-identical loader.

# Integration State: 2026-07-22-quake-consolidated-playable

## Summary

- Date: 2026-07-22
- Note: CONSOLIDATION BASELINE: Quake fully playable on V3D — flicker fixed (vcmbox), ~40fps@1080p, smooth 2-pose monster animation (lerp restored), no-capture full-speed build, exec-from-NFS + NFS-warmup mitigation. KNOWN ISSUE: minor geometry glitch (wrong/missing triangles) on some small items/torch-flames, lerp-independent, cosmetic — deferred to post-release. Latent marginal binner->render GPU wedge exists but normal builds dodge it. Pre-first-public-github-release checkpoint.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | eea9b16 (dirty(23)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 58c87dd (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 4abd7a0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 9c509be (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 50602f3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 434d1b3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 31f3431 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | bb6a64da (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 3296166 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 5b1e0ea (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | ff04a1b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 295aa0d (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 1891611 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 8e8316f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 2e83c79 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | d6c0969 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	eea9b160e57086a45afc6af75c07c87331c8e628	main
libphoenix	58c87dd925718388ecd39a5e34e1477ebd73bc2a	master
phoenix-rtos-build	4abd7a039319b9399356025b970ba365dd42b386	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	9c509be2272e9d6987d934edd44ec03292b460a5	master
phoenix-rtos-doc	50602f302e3f59f0d73de3f5b3a873f2ac3c5428	master
phoenix-rtos-filesystems	434d1b3211d725e1a8bf36df6fd61999355648b6	master
phoenix-rtos-hostutils	31f3431bbe49fd5df77eaeb0213b65d9b8eacf68	master
phoenix-rtos-kernel	bb6a64da0f6bd1691be0149b1834248d647ecd43	master
phoenix-rtos-lwip	32961663d35892b90bd5bee4f6b73fb04b60d3fa	master
phoenix-rtos-ports	5b1e0ea53b0875d92fd598bd8066a7f575043113	master
phoenix-rtos-posixsrv	ff04a1b3a669238147ef8c7c5bc28c2e3652f76d	master
phoenix-rtos-project	295aa0db14d8f5858b4415efc0d1a041fdddcf04	master
phoenix-rtos-tests	1891611ec545eae86a8886a286119b9e2c6bff6b	master
phoenix-rtos-usb	8e8316f95adf6933180d55f31a40064d95b2fb46	master
phoenix-rtos-utils	2e83c7993f909c353797653fb56c2556d19a20ed	master
plo	d6c09690a9a1502d9e38796c6fe4f047958857a7	master
```

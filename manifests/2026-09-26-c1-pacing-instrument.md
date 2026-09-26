# Integration State: 2026-09-26-c1-pacing-instrument

## Summary

- Date: 2026-09-26
- Note: (none)
- Generator: scripts/snapshot-integration-state.sh

## What this state is

The **C1 pacing instrument**, built and HW-validated. It is a rollback point, not a fix: nothing
here changes behaviour, it only makes C1 measurable on axes that did not exist before.

- **libphoenix `8cefd85`** — `malloc_c1Pacing()` (heaps created, bytes mapped) and
  `malloc_c1HeapBoHits()` (heaps whose physical page the V3D driver had closed, out of heaps
  probed). Both unconditional: a gated counter is a counter that can silently be zero.
- **devices `ec05e68`** — a `v3d-winsys: pace` line beside every `flipstat`, carrying
  `t/frames/boc/bore/var/munc/munp/munkb/pool/hbo/hbop/heaps/heapkb`. `t=` is measured from the
  **first frame**, which is the axis C1's fires are pinned to.

**Verified on hardware** (`c1pace1`, 2026-09-26 10:55, build 1 of the instrument): the line appears,
`heaps=` reads a number rather than `?`, and the trial reproduced the ~90 s fire anchor
out-of-sample while its heap-growth curve flattened to **0.0 kB/s** exactly there.

⚠ **Build-freshness gate for this state is NOT `loader.disk`** — the winsys links into the GPU app.
Gate on `strings .buildroot/_fs/<target>/root/usr/bin/supertuxkart | grep -c 'hbo=%lu'` (and
`munp=%lu`); those two strings exist only in this build. `bin/stk` is an 872 KB launcher with no
winsys in it, and the NFS export is not synced until a test cycle starts.

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | db733f0c3 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 8cefd85 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 2f0e5ad (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | a46399c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | ec05e68 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 08d6a16 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | ef4500cb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 8074f1b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | df2f604 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f5f5a29 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 7e6d891 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | ce472cb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 217f288 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	db733f0c3fc3e4cae330d232ec794bfb849079d9	main
libphoenix	8cefd85c4120c934f4ec677e76b89aca8464ff9a	master
phoenix-rtos-build	2f0e5adf4203aafb3d2f323871313eca60cc1e61	master
phoenix-rtos-corelibs	a46399c2c42ac7beb4f947351dd4f259117be2b2	master
phoenix-rtos-devices	ec05e687e4a4fcb394bb45e9de83af32f2614946	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	08d6a16be7e3975b2ac05274dc5df3a27d61591c	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	ef4500cbe59414dc931d1f3fa3d4c7f40a2e2f2b	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	8074f1bdaeb0a37fc70173583f67dc5145209f31	master
phoenix-rtos-posixsrv	df2f6049145503f584931a7abaf58a061106b845	master
phoenix-rtos-project	f5f5a29fca9cbcc9d77f5e0febe318ee4897b55d	master
phoenix-rtos-tests	7e6d891b00e3167f8b5ebf8b68fa6b16040b9e08	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	ce472cb9050260310c57bc466eb1e682561dcad9	master
plo	217f2883afd66845e704d47d2453f448bc8d2e3f	master
```

# Integration State: stk-graph-index-guard-verified

## Summary

- Date: 2026-09-11
- Note: STK AI-pathfinding crash FIXED: port patch 0014 guards three routes to an unchecked drive-graph index (steerToAngle unsigned conversion; findNonCrashingPoint and findNonCrashingPointNew indexing m_next_node_index[]/m_successor_index[] with m_track_node directly). Verified 0 crashes in 6 trials at --numkarts=4 (0 in 12 across both guard builds) against a 1-in-3 baseline. A fourth route (LinearWorld::getRescuePositionIndex) is documented and not yet guarded.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | abaf3e663 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 1c6cc83 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 95d9fca (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 4814fed (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 27170e1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 4f2d8fd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 0c768551 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 9de1250 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | a9e6a66 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | c91eede (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | bb33c9f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | e4dd587 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 3e22b52 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	abaf3e663035825b1204f6a9ed2e7305fa0b9935	main
libphoenix	1c6cc83b96d3aa7c66eb8146c60ea1d32fb31ade	master
phoenix-rtos-build	95d9fcaf5ee6df1077340705e02d573f2e1df47b	master
phoenix-rtos-corelibs	4814fedee83291da75b2c1d127e7ae68a3d99349	master
phoenix-rtos-devices	27170e1ddf4fcec07a4f8130a4f87fa1361dade0	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	4f2d8fdf2201785b9be4026523ea4fd39301deed	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	0c7685519cdf2b05fdabd6229fafec79d0593c33	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	9de12509968a78d6a12c028f0b3c79970a7cb7ac	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	a9e6a66dd62c20d12d4bcad804a3a41fbcdc6273	master
phoenix-rtos-tests	c91eede2c55e305c3916152887deb0665d5b2e94	master
phoenix-rtos-usb	bb33c9ff825c0f2cd85fadc5b4ca749911ac5f6d	master
phoenix-rtos-utils	e4dd587195c5b7ad5745884e2aa5010ec8e3687a	master
plo	3e22b52515ce705233ee97f48ee579bd05406ca6	master
```

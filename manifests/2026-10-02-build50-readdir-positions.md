# Integration State: build50-readdir-positions

## Summary

- Date: 2026-10-02
- Note: build 28: P27 readdir positions (NFS/dummyfs/ext2, MSG_READDIR_NEXT) + P28 ext2 rmdir; dirent 12/12 on NFS and /tmp incl. rm_rf_while_iterating; busybox rm -rf OK; libc 32 suites 0 failures; launcher cache-dir fix
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | db3094694 (dirty(23)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 5dee408 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 71b723d (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 183e7ae (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 961544f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | ec44abd (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 22089bb (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | e3f88ce4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | a7f63a4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | faaa712 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | c1cd405 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 56cb63e (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 6bec396 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 877cace (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 1e624b0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 1772cd0 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	db309469460ea4ebf256c7ac311e3e02206699d0	main
libphoenix	5dee408a1428df6c674c4dea7374851b30a70edc	master
phoenix-rtos-build	71b723d5f669f44c3f2b334656c2ccf90bac07df	master
phoenix-rtos-corelibs	183e7ae6e2f047bf06b9d440c608c39532668204	master
phoenix-rtos-devices	961544fccd9d136b143a8b8b0a04f916a0e8d68a	master
phoenix-rtos-doc	ec44abd576e17c4c0a5293db0336c1d9bc365bbf	master
phoenix-rtos-filesystems	22089bb7118ccf4a9af31f31defcd66c64f84df8	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	e3f88ce41b7232381cc44bca98588e65f5fac1e4	master
phoenix-rtos-lwip	a7f63a4d49dbe9211221f359a1a8a55e7e95860f	master
phoenix-rtos-ports	faaa712383b1f30cc84d33499de62c9adb296120	master
phoenix-rtos-posixsrv	c1cd405319e8515f6ced4c8a5d7d33d25ce86925	master
phoenix-rtos-project	56cb63e991d17f966705d8ff6a6549d9b81d7f62	master
phoenix-rtos-tests	6bec3965ed947f45d47f765e31aff0f3a22dcd6f	master
phoenix-rtos-usb	877caceb936556d144eea70af1063e269a4b5c63	master
phoenix-rtos-utils	1e624b0a167d5d0c38d33b0b0e564fd012d49b22	master
plo	1772cd0707dfa47a96bffe0738f51753a17608ba	master
```

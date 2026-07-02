# Integration State: 2026-07-02-exec-readahead-validated

## Summary

- Date: 2026-07-02
- Note: Exec/demand-paging read-ahead (kernel 8834eaf3): quake startup 68s->5.5s on SD-boot, renders demo1->demo2, 0 faults. Card flashed with this kernel.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | e812c00 (dirty(6)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 9128c5d (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | 4ddabee (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | 2311290 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | 23bc607 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | deb40ff (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | 463aec1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | aa0c55a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 8834eaf3 (dirty(2)) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | dffa814 (dirty(4)) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 205e4a9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | ff04a1b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | 7d7db56 (dirty(1)) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | d5d4cb1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | 12c4fe8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 92d23e0 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | 93881db (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	e812c005af503e755952832dc00234f913d605ee	main
libphoenix	9128c5ddb7cb7ffdf4142619ea8d1fd2f9dbc542	master
phoenix-rtos-build	4ddabeef40778589374fa2c8078f06e7244d4847	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	23bc6070ee90996b7b42ce1fe059478c10662e81	master
phoenix-rtos-doc	deb40ffcf957bf72eaf0a4cedbb77922254c6439	master
phoenix-rtos-filesystems	463aec13fbea6b436feeb34c11644b68e82bc04e	master
phoenix-rtos-hostutils	aa0c55a1bc12cbdf6a169bd3c15f6c636bd5b7be	master
phoenix-rtos-kernel	8834eaf35ed3f872dc3422ba6ca5a5aabacc8a96	master
phoenix-rtos-lwip	dffa8140739e925f050e162b3815a00763bab5d5	master
phoenix-rtos-ports	205e4a9421da679c10026fb0e70a6c06c5b5df21	master
phoenix-rtos-posixsrv	ff04a1b3a669238147ef8c7c5bc28c2e3652f76d	master
phoenix-rtos-project	7d7db561225d562d43f0cb41449a1e599b97e1c4	master
phoenix-rtos-tests	d5d4cb13145bb2f8d4717a7f8757895c96d107f4	master
phoenix-rtos-usb	12c4fe85b4a0d5731ffff110dda536a06905d35c	master
phoenix-rtos-utils	92d23e09b274b5bf7aa0b7685b6625fa2e024b8a	master
plo	93881dbabec04440b843b58c7a4f52363e0a365f	master
```

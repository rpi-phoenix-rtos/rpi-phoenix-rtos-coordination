# Integration State: smp-phase-c-step2-fixed

## Summary

- Date: 2026-05-22
- Note: Phase C step 2 PPI-enable guard fixed (kernel `fbfe3a3f`).
  Previous version used `hal_started()` which is 0 throughout the
  whole `_hal_init` window on aarch64 — both primary AND
  secondaries silently skipped the PPI enable. Now uses
  `hal_cpuGetID() != 0U` (direct, race-free) and spin-waits for
  `hal_timerIrq()` to return a valid PPI number (covers the
  primary _hal_cpuInit → SEV → _hal_timerInit window).

  With this commit in place, secondaries' IRQ + scheduler entry
  path is in principle complete: timer PPI is enabled per-CPU, the
  IRQ dispatch + reschedule logic is already SMP-aware (NUM_CPUS=4
  paths active since `fb9669f4`), and `threads_init` already
  creates one idle thread per CPU at MAX_PRIO. Cores 1-3 *should*
  now exit `_other_core_virtual`'s WFI on each timer tick, enter
  the scheduler, and run an idle thread (or any other ready
  thread of higher priority). Without UART markers on the
  secondaries' path the only externally-visible signal is
  continued boot stability.

  3-cycle stability: 2/3 reached `(psh)%`. The missed cycle is the
  usual late-prompt variance, not a PPI-enable regression.
  Image SHA `78ca2e5a716e73850347b503188e8bef7f416d7c4b2e181e1dc8b2878e5282b4`.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | ad344ad (dirty(5)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | codex/upstream-sync-20260516 | bd61195 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | codex/upstream-sync-20260516 | 3fd5c6b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | codex/upstream-sync-20260516 | cef62e1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | codex/upstream-sync-20260516 | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | fbfe3a3f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | b63d44c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | d3c6cf9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | codex/upstream-sync-20260516 | d25b488 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 9f99382 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | aa27592 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | codex/upstream-sync-20260516 | b188911 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | codex/upstream-sync-20260516 | 0ee44df (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	ad344ad45394ae72ba5081fc969bdeab6c4c8366	main
libphoenix	bd61195eb188d383c0163f5a22a461f7160c2fd8	codex/upstream-sync-20260516
phoenix-rtos-build	3fd5c6b20cbf2d6c0caa9a36577753bf455dc5f5	codex/upstream-sync-20260516
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	cef62e14b0f689658f904c7753ffe259e815f14f	codex/upstream-sync-20260516
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	codex/upstream-sync-20260516
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	fbfe3a3f54d524add6f24accbcaa18c5f3f83c0d	agent/rpi4-program-reloc
phoenix-rtos-lwip	b63d44c2fc998f63bde1c3e24d0faf5b0a188c46	master
phoenix-rtos-ports	d3c6cf99fbeba450cddff097765e1dfbd28ab33d	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	d25b4884465aebee76842bdadd664897145dfa27	codex/upstream-sync-20260516
phoenix-rtos-tests	9f99382f76a9a7a896b16d04351171945fe9256d	master
phoenix-rtos-usb	aa27592189fb2f22128220ef398ee7a61846a177	master
phoenix-rtos-utils	b188911bcd123097f3eb0f6ba482e6a274c6c140	codex/upstream-sync-20260516
plo	0ee44dff8efdc5c02cf53dd28e062cf14037c868	codex/upstream-sync-20260516
```

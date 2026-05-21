# Integration State: smp4-5of5-verified

## Summary

- Date: 2026-05-21 (renamed manifest file is 2026-05-22 wall-time)
- Note: **5/5 boot cycles reach `(psh)%` with full SMP-ready code paths**
  — `PLO_SMP_ENABLE=1` + `NUM_CPUS=4`. The kernel uses real LDAXR/STXR
  spinlocks, 4-bit GIC distributor mask, and per-CPU scheduler
  `current[]` arrays end-to-end. Secondaries are passengers (WFI in
  `_other_core_virtual` after their per-CPU VBAR / GIC / cpuInit);
  Phase C (programming each CPU's gtimer + giving them a real
  scheduler entry on PPI fire) is the next functional step.

  USB-HCD `ops->init` still fails — 4/5 cycles with rc=-19 (0xdead
  poison), 1/5 with rc=-110 (xhci_reset timeout). The remaining
  blocker is a CPU-side pmap aliasing bug in the Phoenix kernel
  AArch64 pmap; bridge HW state is intact (see
  `docs/notes/2026-05-21-pcie-bridge-ageing-codex.md`).

  Image SHA `7ef59fbf823bc7353fe4cdd2fa0962fb71cef39a1e45c5293219b09aca7b9577`.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 4b36ed2 (dirty(5)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | codex/upstream-sync-20260516 | bd61195 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | codex/upstream-sync-20260516 | 3fd5c6b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | codex/upstream-sync-20260516 | cef62e1 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | codex/upstream-sync-20260516 | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | fb9669f4 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
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
_build	4b36ed22c033b93323194c4c9277e4644c2d4a05	main
libphoenix	bd61195eb188d383c0163f5a22a461f7160c2fd8	codex/upstream-sync-20260516
phoenix-rtos-build	3fd5c6b20cbf2d6c0caa9a36577753bf455dc5f5	codex/upstream-sync-20260516
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	cef62e14b0f689658f904c7753ffe259e815f14f	codex/upstream-sync-20260516
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	codex/upstream-sync-20260516
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	fb9669f467d56ae178836c77546d3f5ad274ce91	agent/rpi4-program-reloc
phoenix-rtos-lwip	b63d44c2fc998f63bde1c3e24d0faf5b0a188c46	master
phoenix-rtos-ports	d3c6cf99fbeba450cddff097765e1dfbd28ab33d	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	d25b4884465aebee76842bdadd664897145dfa27	codex/upstream-sync-20260516
phoenix-rtos-tests	9f99382f76a9a7a896b16d04351171945fe9256d	master
phoenix-rtos-usb	aa27592189fb2f22128220ef398ee7a61846a177	master
phoenix-rtos-utils	b188911bcd123097f3eb0f6ba482e6a274c6c140	codex/upstream-sync-20260516
plo	0ee44dff8efdc5c02cf53dd28e062cf14037c868	codex/upstream-sync-20260516
```

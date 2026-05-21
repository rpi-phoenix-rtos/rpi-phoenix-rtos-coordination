# Integration State: pcie-bridge-bisect

## Summary

- Date: 2026-05-21
- Note: BCM2711 PCIe bridge-translation poison localised + partially
  fixed. Two distinct failure modes identified:

  1. `pcie_scanBus` iterating empty slots on the secondary bus
     (bus=1, dev=1..31) tears down the bridge's outbound window
     translation, so xhci's BAR0 reads return `0xdeaddead`. Fixed by
     limiting the per-bus sweep to device 0 — PCIe Express bridges
     are point-to-point and have at most one downstream device
     anyway. Detected via a per-dev pre-read probe.
  2. `cfgio.destroy()` munmapping `PCIE_BCM2711_HOST_BASE` after the
     scan also poisons the outbound window. Bridge mapping now held
     until process exit; documented as TD-USB-pmap (the kernel pmap
     should refcount MAP_DEVICE mappings of bridge registers, then
     this leak goes away).

  Verified: BAR0 reads back `0x01000020` (caplength 0x20, version
  0x0100) through xhci's MMIO mapping. xhci_init still fails with
  rc=-110 (xhci_reset timeout) downstream — VL805 firmware reload
  via `bcm2711NotifyXhciReset` returns -ENOMEM on its
  `MAP_CONTIGUOUS` allocation, leaving the controller in a state
  xhci_reset can't escape — separate bug for the next iteration.

  Boot stability across 3 cycles: 3/3 reach `(psh)%` (USB-HCD init
  still fails but boot path is unaffected).

  Image SHA `fad91b8f7d2544e142e549fd29c25ae80bc2e200a6f566eb43c14c2ad717925f`.
- Generator: scripts/snapshot-integration-state.sh

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | 5924aef (dirty(5)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | codex/upstream-sync-20260516 | bd61195 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | codex/upstream-sync-20260516 | 3fd5c6b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | ff6870b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | codex/upstream-sync-20260516 | c94be27 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | 5083598 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | codex/upstream-sync-20260516 | c7a1401 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 2a894a3 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | agent/rpi4-program-reloc | 2690736b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | b63d44c (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | d3c6cf9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 0cecd86 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | codex/upstream-sync-20260516 | dde9bb5 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 9f99382 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | aa27592 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | codex/upstream-sync-20260516 | b188911 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | codex/upstream-sync-20260516 | 0ee44df (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	5924aef1ea7223761d99cfdb4b0b3b56a85eb800	main
libphoenix	bd61195eb188d383c0163f5a22a461f7160c2fd8	codex/upstream-sync-20260516
phoenix-rtos-build	3fd5c6b20cbf2d6c0caa9a36577753bf455dc5f5	codex/upstream-sync-20260516
phoenix-rtos-corelibs	ff6870be35405ee63bac73b155816f62d05f755d	master
phoenix-rtos-devices	c94be275fa4e4359a7969ed8bdb3144cfd139484	codex/upstream-sync-20260516
phoenix-rtos-doc	5083598b45ec21355a90467656c2c101ed217ea4	master
phoenix-rtos-filesystems	c7a14019c6a70b6e0a6ce8b93fd232c90684ed68	codex/upstream-sync-20260516
phoenix-rtos-hostutils	2a894a3d643df5b24d45ae5147993fb07e3b3bc0	master
phoenix-rtos-kernel	2690736b50850a0049edabc2464d03908441cecb	agent/rpi4-program-reloc
phoenix-rtos-lwip	b63d44c2fc998f63bde1c3e24d0faf5b0a188c46	master
phoenix-rtos-ports	d3c6cf99fbeba450cddff097765e1dfbd28ab33d	master
phoenix-rtos-posixsrv	0cecd86d396030dc5cf65a366d85ddb0a42e501b	master
phoenix-rtos-project	dde9bb5e0bda6a1db1550a5c68c15811efc8c82e	codex/upstream-sync-20260516
phoenix-rtos-tests	9f99382f76a9a7a896b16d04351171945fe9256d	master
phoenix-rtos-usb	aa27592189fb2f22128220ef398ee7a61846a177	master
phoenix-rtos-utils	b188911bcd123097f3eb0f6ba482e6a274c6c140	codex/upstream-sync-20260516
plo	0ee44dff8efdc5c02cf53dd28e062cf14037c868	codex/upstream-sync-20260516
```

# 2026-04-12 Pi 4 Post-Switch UART Lane

## Summary

The Pi 4 post-switch UART lane is now explicit and stable enough for the next
real-board retry. The relocatable `kernel8` trampoline no longer reprograms
PL011 after the firmware baud switch, and the canonical host capture helper now
offers separate `firmware` and `postswitch` profiles.

## Implemented Changes

### `phoenix-rtos-project`

Path:

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-kernel8-reloc.S`

Result:

- the Pi 4 trampoline still emits `TR0..TR3`
- it still copies the embedded `plo` payload and branches to the real loader
- it no longer reprograms PL011 itself after entry
- the intended observable post-switch UART lane is now the firmware-set rate
  rather than a second mid-boot UART transition

### Coordination Repo

Paths:

- `/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh`
- `/Users/witoldbolt/phoenix-rpi/scripts/summarize-rpi4b-uart-log.py`
- `/Users/witoldbolt/phoenix-rpi/docs/testing-automation.md`
- `/Users/witoldbolt/phoenix-rpi/docs/manual-operator-instructions.md`
- `/Users/witoldbolt/phoenix-rpi/docs/pi4-first-hardware-trial.md`
- `/Users/witoldbolt/phoenix-rpi/docs/source-artifacts.md`
- `/Users/witoldbolt/phoenix-rpi/docs/status.md`

Result:

- `capture-rpi4b-uart.sh` now supports:
  - `--profile firmware` -> `115200`
  - `--profile postswitch` -> `103448`
- `summarize-rpi4b-uart-log.py` now recommends a `postswitch` rerun when a
  firmware-profile log ends at the PL011 baud-switch boundary without later
  Phoenix phases
- the current runbooks now tell the operator to perform a dual-profile UART
  retry for the active post-`plo` Pi 4 boundary

## Validation

- `bash -n scripts/capture-rpi4b-uart.sh`: pass
- `python3 -m py_compile scripts/summarize-rpi4b-uart-log.py`: pass
- `python3 scripts/summarize-rpi4b-uart-log.py artifacts/rpi4b-uart/rpi4b-uart-20260412-000322.log`:
  pass, with the expected `postswitch` rerun recommendation
- `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`: pass
  - surfaced caveat:
    - the direct Pi 4 QEMU sanity log still ends in the known later
      `Exception #37: Data Abort (EL1)`
    - a bounded GDB check against the raw direct Pi 4 kernel lane reconfirmed
      that the staged static `system.dtb` still has
      `memory@0 { reg = <0x00 0x00 0x00>; }`
    - so raw direct Pi 4 QEMU GDB without the patched DTB path is not treated
      as authoritative for the current real-hardware kernel-entry boundary
- `./scripts/verify-rpi4b-sdimg.sh`: pass

## Exported Artifact

- image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `7544e3e8012ccf9426134d94a8b9d68be52711e9f42291cfd1760801b7e16965`

## Next Step

- run the first real Pi 4 dual-profile UART retry:
  - one `--profile firmware` capture for early evidence
  - one `--profile postswitch` capture for `TR0..TR3`, `plo`, and kernel output

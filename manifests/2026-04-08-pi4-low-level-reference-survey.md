# Pi 4 Low-Level Reference Survey

Date:

- `2026-04-08`

Related step:

- `STEP-0420`

## Scope

Consolidate the highest-value low-level Raspberry Pi 4 facts from the listed
external references into one reusable board dossier for the next real-hardware
boot-debug steps.

## Sources Reviewed

Official and primary:

- BCM2711 peripherals PDF
- Raspberry Pi firmware boot tree
- Raspberry Pi Linux `bcm2711*.dts*`
- Raspberry Pi legacy `config.txt` documentation

Implementation references:

- Circle
- `rhythm16/rpi4-bare-metal`
- `rust-embedded/rust-raspberrypi-OS-tutorials`
- `sypstraw/rpi4-osdev`
- `markCwatson/rpi-os`
- NuttX BCM2711 porting case study

Supplementary:

- `rpi4os.com`
- Code Embedded GPIO note
- OSDev forum thread
- Stack Overflow peripheral-base thread
- Raspberry Pi forum thread
- CircuitPython broadcom port
- BOOTBOOT
- Ultibo Core

## Main Findings

- The official BCM2711 and Linux DTS sources agree on the practical address
  translation model:
  - `0x7e000000 -> 0xfe000000`
  - `0x7c000000 -> 0xfc000000`
  - `0x40000000 -> 0xff800000`
- The corrected Pi 4 GIC aliases now used by Phoenix are strongly supported:
  - `GICD = 0xff841000`
  - `GICC = 0xff842000`
- The Pi 4 local timer / prescaler aliases and frequency are also strongly
  supported:
  - `LOCAL_CONTROL = 0xff800000`
  - `LOCAL_PRESCALER = 0xff800008`
  - `CNTFRQ_EL0 = 54000000`
- BCM2711 GPIO pull control should use `GPIO_PUP_PDN_CNTRL_REG*`, not the older
  `GPPUD` / `GPPUDCLK` path carried over from Pi 3-era tutorials.
- The normal Pi 4 AArch64 firmware-native convention remains `kernel8.img`
  linked around `0x80000`; `kernel_old=1` is a special-case legacy path and is
  now explicitly treated as stale or risky on newer firmware.
- The staged downstream `system.dtb` is not equivalent to the live
  firmware-patched DTB, especially for `memory@0` and bootloader-filled nodes.
- The current highest-probability remaining boot gap is no longer basic address
  selection; it is that the current custom Phoenix Pi 4 armstub still performs
  less early EL3/EL2/EL1 setup than the known-working Circle and
  `rpi4-bare-metal` armstubs.

## Documentation Updated

- `docs/raspberry-pi-4-low-level-reference-survey.md`
- `docs/platforms/raspberry-pi-4.md`
- `docs/source-artifacts.md`
- `docs/README.md`
- `docs/status.md`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Validation

- checked the new survey against local clone paths under `external/`
- checked the official URLs used as anchor sources
- confirmed the new dossier is indexed from `docs/README.md`

## Next Recommended Step

Use the new survey to scope the next earliest-entry Pi 4 diagnostic step.
The strongest next candidate is a fuller Circle-style armstub register-setup
experiment or an even earlier visible sign-of-life path before the current
`plo` HDMI path.

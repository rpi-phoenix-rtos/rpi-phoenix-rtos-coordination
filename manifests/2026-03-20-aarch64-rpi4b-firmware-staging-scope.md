# Manifest: Pi 4 Firmware Staging Scope

- Date: `2026-03-20`
- Step: `STEP-0100`
- Result: `completed`

## Scope

- inspect the newly validated Pi 4 scaffold together with Raspberry Pi firmware boot requirements already documented in this repo
- choose the smallest next project-local staging step that prepares firmware-visible boot artifacts without real hardware
- stop before implementing that staging step

## Findings

- the current Pi 4 scaffold already produces the raw loader image `plo-aarch64a53-generic.img`, which is the nearest Phoenix artifact to a Raspberry Pi firmware-loadable image
- official Raspberry Pi documentation confirms:
  - Pi 4 firmware loads a kernel-style image from the boot partition
  - an explicit `kernel` filename can be selected in `config.txt`
  - `boot_load_flags=0x1` is the custom-firmware setting for bare-metal style images
  - `uart_2ndstage=1` enables additional firmware UART logging
  - `kernel_address` is a documented load-address override for custom images
- the current generic loader still has two known boot blockers for real firmware boot:
  - no Pi 4 DTB is staged yet
  - the generic `_init.S` path currently requires EL3

## Selected Next Step

- stage a firmware-facing boot directory under `_boot/aarch64a53-generic-rpi4b/`
- add a project-local `config.txt`
- copy the raw `plo` image under a Pi 4 firmware kernel filename
- keep DTB import and EL2/EL3 handoff work out of this step, but record them as the next blockers

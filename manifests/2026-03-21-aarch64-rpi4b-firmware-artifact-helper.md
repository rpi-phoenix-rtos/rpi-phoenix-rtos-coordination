# Manifest: Pi 4 Firmware-Boot-Tree Assembly Helper

- Date: `2026-03-21`
- Step: `STEP-0270`
- Scope: assemble a firmware-visible Pi 4 boot tree from the current staged
  Phoenix outputs plus Raspberry Pi firmware files

## Change

- add:
  - `scripts/assemble-rpi4b-bootfs.sh`

## Validation

- run:
  - `./scripts/assemble-rpi4b-bootfs.sh`

## Result

- the helper assembles:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs`
- the assembled tree contains the expected mixed file set:
  - Phoenix staged files:
    - `config.txt`
    - `kernel8.img`
    - `loader.disk`
    - `bcm2711-rpi-4-b.dtb`
  - Raspberry Pi firmware files:
    - `start4.elf`
    - `fixup4.dat`
  - optional debug firmware files when present:
    - `start4db.elf`
    - `fixup4db.dat`
    - `start4cd.elf`
    - `fixup4cd.dat`

## Conclusion

- the project now has a reproducible no-hardware Pi 4 firmware boot-tree
  assembly step
- the next bounded artifact step should turn that directory into a single
  firmware-partition image or similarly portable artifact

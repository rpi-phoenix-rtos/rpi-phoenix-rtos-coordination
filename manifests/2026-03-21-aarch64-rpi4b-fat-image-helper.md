# Manifest: Pi 4 FAT Firmware-Image Helper

- Date: `2026-03-21`
- Step: `STEP-0272`
- Scope: build a portable FAT firmware image from the assembled Pi 4 boot tree

## Change

- add:
  - `scripts/assemble-rpi4b-bootfs-img.sh`

## Validation

- run:
  - `./scripts/assemble-rpi4b-bootfs-img.sh`

## Result

- created image:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img`
- validated image listing contains the expected firmware-visible file set:
  - `bcm2711-rpi-4-b.dtb`
  - `config.txt`
  - `fixup4.dat`
  - `fixup4cd.dat`
  - `fixup4db.dat`
  - `kernel8.img`
  - `loader.disk`
  - `start4.elf`
  - `start4cd.elf`
  - `start4db.elf`

## Conclusion

- the project now has a portable no-hardware Pi 4 FAT boot artifact in addition
  to the loose assembled boot tree
- the next bounded question is whether the first real-device path should use
  this FAT image directly or whether the project should next assemble a larger
  media image around it

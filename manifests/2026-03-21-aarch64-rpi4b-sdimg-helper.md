# Manifest: Pi 4 Full SD-Card Image Helper

- Date: `2026-03-21`
- Step: `STEP-0277`
- Scope: build a full Pi 4 disk image around the existing FAT boot image

## Change

- add:
  - `scripts/assemble-rpi4b-sdimg.sh`

## Validation

- run:
  - `./scripts/assemble-rpi4b-sdimg.sh`

## Result

- created disk image:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img`
- validated MBR layout:
  - DOS partition table
  - partition 1 bootable
  - partition 1 type `c` `W95 FAT32 (LBA)`
  - partition 1 starts at sector `2048`
  - partition 1 size `131072` sectors (`64M`)
- validated embedded FAT offset:
  - `1048576`
- validated embedded FAT file set still contains:
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

- the project now has a proper VM-local Pi 4 SD-card image artifact that fits
  normal flashing workflows
- the next bounded move is to export that full disk image into a stable
  host-visible artifact path and document the first manual flashing workflow

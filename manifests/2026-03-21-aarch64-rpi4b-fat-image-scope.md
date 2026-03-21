# Manifest: Pi 4 FAT Image Scope

- Date: `2026-03-21`
- Step: `STEP-0271`
- Focus: choose the smallest portable next artifact format for the assembled Pi
  4 boot tree

## Tool Availability

- confirmed in `phoenix-dev`:
  - `/usr/bin/mformat`
  - `/usr/bin/mcopy`
  - `/usr/sbin/mkfs.vfat`

## Selected Next Step

- create one FAT firmware-partition image from the assembled
  `rpi4b-bootfs` directory

## Why This Step

- it is more portable than a loose directory tree
- it is still no-hardware and artifact-focused
- it maps naturally onto the Raspberry Pi firmware-visible FAT partition model

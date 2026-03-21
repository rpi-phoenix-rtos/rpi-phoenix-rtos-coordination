# Manifest: Pi 4 FAT-Image Export Helper

- Date: `2026-03-21`
- Step: `STEP-0275`
- Scope: export the current VM-local Pi 4 FAT image into a stable host-visible
  artifact path

## Change

- add:
  - `scripts/export-rpi4b-fat-image.sh`

## Validation

- run:
  - `./scripts/export-rpi4b-fat-image.sh`
- confirm the exported host artifact exists:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-bootfs.img`
- confirm the host-visible file matches the VM-local source:
  - host:
    - `shasum -a 256 /Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-bootfs.img`
  - VM:
    - `sha256sum /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img`

## Result

- the current Pi 4 FAT image now exports into a stable host-visible path:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-bootfs.img`
- the current validated size is:
  - `67108864`
- the current validated SHA-256 is:
  - `fab57080ef7c770ac9346cfd9e86b6ef71c31d47559fe0bd955bee6b71d3a108`

## Conclusion

- the first Pi 4 boot artifact is now directly accessible from macOS without
  manual VM file copying
- the next bounded move should improve SD-card usability, because a raw FAT
  filesystem image still forces manual partition handling during the first real
  hardware trial

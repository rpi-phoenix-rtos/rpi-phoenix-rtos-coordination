# Manifest: FAT Image Handoff Scope

- Date: `2026-03-21`
- Step: `STEP-0274`
- Focus: choose the smallest operator-facing improvement around the current Pi 4
  FAT image

## Selected Improvement

- add one host-side export helper that copies the VM-local Pi 4 FAT image into
  a stable host-visible artifact directory inside this workspace

## Why This Step

- the current `rpi4b-bootfs.img` exists only in the VM-local buildroot
- the operator needs a host-visible artifact before any SD-card writing step is
  practical on macOS
- this stays tightly focused on handoff, not flashing or hardware execution

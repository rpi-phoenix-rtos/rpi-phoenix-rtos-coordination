# 2026-03-21: rerun the full Pi 4 artifact-refresh chain

## Scope

- Step: `STEP-0293`
- Goal: refresh the host-visible Pi 4 SD image so it includes the latest HDMI
  firmware refinement

## Sequence Run

1. `./scripts/assemble-rpi4b-bootfs.sh`
2. `./scripts/assemble-rpi4b-bootfs-img.sh`
3. `./scripts/assemble-rpi4b-sdimg.sh`
4. `./scripts/export-rpi4b-sdimg.sh`

## Validation

Observed rebuilt bootfs image contents included:

```text
hdmi_force_hotplug=1
disable_overscan=1
```

Verified by reading `::config.txt` from the rebuilt:

- `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img`

Refreshed host-visible artifact:

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`

Refreshed host-visible SHA-256:

- `acfdb8c251be03a716cdd9811b151c412de1e3a11c24db76ed5a476d8fc8f107`

Artifact size:

- `69206016` bytes

## Result

- the full helper chain now works again from the refreshed Pi 4 build outputs
- the host-visible SD image is up to date and includes the current HDMI
  firmware refinement
- the next practical move can now be the first manual Pi 4 board trial, or one
  more tiny pre-hardware checklist refinement if needed

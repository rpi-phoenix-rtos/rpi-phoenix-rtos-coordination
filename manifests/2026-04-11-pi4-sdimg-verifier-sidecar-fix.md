# Pi 4 SD-Image Verifier Sidecar Fix

- Date: `2026-04-11`
- Scope: host-side Pi 4 SD-image export and verification helpers

## Summary

The standalone Pi 4 SD-image verifier reported a false SHA-256 mismatch on the
current exported image because it still hardcoded an old historical checksum.

Fix:

- `scripts/export-rpi4b-sdimg.sh` now writes:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img.meta.txt`
- `scripts/verify-rpi4b-sdimg.sh` now reads expected size and SHA-256 from that
  sidecar by default
- `scripts/rebuild-rpi4b-fast.sh` now uses the normal verifier path after
  export instead of injecting temporary checksum overrides

## Validation

- `bash -n scripts/verify-rpi4b-sdimg.sh`
  - result: pass
- `bash -n scripts/export-rpi4b-sdimg.sh`
  - result: pass
- `bash -n scripts/rebuild-rpi4b-fast.sh`
  - result: pass
- refreshed export:
  - result: pass
- standalone verifier on the current image:
  - result: pass
  - current SHA-256:
    `610dbbfd0192760f061395f7e85573261b85b18857bea426e6adab4930468698`

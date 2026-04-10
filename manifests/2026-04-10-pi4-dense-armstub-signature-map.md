# Pi 4 Dense Armstub Signature Map

Date: `2026-04-10`

## Goal

- replace the vague stage-`3 -> 4` answer with an instruction-band answer
- instrument the whole late armstub fixed-target signature seam at once

## Implemented

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
  now emits this dense late seam ladder:
  - `23`: late armstub seam entered
  - `24`: fixed target address loaded
  - `25`: first signature word read
  - `26`: second signature word read
  - `27`: first expected signature constant loaded
  - `28`: first compare passed
  - `29`: second expected signature constant loaded
  - `30`: second compare passed
  - `4`: signature verified before branch
  - `31`: mismatch halt
- the armstub now also installs an EL2 exception vector table and emits:
  - `0`: EL2 exception trap during the seam
- later `plo` stages remain stable from stage `5` onward

## Validation

- Pi 4 A72 rebuild from refreshed copied buildroot: pass
- direct Pi 4 QEMU serial sanity:
  - `call: exec go!`
  - `go: enter`
  - `hal: jump exit el1`
  - `A3`
  - `KLM`
  - later `Exception #37`
- bootfs assembly: pass
- FAT image assembly: pass
- SD-image assembly: pass
- canonical SD-image export: pass
- FAT-aware verifier: pass

## Artifact

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `6b349fe6c2afe11ea0fdeb5d9fc874eb5ae1b990ee83d42c48f10662445875e8`

## Next Video Interpretation

- highest `3`: fail before dense late seam starts
- highest `23`: fail on fixed target address load
- highest `24`: fail on first signature-word read
- highest `25`: fail on second signature-word read
- highest `27` then `31`: first compare mismatch path
- highest `29` then `31`: second compare mismatch path
- highest `30` but no `4`: fail between final compare pass and signature-ok marker
- highest `4` but no `5`: fail after verified branch setup but before successful `plo` veneer entry
- highest `0` after any late stage: EL2 exception in that band

# Manifest: Distinct-Output External-Applet Validation

- Date: `2026-03-21`
- Step: `STEP-0268`
- Scope: validate the first clean external-applet smoke on both fast lanes

## Command Under Test

- `echo -h`

## Result

- generic `virt` passes:
  - `(psh)% echo -h`
  - `Usage: echo [options] [string]`
  - returned `(psh)%`
- Pi 4 `raspi4b` passes with the same markers

## Conclusion

- the fast QEMU lane now has two clean shell-level proof points:
  - built-in `help`
  - external applet `echo -h`
- that is enough shell confidence for the moment; the next bounded step should
  pivot back toward the actual Pi 4 boot path and firmware-style image
  preparation

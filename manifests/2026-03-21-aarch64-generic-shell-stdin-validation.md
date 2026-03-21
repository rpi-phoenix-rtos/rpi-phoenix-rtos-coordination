# Manifest: Generic `virt` Shell Stdin Validation

- Date: `2026-03-21`
- Step: `STEP-0260`
- Scope: validate the smallest generic-QEMU stdin-path adjustment

## Change Under Test

- replace the generic interactive smoke launch mode:
  - from:
    `-serial mon:stdio -serial null -display none`
  - to:
    `-nographic -monitor none`

## Validation

- run the same `expect`-driven `help` smoke as before
- save the new log to:
  - `/tmp/generic-shell-smoke-stdio.log`

## Result

- generic `virt` now matches the Pi 4 shell-smoke result:
  - `(psh)% help`
  - `Available commands:`
  - returned `(psh)%`

## Conclusion

- the earlier generic-only failure was in the QEMU stdio launch path, not in
  Phoenix shell code
- both fast QEMU lanes now support the same first interactive shell smoke
- the next small step should package that validated smoke into a reusable
  helper instead of repeating long ad hoc commands

# Manifest: Console-Readiness Scope After DTB Validation

- Date: `2026-03-20`
- Step: `STEP-0121`
- Status: `completed`

## Goal

- choose the smallest next step after both the generic QEMU lane and the Pi 4 DTB-backed QEMU lane reach `pl011-tty: started`

## Evidence

Generic `virt` lane:

- reaches:
  - kernel banner
  - `pl011-tty: started`
- still does not reach:
  - `pl011-tty: tty0 ready`
  - `pl011-tty: console ready`

Pi 4 DTB-backed `raspi4b` lane:

- reaches:
  - loader startup
  - `pl011-tty: started`
- still does not reach:
  - kernel banner on UART
  - `pl011-tty: tty0 ready`
  - `pl011-tty: console ready`

Prior project findings already preserved:

- the generic runtime boundary had already been narrowed to the first successful `create_dev()` path in `pl011-tty`
- the `wait` command is not a valid unattended timing workaround

## Conclusion

- the next smallest step should target the shared `pl011-tty` console-readiness boundary, not another Pi 4 DTB or loader-transport change
- fixing or explaining the `create_dev()` / console-registration path is more likely to produce a usable UART shell quickly than chasing the missing Pi 4 kernel banner first

## Selected Next Step

- scope and implement the smallest `pl011-tty` console-readiness follow-up:
  - reuse the existing generic diagnostic history
  - keep the change as shared userspace/driver work if possible
  - validate it on both the generic and Pi 4 DTB-backed QEMU lanes

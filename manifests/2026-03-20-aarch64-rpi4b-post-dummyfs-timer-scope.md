# Manifest: Pi 4 Post-`dummyfs` Timer Follow-Up Scope

- Date: `2026-03-20`
- Step: `STEP-0196`
- Status: `completed`

## Goal

- choose the single highest-signal runtime follow-up after the automated Pi 4
  QEMU DTB memory hook

## Evidence Reviewed

Current Pi 4 A72 patched-lane evidence:

- `vm: map init done`
- `gtimer: source virtual irq 27`
- `gic: timer handler set grp 1 en 1`
- `threads: wakeup programmed`
- `dummyfs: devfs initialized`
- no visible:
  - `gic: timer dispatch`
  - `threads: timer irq`
  - `pl011-tty: tty0 wake`

Current common timer-source policy in
`sources/phoenix-rtos-kernel/hal/aarch64/dtb.c:dtb_chooseTimerSource()`:

- prefer the virtual timer first
- fall back to the non-secure physical timer second

Important comparison:

- the generic `virt` lane works with the current virtual-timer-first policy
- the Pi 4 patched lane does not dispatch under that same policy

## Selected Next Experiment

- force the Pi 4 patched lane to use the non-secure physical timer instead of
  the current virtual timer

## Why This Is The Right Next Step

- it changes exactly one runtime variable: timer source selection
- it stays inside code that already supports both candidate sources
- it does not widen into GIC redesign, scheduler changes, or DTB parser work
- if it works, it should move the Pi 4 lane immediately past the current sleep
  or wakeup stall
- if it fails, it eliminates timer-source choice and points the next step back
  to interrupt delivery rather than timer arming policy

## Selected Implementation Shape

- keep the experiment narrow and temporary
- prefer a Pi 4 A72-specific override or similarly bounded diagnostic hook over
  changing the common policy permanently on the first try

## Selected Next Step

- implement the bounded physical-timer-source experiment on the Pi 4 patched
  lane and validate it against the automated `RPI4B_QEMU_MEMORY_SIZE` build
  path

# Manifest: Later-Boot Interactive Console Probe

- Date: `2026-03-21`
- Step: `STEP-0231`
- Status: `completed`

## Goal

- determine whether the spawned `psh` process is already live and responsive on
  the console without further code changes

## Validation

### Generic `virt` interactive PTY probe

- started the generic QEMU lane in an interactive PTY-backed session
- waited for normal boot output to quiet after:
  - `main: spawned psh (4)`
  - `dummyfs: initialized`
- sent:
  - `help\n`
  - `\n`
  - later `Ctrl-C` only to stop the session cleanly

Observed result:

- console input is echoed back on the serial line
- no `psh` prompt, help text, or command response appears
- no additional later-boot markers appear during the same session

### Pi 4 `raspi4b`

- not repeated in this step

Reason:

- the step acceptance criteria only required the Pi 4 repeat if the generic lane
  proved interactive
- the generic lane did not prove interactive, so the smallest next move stays in
  the shared later-boot path

## Result

- the later-boot silence is no longer in the kernel syspage spawn loop
- console RX echo is present, but there is still no visible shell response
- the next bounded blocker is now inside the `psh` startup / tty-control /
  first-read path rather than in kernel-side process spawning

## Next Step

- scope the smallest `psh`-local visibility step so the next patch can split:
  - pre-`psh_run()`
  - pre-`psh_ttyopen()`
  - pre-terminal-control
  - pre-first `psh_readcmd()`

# Manifest: `psh` Startup Visibility Scope

- Date: `2026-03-21`
- Step: `STEP-0232`
- Status: `completed`

## Goal

- choose the smallest `psh`-local visibility patch after the negative
  interactive-console probe

## Evidence Reviewed

Current later-boot evidence:

- the kernel now proves `psh` is spawned successfully on both generic and Pi 4
- the generic interactive PTY probe echoes serial input but produces no prompt
  or command response

Relevant source paths:

- `sources/phoenix-rtos-utils/psh/psh.c`
- `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`

Relevant control flow:

- `main()` waits for `/`, resolves the invoked app, and runs `psh_pshapp()`
- `psh_pshapp()` selects interactive mode and calls `psh_run()`
- `psh_run()`:
  - sleeps for one second
  - retries `psh_ttyopen(console)`
  - waits for foreground ownership
  - puts the shell in its own process group
  - calls `tcgetattr()`
  - calls `tcsetpgrp()`
  - enters the `psh_readcmd()` loop

## Selected Next Implementation Step

- add tightly bounded startup markers in `psh` only:
  - in `psh.c`:
    - after `/` becomes available
    - before running the selected app
    - after the app returns
  - in `pshapp.c`:
    - on `psh_run()` entry
    - before and after the `psh_ttyopen()` retry loop
    - before `tcsetpgrp()`
    - immediately before the first `psh_readcmd()` call

## Why This Is The Right Next Step

- it stays entirely inside the process that already proved it spawned
- it can distinguish:
  - `psh` never reaches interactive mode
  - `psh` stalls in tty open or terminal control
  - `psh` reaches the first read loop and the silence is inside command input
- it avoids reopening the kernel, loader, or PL011 driver paths unless the
  shell trace points back there

## Selected Next Step

- implement bounded `psh` startup visibility in:
  - `sources/phoenix-rtos-utils/psh/psh.c`
  - `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`

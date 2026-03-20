# Manifest: `psh` Root-Lookup Success Trace Scope

- Date: `2026-03-21`
- Step: `STEP-0236`
- Status: `completed`

## Goal

- choose the smallest next hook after first user execution that can prove
  whether `psh` gets past its `lookup("/")` wait loop

## Evidence Reviewed

Current cross-lane result:

- both generic and Pi 4 now print:
  - `threads: psh user scheduled`
- neither lane prints any visible `psh:` userspace marker

Relevant source paths:

- `sources/phoenix-rtos-kernel/syscalls.c`
- `sources/phoenix-rtos-utils/psh/psh.c`

Relevant control flow:

- `psh.c:main()` loops on:
  - `lookup("/", NULL, &oid)`
- once that succeeds, `psh` can progress toward `psh_pshapp()` and later
  `psh_ttyopen("/dev/console")`
- `syscalls_lookup()` already contains tightly filtered diagnostic helpers for
  the earlier `create_dev` work, so it is a natural small hook point

## Selected Next Implementation Step

- add one bounded one-time marker in `syscalls_lookup()` only when:
  - `proc_current()->process->path` is `psh`
  - `name` is `/`
  - `proc_portLookup()` returns success

## Why This Is The Right Next Step

- it is the earliest `psh`-specific syscall result after first user execution
- it stays narrower than a general pathname trace
- it can distinguish:
  - `psh` is still stuck waiting for `/`
  - `psh` gets past `/` and the next blocker is later, likely around
    `/dev/console`

## Selected Next Step

- implement bounded `psh` root-lookup success visibility in:
  - `sources/phoenix-rtos-kernel/syscalls.c`

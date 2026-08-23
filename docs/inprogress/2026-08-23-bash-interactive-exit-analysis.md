# Bash exits in interactive mode — root-cause analysis (owner-reported, 2026-08-23)

**Owner framing (the key steer):** interactive bash exits immediately, YET interactive I/O
clearly works on Phoenix — (a) xterm is interactive, (b) busybox/ash is interactive, (c) Quake
consoles read UART interactively. So the interactive-input primitives are fine; something
**bash-specific** trips it. "We have the solution in reach; it needs more thinking."

## The old theory is wrong (owner's discriminator kills it)
KNOWN-ISSUES.md + tools/pty-run/pty-run.c both claim: *"bash's fd 0 isn't a working interactive
tty → readline hits immediate EOF and exits."* This CANNOT be right: **busybox ash reads
interactively from the exact same psh-handed fd 0.** If fd 0 delivered EOF, ash would exit too. It
doesn't. So fd 0 delivers bytes; bash must put the fd (or itself) into a state where Phoenix's
`read()` returns 0, that ash never enters.

## The smoking gun: job control
Comparing how the two shells are built/configured on Phoenix:

| | busybox ash | GNU bash (our port) |
|---|---|---|
| line editing / raw mode | **ON** (`CONFIG_FEATURE_EDITING=y`) — and it works | ON (readline, always raw) |
| **job control** | **OFF** (`CONFIG_ASH_JOB_CONTROL is not set`) | **ON** (`bash_cv_job_control_missing=present`, `ac_cv_func_tcgetpgrp=yes`) |

(busybox config: `sources/phoenix-rtos-ports/busybox/config`; bash config:
`sources/phoenix-rtos-ports/bash/config.cache`.)

- ash proves **raw / non-canonical reads work** on Phoenix's console (editing ON, works) → the
  "readline raw mode breaks reads" hypothesis is **refuted**.
- The one thing bash does that ash does NOT is **job control**: at interactive startup bash's
  `initialize_job_control()` (jobs.c) calls `setpgid()` to put itself in its own process group,
  then `tcsetpgrp(shell_tty, pgrp)` to become the controlling terminal's **foreground** process
  group, and installs SIGTTIN/SIGTTOU/SIGTSTP handlers. ash (job control off) never touches any of
  this — it just reads fd 0 in the process group psh gave it.

## Mechanism — REFINED by a full Phoenix-source audit (2026-08-23; my first sketch was partly wrong)
A read-only audit of libphoenix + kernel + libtty + posixsrv (see below) **refuted the
"background-read returns EOF" sketch** but confirmed the deeper picture:

- `tcsetpgrp`/`tcgetpgrp` are **real** (libphoenix termios.c → `ioctl(TIOCSPGRP/TIOCGPGRP)` →
  libtty stores `tty->pgrp`), NOT stubs. `setpgid`/`getpgid`/`setsid` are real kernel syscalls.
- **psh already makes bash the tty's foreground group BEFORE exec** — `runfile.c` does
  `setpgid(pid,pid)` then `tcsetpgrp(STDIN,pid)` before `execv`. So when bash starts,
  `tcgetpgrp(shell_tty) == getpgrp()` → bash's foreground self-check **PASSES** → bash **enters
  full job-control mode** (it does NOT fail to foreground, and does NOT become a background reader).
- A blocking console read (pl011-tty → `libtty_read`, canonical) **never returns a spurious 0** — only
  a real Ctrl-D/VEOF. So the exit is **not** a background-read EOF.
- **Phoenix has NO real job control**: `tty->pgrp` is a signal-delivery target only (never gates
  reads); there is **no** foreground-pgrp read enforcement, **no** SIGTTIN/SIGTTOU generation, **no**
  kernel stop-state, and libphoenix defaults SIGTSTP/SIGTTIN/SIGTTOU/SIGSTOP/SIGCONT to
  `_signal_ignore` (signal.c:104-112, TODO); `setsid` is a `FIXME` stub (pgid=pid). Sessions are
  merely simulated via pgrp.
- FIONREAD (readline input-availability, `bash_cv_fionread_in_ioctl=yes`) was a real gap, already
  **FIXED** (commit b247643) — its message states it is "not the sole cause" of the EOF-exit.

**So the real fault is a configuration mismatch:** bash was built **with** job control
(`bash_cv_job_control_missing=present`) and *enters* JC mode, then interacts with Phoenix's
**partial/absent** JC semantics (stop signals ignored, no real sessions/stop-state) in a way that
ends the interactive session. The exact line is inside bash's own `jobs.c`/`shell.c` (fetched at
build, not in this tree) — but it does not need to be pinned to fix the bug, because **the entire
JC path is inappropriate on an OS with no job control.** ash proves this: it has JC compiled out and
never runs any of it → works. bash-with-JC on a no-JC OS is the misconfiguration.

## Fixes
**The audit reframes the choice:** Phoenix has *no* job control (no SIGTTIN, stop signals ignored,
no sessions/stop-state). So building bash WITH job control is a capability mismatch, and busybox
ash already ships the correct answer for this OS (JC compiled out). Fix A is therefore the
*correct* configuration, not merely a workaround.

**Fix A — build bash WITHOUT job control (correct for a no-JC OS; matches ash).**
Set `bash_cv_job_control_missing=missing` in the port config.cache (defines `JOB_CONTROL_MISSING`
→ bash never setpgid/tcsetpgrp's, stays in psh's foreground group, reads return bytes). Cost:
no `Ctrl-Z`/`fg`/`bg`/`&`-with-signals — but INTERACTIVE bash (the owner's ask) works. This is the
same trade busybox ash already makes and ships.

**Fix B — correct, larger: implement terminal foreground-process-group + SIGTTIN in Phoenix.**
Make `tcsetpgrp`/`tcgetpgrp` actually track a per-terminal foreground pgrp (kernel/posixsrv + the
tty drivers), and make a background-group `read()` raise SIGTTIN instead of returning 0. This gives
bash FULL job control. Bigger change (kernel/posixsrv/tty); do after Fix A proves the diagnosis.

## Verification plan (fold into the next gcc-16 boot-verify Pi cycle — one cycle tests both)
Build two bashes and stage both into the gcc-16 NFS export:
- `/bin/bash` (JC on, current) — expect: launch `bash -i`, it exits at/near the first prompt.
- `/bin/bash-nojc` (JC off, `bash_cv_job_control_missing=missing`) — expect: prints `$PS1` and
  **waits** for input (only EOFs when the harness closes stdin — which is the separate
  automation-can't-sustain-input limitation, NOT the startup bug).
Discriminator observable even under psh-interact automation: **does bash reach + print its PS1
prompt and wait, or exit before any input is sent?** JC-off reaching the prompt while JC-on exits
at startup = hypothesis confirmed. Also try `pty-run bash` vs `pty-run bash-nojc`.
(Advisor unlock: this is a pure-software tty/pgrp issue → likely reproducible in QEMU, which is not
Pi-locked — a faster iterate loop than the Pi if QEMU brings up a console.)

## If confirmed
Make Fix A the bash port default (`bash_cv_job_control_missing=missing`) + note the job-control
limitation in KNOWN-ISSUES, and correct the stale KNOWN-ISSUES/pty-run "fd 0 EOF" explanation.
Then scope Fix B (real foreground-pgrp + SIGTTIN) as the follow-up for full job control.

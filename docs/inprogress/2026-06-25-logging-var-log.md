# Task #31 — klog/stderr → /var/log, DEBUG/USER build mode, view command

Status: IMPLEMENTED (code complete + committed; HW validation by the orchestrator pending).
Date: 2026-06-25. Owner: logging subagent (unattended; UART reserved for vkQuake).

## Goal

Linux-like logging for the Pi4 port:
- A persistent (where the rootfs allows) `/var/log/messages` file capturing the kernel
  log (klog).
- A build mode switch:
  - **DEBUG (default)** — console klog ON exactly as today (UART + HDMI fbcon). This is
    the safety contract: byte-for-byte the current behavior.
  - **USER** — verbose console klog SUPPRESSED on both console sinks (UART mirror + HDMI
    fbcon), critical/panic prints kept; the klog is captured to `/var/log/messages` by a
    small daemon.
- A view command to read the captured log file.

## Architecture found (how klog reaches the console today)

Two independent klog console sinks on Pi4:

1. **UART mirror (kernel).** `sources/phoenix-rtos-kernel/log/log.c:log_write()` pushes each
   byte into the klog ring AND mirrors it straight to the UART via `hal_consolePutch()`,
   unconditionally (gated only by `log_common.enabled`, which is the panic flag — always 1
   in normal operation). This is the kernel's own always-complete boot log; it does not
   depend on any userspace reader. The `else` branch (enabled==0, i.e. panic) also mirrors.

2. **HDMI fbcon drain (pl011-tty).** `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
   `pl011_klogthr()` attaches DIRECTLY to the kernel log port (well-known oid `{port:0,id:0}`)
   via `mtOpen(O_RDONLY)` then loops `mtRead`, writing the bytes to the HDMI fbcon ONLY
   (`pl011_fbcon_write`, never the UART — the kernel mirror covers the UART). The split
   keeps UART and fbcon from doubling up.

The klog ring itself is in `log/log.c` (size `KERNEL_LOG_SIZE`, 64 KiB on Pi4 via
board_config.h). A userspace reader pulls bytes with `mtOpen`+`mtRead` on oid `{0,0}`, OR
via POSIX `open(_PATH_KLOG /* "/dev/kmsg" */, O_RDONLY)` — devfs registers `/dev/kmsg`
backed by that port. The existing `dmesg` psh builtin
(`sources/phoenix-rtos-utils/psh/dmesg/dmesg.c`) reads `/dev/kmsg` and streams it to stdout.

Program stdout/stderr default to the console oid `{0,0}` (kernel `posix/posix.c`,
init fds 0/1/2 = ftTty). This carries psh's own prompt/echo/output. We deliberately
DO NOT redirect stderr to file (see "Scope" below) — that would hide the interactive shell.

Both `log.c` and `pl011-tty.c` `#include <board_config.h>` (verified) — so a single macro
defined there reaches both console sinks at compile time.

## Design (single-source switch, DEBUG-safe by construction)

One source of truth: `RPI4_LOG_TO_FILE` in
`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`,
**default 0**. It drives, with no second coordinated setting:

1. **Kernel UART mirror (compile-time).** In `log_write()` the normal-path
   `hal_consolePutch()` is wrapped `#if !RPI4_LOG_TO_FILE`. The panic path (`enabled==0`)
   ALWAYS mirrors — critical/panic output is never suppressed. The ring is unchanged, so
   dmesg and the klogd daemon still read the full log.

2. **HDMI fbcon drain (compile-time).** In `pl011_klogthr()` the `pl011_fbcon_write()` call
   is wrapped `#if !RPI4_LOG_TO_FILE`. (The thread still drains the ring so the reader does
   not back up — it just discards instead of painting fbcon.)

3. **klogd launch gate (plo, derived from the macro).** The rebuild wrapper greps
   board_config.h for `RPI4_LOG_TO_FILE 1` and, if found, exports `RPI4_LOG_TO_FILE=1` into
   the build env — exactly the existing `--variant → RPI4B_VARIANT` pattern. user.plo.yaml
   gates the klogd launch on `{{ env.RPI4_LOG_TO_FILE ... }}`. Because the env is COMPUTED
   from the macro every build, the macro and the launch can't desync.

The klogd binary is ALWAYS bundled (it is a few KB; an unlaunched binary in loader.disk
does not change DEBUG behavior, and gating DEFAULT_COMPONENTS would reintroduce the
"Makefile can't see a C macro" problem). Only its LAUNCH is gated.

Why DEBUG is provably unchanged: macro=0 → both console seams compile to today's code
(byte-identical) AND the wrapper grep fails → env unset → klogd not launched (no extra
process / no extra thread-create line). The only inconsistent cell (macro=1, launch off)
cannot occur (launch derived from macro), and even if it did it would only break a USER
build — which the orchestrator HW-validates — never DEBUG.

## The klogd daemon

New userspace component `sources/phoenix-rtos-devices/misc/rpi4-klogd/`, following the
rpi4-* daemon pattern. It:
- `mkdir("/var")` then `mkdir("/var/log")` itself (EEXIST tolerated) — no second `-x mkdir`
  in the plo render (that aliases-collide and brick the boot).
- attaches DIRECTLY to the kernel log port (oid `{0,0}`) via `mtOpen(O_RDONLY)` + `mtRead`,
  exactly as pl011-tty's klog→fbcon drain does. It does NOT `open("/dev/kmsg")`: on Pi4
  nothing registers that devfs node — pl011-tty replaced libklog's devfs drain with this
  same direct attach and never calls the libklog registrar (`libklog_init`), so the
  `/dev/kmsg` path `dmesg` uses does not resolve on this build. (Verified: no `libklog_init`
  call in pl011-tty.c; the `kmsg`-registering `pumpthr` in libklog is only reached via
  `libklog_init`.) On `mtOpen` the kernel sets the reader index to the ring head, so the
  whole pre-attach boot backlog is replayed into the file too.
- `open("/var/log/messages", O_WRONLY|O_CREAT|O_APPEND, 0644)`.
- pump loop: blocking `mtRead(klog)` → `write(file)`.
- emits ONE acceptance line to stdout (`rpi4-klogd: capturing klog -> /var/log/messages`)
  so USER mode still has a positive console signal.

It is a klog→file pump ("klogd"), NOT an AF_UNIX syslogd (no /dev/log socket server).

Note: the existing `dmesg` psh builtin reads `/dev/kmsg`, which (per the above) does not
resolve on Pi4 — so `dmesg` is effectively non-functional on this port already. `logread`
deliberately reads the FILE, not the ring, so it is unaffected. Fixing `dmesg` to direct-
attach is a separate, out-of-scope cleanup.

## The view command — `logread`

New psh builtin `sources/phoenix-rtos-utils/psh/logread/logread.c`, modeled on dmesg.c:
- `logread` — cat `/var/log/messages` to stdout.
- `logread -f` — follow (tail -f style: keep reading appended bytes).
- `logread -h` — help.
`dmesg` already covers the live kernel ring; `logread` is specifically the file view.

## Variants — /var/log writability

| Variant  | Root fs        | Writable | Persistent across reboot |
|----------|----------------|----------|--------------------------|
| netboot  | dummyfs (RAM)  | yes      | NO (RAM-only)            |
| sd       | ext2 on SD     | yes      | yes                      |
| nfsroot  | NFS export     | yes      | yes (on the NFS server)  |

All three support a writable `/var/log/messages`. On netboot the file is RAM-backed and
lost on power-cycle (still useful within a boot, and `logread` works). The klogd launch is
placed after posixsrv (netboot/sd) / in the post-takeover block (nfsroot), so the root fs
and /dev are up first.

## Enabling USER mode (operator)

1. Edit board_config.h: `#define RPI4_LOG_TO_FILE 1` (default is `0`).
2. Rebuild core: `./scripts/rebuild-rpi4b-fast.sh --scope core` (kernel + devices + plo
   render all pick up the change; the wrapper greps the macro and exports the env).
3. Boot. Console klog is suppressed; `logread` at psh shows the captured boot log.

To return to DEBUG: set the macro back to `0` and rebuild core.

## HW validation steps (orchestrator)

DEBUG (macro=0, default):
- Boot; UART shows the FULL klog exactly as today (kernel boot log, driver banners,
  `(psh)%`). HDMI fbcon shows the full klog as today. No `rpi4-klogd` acceptance line.
  This must be byte-for-byte the current behavior.

USER (macro=1):
- Build with `RPI4_LOG_TO_FILE 1`. Boot.
- UART: NO verbose klog flood; the `rpi4-klogd: capturing klog -> /var/log/messages`
  acceptance line appears; psh prompt is reachable and usable (psh's own output still on
  console — only klog is suppressed). Panic/critical kernel output still reaches UART.
- At psh: `logread` prints the captured boot messages (the same klog that DEBUG would have
  shown on the console). `logread -f` follows new lines.

## Scope decisions

- **stderr/stdout capture: OUT OF SCOPE (intentional).** Program stdout/stderr → console is
  where psh's prompt/echo live; redirecting it to file would hide the interactive shell. The
  klog is the flood; suppressing klog + keeping psh on console is the correct USER behavior.
  Driver one-line banner printfs still reach the console (one-liners, not the flood). A
  future syslogd with a /dev/log socket could capture app logs — explicitly deferred.
- **Default safety:** DEBUG default + compile-time gate means a logging bug cannot regress
  console visibility.

## Files

Kernel (phoenix-rtos-kernel):
- `log/log.c` — `#if !RPI4_LOG_TO_FILE` around the normal-path UART mirror.

Devices (phoenix-rtos-devices):
- `tty/pl011-tty/pl011-tty.c` — `#if !RPI4_LOG_TO_FILE` around the fbcon klog write.
- `misc/rpi4-klogd/{rpi4-klogd.c,Makefile}` — NEW daemon.
- `_targets/Makefile.aarch64a72-generic` — `DEFAULT_COMPONENTS += rpi4-klogd`.

Utils (phoenix-rtos-utils):
- `psh/logread/{logread.c,Makefile}` — NEW psh builtin.
- psh build target registration for `logread`.

Project (phoenix-rtos-project):
- `_projects/aarch64a72-generic-rpi4b/board_config.h` — `#define RPI4_LOG_TO_FILE 0`.
- `_projects/aarch64a72-generic-rpi4b/user.plo.yaml` — gated klogd launch.

Coordination (this repo):
- `scripts/rebuild-rpi4b-fast.sh` — grep the macro, export `RPI4_LOG_TO_FILE`.
- this doc.

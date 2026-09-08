# V3D per-session BO leak: root cause found, fix plan, and why no code landed yet

Supersedes the "cause" section of `2026-09-08-x-soak-and-v3d-bo-residue.md`, which said only
that the daemon "has no disconnect handler". It is more specific than that, and the reason is
actionable.

## Root cause

The daemon's `mtOpen`/`mtClose` arm (`rpi4-v3d.c:194-197`) is **dead code for the Mesa client
path**, because the client never opens a file descriptor on the device:

* `libv3d-client.c:118` resolves the node with `lookup("/dev/v3d-srv", ...)` (or a raw `mtLookup`
  at `:81-107`) and then issues bare `msgSend` on the port (`:150-153`, `:211-216`). No `open()`.
* Mesa's DRM entry point discards the fd outright — `phoenix_v3d_ioctl(int fd, ...)` begins with
  `(void)fd;` (`libv3d-client.c:299-305`), and `drmIoctl()` is an inline in the shim
  (`mesa/v3d_libdrm_shim.c:4`) forwarding straight to it, bypassing libphoenix `ioctl()` and the
  kernel fd table entirely.

So no `open_file_t` exists for `/dev/v3d-srv`. That matters because **the kernel's only
"client is gone" signal is a synthesized `mtClose`**, and it is delivered exclusively to servers
the dead process held an fd on:

`process_destroy` (`kernel proc/process.c:82-127`) → `posix_died` → `posix_exit`
(`posix/posix.c:3514`, `:3527`) → `posix_sweepFds` (`:656`, `:607-624`) → `posix_fileDeref` →
`proc_close(f->oid, ...)` (`:168`) → a real `mtClose` to `oid.port` (`proc/name.c:481`, `:485`).

With no fd, `posix_sweepFds` has nothing to sweep, so nothing is ever sent. Meanwhile
`v3d_srv_handleMsg` (`rpi4-v3d.c:138`) switches on request type only, `v3d_srv_gemClose`
(`:93-95`) passes just `req->handle`, and `struct pbo` (`v3d_gpu.c:168-177`) has no owner field —
one flat global table. Hence: every BO a SIGTERM'd client did not explicitly close leaks for the
daemon's lifetime.

Also confirmed **absent**: any kernel notification to a port owner on client death.
`proc_portsDestroy` (`proc/ports.c:177-192`) destroys only the *dying* process's own ports;
`port_t` (`proc/ports.h:99-114`) has no client list and no death hook, and the `mt*` enum
(`include/msg.h:26-39`) has no disconnect type. A client killed mid-request does not even show up
as a truncated transaction — `proc/msg.c:390-399` makes the sender's wait uninterruptible.

## Two implementable fixes (no kernel change needed either way)

**Option A — the established in-tree pattern (exact, deterministic).** Used by ptmx
(`posixsrv/pty.c:664-673`) and ade9113 (`devices/adc/ade9113/msgapi.c:96`): have `mtOpen` return
a fresh session id as a *positive* `msg.o.err`; the kernel stores it as `f->oid.id`
(`kernel posix/posix.c:874-881`), and every later message on that fd — including the
death-synthesized `mtClose` — carries it. Then tag each BO with `msg.oid.id` and reap by session
on close.
Requires the client to actually hold an fd: add an `open("/dev/v3d-srv", O_RDWR)` in
`v3d_cli_resolve()` and stash the returned session id into `v3d_cli.oid.id` for subsequent sends.
Reaping becomes automatic on SIGTERM/crash/`_exit`, with correct DRM-file semantics across
`fork()`/`dup()` (session is per-`open_file_t`, not per-pid).

⚠️ Do **not** key cleanup on `msg.pid` for the death `mtClose`: that message is stamped by
whatever context runs `process_destroy`, not the dying process, and is observed as pid 1. There
is an in-tree FIXME saying exactly this at `devices/net/usbwlan/usbwlan.c:571`.

**Option B — server-only, no client change (stopgap).** `msg.pid` *is* reliable for
client-*sent* messages (the kernel stamps it in the sender's own context, `proc/msg.c:375`), so
store it on each BO at create time and sweep periodically, probing liveness with `kill(pid, 0)`
(valid on Phoenix: `libphoenix signal/signal.c:194-200`, and `posix_killOne` returns `-ESRCH`
for an unknown pid, `posix/posix.c:3246-3249`).
Failure modes are all in the safe direction — a zombie and a reused pid both read as *alive*, so
the sweep under-reclaims rather than freeing a live client's memory.

## Why no code landed this turn

Option B looked safe enough to implement until one hazard surfaced: **reaping a dead client's BOs
can free memory a GPU job is still reading.** The daemon has no way to know whether a submitted
job referencing those BOs has retired, and this is the same class as the render wedges that took
several sessions to chase. Freeing a BO out from under CT0/CT1 would be far worse than the leak.

Weighed against a 15.7 MB-per-session cost on a 4 GB board — no demo impact — that is not a
trade worth making right now, especially with a core stdio change already landed this session
and needing to stay verified. Option A does not have the hazard in the same way (the `mtClose`
arrives through the kernel after the process is fully gone) but it modifies
`libv3d-client.c`, which every game and the X desktop link.

**Recommended order when this is picked up:** implement Option A, and gate the reap on job
completion (or drain the GPU) before freeing. Verify with the `/bin/mem` 3-lifecycle measurement
below.

## Measurement notes

Established: after boot 91040 KB / 193 map entries; after one X lifecycle 151320-151404 KB
(reproduced twice); after a second lifecycle 167044 KB / 448 entries — i.e. **+15.7 MB and +85
entries for the second session**, so the cost is not purely one-time setup.

⚠️ **Not established: whether it keeps growing linearly.** A 3-lifecycle run was attempted
2026-09-08 and only the first session completed (7 commands did not fit the capture window), so
there is no third data point. Re-run with fewer commands per boot, or a longer window, before
claiming the leak is unbounded.

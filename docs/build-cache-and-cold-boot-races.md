# Build-cache pollution and cold-boot races on Pi 4

Status: 2026-05-19, captured after a multi-hour session that ate a lot of
debugging time on what turned out to be one infra-level gotcha and one
genuine race.

## Lesson 1: `./scripts/rebuild-rpi4b-fast.sh` is incremental — use `--scope full-clean` after rapid back-and-forth edits

`rebuild-rpi4b-fast.sh` defaults to `project image` scope, which is an
incremental build of the userspace + image assembly. Object files from the
kernel and from sibling libraries (libphoenix, libusb, libtty) are reused
unless the rebuild scope explicitly requests `core` or `clean`.

When you do back-and-forth edits — e.g. `git revert` a change, `git revert`
the revert, etc. — the incremental builder's dependency tracking is fooled
by the unchanged source mtimes and produces a binary that *links objects
from intermediate states*. The result behaves nothing like what the source
tree says it should: in one observed case, the boot hung in a 32-second
devfs lookup that the same source tree boots through cleanly when rebuilt
from scratch.

Concretely: in this session we did

  1. apply pl011_thr one-batch fix; rebuild → boot hangs at td14 ~32 s
  2. revert one-batch fix; rebuild → boot still hangs the same way (!)
  3. revert sleep(1) restoration too; rebuild → still hangs (!!)
  4. `rebuild-rpi4b-fast.sh --scope full-clean` → boot reaches `fbcon: ok`

Steps 2 and 3 were rolling back to literally the morning's working
commits, but the incremental builder was reusing stale objects compiled
when steps 1 and 2 (between them) had introduced uncommitted intermediate
states. `--scope full-clean` rebuilds host + core + project + image from
nothing and produces a deterministic binary.

**Operational rule:** any time you revert/reapply code in libraries that
the kernel or pl011-tty link against, or any time the boot hits a hang you
*believe* shouldn't be there from the source, your first move is a
`--scope full-clean` rebuild. Don't burn an hour on Heisenbug analysis
that's actually `make` caching.

## Lesson 2: the cold-boot race between dummyfs/devfs registration and pl011-tty's first `lookup("devfs")` is intermittent

Even on a known-good image, the boot occasionally hangs at the same
`td14: send devfs port=1 total=2.16e7 us state=2 oerr=-2` trace —
about 21.6 s of wall time for one `lookup("devfs")` IPC round-trip, where
the response eventually comes back ENOENT (the name wasn't registered yet
by the time the lookup got serviced) and the caller burns its retry
budget.

What's happening:

- `main_initthr` spawns the syspage programs sequentially but does **not**
  wait for each to advertise readiness before spawning the next. So
  `dummyfs-root`, `dummyfs -N devfs -D` (the devfs server), `pl011-tty`,
  `usb`, and `psh` all start within a few timer ticks of each other.
- `pl011-tty`'s `pl011_createTty0` calls `lookup("devfs")` in a 30-retry
  loop with 100 ms sleeps, so it has a 3 s budget to wait for the devfs
  server to register its name. Most boots, devfs is ready well inside
  that budget.
- A small fraction of boots — somewhere around 20-30 % in this session —
  hit a kernel-side stall where the IPC `proc_send` waits 21.6 s before
  the response comes back. The trace records that the response was
  ENOENT and `state == msg_responded`, so the message did reach the
  server eventually and the server did respond. But it took 21.6 s.

The 21.6 s magic number is suspicious (it's exactly half the documented
upper bound of 43 s for `proc_send` round-trip times on this hardware in
TD-14 telemetry) but I haven't tracked down the underlying mechanism.
Theories:

- Single-CPU scheduler starvation: pl011-tty's threads (poolthr, pl011_thr,
  pumpthr, main) collectively run at priority 4. If `pl011_thr` is busy in
  the TX/RX loop, the devfs server thread (also priority 4) doesn't run.
  But it should round-robin under the 1 ms tick. So this would only
  account for ms-scale delays, not 21 s.
- Kernel spinlock contention around the port queue when many lookups
  arrive concurrently. The TD-14 telemetry suggests this is happening
  somewhere, but I haven't isolated it.
- Wakeup-timer programming on aarch64 generic timer wraps or is
  misprogrammed on a particular code path. The `_threads_programWakeup`
  in `kernel/proc/threads.c` clamps wakeups to `SYSTICK_INTERVAL` (1 ms)
  but the actual wakeup might be much longer on a cold core that's just
  joined the coherency domain.

When the user is interactively driving the Pi over CoolTerm with serial,
the race almost always resolves favourably and the prompt arrives within
a couple of seconds of `fbcon: ok`. When running blind in an automated
test cycle (which races dnsmasq + DHCP + TFTP + boot all back-to-back),
the race fails ~1 in 3-5 attempts.

**Operational rule:** until TD-14 is rooted out, treat
`test-cycle-netboot.sh` results as advisory — if the run looks bad, retry
before chasing a code-level theory. For real "does the build work?"
verification, drive the Pi interactively from CoolTerm (or `picocom`
directly) and prompt yourself.

## Lesson 3: removing the upstream `psh sleep(1)` is a load-bearing change on Pi 4

The upstream commit `9d0ffff3` adds a flat `sleep(1)` at the top of
`psh_run` to give klog/dmesg time to drain before psh takes the terminal.
On Pi 4 the same `sleep(1)` *also* serendipitously gives `pl011-tty`,
`devfs`, and `usb` time to finish their startup before psh starts hitting
poolthr with ioctls.

When we removed `sleep(1)` and tested, two symptoms appeared depending on
how the race resolved:

1. The user observed "prompt appears immediately, then input lags by
   seconds." That is `pl011_thr` saturated by klog TX drain blocking the
   RX-FIFO drain — fixed by the one-batch pl011_thr loop.
2. The automated test cycle observed "devfs lookup hangs 21.6 s, prompt
   never appears in the capture window." That is the race in lesson 2
   happening more often, because psh's eagerness to call ioctls now
   competes with pl011-tty's createTty0 retries for the same poolthr
   resource.

The one-batch pl011_thr fix addresses (1) but does NOT fully address (2)
— removing `sleep(1)` re-exposes (2) every time the race resolves badly.
The conservative choice is to **keep `sleep(1)` restored** so the prompt
arrives ~1 s after `fbcon: ok` reliably, even on the unlucky cold-boot
schedules.

When TD-14 / TD-04 (slow / racy IPC) is properly rooted out and
deterministic startup ordering is restored, `sleep(1)` becomes redundant
and we can drop it cleanly. Until then, leave it in.

## Action items

- [ ] Squash the revert-of-revert chain in `phoenix-rtos-utils` and
      `phoenix-rtos-devices` into clean commits before any upstream PR.
- [ ] Add a `--scope full-clean` reminder banner to `rebuild-rpi4b-fast.sh`
      when the last build's git HEADs differ from the current ones, so
      operators don't get bit by the same cache-pollution gotcha.
- [ ] Investigate the 21.6 s number in the td14 trace — is it a fixed
      timer programming, a wakeup deadline that's wrong on second-core
      threads, or a real syscall latency? See `kernel/proc/threads.c`
      `_threads_programWakeup` and `_proc_threadSleepAbs`.

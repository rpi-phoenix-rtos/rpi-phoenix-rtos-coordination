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

## Lesson 2: the cold-boot 21.6 s "race" was actually a real bug — the devfs fast-path predicate was a no-op

**Update 2026-05-19, evening: ROOT-CAUSED and fixed in kernel commit
`c8a81d5e` "rpi4b/kernel/proc/name: restore devfs fast-path predicate
broken by Pass 4 cleanup".**

The intermittent `td14: send devfs port=1 total=2.16e7 us state=2 oerr=-2`
trace had two contributing factors:

1. The Pass 4 debug-noise strip (commit `334638ee`) accidentally made
   `name_traceDevfsLookup()` in `kernel/proc/name.c` always return 0.
   That function's name was misleading — it served *two* roles:
   (a) gating a small set of `hal_consolePrint` traces (the cosmetic
       use the cleanup correctly targeted), and
   (b) gating the **fast path** in `proc_portLookup` that short-
       circuits any `lookup("devfs")` directly to the cached
       `devfs_oid` once devfs has registered, skipping both the dcache
       walk and the `proc_send` round-trip to the root server.
   Making (a) a no-op silently killed (b), so every `lookup("devfs")`
   on cold boot fell through to the slow path: dcache miss → mtLookup
   to dummyfs-root → ENOENT (because dummyfs-root doesn't own /devfs
   as a regular directory entry; devfs is only reachable via the
   kernel name namespace).

2. With (b) disabled, every cold-boot lookup of "devfs" raced against
   `dummyfs/devfs`'s own `portRegister`. Depending on scheduling order,
   the IPC roundtrip either returned in 8-11 ms (happy case) or sat
   on the dummyfs-root msg queue for 21.6 s before returning ENOENT
   (the unhappy case — likely a wakeup deadline that doesn't fire
   until a long-running operation in dummyfs-root completes).

After commit `c8a81d5e` restored the predicate to its original form
(`return name_traceIs(name, "devfs")`), the test-cycle log shows
exactly ONE `td14: send` trace per cold boot — the unavoidable first
lookup that fires before devfs has finished registering — followed by
silent fast-path hits for every subsequent lookup, and a clean
`fbcon: ok` shortly after. The cosmetic trace-print no-ops in
`name_traceRegister` / `name_traceDevfs` are kept (those were the
genuinely-debug-only RPi4-bringup additions).

**Operational rule** (still useful as a general guideline even though the
specific symptom is now fixed): when adding cleanup passes to the
kernel's `proc/name.c`, watch out for helper functions whose names imply
"trace only" but whose *predicate* role is load-bearing. Read every call
site before turning a function into a no-op. The same caution applies
anywhere else in the codebase where a single function's name suggests
"diagnostic" but its return value is used in a control-flow branch.

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

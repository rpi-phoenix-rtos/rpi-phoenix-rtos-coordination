# Pi 4 stress-test / micro-benchmark suite — results (2026-06-26)

A synthetic stress + micro-benchmark suite was designed, built (5 utils + host load generators), and
run on real hardware to hunt for crashes, hangs, leaks, races, deadlocks, corruption, and slowdowns
across the OS layers. Shared three-bucket contract (`tools/stress/stress.h`): **OK** (succeeded) /
**LIMIT** (OS correctly refused at a resource boundary — not a fault) / **FAULT** (real defect).
Utils are static aarch64 ELFs staged on NFS, run from psh; heartbeats (`STRESS-HB`) localize hangs.

## Headline: the system is ROBUST under stress
Every layer tested came back **clean (fault=0)** at modest-to-high intensity. The only real fault
surfaced is a **pre-known** issue (#121, USB heap). Two test-harness false-positives were caught and
fixed (per the discipline of ruling out test bugs before blaming the kernel). Details:

### ✅ Memory (`stress-core mem`) — clean, NO LEAK
- malloc/free churn (16B–1MB, pattern-verified): no corruption.
- Near-full-RAM watermark (incremental, write-touching each page, `meminfo` per step): allocates
  cleanly, recovers on free (4 MB cap test: free 3.92 GB → touch 128 MB → free recovers to 3.91 GB).
- **Leak trend over 600 iterations: free RAM FLAT at 3,914,628 KB the entire run** — a plateau, not
  monotonic decline → no leak (arena retention only). fault=0.

### ✅ Threads (`stress-core thread`) — race-free + deadlock-free
- N threads × mutex-counter: final == N×M EXACTLY (20×160000 = 3,200,000) → no lost updates / no race.
- condvar producer/consumer: 40,000 handoffs intact → no deadlock.
- (cpu0-only scheduler → this exercises preemption races, not true SMP parallelism.)

### ✅ Scheduler (`stress-core sched`) — fair
- 12 threads, fixed wall-time: progress spread **2%** (min 134.5M / max 137.8M counts), no starvation.

### ✅ Syscall (`stress-core syscall`) — stable throughput, monotonic clock. fault=0.

### ✅ Process storm (`stress-proc`) — clean
- 32 concurrent fork+exec children, all reaped with correct exit codes; 200 spawn/exit churn cycles,
  no pid/handle-leak onset, no slowdown. fault=0.

### ✅ File I/O on NFS (`stress-fs`) — integrity verified
- Sequential 4 MB write+readback **verified** (write 6.27 / read 6.21 MB/s); random + concurrent +
  many-small-files all pattern-clean. fault=0. (The known NFS large-write stall did NOT trigger at
  4 MB; every write is heartbeated so a stall would localize.)

### ✅ IPC / message-passing (`stress-ipc`) — the microkernel core layer, SOLID
- echo: 8 clients × 500 rounds = **4000 round-trips, all nonces intact** (no crossed/lost replies).
- **vcmbox: 8 threads × 64 = 1024 concurrent property transactions, FIFO serialized correctly, no
  garbled/crossed replies** — directly validates the VideoCore-mailbox serialization fix under load.
- lookup: 6400 concurrent port/oid resolutions, resolver stable. fault=0.

### ✅ Network flood (`tools/stress/net/flood.py`, host→Pi) — graceful
- 15s UDP flood + TCP connect-storm + ping-under-load: Pi stayed **fully responsive** (ping 15/15,
  RTT 0.93 ms ≈ 0.88 ms baseline — no degradation) and **survived** (healthy after). TCP-refused
  (120k) = correct LIMIT (no TCP listener on the UDP diag port). lwip handles flood without wedging.

## Faults / issues found
1. **#121 USB DMA-pool free-list corruption (REAL, pre-known, non-fatal, ATTENDED).** A deterministic
   boot-time `Data Abort (EL0)` in the `usb` daemon (`usb_allocFrom`, usb/mem.c) — the `usb_mem`
   free-list head is corrupted by an upstream overflow; the `usb_chunkSane` guard catches it (bounded
   leak) so boot continues. Re-confirmed by this session as a deterministic every-boot repro (new
   evidence for #33). Root cause unconfirmed; USB-daemon internals are attended-scope. NOT a stress-
   suite-induced fault — it fires during USB bring-up at boot.
   - **Debugging lesson:** initially misattributed to rpi4-audio because addr2line was run on the wrong
     static binary (ASLR-free statics share virtual addresses). Always read the exception's
     `process "<name>" (PID)` line first, then addr2line the matching `prog/<name>`.
2. **#156 NFS first-access ENOENT (REAL, recurring usability fault).** First access to a `/nfstest/...`
   path after boot intermittently returns "not found" until the NFS mount settles; it bit the stress
   runs (stress-fs/ipc "not found" on first try, succeeded after an exact-file warmup). Workaround:
   `ls` the exact path first / retry. Tracked; the proper fix is the boot-order/settle race.
3. **lighttpd HTTP-load test — blocked on config paths (follow-up, not a Pi fault).** lighttpd exits at
   startup on netboot because its stress conf references `/var/tmp` + `/var/run` (absent on the
   dummyfs RAM root; the `/nfstest`-prefix problem) and a docroot path. Works on the `nfsroot` variant
   where absolute paths resolve on the real NFS root. The flood test (above) covers the network stack;
   the concurrent-HTTP-server test needs the conf paths fixed or an nfsroot run.

## Test-harness false-positives caught + fixed (NOT kernel faults)
- **thread-mode join timeout:** at `thread 64` (64 threads × 1.28M = 82M serialized mutex ops on one
  CPU), the fixed 30s `threadJoin` timeout fired before the legitimately-slow work finished and
  reported a "deadlock". Fixed (stress-core.c 15f1d99): the join now polls the shared counter and only
  FAULTs if it is FROZEN for 30s — slow-but-progressing is not a fault; work is capped at 8M ops.
- **flood vs powered-off Pi:** an initial flood "FAULT (100% loss)" was the Pi already powered off (a
  background cycle had ended) — re-run with reachability confirmed before flooding → clean pass.

## Suite contents (tools/stress/)
- `stress.h` (contract), `stress-core.c` (mem/thread/sched/syscall), `stress-proc.c`, `stress-fs.c`,
  `stress-ipc.c`, `stress-net.c` (echo), `net/{http-load,flood,echo-load}.py` + `stage-lighttpd.sh`,
  per-util `build-*.sh`. All staged to `/srv/phoenix-rpi4-nfs/bin/`.
- Re-run: boot-to-psh netboot image, `ls /nfstest/bin/<util>` (#156 warmup), then
  `/nfstest/bin/<util> <mode> [intensity]`; network from the host per `net/` scripts.

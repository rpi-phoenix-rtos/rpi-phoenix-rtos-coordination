# Pi4 NFS / networking performance + stability — results (2026-06-15)

Follow-up to `2026-06-14-network-fs-performance-plan.md`. The Pi4 Phoenix NFS
stack was "≥20× too slow and intermittently broken"; it is now **fast and
problem-less**: a consistent **~8.5 MB/s** NFS read (≈68 % of the 100 Mbps
link, up from ~1 MB/s worst-case) with reliable mounts across rapid reboots.

## Diagnosis method (reusable)

Host-side `ss -tin 'dst <pi-ip>'` sampled every 0.2 s during a boot = the NFS
**server's own TCP view** of the Pi connection. The decisive fields:
`cwnd`, `bytes_retrans`, `rwnd_limited`, `delivery_rate`, `app_limited`. This
cut through what packet captures couldn't (a header-only `tcpdump` can't show
READ boundaries; `tshark` is AppArmor-blocked from `/tmp` pcaps → use
`sudo tcpdump -r`). Server stale-state gotcha: rapid reboots expire the NFS4
lease → `sudo systemctl restart nfs-server` to clear state for a clean run.

## Root causes + fixes (all committed + HW-validated)

1. **genet RX buffer aliasing → packet loss → TCP cwnd collapse** (lwip `6b01087`).
   The 256-descriptor RX ring was backed by only 16 unique DMA buffers, aliased
   cyclically; once the HW producer got >16 ahead of the (caches-off) drain it
   overwrote in-flight frames → loss + reordering → cwnd:1. `ss` pre-fix: cwnd:1
   (486 samples), 2.5 % retrans, delivery 53 Mbps; read 0.97–8.22 MB/s (usually
   ~1). Fix: 256 unique buffers in ONE contiguous `dmammap` (the 16-count was a
   workaround for 256 *separate* allocations stalling the allocator). Post-fix:
   0 cwnd:1, retrans ~100× lower, delivery 88 Mbps, **consistent 8.0–8.1 MB/s**.

2. **Per-frame copy + malloc in the RX drain → zero-copy RX** (lwip `ed44f03`).
   The drain copied every frame into a fresh `PBUF_RAM` (an uncached read+write
   pass + malloc with caches off) and was ~11 % receive-window-limited. Fix: wrap
   each DMA buffer in a per-buffer custom pbuf (`pbuf_alloced_custom`/`PBUF_REF`)
   and hand it to `tcpip_input`; re-arm the BD from a mutex-protected free list
   (pool = 256 BDs + 256 in-flight slack); `genet_rxbufFree` returns the buffer
   on the tcpip thread; copy-fallback if the free list is dry (degrades to slow,
   never corrupt). Post-fix: 100 % zero-copy, 0 drops/fallback, **8.53 MB/s**.
   The bulk socket is now `app_limited` (negligible retrans) — the wall is the
   client read latency, not the driver. Modest at 100 Mbps (caches-off: the
   consumer still reads the payload uncached either way) but removes the
   per-frame copy/malloc that would dominate at gigabit.

3. **#156 rapid-reboot `NFS4ERR_EXPIRED` → stable NFS4 client id**
   (filesystems `4b5acb4`, utils `dc91302`). libnfs's default client id
   `"Libnfs pid:<pid> <time>"` changes every boot → the server retains each
   prior reboot's lease (~90 s) → rapid reboots accumulate stale state → a fresh
   mount intermittently gets `NFS4ERR_EXPIRED` → the boot-critical `/nfstest`
   mount fails ("pak0 not found", ~1/3 of rapid boots; proven artifact — the
   boot right after `systemctl restart nfs-server` always succeeded). Fix:
   `nfs4_set_client_name()` with a **stable, role-distinct** id
   (`phoenix-rpi4-nfsfs`, `phoenix-rpi4-nfssmoke`) → a reboot REPLACES the prior
   state (RFC 7530 same-id + new-verifier) and the two same-host clients don't
   collide. Also nfs-smoke unlinks its write-test marker before creat (was a
   benign `NFS4ERR_EXIST` every boot). **NOT idle-lease-expiry** — the boot reads
   continuously — so NFS4 RENEW was deliberately NOT added.

(Earlier same-week, retained: `nfs_set_poll_timeout(1)` — Phoenix socket poll()
doesn't wake on readiness so libnfs's 100 ms default stalled every RPC; and
`readmax`/`writemax` 1 MB.)

## Validation

Multi-boot: 4 consecutive post-fix boots (incl. 3 rapid back-to-back) all
mounted + found pak0, FSHEALTH 8.3–9.0 MB/s HEALTHY, WRITE ok, **0 NFS4 errors,
0 faults**. RXSTATS confirms 100 % zero-copy, 0 drops, free list never below
~251/256.

## Remaining levers (deferred — diminishing at 100 Mbps, gated on gigabit)

The bulk socket is now `app_limited`: the bottleneck is the **synchronous
one-READ-RPC-at-a-time** path (`nfs_ops_read` → sync `nfs_pread`, ~1 ms RTT +
kernel↔nfs-fs IPC + caches-off processing per op). At 100 Mbps we are at ~68 %
of a **cable-capped** link (the crossover cable carries only 2 pairs →
100 Mbps, see `project_pi4_netboot_100mbps_cable`), so further squeezing is
small in absolute terms; the real payoff is at gigabit.

- **Read pipelining / read-ahead** — overlap multiple READ RPCs so round-trips
  hide. Options, by tractability: (a) nfs-fs per-fh async prefetch of the next
  sequential block into a small cache (needs libnfs async + mtfs servicing);
  (b) kernel VFS read-ahead issuing concurrent reads → mtfs worker threads
  already parallelise them; (c) libnfs `_nfs_pread_async` issuing the rsize
  chunks concurrently instead of the serial `r_cb` chain (only helps reads >
  readmax). Substantial cross-layer work; **do it with the gigabit cable in
  place** so it's measured against the true ceiling (at gigabit the same 68 %
  efficiency = ~85 MB/s, and pipelining is the difference to ~125 MB/s).
- **TD-16 caches** — the ~1 ms RTT is largely genet IRQ→wake latency inflated by
  caches-off; the single biggest multiplier. Boot-risk/attended (SLC).
- **Gigabit cable** — hardware; the driver is now gigabit-ready (zero-copy, no
  per-frame copy/malloc).

## Known residual: nfs-fs VFS large-write hang (root cause REFINED 2026-06-15)

Separate from the boot/read path (which is solid). A multi-write through the
nfs-fs VFS bridge (Pi `fwrite` → kernel → `nfs_ops_write`) stalls after the 1st
4 KB write; the 2nd `nfs_pwrite` never returns. Direct libnfs writes (nfs-smoke
BIGWRITE, 512 KB ×16 on one fh) work fine (5.68 MB/s) — so libnfs + net are OK;
the trigger is specific to the nfs-fs-opened fh.

**CORRECTION to the earlier `getservbyport` analysis (it was a red herring).**
Reading libnfs `lib/socket.c` `rpc_connect_async`/the reserved-port binder:
`getservbyport(port,"tcp")` (socket.c:1515) is only used to *skip well-known
ports*; libphoenix returns NULL ("not implemented") which correctly means "no
well-known service here, bind is fine" — it does NOT cause the hang. The
`getservbyport` UART flood is a **symptom**: the reserved-port bind loop
(`do { bind(512..1023) } while rc!=0 && portOfs!=startOfs`, socket.c:1547)
sweeps all 511 reserved ports once per connect, and ×25739 calls ÷ 511 ≈ **~50
full sweeps = ~50 reconnect attempts**, each failing to `bind()` *every* reserved
port (errno ≠ EACCES, or the loop would break at 1543), then libnfs retries the
whole connect. So the real mechanism is: **after the 1st nfs-fs write the
connection drops → libnfs reconnects (socket.c:1456 `dup2(fd,old_fd)` path) →
lwip `bind()` to reserved ports fails for all 511 → ~50 connect retries → hang.**

Two open sub-questions (need on-device instrumentation, deferred — deep, and the
write path isn't boot/Quake-critical; the capture harness uses a TCP sink, saves
are the only user): **(A)** why does the 1st nfs-fs write drop the connection
(nfs-smoke's identical libnfs calls on an `nfs_creat` fh don't)? **(B)** why does
lwip reserved-port `bind()` fail for all 511 ports on reconnect (capture the
errno; likely a leaked/TIME_WAIT old socket or lwip PCB exhaustion across the ~50
retries). Likely fixes once (B) is known: `SO_REUSEADDR` on the rebind, or use an
**ephemeral source port** (NFSv4 doesn't require a privileged port — set the host
export `insecure` + disable libnfs's reserved-port bind), or fix an old-socket
leak. Decisive next experiment: patch libnfs to print the bind errno on reconnect,
one Pi cycle.

## Status

High-value software-side NFS/networking work is **complete + tested**. The
remaining levers are either hardware (cable), attended (caches), or substantial
cross-layer pipelining whose payoff is gigabit-gated — documented above for when
the cable lands. Proceeding to the GLQuake capstone per the loop directive.

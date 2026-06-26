# Network stress tests — Phoenix-RTOS RPi4 (task #40)

Host-side load generators + Pi-side server bits for stressing the Phoenix-RTOS
Raspberry Pi 4 network stack (lwip + BCM2711 GENET). **The orchestrator runs all
hardware**; the scripts here only generate/measure load and classify the result.

## Topology

The Pi netboots and is reachable at **10.42.0.12** on the 10.42.0.0/24 link. The
dev host is the gateway **10.42.0.1** (also NFS + TFTP server). Network stress =
the host drives load at the Pi (and the Pi echoes/serves back). The orchestrator
captures the Pi UART during load and greps it for fault signals (see below).

> The link has run at **100 Mbps** in the past (crossover cable carries 2 pairs).
> Throughput numbers are link-bounded until a gigabit cable/switch is in place.
> This does not affect fault classification, only absolute MiB/s.

## Three-bucket model (from `tools/stress/stress.h`)

- **OK** — operation succeeded as intended.
- **LIMIT** — the stack *correctly refused* at a resource boundary (connection
  refused/reset under load, HTTP 503, server at capacity). **Correct behaviour,
  not a fault.** Generators print `LIMIT:` and still exit 0.
- **FAULT** — a real defect: a wedge/hang (timeout), data corruption (echo
  mismatch), RPS collapse, latency cliff, or 100% packet loss. Generators print
  `FAULT:` and exit **2**.

Every generator prints a final `RESULT: OK` / `RESULT: OK (with LIMITs)` /
`RESULT: FAULT` line and exits `0` (OK/LIMIT) or `2` (FAULT), `1` on usage error.

---

## Deliverable 1 — HTTP load (lighttpd, already ported)

### One-time host staging (no Pi boot)

```
tools/stress/net/stage-lighttpd.sh
```

Stages onto the NFS export (`/srv/phoenix-rpi4-nfs`):
- `etc/lighttpd-stress.conf` — a **separate** conf from the HW-validated
  `etc/lighttpd/lighttpd.conf` (left untouched).
- `srv/stress-www/index.html` — small page (latency / RPS).
- `srv/stress-www/1mb.bin` — 1 MiB deterministic file (throughput).
- `var/log`, `var/run` — lighttpd errorlog + pid-file targets.

> **lighttpd is NFS-staged, not bundled.** The binary already exists at
> `/srv/phoenix-rpi4-nfs/usr/sbin/lighttpd` (built by the lighttpd port). It is
> served from the NFS root; no image rebuild needed.
>
> **Module set is fixed at build time.** The binary was compiled
> `LIGHTTPD_STATIC` with exactly `mod_indexfile`, `mod_dirlisting`,
> `mod_staticfile` (the port greps `mod_` out of the conf present at build
> time). The stress conf stays within that set. Do **not** add other modules
> without rebuilding the port — lighttpd aborts at startup otherwise.
>
> **Throughput confound:** the port was built `--disable-mmap`, so lighttpd
> `read()`s the file per request, and the docroot is on NFS. HTTP throughput
> therefore partly measures the NFS read path + GENET TX, not TX alone. Fine for
> a stress test; just know what the number means.

### Launch on the Pi (orchestrator, psh)

```
/usr/sbin/lighttpd -D -f /etc/lighttpd-stress.conf
```

`-D` = stay in foreground. The conf binds **0.0.0.0:80** (task spec). The
HW-proven port in earlier runs was **8080** — if 80 misbehaves, change
`server.port` in the conf (or just point the generator's `--port` elsewhere if
you launch the existing `lighttpd.conf` on 8080 instead). Logs:
`/var/log/lighttpd-stress-error.log`.

### Host load (orchestrator)

```
# requests/sec + latency (small page), ramp concurrency 1 / 8 / 32 / 64:
python3 tools/stress/net/http-load.py --host 10.42.0.12 --port 80 --conc 32 --secs 30

# throughput (1 MiB body):
python3 tools/stress/net/http-load.py --host 10.42.0.12 --port 80 --path /1mb.bin --conc 8 --secs 30
```

Measures: requests/sec, latency **p50/p95/max**, connection failures, non-200 /
truncated responses, bytes/sec. Flags: timeouts (wedge), non-200/truncation
(mis-serve), **RPS collapse over the run**, **latency cliff** (p95 near the
timeout). A pre-flight GET learns the expected body length so a short body is
caught as truncation. Connection refusals under load are reported as `LIMIT`.

---

## Deliverable 2 — Socket flood (host → Pi)

```
# all three sub-tests (udp flood / tcp storm / ping), 20 s each:
python3 tools/stress/net/flood.py --host 10.42.0.12 --tcp-port 80 --secs 20

# individual modes:
python3 tools/stress/net/flood.py --host 10.42.0.12 --mode udp  --udp-port 9999 --secs 20
python3 tools/stress/net/flood.py --host 10.42.0.12 --mode tcp  --tcp-port 80   --secs 20
python3 tools/stress/net/flood.py --host 10.42.0.12 --mode ping --secs 20
```

- **udp** — blasts small (1 B), large (1400 B, one frame), and oversized
  (8000 B, fragmented → reassembly) datagrams as fast as possible. Fire-and-
  forget; **needs no responder**. Default port 9999 (diag-udp's port if that
  responder is present in the image; a closed port still loads RX and triggers
  ICMP port-unreachable, which is itself RX work). Host-side this can't FAULT —
  the verdict lives in the Pi UART.
- **tcp** — connect/teardown storm. Point `--tcp-port` at a **real listener**
  (lighttpd :80 or stress-net :7777) so it exercises accept + teardown, not just
  RST. Refused/reset = `LIMIT` (backlog/capacity). Connect timeouts *alongside*
  successes, or *all* connects timing out with no RST = `FAULT` (stall/wedge).
- **ping** — sustained ICMP RTT check. >20% loss, 100% loss, or max RTT >500 ms
  = `FAULT`.

> The **authoritative** flood verdict is the Pi UART, captured by the
> orchestrator. The host only sees its own send rate / connect success / RTT.

---

## Deliverable 3 — Pi-side TCP echo + host corruption check

### Build + stage the server (host-side, no Pi boot)

```
tools/stress/net/build-stress-net.sh        # -> /srv/phoenix-rpi4-nfs/bin/stress-net
```

`tools/stress/stress-net.c` is a bounded BSD-socket echo server (built static
aarch64). It runs for a fixed duration (or connection count) then prints
`STRESS-SUMMARY` and exits — psh has no job control, so it self-terminates.

### Run

```
# Pi (psh): listen on :7777 for 120 s
/bin/stress-net 7777 120

# Host: open 64 connections (8 concurrent), 4 KiB each, verify echoes
python3 tools/stress/net/echo-load.py --host 10.42.0.12 --port 7777 --conns 64 --conc 8 --size 4096
```

Each connection sends a **distinct random payload** and verifies the echoed
bytes match exactly. **Echo mismatch = `FAULT` (data corruption)** — the
strongest signal in the suite (integrity, not timing). Hang past the io-timeout
= `FAULT`. Refused/reset (server at capacity or duration-exited) = `LIMIT`.

> `stress-net.c` **was built** (compiles warning-clean to a static aarch64 ELF
> and stages to the NFS export). The lwip BSD listen/accept server path is
> already exercised on the Pi by `psh/nc/nc.c`, so the semantics are not novel.

---

## FAULT signals the orchestrator should grep in the Pi UART during load

The host generators classify the host-visible side. During *any* load, grep the
captured UART for these (a hit during load = FAULT, regardless of host verdict):

- `Exception` / `Data Abort` / `Prefetch Abort` / `SError` — a crash.
- `KERNEL PANIC` / `Kernel panic` — fatal.
- No further output / no heartbeat for the rest of the capture — a **hang**
  (the host side will show timeouts to match).
- `STRESS net.* FAULT ...` — emitted by `stress-net` itself (recv/send errno
  that is not a legitimate limit).
- lwip allocation churn / `pbuf` / `mem_malloc` failures spamming — back-pressure
  approaching a LIMIT; a *clean* drop is OK, a crash that follows is FAULT.

A **clean** picture during load — psh still responsive, no Exception, host sees
LIMITs but no timeouts/corruption — is the OK/LIMIT outcome and is the correct
result for a stress test that pushes the stack to its boundary.

## Validation status (host-side, done without the Pi)

- All three Python generators: `py_compile` clean, `--help` parses, and
  functionally validated against localhost servers — OK / LIMIT / FAULT paths
  each produce the right `RESULT:` line and exit code.
- `stress-net.c`: cross-compiles warning-clean to a static aarch64 ELF; staged.
- `stage-lighttpd.sh`: idempotent; confirmed the lighttpd binary is present on
  the export.

#!/usr/bin/env python3
"""
http-load.py — multi-threaded HTTP load generator for the Phoenix-RTOS RPi4
network stress test (task #40). Host-side; drives load at lighttpd running on
the Pi. Stdlib only (threads + http.client + socket).

Drives N concurrent client threads at a target URL for a fixed duration and
reports requests/sec, latency p50/p95/max, connection failures, non-200s, and
bytes/sec. Flags anomalies and exits non-zero on a FAULT so the orchestrator
can classify automatically.

Three-bucket classification (see tools/stress/stress.h):
  OK     requests succeed (HTTP 200, body length as expected).
  LIMIT  the server correctly refuses at a boundary — connection refused / reset
         under load, or HTTP 503. Counted but NOT a FAULT on its own.
  FAULT  a timeout/hang, RPS collapse over the run, or a latency cliff — these
         are the wedge/back-pressure-failure signals.

Typical orchestrator use (Pi already serving on :80):
  python3 http-load.py --host 10.42.0.12 --port 80 --conc 32 --secs 30
  python3 http-load.py --host 10.42.0.12 --path /1mb.bin --conc 8 --secs 30   # throughput

Exit codes: 0 = OK/LIMIT only, 2 = FAULT detected, 1 = usage / setup error.
"""

import argparse
import http.client
import socket
import sys
import threading
import time

# Per-request hard ceiling. A wedged Pi must surface as a FAULT (timeout),
# never hang the generator. Keep generous enough for a 1 MiB body over a
# possibly-100Mbps link, but bounded.
DEFAULT_TIMEOUT = 10.0


class Stats:
    """Thread-safe accumulator for per-request outcomes."""

    def __init__(self):
        self.lock = threading.Lock()
        self.latencies = []      # seconds, successful (200) requests only
        self.ok = 0              # HTTP 200 with expected-ish body
        self.non200 = 0          # got a response but status != 200
        self.conn_fail = 0       # connect/refused/reset (LIMIT-ish)
        self.timeouts = 0        # request exceeded timeout (FAULT-ish)
        self.bytes = 0           # total body bytes read
        # Coarse time-bucketed request counts to detect RPS collapse over time.
        self.bucket_secs = 1.0
        self.buckets = {}        # int(second_offset) -> count
        self.t0 = None

    def record_ok(self, latency, nbytes, now):
        with self.lock:
            self.ok += 1
            self.latencies.append(latency)
            self.bytes += nbytes
            self._bucket(now)

    def record_non200(self, now):
        with self.lock:
            self.non200 += 1
            self._bucket(now)

    def record_conn_fail(self):
        with self.lock:
            self.conn_fail += 1

    def record_timeout(self):
        with self.lock:
            self.timeouts += 1

    def _bucket(self, now):
        if self.t0 is None:
            self.t0 = now
        b = int((now - self.t0) // self.bucket_secs)
        self.buckets[b] = self.buckets.get(b, 0) + 1


def worker(stop_at, host, port, path, timeout, expect_len, stats):
    """One client thread: open a connection, issue requests until stop_at.

    Reuses the connection (keep-alive) when the server allows it; reconnects on
    any error. Every socket op is timeout-bounded.
    """
    conn = None
    while time.monotonic() < stop_at:
        try:
            if conn is None:
                conn = http.client.HTTPConnection(host, port, timeout=timeout)
            t = time.monotonic()
            conn.request("GET", path, headers={"Connection": "keep-alive"})
            resp = conn.getresponse()
            body = resp.read()
            latency = time.monotonic() - t
            now = time.monotonic()
            if resp.status == 200:
                # If caller knows the expected length, a short body is corruption.
                if expect_len is not None and len(body) != expect_len:
                    stats.record_non200(now)  # treat truncated/oversize as non-OK
                else:
                    stats.record_ok(latency, len(body), now)
            else:
                stats.record_non200(now)
            # Honour server's connection-close intent.
            if resp.will_close:
                conn.close()
                conn = None
        except socket.timeout:
            stats.record_timeout()
            if conn is not None:
                conn.close()
                conn = None
        except (ConnectionRefusedError, ConnectionResetError, BrokenPipeError,
                http.client.HTTPException, OSError):
            stats.record_conn_fail()
            if conn is not None:
                try:
                    conn.close()
                except Exception:
                    pass
                conn = None
            # Brief backoff so a refusing/wedged server doesn't spin the CPU.
            time.sleep(0.01)
    if conn is not None:
        try:
            conn.close()
        except Exception:
            pass


def percentile(sorted_vals, q):
    if not sorted_vals:
        return float("nan")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    idx = q / 100.0 * (len(sorted_vals) - 1)
    lo = int(idx)
    hi = min(lo + 1, len(sorted_vals) - 1)
    frac = idx - lo
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


def probe_expected_len(host, port, path, timeout):
    """One pre-flight GET to learn the body length (so workers can detect
    truncation). Returns (content_length_or_None, status) or raises on failure."""
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read()
        return len(body), resp.status
    finally:
        conn.close()


def main():
    ap = argparse.ArgumentParser(description="HTTP load generator for Phoenix-RTOS RPi4 stress test")
    ap.add_argument("--host", required=True, help="Pi IP (e.g. 10.42.0.12)")
    ap.add_argument("--port", type=int, default=80, help="HTTP port (default 80; HW-proven path is 8080)")
    ap.add_argument("--path", default="/index.html", help="URL path (default /index.html; use /1mb.bin for throughput)")
    ap.add_argument("--conc", type=int, default=32, help="concurrent client threads (e.g. 1/8/32/64)")
    ap.add_argument("--secs", type=float, default=30.0, help="run duration in seconds")
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="per-request timeout (s)")
    ap.add_argument("--no-expect-len", action="store_true",
                    help="skip the pre-flight content-length probe (don't flag truncation)")
    args = ap.parse_args()

    if args.conc < 1:
        print("ERROR: --conc must be >= 1", file=sys.stderr)
        return 1

    print("=== http-load: host=%s:%d path=%s conc=%d secs=%.0f timeout=%.1fs ==="
          % (args.host, args.port, args.path, args.conc, args.secs, args.timeout))

    # Pre-flight: confirm reachable + learn expected body length.
    expect_len = None
    try:
        ln, status = probe_expected_len(args.host, args.port, args.path, args.timeout)
        print("pre-flight: GET %s -> %d, %d bytes" % (args.path, status, ln))
        if status != 200:
            print("WARNING: pre-flight status %d (expected 200) — proceeding anyway" % status)
        if not args.no_expect_len and status == 200:
            expect_len = ln
    except Exception as e:
        print("FAULT: pre-flight request failed: %r (Pi not serving / not reachable)" % e)
        print("RESULT: FAULT")
        return 2

    stats = Stats()
    stop_at = time.monotonic() + args.secs
    threads = []
    for _ in range(args.conc):
        t = threading.Thread(target=worker,
                             args=(stop_at, args.host, args.port, args.path,
                                   args.timeout, expect_len, stats),
                             daemon=True)
        t.start()
        threads.append(t)

    run_start = time.monotonic()
    for t in threads:
        # Join with margin so a stuck worker (wedged Pi) can't hang us forever.
        t.join(timeout=args.secs + args.timeout + 5.0)
    elapsed = time.monotonic() - run_start

    # ---- Summary ----
    lat = sorted(stats.latencies)
    total_reqs = stats.ok + stats.non200
    rps = total_reqs / elapsed if elapsed > 0 else 0.0
    mibps = (stats.bytes / (1024 * 1024)) / elapsed if elapsed > 0 else 0.0

    p50 = percentile(lat, 50) if lat else float("nan")
    p95 = percentile(lat, 95) if lat else float("nan")
    pmax = lat[-1] if lat else float("nan")

    print("--- results (elapsed %.2fs) ---" % elapsed)
    print("requests:    ok=%d non200=%d conn_fail=%d timeouts=%d" %
          (stats.ok, stats.non200, stats.conn_fail, stats.timeouts))
    print("throughput:  %.1f req/s, %.2f MiB/s (%d bytes)" % (rps, mibps, stats.bytes))
    if lat:
        print("latency:     p50=%.1f ms  p95=%.1f ms  max=%.1f ms" %
              (p50 * 1e3, p95 * 1e3, pmax * 1e3))
    else:
        print("latency:     (no successful requests)")

    # ---- Anomaly detection -> three buckets ----
    faults = []
    limits = []

    if stats.timeouts > 0:
        faults.append("%d request timeout(s) — possible wedge/hang" % stats.timeouts)
    if stats.ok == 0 and total_reqs == 0:
        faults.append("zero successful requests")
    if stats.non200 > 0:
        # Non-200 / truncated under a static-file load is unexpected (FAULT-ish:
        # the server mis-served or corrupted). Connection-level refusal is LIMIT.
        faults.append("%d non-200 / truncated response(s)" % stats.non200)
    if stats.conn_fail > 0:
        # Graceful refusal/reset under load is a correct LIMIT, not a fault — but
        # surface it; a high ratio is worth the orchestrator's attention.
        limits.append("%d connection failure(s) (refused/reset) — graceful LIMIT if server is at capacity" % stats.conn_fail)

    # Latency cliff: p95 within ~1 RTT of the timeout ceiling = server stalling.
    if lat and p95 >= 0.8 * args.timeout:
        faults.append("p95 latency %.1f ms near timeout %.0f ms — latency cliff" %
                      (p95 * 1e3, args.timeout * 1e3))

    # RPS collapse over time: compare first third vs last third of buckets.
    collapse = rps_collapse(stats)
    if collapse is not None:
        first_rps, last_rps = collapse
        if first_rps > 0 and last_rps < 0.3 * first_rps and total_reqs > 20:
            faults.append("RPS collapse: %.1f -> %.1f req/s over run (degradation/wedge)" %
                          (first_rps, last_rps))

    for m in limits:
        print("LIMIT: " + m)
    for m in faults:
        print("FAULT: " + m)

    if faults:
        print("RESULT: FAULT")
        return 2
    print("RESULT: OK" + (" (with LIMITs)" if limits else ""))
    return 0


def rps_collapse(stats):
    """Return (first_third_rps, last_third_rps) over the FULL (interior) time
    buckets, or None. The first and last buckets are dropped: they are partial
    (run started/ended mid-second) and dividing their fractional request count
    by a full bucket_secs would fabricate a bogus near-zero rate at the tail."""
    if not stats.buckets:
        return None
    keys = sorted(stats.buckets)
    # Drop the leading warmup bucket and the trailing teardown bucket, both of
    # which span less than bucket_secs of wall time.
    interior = keys[1:-1]
    n = len(interior)
    if n < 3:
        return None
    third = max(1, n // 3)
    first = interior[:third]
    last = interior[-third:]
    first_rps = sum(stats.buckets[k] for k in first) / (third * stats.bucket_secs)
    last_rps = sum(stats.buckets[k] for k in last) / (third * stats.bucket_secs)
    return first_rps, last_rps


if __name__ == "__main__":
    sys.exit(main())

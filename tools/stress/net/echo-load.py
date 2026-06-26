#!/usr/bin/env python3
"""
echo-load.py — host client for the Pi-side TCP echo server (tools/stress/
stress-net.c). Opens many connections, sends randomized payloads, verifies the
echoed bytes match exactly, and measures throughput / latency. Stdlib only.

The whole point is the CORRUPTION check: stress-net echoes faithfully, so any
byte-level mismatch between what we sent and what we got back is a FAULT (data
corruption in the Pi's TCP path / buffers). This is the strongest fault signal
in the suite because it's an integrity check, not a timing heuristic.

Three-bucket model (see tools/stress/stress.h):
  OK     every connection echoed its payload back byte-for-byte.
  LIMIT  the server refused/reset under load (connection refused, the bounded
         server hit max_conns, or it exited at its duration) — correct, NOT a
         fault. Detected as a clean refusal with no corruption.
  FAULT  echo MISMATCH (corruption), a connection that hangs past the timeout,
         or a short/over read where the server accepted but mangled the stream.

Pairs with stress-net on the Pi (default port 7777):
  Pi:   /bin/stress-net 7777 120         # listen on :7777 for 120 s
  Host: python3 echo-load.py --host 10.42.0.12 --port 7777 --conns 64 --size 4096

Exit codes: 0 = OK/LIMIT only, 2 = FAULT, 1 = usage/setup error.
"""

import argparse
import os
import socket
import sys
import threading
import time


def recv_exact(sock, n, deadline):
    """Receive exactly n bytes or raise. Timeout-bounded by `deadline`."""
    buf = bytearray()
    while len(buf) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise socket.timeout("recv_exact deadline exceeded (got %d/%d)" % (len(buf), n))
        sock.settimeout(remaining)
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed mid-stream (got %d/%d)" % (len(buf), n))
        buf.extend(chunk)
    return bytes(buf)


class Outcome:
    __slots__ = ("ok", "mismatch", "refused", "reset", "timeout", "other", "latencies", "bytes")

    def __init__(self):
        self.ok = 0
        self.mismatch = 0       # FAULT: echoed bytes != sent bytes
        self.refused = 0        # LIMIT
        self.reset = 0          # LIMIT
        self.timeout = 0        # FAULT: hang
        self.other = 0
        self.latencies = []     # round-trip seconds per successful echo
        self.bytes = 0          # verified echoed bytes


def one_echo(host, port, payload, connect_timeout, io_timeout, out):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(connect_timeout)
    try:
        s.connect((host, port))
    except ConnectionRefusedError:
        out.refused += 1
        s.close()
        return
    except ConnectionResetError:
        out.reset += 1
        s.close()
        return
    except (socket.timeout, OSError):
        out.timeout += 1
        s.close()
        return

    try:
        t = time.monotonic()
        deadline = t + io_timeout
        s.sendall(payload)
        echoed = recv_exact(s, len(payload), deadline)
        latency = time.monotonic() - t
        if echoed == payload:
            out.ok += 1
            out.latencies.append(latency)
            out.bytes += len(payload)
        else:
            # Integrity failure — corruption in the Pi's TCP path.
            out.mismatch += 1
    except socket.timeout:
        out.timeout += 1
    except (ConnectionResetError, BrokenPipeError):
        out.reset += 1
    except (ConnectionError, OSError):
        out.other += 1
    finally:
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        s.close()


def worker(host, port, payloads, connect_timeout, io_timeout, lock, shared, idx_iter):
    local = Outcome()
    for payload in payloads:
        one_echo(host, port, payload, connect_timeout, io_timeout, local)
    with lock:
        shared.ok += local.ok
        shared.mismatch += local.mismatch
        shared.refused += local.refused
        shared.reset += local.reset
        shared.timeout += local.timeout
        shared.other += local.other
        shared.latencies.extend(local.latencies)
        shared.bytes += local.bytes


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


def main():
    ap = argparse.ArgumentParser(description="Echo load / corruption check for Phoenix-RTOS RPi4 stress-net")
    ap.add_argument("--host", required=True, help="Pi IP (e.g. 10.42.0.12)")
    ap.add_argument("--port", type=int, default=7777, help="stress-net listen port (default 7777)")
    ap.add_argument("--conns", type=int, default=64, help="total connections to make")
    ap.add_argument("--conc", type=int, default=8, help="concurrent worker threads")
    ap.add_argument("--size", type=int, default=4096, help="payload bytes per connection")
    ap.add_argument("--connect-timeout", type=float, default=3.0)
    ap.add_argument("--io-timeout", type=float, default=10.0, help="send+echo round-trip timeout (s)")
    args = ap.parse_args()

    if args.conns < 1 or args.conc < 1 or args.size < 1:
        print("ERROR: --conns/--conc/--size must be >= 1", file=sys.stderr)
        return 1

    print("=== echo-load: host=%s:%d conns=%d conc=%d size=%d ===" %
          (args.host, args.port, args.conns, args.conc, args.size))

    # Distinct random payloads so a mismatch can't hide behind identical bytes.
    payloads = [os.urandom(args.size) for _ in range(args.conns)]

    # Partition payloads across worker threads.
    buckets = [[] for _ in range(args.conc)]
    for i, p in enumerate(payloads):
        buckets[i % args.conc].append(p)

    shared = Outcome()
    lock = threading.Lock()
    ths = []
    t0 = time.monotonic()
    for i in range(args.conc):
        t = threading.Thread(target=worker,
                             args=(args.host, args.port, buckets[i],
                                   args.connect_timeout, args.io_timeout, lock, shared, None),
                             daemon=True)
        t.start()
        ths.append(t)
    # Bound the join so a wedged Pi can't hang the harness.
    per_thread_budget = (len(buckets[0]) + 1) * (args.connect_timeout + args.io_timeout) + 10
    for t in ths:
        t.join(timeout=per_thread_budget)
    elapsed = time.monotonic() - t0

    lat = sorted(shared.latencies)
    mibps = (shared.bytes / (1024 * 1024)) / elapsed if elapsed > 0 else 0.0

    print("--- results (elapsed %.2fs) ---" % elapsed)
    print("connections: ok=%d mismatch=%d refused=%d reset=%d timeout=%d other=%d" %
          (shared.ok, shared.mismatch, shared.refused, shared.reset, shared.timeout, shared.other))
    print("throughput:  %.2f MiB/s (%d verified bytes)" % (mibps, shared.bytes))
    if lat:
        print("latency:     p50=%.1f ms  p95=%.1f ms  max=%.1f ms (round-trip echo)" %
              (percentile(lat, 50) * 1e3, percentile(lat, 95) * 1e3, lat[-1] * 1e3))
    else:
        print("latency:     (no successful echoes)")

    faults = []
    limits = []

    if shared.mismatch > 0:
        faults.append("%d echo MISMATCH(es) — data corruption in the Pi TCP path" % shared.mismatch)
    if shared.timeout > 0:
        faults.append("%d connection timeout(s) — server accepted but stalled / Pi wedged" % shared.timeout)
    if shared.other > 0:
        faults.append("%d unexpected socket error(s) (non-limit)" % shared.other)

    # Refused/reset = the bounded server at capacity / exited at its duration =
    # graceful LIMIT, as long as nothing succeeded-then-corrupted.
    if shared.refused or shared.reset:
        limits.append("%d refused + %d reset — server at capacity / duration-exited (graceful LIMIT)" %
                      (shared.refused, shared.reset))

    # Zero successes AND zero clean refusals = the server isn't there / wedged.
    total = shared.ok + shared.mismatch + shared.refused + shared.reset + shared.timeout + shared.other
    if shared.ok == 0 and shared.refused == 0 and shared.reset == 0 and total > 0:
        faults.append("zero successful echoes and no clean refusal — server unreachable or wedged")

    for m in limits:
        print("LIMIT: " + m)
    for m in faults:
        print("FAULT: " + m)
    if faults:
        print("RESULT: FAULT")
        return 2
    print("RESULT: OK" + (" (with LIMITs)" if limits else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

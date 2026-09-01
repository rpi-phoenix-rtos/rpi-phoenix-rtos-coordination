#!/usr/bin/env python3
"""Host side of the Pi 4 WiFi throughput test (see scripts/wifi-perf.md).

Protocol, deliberately trivial so both ends can be read at a glance:
Repeats for several runs, because a single run on this link is not a
measurement (identical code has spanned 2.6x). Per connection:
  1. the Pi connects
  2. the Pi sends its TX payload, then shuts down the write side
  3. we read to EOF and report the rate we saw  (= the Pi's TX / our RX)
  4. we send our payload back and close
  5. the Pi reads to EOF and reports its own rate (= the Pi's RX)

Rates are computed from the first byte moved, not from accept(), so Python
startup on the Pi does not pollute the measurement.
"""
import socket, sys, time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
SEND_BYTES = int(sys.argv[2]) if len(sys.argv) > 2 else 2 * 1024 * 1024
CHUNK = 64 * 1024

RUNS = int(sys.argv[3]) if len(sys.argv) > 3 else 3
CHUNK = 64 * 1024

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(4)
srv.settimeout(600)
print("wifi-perf-host: listening on :%d for %d run(s)" % (PORT, RUNS), flush=True)

for run in range(1, RUNS + 1):
    try:
        conn, addr = srv.accept()
    except socket.timeout:
        print("wifi-perf-host: timed out waiting for run %d" % run, flush=True)
        break
    conn.settimeout(120)
    try:
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass

    # phase 1: drain what the Pi sends
    total, t0 = 0, None
    while True:
        try:
            b = conn.recv(CHUNK)
        except (socket.timeout, OSError):
            break
        if not b:
            break
        if t0 is None:
            t0 = time.time()
        total += len(b)
    t1 = time.time()
    rate = (total / 1048576.0 / max(t1 - t0, 1e-9)) if (total and t0) else 0.0
    print("PI-TX  run %d: %.2f MiB = %.2f MB/s" % (run, total / 1048576.0, rate), flush=True)

    # phase 2: blast back so the Pi can measure its RX
    payload = bytes(CHUNK)
    sent, t2 = 0, time.time()
    try:
        while sent < SEND_BYTES:
            n = conn.send(payload[:min(CHUNK, SEND_BYTES - sent)])
            if n <= 0:
                break
            sent += n
    except OSError as e:
        print("wifi-perf-host: run %d send stopped: %s" % (run, e), flush=True)
    t3 = time.time()
    print("HOST-TX run %d: %.2f MiB = %.2f MB/s (the Pi reports its own RX rate)" %
          (run, sent / 1048576.0, sent / 1048576.0 / max(t3 - t2, 1e-9)), flush=True)
    conn.close()

srv.close()
print("wifi-perf-host: done", flush=True)

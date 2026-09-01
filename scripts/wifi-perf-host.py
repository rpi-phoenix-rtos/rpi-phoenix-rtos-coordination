#!/usr/bin/env python3
"""Host side of the Pi 4 WiFi throughput test (see scripts/wifi-perf.md).

Protocol, deliberately trivial so both ends can be read at a glance:
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
SEND_BYTES = int(sys.argv[2]) if len(sys.argv) > 2 else 8 * 1024 * 1024
CHUNK = 64 * 1024

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(1)
print("wifi-perf-host: listening on :%d" % PORT, flush=True)

conn, addr = srv.accept()
conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
print("wifi-perf-host: connection from %s:%d" % addr, flush=True)

# --- phase 1: drain what the Pi sends ---
total, t0 = 0, None
while True:
    b = conn.recv(CHUNK)
    if not b:
        break
    if t0 is None:
        t0 = time.time()
    total += len(b)
t1 = time.time()
if total and t0:
    print("PI-TX  %.2f MiB in %.3f s = %.2f MB/s" %
          (total / 1048576.0, t1 - t0, total / 1048576.0 / max(t1 - t0, 1e-9)), flush=True)
else:
    print("PI-TX  no data received", flush=True)

# --- phase 2: blast back so the Pi can measure its RX ---
payload = bytes(CHUNK)
sent, t2 = 0, time.time()
try:
    while sent < SEND_BYTES:
        n = conn.send(payload[:min(CHUNK, SEND_BYTES - sent)])
        if n <= 0:
            break
        sent += n
except OSError as e:
    print("wifi-perf-host: send stopped:", e, flush=True)
t3 = time.time()
print("HOST-TX %.2f MiB in %.3f s = %.2f MB/s (the Pi reports its own RX rate)" %
      (sent / 1048576.0, t3 - t2, sent / 1048576.0 / max(t3 - t2, 1e-9)), flush=True)
conn.close()
srv.close()

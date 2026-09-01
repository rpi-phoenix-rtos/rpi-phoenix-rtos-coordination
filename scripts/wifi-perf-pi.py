#!/usr/bin/env python3
"""Pi side of the WiFi throughput test. Staged into the netboot root and run as
   python3 /root/wifi-perf.py <host-ip> [port] [bind-ip] [bytes] [runs]

Repeats the transfer several times in ONE boot and reports the median, because
a single run on this link is not a measurement: identical code has measured
1.73 and 0.66 MB/s on two runs (a 2.6x spread), which is wide enough to "prove"
whichever side of an A/B you happen to run first.

Binds the source address to the WiFi netif's own IP so the traffic provably
leaves over wl2 -- both interfaces are up, and a wrong source address would
quietly measure the gigabit link instead.
"""
import socket, sys, time

HOST = sys.argv[1] if len(sys.argv) > 1 else "10.43.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 7777
BIND = sys.argv[3] if len(sys.argv) > 3 else "10.43.0.89"
TX_BYTES = int(sys.argv[4]) if len(sys.argv) > 4 else 2 * 1024 * 1024
RUNS = int(sys.argv[5]) if len(sys.argv) > 5 else 3
CHUNK = 32 * 1024


def median(xs):
    xs = sorted(xs)
    n = len(xs)
    if n == 0:
        return 0.0
    return xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2.0


def one_run(idx):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind((BIND, 0))
    except OSError as e:
        print("WIFIPERF bind(%s) failed: %s -- refusing to measure the wrong link" % (BIND, e), flush=True)
        raise SystemExit(2)
    s.settimeout(120)
    s.connect((HOST, PORT))
    try:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass

    payload = bytes(CHUNK)
    sent, t0 = 0, time.time()
    while sent < TX_BYTES:
        n = s.send(payload[:min(CHUNK, TX_BYTES - sent)])
        if n <= 0:
            break
        sent += n
    t1 = time.time()
    tx = sent / 1048576.0 / max(t1 - t0, 1e-9)
    s.shutdown(socket.SHUT_WR)

    total, t2 = 0, None
    while True:
        try:
            b = s.recv(CHUNK)
        except socket.timeout:
            break
        if not b:
            break
        if t2 is None:
            t2 = time.time()
        total += len(b)
    t3 = time.time()
    rx = (total / 1048576.0 / max(t3 - t2, 1e-9)) if (total and t2) else 0.0
    s.close()
    print("WIFIPERF-RUN %d tx=%.2f MB/s rx=%.2f MB/s" % (idx, tx, rx), flush=True)
    return tx, rx


txs, rxs = [], []
for i in range(RUNS):
    try:
        tx, rx = one_run(i + 1)
    except OSError as e:
        print("WIFIPERF-RUN %d failed: %s" % (i + 1, e), flush=True)
        continue
    txs.append(tx)
    rxs.append(rx)
    time.sleep(1)

if txs:
    print("WIFIPERF-MEDIAN runs=%d tx=%.2f MB/s (min %.2f max %.2f) rx=%.2f MB/s (min %.2f max %.2f)" %
          (len(txs), median(txs), min(txs), max(txs), median(rxs), min(rxs), max(rxs)), flush=True)
else:
    print("WIFIPERF-MEDIAN no successful runs", flush=True)
print("WIFIPERF-DONE", flush=True)

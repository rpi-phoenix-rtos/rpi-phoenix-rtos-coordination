#!/usr/bin/env python3
"""
flood.py — socket flood / RX-saturation stress for the Phoenix-RTOS RPi4
network stack (task #40). Host-side; blasts the Pi's genet RX path. Stdlib only.

Three sub-tests (run all by default, or select with --mode):
  udp   Blast small AND large UDP datagrams at a Pi port as fast as possible
        (fire-and-forget; needs NO responder). Default target port 9999 — the
        diag-udp responder's port if present; a CLOSED port still loads RX and
        the kernel will emit ICMP port-unreachable, which is itself RX work.
  tcp   TCP connect/teardown storm: open many connections rapidly and close
        them immediately. Aim at a REAL listener (lighttpd :80 / stress-net
        :7777) so it exercises accept()+teardown, not just RST generation.
  ping  Sustained ICMP latency check (via the system `ping`) to watch RTT
        stability under (or alongside) the flood.

Three-bucket model (see tools/stress/stress.h):
  OK     stack keeps up / drops gracefully and stays responsive.
  LIMIT  stack back-pressures or refuses (connect refused/reset/ETIMEDOUT on a
         busy listener) — correct behaviour, NOT a fault on its own.
  FAULT  the Pi wedges: connect storms all time out where they previously
         succeeded, ping RTT explodes / packets stop, the host sees the link go
         dead. The orchestrator should also grep the Pi UART for Exception /
         Data Abort / no-heartbeat during the flood.

This tool measures only the HOST-VISIBLE side (send rate, connect success, RTT).
The authoritative FAULT signal during a flood is the Pi UART — see README.

Usage:
  python3 flood.py --host 10.42.0.12 --secs 20                       # all modes
  python3 flood.py --host 10.42.0.12 --mode udp  --udp-port 9999 --secs 20
  python3 flood.py --host 10.42.0.12 --mode tcp  --tcp-port 80   --secs 20
  python3 flood.py --host 10.42.0.12 --mode ping --secs 20

Exit codes: 0 = OK/LIMIT only, 2 = FAULT, 1 = usage/setup error.
"""

import argparse
import re
import socket
import subprocess
import sys
import threading
import time


# ----------------------- UDP flood -----------------------

def udp_flood(host, port, secs, stop_evt):
    """Fire small + large datagrams as fast as one thread can. Returns a dict."""
    small = b"P"  # 1 byte
    # ~1400 B stays under the typical 1500 MTU (avoid IP fragmentation so each
    # datagram is one RX frame at the Pi); a separate oversized burst exercises
    # the reassembly path.
    large = b"X" * 1400
    huge = b"Z" * 8000  # > MTU -> fragmented; stresses reassembly

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    dst = (host, port)

    sent = 0
    send_errs = 0
    t_end = time.monotonic() + secs
    i = 0
    while time.monotonic() < t_end and not stop_evt.is_set():
        # Rotate payload sizes so we exercise small-frame and large/fragmented.
        payload = (small, large, huge)[i % 3]
        i += 1
        try:
            sock.sendto(payload, dst)
            sent += 1
        except BlockingIOError:
            # Host send buffer full — back off a hair; this is the host, not the Pi.
            time.sleep(0.0005)
        except OSError:
            send_errs += 1
            time.sleep(0.001)
    sock.close()
    return {"sent": sent, "send_errs": send_errs}


def run_udp(host, port, secs, threads):
    print("--- udp flood: %s:%d for %.0fs x%d thread(s) ---" % (host, port, secs, threads))
    stop_evt = threading.Event()
    results = [None] * threads
    ths = []

    def runner(idx):
        results[idx] = udp_flood(host, port, secs, stop_evt)

    t0 = time.monotonic()
    for i in range(threads):
        t = threading.Thread(target=runner, args=(i,), daemon=True)
        t.start()
        ths.append(t)
    for t in ths:
        t.join(timeout=secs + 10)
    elapsed = time.monotonic() - t0

    total_sent = sum(r["sent"] for r in results if r)
    total_err = sum(r["send_errs"] for r in results if r)
    rate = total_sent / elapsed if elapsed > 0 else 0.0
    print("udp: sent=%d send_errs=%d  rate=%.0f pkt/s over %.2fs" %
          (total_sent, total_err, rate, elapsed))
    # A UDP flood can't FAULT host-side (fire-and-forget). It's informational;
    # the Pi UART is the place a wedge shows. Report zero-sent as a setup issue.
    faults = []
    if total_sent == 0:
        faults.append("udp: zero datagrams sent (host socket setup failed)")
    return {"faults": faults, "limits": []}


# ----------------------- TCP connect/teardown storm -----------------------

def tcp_storm(host, port, secs, connect_timeout, stop_evt, res):
    ok = 0
    refused = 0
    reset = 0
    timeout = 0
    other = 0
    t_end = time.monotonic() + secs
    while time.monotonic() < t_end and not stop_evt.is_set():
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(connect_timeout)
        try:
            s.connect((host, port))
            ok += 1
            # Immediate teardown — the point is connect churn.
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        except ConnectionRefusedError:
            refused += 1
        except ConnectionResetError:
            reset += 1
        except socket.timeout:
            timeout += 1
        except OSError:
            other += 1
        finally:
            s.close()
    res.update(ok=ok, refused=refused, reset=reset, timeout=timeout, other=other)


def run_tcp(host, port, secs, threads, connect_timeout):
    print("--- tcp connect/teardown storm: %s:%d for %.0fs x%d thread(s) ---" %
          (host, port, secs, threads))
    stop_evt = threading.Event()
    results = [dict() for _ in range(threads)]
    ths = []
    t0 = time.monotonic()
    for i in range(threads):
        t = threading.Thread(target=tcp_storm,
                             args=(host, port, secs, connect_timeout, stop_evt, results[i]),
                             daemon=True)
        t.start()
        ths.append(t)
    for t in ths:
        t.join(timeout=secs + connect_timeout + 10)
    elapsed = time.monotonic() - t0

    agg = {k: sum(r.get(k, 0) for r in results) for k in ("ok", "refused", "reset", "timeout", "other")}
    total = sum(agg.values())
    rate = agg["ok"] / elapsed if elapsed > 0 else 0.0
    print("tcp: attempts=%d ok=%d refused=%d reset=%d timeout=%d other=%d  conn-rate=%.0f/s over %.2fs" %
          (total, agg["ok"], agg["refused"], agg["reset"], agg["timeout"], agg["other"], rate, elapsed))

    faults = []
    limits = []
    # Refused/reset = the listener's backlog/capacity boundary -> graceful LIMIT.
    if agg["refused"] or agg["reset"]:
        limits.append("tcp: %d refused + %d reset (listener backlog/capacity LIMIT)" %
                      (agg["refused"], agg["reset"]))
    # Timeouts where SOME connects succeeded = the stack stalling = FAULT signal.
    if agg["timeout"] > 0 and agg["ok"] > 0:
        faults.append("tcp: %d connect timeout(s) alongside %d successes — stack stalling under storm" %
                      (agg["timeout"], agg["ok"]))
    # All attempts timed out and none refused = listener absent OR wedged. If the
    # orchestrator pointed at a real listener, this is a FAULT; flag it.
    if total > 0 and agg["ok"] == 0 and agg["refused"] == 0 and agg["timeout"] == total:
        faults.append("tcp: ALL %d connects timed out (no RST, no accept) — port unreachable or Pi wedged" % total)
    return {"faults": faults, "limits": limits}


# ----------------------- ping flood latency -----------------------

def run_ping(host, secs):
    print("--- ping latency under load: %s for ~%.0fs ---" % (host, secs))
    # Flood-ish but polite: interval 0.01s (100 pps) for the duration. Avoid
    # `ping -f` (needs root); -i 0.01 also needs root for <0.2s on Linux, so
    # fall back to default interval and just measure RTT stability.
    count = max(5, int(secs))
    cmd = ["ping", "-c", str(count), "-W", "2", host]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=secs + 20)
    except FileNotFoundError:
        print("ping: `ping` not found on host; skipping")
        return {"faults": [], "limits": []}
    except subprocess.TimeoutExpired:
        print("ping: timed out")
        return {"faults": ["ping: command timed out — Pi unresponsive"], "limits": []}

    text = out.stdout + out.stderr
    # Parse "X packets transmitted, Y received, Z% packet loss"
    m = re.search(r"(\d+) packets transmitted, (\d+) received.*?(\d+(?:\.\d+)?)% packet loss", text, re.S)
    rtt = re.search(r"min/avg/max(?:/mdev)? = ([\d.]+)/([\d.]+)/([\d.]+)", text)
    faults = []
    if m:
        tx, rx, loss = int(m.group(1)), int(m.group(2)), float(m.group(3))
        print("ping: tx=%d rx=%d loss=%.0f%%" % (tx, rx, loss), end="")
        if rtt:
            print("  rtt min/avg/max = %s/%s/%s ms" % (rtt.group(1), rtt.group(2), rtt.group(3)))
        else:
            print("")
        if loss >= 100.0:
            faults.append("ping: 100% packet loss — Pi unreachable/wedged")
        elif loss > 20.0:
            faults.append("ping: %.0f%% packet loss — stack dropping ICMP under load" % loss)
        if rtt and float(rtt.group(3)) > 500.0:
            faults.append("ping: max RTT %.0f ms — severe latency under load" % float(rtt.group(3)))
    else:
        print("ping: could not parse output:\n%s" % text)
        faults.append("ping: unparseable / failed")
    return {"faults": faults, "limits": []}


def main():
    ap = argparse.ArgumentParser(description="Socket flood / RX-saturation stress for Phoenix-RTOS RPi4")
    ap.add_argument("--host", required=True, help="Pi IP (e.g. 10.42.0.12)")
    ap.add_argument("--mode", choices=["all", "udp", "tcp", "ping"], default="all")
    ap.add_argument("--secs", type=float, default=20.0, help="duration per sub-test (s)")
    ap.add_argument("--udp-port", type=int, default=9999, help="UDP target port (default 9999, diag-udp)")
    ap.add_argument("--tcp-port", type=int, default=80, help="TCP target port — a REAL listener (lighttpd 80 / stress-net 7777)")
    ap.add_argument("--threads", type=int, default=4, help="flood threads per sub-test")
    ap.add_argument("--connect-timeout", type=float, default=3.0, help="TCP connect timeout (s)")
    args = ap.parse_args()

    print("=== flood: host=%s mode=%s secs=%.0f threads=%d ===" %
          (args.host, args.mode, args.secs, args.threads))

    faults = []
    limits = []

    if args.mode in ("all", "udp"):
        r = run_udp(args.host, args.udp_port, args.secs, args.threads)
        faults += r["faults"]; limits += r["limits"]
    if args.mode in ("all", "tcp"):
        r = run_tcp(args.host, args.tcp_port, args.secs, args.threads, args.connect_timeout)
        faults += r["faults"]; limits += r["limits"]
    if args.mode in ("all", "ping"):
        r = run_ping(args.host, args.secs)
        faults += r["faults"]; limits += r["limits"]

    print("--- flood summary ---")
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

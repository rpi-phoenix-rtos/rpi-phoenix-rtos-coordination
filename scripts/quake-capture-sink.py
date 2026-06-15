#!/usr/bin/env python3
"""
quake-capture-sink.py — host listener for the Pi's Quake capture TCP sink.
The Pi streams [u32 idx][u32 tgalen][tgalen bytes] records (network byte order);
this writes each to <out>/cap_<idx>.tga. Bypasses the broken nfs-fs VFS-write
path. See docs/inprogress/2026-06-15-quake-visual-regression-harness.md.

  scripts/quake-capture-sink.py [--out DIR] [--port 5599] [--host 0.0.0.0]
Exits after the Pi closes the connection (one capture run).
"""
import argparse, os, socket, struct, sys


def recvall(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="/tmp/quake-pi/id1")
    ap.add_argument("--port", type=int, default=5599)
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for f in os.listdir(args.out):
        if f.startswith("cap_") and f.endswith(".tga"):
            os.remove(os.path.join(args.out, f))

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(1)
    print(f"sink listening on {args.host}:{args.port} -> {args.out}", flush=True)
    conn, addr = srv.accept()
    print(f"connected: {addr}", flush=True)

    nframes = 0
    while True:
        rec = recvall(conn, 8)
        if rec is None:
            break
        idx, tgalen = struct.unpack("!II", rec)
        if tgalen == 0 or tgalen > 64 * 1024 * 1024:
            print(f"bad tgalen {tgalen} at idx {idx}", file=sys.stderr)
            break
        data = recvall(conn, tgalen)
        if data is None:
            print(f"short frame at idx {idx}", file=sys.stderr)
            break
        with open(os.path.join(args.out, f"cap_{idx:04d}.tga"), "wb") as f:
            f.write(data)
        nframes += 1
        if nframes % 20 == 0:
            print(f"  {nframes} frames", flush=True)
    conn.close()
    srv.close()
    print(f"done: {nframes} frames in {args.out}", flush=True)


if __name__ == "__main__":
    sys.exit(main())

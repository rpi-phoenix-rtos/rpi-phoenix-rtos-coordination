"""TCP loopback ping-pong through lwIP: round trips/s and a bulk loopback rate.

Run on the Pi: python3 /root/lwip-pingpong.py
Every send/recv is a message to the lwip process, which takes the TCPIP core
lock, posts to a recvmbox and signals a semaphore/condition: the paths whose
lock costs the user-space-lock change removes.
"""
import socket
import threading
import time

ROUNDS = 300
BULK = 8 * 1024 * 1024
CHUNK = 64 * 1024


def echo_server(srv):
    conn, _ = srv.accept()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    while True:
        b = conn.recv(64)
        if not b:
            break
        conn.sendall(b)
    conn.close()


def sink_server(srv, out):
    conn, _ = srv.accept()
    n = 0
    while True:
        b = conn.recv(CHUNK)
        if not b:
            break
        n += len(b)
    out.append(n)
    conn.close()


def listener():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    s.listen(1)
    return s


srv = listener()
t = threading.Thread(target=echo_server, args=(srv,))
t.start()
c = socket.create_connection(srv.getsockname())
c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
msg = b"x" * 32
print("PP start", flush=True)
t0 = time.monotonic()
for i in range(ROUNDS):
    if i == 20:
        print("PP first20 ms=%.1f" % ((time.monotonic() - t0) * 1000), flush=True)
    c.sendall(msg)
    got = 0
    while got < len(msg):
        got += len(c.recv(64))
dt = time.monotonic() - t0
c.close()
t.join()
srv.close()
print("PINGPONG rounds=%d dur=%.3fs rtt=%.1fus rate=%.0f/s" % (ROUNDS, dt, dt / ROUNDS * 1e6, ROUNDS / dt))

srv = listener()
res = []
t = threading.Thread(target=sink_server, args=(srv, res))
t.start()
c = socket.create_connection(srv.getsockname())
buf = b"y" * CHUNK
t0 = time.monotonic()
sent = 0
while sent < BULK:
    c.sendall(buf)
    sent += CHUNK
c.close()
t.join()
dt = time.monotonic() - t0
srv.close()
print("LOOPBACK bytes=%d dur=%.3fs rate=%.2f MB/s" % (res[0], dt, res[0] / dt / 1048576))

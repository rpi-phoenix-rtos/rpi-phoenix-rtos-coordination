import time
t0 = time.monotonic()
print("LD python up", flush=True)
import socket, threading
print("LD imports %.1fs" % (time.monotonic() - t0), flush=True)
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(("127.0.0.1", 0)); s.listen(1)
print("LD listening", s.getsockname(), flush=True)
acc = []
def a():
    c, _ = s.accept(); acc.append(c); print("LD accepted", flush=True)
threading.Thread(target=a, daemon=True).start()
c = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
c.settimeout(20)
try:
    c.connect(s.getsockname()); print("LD connected %.1fs" % (time.monotonic() - t0), flush=True)
except Exception as e:
    print("LD connect failed", repr(e), flush=True)
time.sleep(1)
if acc:
    c.sendall(b"ping"); acc[0].settimeout(10)
    try:
        print("LD echo", acc[0].recv(10), flush=True)
    except Exception as e:
        print("LD recv failed", repr(e), flush=True)
print("LD done %.1fs" % (time.monotonic() - t0), flush=True)

"""Does CPython threading itself work on the Pi? No sockets at all.

Run on the Pi: python3 /root/py-thread-probe.py
Each step prints before and after; the last line printed names the step that hangs.
"""
import time
import threading

t0 = time.monotonic()
def p(msg):
    print("PT %-28s %.2fs" % (msg, time.monotonic() - t0), flush=True)

p("start")
t = threading.Thread(target=lambda: p("thread body runs"))
p("Thread() created")
t.start()
p("start() returned")
t.join(5)
p("join returned alive=%s" % t.is_alive())

ev = threading.Event()
threading.Thread(target=lambda: (time.sleep(0.2), ev.set()), daemon=True).start()
p("Event.wait -> %s" % ev.wait(5))

lk = threading.Lock(); lk.acquire()
t1 = time.monotonic(); r = lk.acquire(timeout=0.5)
p("Lock.acquire(0.5) -> %s after %.2fs" % (r, time.monotonic() - t1))

import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); s.listen(1)
p("listening %s" % (s.getsockname(),))
c = socket.socket(); c.settimeout(5)
p("settimeout ok")
try:
    c.connect(s.getsockname()); p("single-thread connect ok")
    a, _ = s.accept(); p("single-thread accept ok")
except Exception as e:
    p("single-thread connect failed %r" % (e,))
p("DONE")

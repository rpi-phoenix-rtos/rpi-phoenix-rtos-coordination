#!/usr/bin/env python3
"""Host-side report of a Phoenix-RTOS `prof record` trace, with symbols.

Reads the raw per-CPU CTF channels and prof.info that `prof record` (phoenix-rtos-utils/prof)
writes, symbolizes user and kernel addresses with aarch64-phoenix-addr2line against the UNSTRIPPED
binaries of the build, and prints:

  * CPU per process (user / kernel / idle) and the hottest functions per thread,
  * the longest single waits with the user call stack, the kernel function the thread blocked in,
    the deadline, what ended the wait and who,
  * a timeline of every wait longer than --long-ms of the selected process (--pid),
  * waits per thread grouped by reason, by total blocked time,
  * who blocked whom: msgSend client -> port -> serving thread, and for the longest message waits
    what the serving thread itself was doing meanwhile (its own waits, its CPU), two levels deep,
  * optionally folded stacks (--folded FILE) for flamegraph.pl / speedscope / inferno.

User stacks: the frame-pointer chain is used where it is valid; code built without frame pointers
(WebKit, most ports) is unwound by scanning the copied top of the stack for return addresses (words
inside executable code that follow a BL/BLR). Scanned stacks may contain stale frames: they are a
lead, not proof. Record with -s/-w > 0 (prof record default 512 bytes) to get them.

usage: scripts/prof-report.py <trace dir> [--pid PID] [--long-ms 50] [--folded out.folded]
       [--bin NAME=PATH ...] [--search DIR ...] [--kernel ELF] [--top 15]

Fetch the trace from the Pi first (it is on the NFS root when recorded to e.g. /root/prof).
"""

import argparse
import bisect
import collections
import glob
import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EV_FIXED = {
    0x20: 1, 0x21: 1, 0x22: 2, 0x23: 2, 0x24: 2, 0x25: 2, 0x26: 133, 0x27: 4, 0x28: 3, 0x29: 3,
    0x2a: 1, 0x2b: 1, 0x2c: 20, 0x2d: 6, 0x2e: 6, 0x2f: 6, 0x30: 6, 0x31: 3, 0x32: 2, 0x33: 133,
    0x42: 5, 0x43: 14, 0x44: 12, 0x45: 10,
}
EV_SAMPLE, EV_WAIT, EV_WAKEUP, EV_SEND, EV_RECV, EV_RESPOND = 0x40, 0x41, 0x42, 0x43, 0x44, 0x45
EV_CREATE, EV_EXEC, EV_SC_ENTER, EV_SC_EXIT, EV_WAKING = 0x26, 0x33, 0x28, 0x29, 0x25
EV_IRQ_ENTER, EV_IRQ_EXIT = 0x20, 0x21
CAUSES = ["wakeup", "timeout", "signal/exit", "lock handover", "other"]
KERNEL_BASE = 0xFFFF000000000000

# kernel functions on every wait path: the reason is the first frame that is none of these
KPLUMBING = re.compile(r"^(_trace_|trace_|_threadWaitRecord|_threads_enqueued|_proc_threadEnqueue|"
                       r"proc_threadWait|_proc_threadWait|_proc_threadSleep|hal_cpuReschedule|_threads_)")


# ---------------------------------------------------------------- trace parsing

def urec_end(b, p, o, avail):
    """size of the payload at p whose user part starts at p + o, 0 if it does not fit in avail"""
    if avail < o + 33:
        return 0
    o += 33 + b[p + o + 32] * 8
    if avail < o + 2:
        return 0
    ns = struct.unpack_from("<H", b, p + o)[0]
    o += 2 + ns * 8
    return 0 if avail < o else o


def parse_urec(b, o):
    pc, lr, sp, fp, nf = struct.unpack_from("<QQQQB", b, o)
    o += 33
    frames = list(struct.unpack_from("<%dQ" % nf, b, o))
    o += nf * 8
    ns = struct.unpack_from("<H", b, o)[0]
    stack = list(struct.unpack_from("<%dQ" % ns, b, o + 2))
    return pc, lr, sp, fp, frames, stack


def ev_size(b, o):
    """payload size of the event at o, 0 if unknown or truncated"""
    eid = b[o + 4]
    p = o + 5
    avail = len(b) - p
    if eid == EV_SAMPLE:
        return 0 if avail < 12 else urec_end(b, p, 12 + b[p + 11] * 8, avail)
    if eid == EV_WAIT:
        return 0 if avail < 44 else urec_end(b, p, 44 + b[p + 43] * 8, avail)
    sz = EV_FIXED.get(eid, 0)
    return sz if avail >= sz else 0


RESYNC_RUN = 6


def sync_at(b, o):
    """a rolling trace drops bytes, not events: o is a boundary if RESYNC_RUN events (or all up to the
    end) parse back to back with time not going back"""
    n, prev = 0, 0
    while n < RESYNC_RUN:
        if o == len(b):
            return n > 0
        if o + 5 > len(b):
            return False
        ts = struct.unpack_from("<I", b, o)[0]
        sz = ev_size(b, o)
        if sz == 0 or (n and ts < prev):
            return False
        prev, n, o = ts, n + 1, o + 5 + sz
    return True


def load_stream(path, cpu, out):
    b = open(path, "rb").read()
    o, seq, sync, skipped = 0, len(out), True, 0
    while o + 5 <= len(b):
        if sync:
            start = o
            while o + 5 <= len(b) and not sync_at(b, o):
                o += 1
            skipped += o - start
            sync = False
            if o + 5 > len(b):
                break
        ts, eid = struct.unpack_from("<IB", b, o)
        sz = ev_size(b, o)
        if sz == 0:
            sync = True
            continue
        out.append((ts, seq, cpu, eid, b[o + 5:o + 5 + sz]))
        seq += 1
        o += 5 + sz
    if skipped:
        print(f"warning: {path}: skipped {skipped} bytes that are not whole events (rolling trace?)", file=sys.stderr)


def load_trace(d):
    evs = []
    cpu = 0
    while True:
        found = False
        for kind in ("meta", "event"):
            path = os.path.join(d, f"channel_{kind}{cpu}")
            if os.path.exists(path):
                load_stream(path, cpu, evs)
                found = True
        if not found:
            break
        cpu += 1
    evs.sort(key=lambda e: (e[0], e[1]))
    return evs, cpu


def load_info(d):
    info = {"threads": {}, "maps": collections.defaultdict(list), "files": {}, "pid": 0}
    path = os.path.join(d, "prof.info")
    if not os.path.exists(path):
        return info
    for line in open(path, errors="replace"):
        w = line.split()
        if not w:
            continue
        if w[0] in ("thread", "thread-end") and len(w) >= 4:
            info["threads"].setdefault(int(w[1]), (int(w[2]), " ".join(w[4:])))
        elif w[0] == "map" and len(w) == 8:
            info["maps"][int(w[1])].append((int(w[2], 16), int(w[3], 16), int(w[4], 16), int(w[5]),
                                            (int(w[6]), int(w[7]))))
        elif w[0] == "file" and len(w) >= 4:
            info["files"][(int(w[1]), int(w[2]))] = " ".join(w[3:])
        elif w[0] == "pid":
            info["pid"] = int(w[1])
    for pid in info["maps"]:
        info["maps"][pid] = sorted(set(info["maps"][pid]))
    return info


def syscall_names(path):
    try:
        return re.findall(r"ID\((\w+)\)", open(path).read())
    except OSError:
        return []


# ---------------------------------------------------------------- ELF + symbols

class Elf:
    def __init__(self, path):
        self.path = path
        self.ok = False
        self.exec = []  # (start, end, bytes)
        self.loads = []  # (p_offset, p_vaddr, p_filesz)
        self.etype = 0
        self.symtab = False
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            return
        if data[:4] != b"\x7fELF" or data[4] != 2:
            return
        self.etype = struct.unpack_from("<H", data, 16)[0]
        phoff, shoff = struct.unpack_from("<QQ", data, 32)
        phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", data, 54)
        for i in range(phnum):
            ptype, _, off, vaddr, _, filesz = struct.unpack_from("<IIQQQQ", data, phoff + i * phentsize)
            if ptype == 1:
                self.loads.append((off, vaddr, filesz))
        for i in range(shnum):
            name, stype, flags, addr, off, size = struct.unpack_from("<IIQQQQ", data, shoff + i * shentsize)
            if stype == 2:
                self.symtab = True
            if (flags & 0x4) and stype == 1 and size:
                self.exec.append((addr, addr + size, data[off:off + size]))
        self.exec.sort()
        self.starts = [e[0] for e in self.exec]
        self.ok = True

    def to_vaddr(self, fileoff):
        for off, vaddr, filesz in self.loads:
            if off <= fileoff < off + filesz:
                return fileoff - off + vaddr
        return None

    def is_return_site(self, addr):
        """addr follows a BL/BLR in executable code"""
        i = bisect.bisect_right(self.starts, addr - 4) - 1
        if i < 0 or addr % 4:
            return False
        start, end, code = self.exec[i]
        if not (start <= addr - 4 < end):
            return False
        insn = struct.unpack_from("<I", code, addr - 4 - start)[0]
        return (insn & 0xFC000000) == 0x94000000 or (insn & 0xFFFFFC1F) == 0xD63F0000

    def in_code(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        return i >= 0 and self.exec[i][0] <= addr < self.exec[i][1]


class Symbols:
    def __init__(self, args, info):
        self.args = args
        self.info = info
        self.elfs = {}
        self.index = None
        self.overrides = dict(kv.split("=", 1) for kv in args.bin)
        self.pending = collections.defaultdict(set)
        self.cache = {}
        bdir = os.path.join(args.buildroot, "_build", args.target)
        self.kernel = self.elf(args.kernel or os.path.join(bdir, "prog", f"phoenix-{args.target.rsplit('-', 1)[0]}.elf"))
        if self.kernel is None:
            cands = glob.glob(os.path.join(bdir, "prog", "phoenix-*.elf"))
            self.kernel = self.elf(cands[0]) if cands else None
        self.roots = list(args.search) + [
            os.path.join(bdir, "prog"),
            os.path.join(bdir, "webkit_wpe-build"),
            os.path.join(bdir, "webkit_wpe-build", "webkit-build", "bin"),
            os.path.join(bdir, "webkit_wpe-build", "webkit-build", "lib"),
        ] + sorted(glob.glob(os.path.join(bdir, "versioned-ports", "*", "bin"))) \
          + sorted(glob.glob(os.path.join(bdir, "versioned-ports", "*", "lib"))) \
          + [os.path.join(bdir, "sysroot", "usr", "lib"), os.path.join(bdir, "sysroot", "lib")]

    def elf(self, path):
        if not path:
            return None
        if path not in self.elfs:
            e = Elf(path)
            self.elfs[path] = e if e.ok else None
        return self.elfs[path]

    def find(self, name):
        """unstripped ELF for a binary/library basename"""
        if name in self.overrides:
            return self.elf(self.overrides[name])
        if self.index is None:
            self.index = collections.defaultdict(list)
            for r in self.roots:
                if os.path.isdir(r):
                    for f in os.listdir(r):
                        p = os.path.join(r, f)
                        if os.path.isfile(p):
                            self.index[f].append(p)
        best = None
        for p in self.index.get(name, []):
            e = self.elf(p)
            if e is not None and (best is None or (e.symtab and not best.symtab)):
                best = e
        return best

    def locate(self, pid, procname, addr):
        """(elf, elf address) for a runtime address of pid, or (None, addr)"""
        if addr >= KERNEL_BASE:
            return self.kernel, addr
        for vaddr, size, offs, prot, oid in self.info["maps"].get(pid, ()):
            if vaddr <= addr < vaddr + size:
                path = self.info["files"].get(oid)
                if path is None:
                    break
                e = self.find(os.path.basename(path))
                if e is None:
                    return None, addr
                if e.etype == 3:  # ET_DYN: loaded at vaddr
                    va = e.to_vaddr(addr - vaddr + offs)
                    return (e, va) if va is not None else (None, addr)
                return e, addr
        e = self.find(procname) if procname else None
        return e, addr

    def request(self, elf, addr, is_ret):
        if elf is not None:
            self.pending[elf.path].add(addr - 1 if is_ret else addr)

    def resolve(self):
        a2l = self.args.addr2line
        for path, addrs in self.pending.items():
            addrs = sorted(a for a in addrs if (path, a) not in self.cache)
            if not addrs:
                continue
            try:
                out = subprocess.run([a2l, "-f", "-C", "-a", "-e", path], input="\n".join("0x%x" % a for a in addrs),
                                     capture_output=True, text=True, check=False).stdout.splitlines()
            except OSError as exc:
                sys.exit(f"cannot run {a2l}: {exc}")
            for i in range(0, len(out) - 2, 3):
                a = int(out[i], 16)
                fn, loc = out[i + 1], out[i + 2]
                loc = os.path.basename(loc.split(" ")[0]) if loc and not loc.startswith("??") else ""
                self.cache[(path, a)] = (fn if fn != "??" else None, loc)
        self.pending.clear()

    def name(self, elf, addr, is_ret, with_loc=False):
        if elf is None:
            return "0x%x" % addr
        fn, loc = self.cache.get((elf.path, addr - 1 if is_ret else addr), (None, ""))
        base = os.path.basename(elf.path)
        if fn is None:
            return "%s+0x%x" % (base, addr)
        return "%s (%s)" % (fn, loc) if with_loc and loc else fn


# ---------------------------------------------------------------- analysis

class Thread:
    def __init__(self, tid):
        self.tid = tid
        self.pid = -1
        self.name = "?"
        self.samples = [0, 0, 0]
        self.syscall = None
        self.msg = None  # (mid, port, type) of the syscall in flight
        self.wait = None
        self.wake = None  # (waker, cause) seen before thread_waking
        self.irq = {}


def basename_of(procname):
    first = procname.split(" ")[0] if procname else ""
    return os.path.basename(first) or "?"


class Analysis:
    def __init__(self, evs, ncpus, info, scnames, args):
        self.evs, self.ncpus, self.info, self.sc, self.args = evs, ncpus, info, scnames, args
        self.threads = {}
        self.samples = []  # (ts, tid, mode, kstack, ustack(list of (addr, is_ret)), pc)
        self.waits = []  # dicts
        self.msgs = {}
        self.irq_depth = collections.defaultdict(list)
        for tid, (pid, name) in info["threads"].items():
            t = self.thread(tid)
            t.pid, t.name = pid, basename_of(name)
        self.end_ts = evs[-1][0] if evs else 0

    def thread(self, tid):
        t = self.threads.get(tid)
        if t is None:
            t = self.threads[tid] = Thread(tid)
        return t

    def scname(self, n):
        return self.sc[n] if n is not None and 0 <= n < len(self.sc) else None

    def run(self, syms):
        for ts, _, cpu, eid, p in self.evs:
            if eid in (EV_CREATE, EV_EXEC):
                pid, tid = struct.unpack_from("<HH", p, 0)
                t = self.thread(tid)
                t.pid, t.name = pid, basename_of(p[5:133].split(b"\0")[0].decode(errors="replace"))
            elif eid == EV_IRQ_ENTER:
                self.irq_depth[cpu].append(p[0])
            elif eid == EV_IRQ_EXIT:
                if self.irq_depth[cpu]:
                    self.irq_depth[cpu].pop()
            elif eid == EV_SC_ENTER:
                n, tid = struct.unpack_from("<BH", p, 0)
                t = self.thread(tid)
                t.syscall, t.msg = n, None
            elif eid == EV_SC_EXIT:
                t = self.thread(struct.unpack_from("<BH", p, 0)[1])
                t.syscall, t.msg = None, None
            elif eid == EV_SAMPLE:
                self.on_sample(ts, p, syms)
            elif eid == EV_WAIT:
                self.on_wait(ts, p, syms)
            elif eid == EV_WAKEUP:
                tid, waker, cause = struct.unpack_from("<HHB", p, 0)
                t = self.thread(tid)
                if t.wait is not None:
                    irq = self.irq_depth[cpu][-1] if self.irq_depth[cpu] and cause == 0 else None
                    t.wake = (waker, min(cause, 4), irq)
            elif eid == EV_WAKING:
                t = self.thread(struct.unpack_from("<H", p, 0)[0])
                if t.wait is not None:
                    self.end_wait(t, ts, False)
            elif eid == EV_SEND:
                tid, port, mtype, mid = struct.unpack_from("<HIII", p, 0)
                self.thread(tid).msg = (mid, port, mtype)
                self.msgs[mid] = {"client": tid, "port": port, "type": mtype, "server": None, "ts": ts}
            elif eid == EV_RECV:
                tid, port, mid, spid = struct.unpack_from("<HIIH", p, 0)
                if mid in self.msgs:
                    self.msgs[mid]["server"] = tid
            elif eid == EV_RESPOND:
                tid, port, mid = struct.unpack_from("<HII", p, 0)
                if mid in self.msgs:
                    self.msgs[mid]["server"] = tid
        for t in self.threads.values():
            if t.wait is not None:
                self.end_wait(t, self.end_ts, True)

    def user_stack(self, pid, procname, pc, lr, frames, stack, syms, leaf_pc=True):
        """[(elf, addr, is_ret)] innermost first"""
        out = []
        e, a = syms.locate(pid, procname, pc)
        out.append((e, a, not leaf_pc))
        chain = []
        for f in frames:
            fe, fa = syms.locate(pid, procname, f)
            if fe is None or not fe.is_return_site(fa):
                break
            chain.append((fe, fa, True))
        if len(chain) >= 2 or (chain and not stack):
            return out + chain
        # no usable frame-pointer chain: lr (a leaf's caller), then return addresses on the stack
        le, la = syms.locate(pid, procname, lr)
        if le is not None and le.is_return_site(la):
            out.append((le, la, True))
        if not self.args.no_scan:
            for w in stack:
                if w == 0 or w >= KERNEL_BASE:
                    continue
                we, wa = syms.locate(pid, procname, w)
                if we is not None and we.is_return_site(wa) and (we, wa, True) != out[-1]:
                    out.append((we, wa, True))
        return out[:1 + self.args.depth]

    def kernel_stack(self, kpc, kframes, syms):
        out = [(syms.kernel, kpc, False)] if kpc else []
        return out + [(syms.kernel, f, True) for f in kframes if f >= KERNEL_BASE]

    def on_sample(self, ts, p, syms):
        tid, mode, kpc, nk = struct.unpack_from("<HBQB", p, 0)
        kframes = list(struct.unpack_from("<%dQ" % nk, p, 12))
        pc, lr, sp, fp, frames, stack = parse_urec(p, 12 + nk * 8)
        t = self.thread(tid)
        if mode > 2:
            return
        t.samples[mode] += 1
        ks = self.kernel_stack(kpc, kframes, syms) if mode != 0 else []
        us = self.user_stack(t.pid, t.name, pc, lr, frames, stack, syms, leaf_pc=(mode == 0)) if pc else []
        for e, a, r in ks + us:
            syms.request(e, a, r)
        self.samples.append((ts, tid, mode, ks, us))

    def on_wait(self, ts, p, syms):
        tid, flags, queue, timeout = struct.unpack_from("<HBII", p, 0)
        args = struct.unpack_from("<4Q", p, 11)
        nk = p[43]
        kframes = list(struct.unpack_from("<%dQ" % nk, p, 44))
        pc, lr, sp, fp, frames, stack = parse_urec(p, 44 + nk * 8)
        t = self.thread(tid)
        if t.wait is not None:
            self.end_wait(t, ts, True)
        ks = self.kernel_stack(0, kframes, syms)
        us = self.user_stack(t.pid, t.name, pc, lr, frames, stack, syms, leaf_pc=False) if pc else []
        for e, a, r in ks + us:
            syms.request(e, a, r)
        sc = None if flags & 1 else t.syscall
        t.wait = {"tid": tid, "ts": ts, "existing": bool(flags & 1), "queue": queue, "timeout": timeout,
                  "args": args, "syscall": sc, "msg": t.msg if self.scname(sc) == "msgSend" else None,
                  "ks": ks, "us": us}
        t.wake = None

    def end_wait(self, t, ts, still):
        w = t.wait
        w["end"] = ts
        w["us_len"] = ts - w["ts"]
        w["open"] = still
        waker, cause, irq = (None, 4, None) if still or t.wake is None else t.wake
        w["waker"], w["cause"], w["irq"] = waker, cause, irq
        if w["msg"] is not None:
            m = self.msgs.get(w["msg"][0])
            w["server"] = m["server"] if m and m["server"] else waker
        else:
            w["server"] = None
        self.waits.append(w)
        t.wait, t.wake = None, None


# ---------------------------------------------------------------- output

def fmt_thread(an, tid):
    t = an.threads.get(tid)
    return "%d %s[%d]" % (tid, t.name if t else "?", t.pid if t else -1)


def kernel_reason(syms, ks):
    for e, a, r in ks:
        n = syms.name(e, a, r)
        if not KPLUMBING.match(n):
            return n
    return "?"


def wait_reason(an, syms, w):
    sc = an.scname(w["syscall"])
    a0 = w["args"][0]
    if sc != "futexWait":
        a0 &= 0xFFFFFFFF  # a 32-bit argument leaves the upper half of its register unspecified
    if sc == "msgSend":
        port = w["msg"][1] if w["msg"] else a0
        typ = (" type %d" % w["msg"][2]) if w["msg"] else ""
        return "msgSend port %d%s" % (port, typ)
    if sc == "msgRecv":
        return "msgRecv port %d" % a0
    if sc == "futexWait":
        return "futexWait 0x%x" % a0
    if sc in ("phMutexLock", "phCondWait"):
        return "%s handle %d" % (sc, a0)
    if sc == "nsleep":
        return "nsleep"
    if sc is not None:
        return "%s(0x%x) in %s" % (sc, a0, kernel_reason(syms, w["ks"]))
    return "in %s" % kernel_reason(syms, w["ks"])


def ustack_str(syms, us, n=6):
    return " <- ".join(syms.name(e, a, r) for e, a, r in us[:n]) or "-"


def first_user_caller(syms, us):
    """skip libc wrappers / syscall stubs: the first frame that is not in libphoenix-ish names"""
    for e, a, r in us:
        n = syms.name(e, a, r)
        if not re.match(r"^(msgSend|msgRecv|futexWait|phCondWait|phMutexLock|nsleep|usleep|nanosleep|"
                        r"pthread_cond_|pthread_mutex_|__futex|sys_|condWait|mutexLock|_?sem)", n):
            return n
    return syms.name(*us[0]) if us else "-"


def describe(an, syms, w):
    s = "%9.1f ms  %-26s %s" % (w["us_len"] / 1000.0, fmt_thread(an, w["tid"]), wait_reason(an, syms, w))
    if w["timeout"]:
        s += ", timeout %.1f ms" % (w["timeout"] / 1000.0)
    if w["open"]:
        s += " -> STILL WAITING at the end"
    else:
        s += " -> %s" % CAUSES[w["cause"]]
        if w["irq"] is not None:
            s += " from irq %d" % w["irq"]
        elif w["waker"]:
            s += " by %s" % fmt_thread(an, w["waker"])
    if w["existing"]:
        s += " (began before the trace)"
    return s


def overlapping(an, tid, t0, t1):
    out = []
    for w in an.waits_by_tid.get(tid, ()):
        ov = min(w["end"], t1) - max(w["ts"], t0)
        if ov > 0:
            out.append((ov, w))
    out.sort(key=lambda x: -x[0])
    return out


def cpu_in(an, tid, t0, t1):
    ts = an.sample_ts_by_tid.get(tid, [])
    return bisect.bisect_left(ts, t1) - bisect.bisect_left(ts, t0)


def chain(an, syms, w, depth, indent):
    srv = w.get("server")
    if not srv or depth == 0:
        return
    t0, t1 = w["ts"], w["end"]
    period = an.period_us
    print("%s served by %s: on CPU ~%.1f ms of the %.1f ms" % (indent, fmt_thread(an, srv),
          cpu_in(an, srv, t0, t1) * period / 1000.0, (t1 - t0) / 1000.0))
    for ov, sw in overlapping(an, srv, t0, t1)[:3]:
        print("%s   blocked %.1f ms of it: %s  [%s]" % (indent, ov / 1000.0, wait_reason(an, syms, sw),
              first_user_caller(syms, sw["us"])))
        chain(an, syms, sw, depth - 1, indent + "     ")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--pid", type=int, default=None, help="process of interest (default: prof record -p)")
    ap.add_argument("--top", type=int, default=15)
    ap.add_argument("--long-ms", type=float, default=50.0, help="timeline of waits at least this long")
    ap.add_argument("--depth", type=int, default=24, help="user frames kept per stack")
    ap.add_argument("--folded", help="write folded stacks of the samples here")
    ap.add_argument("--folded-waits", help="write folded stacks of blocked time (us) here")
    ap.add_argument("--target", default="aarch64a72-generic-rpi4b")
    ap.add_argument("--buildroot", default=os.path.join(REPO, ".buildroot"))
    ap.add_argument("--kernel", help="kernel ELF (default: the build's prog/phoenix-*.elf)")
    ap.add_argument("--bin", action="append", default=[], help="NAME=PATH of an unstripped binary or library")
    ap.add_argument("--search", action="append", default=[], help="more directories with unstripped binaries")
    ap.add_argument("--syscalls", default=os.path.join(REPO, "sources", "phoenix-rtos-kernel", "include", "syscalls.h"))
    ap.add_argument("--addr2line", default=os.path.join(REPO, ".toolchain", "aarch64-phoenix", "bin",
                                                         "aarch64-phoenix-addr2line"))
    ap.add_argument("--no-scan", action="store_true", help="no stack scanning: frame-pointer chains only")
    args = ap.parse_args()

    evs, ncpus = load_trace(args.dir)
    if not evs:
        sys.exit(f"no trace in {args.dir}")
    info = load_info(args.dir)
    pid_sel = args.pid if args.pid is not None else (info["pid"] or None)
    syms = Symbols(args, info)
    if syms.kernel is None:
        print("warning: kernel ELF not found, kernel frames unsymbolized (--kernel)", file=sys.stderr)

    an = Analysis(evs, ncpus, info, syscall_names(args.syscalls), args)
    an.run(syms)
    syms.resolve()

    period = 1000
    m = re.search(r"^period_us (\d+)", open(os.path.join(args.dir, "prof.info")).read(), re.M) \
        if os.path.exists(os.path.join(args.dir, "prof.info")) else None
    if m:
        period = int(m.group(1))
    an.period_us = period
    an.waits_by_tid = collections.defaultdict(list)
    for w in an.waits:
        an.waits_by_tid[w["tid"]].append(w)
    an.sample_ts_by_tid = collections.defaultdict(list)
    for s in an.samples:
        an.sample_ts_by_tid[s[1]].append(s[0])

    def selected(tid):
        return pid_sel is None or (tid in an.threads and an.threads[tid].pid == pid_sel)

    dur = an.end_ts / 1e6
    print("trace %s: %.2f s, %d CPUs, %d events, %d samples, %d waits%s" % (
        args.dir, dur, ncpus, len(evs), len(an.samples), len(an.waits),
        (", process of interest pid %d" % pid_sel) if pid_sel else ""))

    # CPU per process; idle = kernel-thread samples in the idle loop
    total = len(an.samples) or 1
    procs = collections.defaultdict(lambda: [0, 0, 0, "?"])
    for ts, tid, mode, ks, us in an.samples:
        t = an.threads[tid]
        key = t.pid
        if mode == 2:
            fn = syms.name(*ks[0]) if ks else "?"
            key = "idle" if re.search(r"idle|hal_cpuHalt|hal_cpuLowPower|wfi", fn) else 0
        procs[key][min(mode, 2)] += 1
        procs[key][3] = "[idle]" if key == "idle" else ("[kernel threads]" if key == 0 else t.name)
    print("\nCPU by process (%% of all CPU time over %d CPUs)" % ncpus)
    print("  %6s %-28s %7s %7s %7s" % ("pid", "process", "user%", "sys%", "total%"))
    for key, (u, k, kt, name) in sorted(procs.items(), key=lambda kv: -sum(kv[1][:3])):
        print("  %6s %-28.28s %6.1f%% %6.1f%% %6.1f%%" % (key, name, 100.0 * u / total, 100.0 * (k + kt) / total,
                                                          100.0 * (u + k + kt) / total))

    # hottest functions per thread (self time)
    per = collections.defaultdict(collections.Counter)
    for ts, tid, mode, ks, us in an.samples:
        if mode == 2 or not selected(tid):
            continue
        leaf = ks[0] if mode == 1 and ks else (us[0] if us else None)
        if leaf is not None:
            per[tid][("[k] " if mode == 1 else "") + syms.name(*leaf)] += 1
    print("\nHottest functions (self samples) of the busiest threads")
    for tid, c in sorted(per.items(), key=lambda kv: -sum(kv[1].values()))[:args.top]:
        n = sum(c.values())
        print("  %-30s %6d samples = %.1f%% of a CPU" % (fmt_thread(an, tid), n, 100.0 * n / max(1, dur * 1e6 / period)))
        for fn, k in c.most_common(6):
            print("      %5.1f%%  %s" % (100.0 * k / n, fn))

    # longest waits
    sel_waits = [w for w in an.waits if selected(w["tid"])]
    print("\nLongest single waits%s" % ("" if pid_sel is None else " (pid %d)" % pid_sel))
    for w in sorted(sel_waits, key=lambda w: -w["us_len"])[:args.top]:
        print("  " + describe(an, syms, w))
        print("               user: %s" % ustack_str(syms, w["us"]))
        print("               kernel: %s" % kernel_reason(syms, w["ks"]))
        chain(an, syms, w, 2, "              ")

    # timeline
    long_us = args.long_ms * 1000.0
    tl = sorted((w for w in sel_waits if w["us_len"] >= long_us), key=lambda w: w["ts"])
    print("\nTimeline: waits >= %.0f ms%s, by start time" % (args.long_ms, "" if pid_sel is None else " (pid %d)" % pid_sel))
    for w in tl[:200]:
        print("  t=%8.3f s %s  [%s]" % (w["ts"] / 1e6, describe(an, syms, w), first_user_caller(syms, w["us"])))
    if len(tl) > 200:
        print("  ... %d more" % (len(tl) - 200))

    # waits by reason
    agg = collections.defaultdict(lambda: [0, 0, 0, collections.Counter(), ""])
    for w in sel_waits:
        key = (w["tid"], wait_reason(an, syms, w), first_user_caller(syms, w["us"]))
        a = agg[key]
        a[0] += 1
        a[1] += w["us_len"]
        a[2] = max(a[2], w["us_len"])
        a[3][CAUSES[w["cause"]] if not w["open"] else "open"] += 1
    print("\nWaits by reason (per thread, by total blocked time)")
    print("  %-26s %7s %10s %9s  %-34s %s" % ("thread", "count", "total ms", "max ms", "waiting in", "called from / ended by"))
    for (tid, reason, caller), (n, tot, mx, causes, _) in sorted(agg.items(), key=lambda kv: -kv[1][1])[:3 * args.top]:
        print("  %-26.26s %7d %10.1f %9.1f  %-34.34s %s  [%s]" % (
            fmt_thread(an, tid), n, tot / 1000.0, mx / 1000.0, reason, caller,
            " ".join("%s:%d" % kv for kv in causes.most_common())))

    # who blocked whom
    edges = collections.defaultdict(lambda: [0, 0, 0])
    for w in an.waits:
        if w["msg"] is not None and (selected(w["tid"]) or (w["server"] and selected(w["server"]))):
            e = edges[(w["tid"], w["msg"][1], w["server"])]
            e[0] += 1
            e[1] += w["us_len"]
            e[2] = max(e[2], w["us_len"])
    print("\nWho blocked whom: msgSend client -> port -> serving thread (by total wait)")
    for (c, port, s), (n, tot, mx) in sorted(edges.items(), key=lambda kv: -kv[1][1])[:2 * args.top]:
        print("  %-26s -> port %-6d -> %-26s %6d msgs %10.1f ms total %9.1f ms max" % (
            fmt_thread(an, c), port, fmt_thread(an, s) if s else "?", n, tot / 1000.0, mx / 1000.0))

    def fold(t, frames_k, frames_u):
        names = [syms.name(*f) for f in reversed(frames_u)] + ["[k] " + syms.name(*f) for f in reversed(frames_k)]
        return ";".join([t.name + "[%d]" % t.pid, "%s-%d" % (t.name, t.tid)] + [n.replace(";", ":") for n in names])

    if args.folded:
        c = collections.Counter()
        for ts, tid, mode, ks, us in an.samples:
            t = an.threads[tid]
            if mode == 2:
                c[";".join(["[kernel]", "tid-%d" % tid] + ["[k] " + syms.name(*f) for f in reversed(ks)])] += 1
            else:
                c[fold(t, ks if mode == 1 else [], us)] += 1
        with open(args.folded, "w") as f:
            for k, n in c.most_common():
                f.write("%s %d\n" % (k, n))
        print("\nfolded samples: %s (flamegraph.pl %s > cpu.svg)" % (args.folded, args.folded))
    if args.folded_waits:
        c = collections.Counter()
        for w in an.waits:
            t = an.threads[w["tid"]]
            c[fold(t, [], w["us"]) + ";[wait] " + wait_reason(an, syms, w).replace(";", ":")] += w["us_len"]
        with open(args.folded_waits, "w") as f:
            for k, n in c.most_common():
                f.write("%s %d\n" % (k, n))
        print("folded blocked time (us): %s (flamegraph.pl --countname=us %s > waits.svg)" % (
            args.folded_waits, args.folded_waits))


if __name__ == "__main__":
    main()

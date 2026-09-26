#!/usr/bin/env python3
"""E2: summarize the `v3d-winsys: subprof-*` lines of one or more UART logs.

The analysis is PRE-REGISTERED in docs/gpu-new-lane/E2-stk-submit-breakdown.md; this
script is that plan in executable form, written before the first Pi run so the
numbers cannot choose their own method afterwards.

Per 5 s window the winsys prints three lines sharing t= (ms since the first flip):
  subprof-a  : frame count, counter wall, clock_gettime wall, ioctl/lock/flip/instrument
               time, computed CPU time, inter-GPU-job gap
  subprof-cl : SUBMIT_CL count and its phases (pre, tlb, l2t, fixa, bin, hand, rend, post)
  subprof-x  : TFU / CSD phases, BO-ioctl counts and times
A window is analysed only when all three lines parsed (the UART corrupts ~1.3% of
lines; a corrupted line drops its window, and the drop is COUNTED, not hidden).

Gameplay windows: fps > 3 (menus/loading run < 1 fps, the race ~8); the first window
of the race and the last one before fps falls again are dropped as transitions.
Windows with a wedge, a silent spin-cap exit, or a TIMEOUT/wedge line are excluded.

Usage: e2-summarize.py <uart.log> [more.log ...]   (each log summarized separately)
Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import re
import sys

RACE_FPS = 3.0

RE_A = re.compile(r"subprof-a t=(\d+)ms fr=(\d+) wall=(\d+) cg=(\d+) hz=(\d+) ioc=(\d+) lock=(\d+) "
                  r"flip=(\d+) rep=(\d+) cpu=(\d+) gap=(\d+)/(\d+) gapmax=(\d+) fmin=(\d+) fmax=(\d+) "
                  r"gpumax=(\d+)\s*$")
RE_CL = re.compile(r"subprof-cl t=(\d+)ms fr=(\d+) n=(\d+) ioc=(\d+) sum=(\d+) pre=(\d+) tlb=(\d+) "
                   r"l2t=(\d+) fixa=(\d+) bin=(\d+) hand=(\d+) rend=(\d+) post=(\d+) oom=(\d+) "
                   r"wedge=(\d+) l2tto=(\d+) tlbto=(\d+)\s*$")
RE_X = re.compile(r"subprof-x t=(\d+)ms fr=(\d+) tfu=(\d+)/(\d+)\[(\d+) (\d+) (\d+) (\d+)\] "
                  r"csd=(\d+)/(\d+)\[(\d+) (\d+) (\d+)\] create=(\d+)/(\d+) close=(\d+)/(\d+) "
                  r"mmap=(\d+)/(\d+) unl=(\d+)/(\d+) other=(\d+)/(\d+)\s*$")
RE_BAD = re.compile(r"TIMEOUT|GPU wedged|DROPPED job")
A_KEYS = "t fr wall cg hz ioc lock flip rep cpu gap gapn gapmax fmin fmax gpumax".split()
CL_KEYS = "t fr n ioc sum pre tlb l2t fixa bin hand rend post oom wedge l2tto tlbto".split()
X_KEYS = ("t fr tfun tfu tfupre tfuspin tfudiag tfupost csdn csd csdpre csdspin csdpost "
          "createn create closen close mmapn mmap unln unl othern other").split()


RE_ARM_WS = re.compile(r"SUBMIT PROFILE build \(V3D_PHX_SUBMIT_PROFILE\) cntfrq=(\d+)")
RE_ARM_LN = re.compile(r"stk-prof: DATADIR=")


def parse(path):
    wins, bad_t, seen, parsed = {}, set(), 0, 0
    last_t = 0
    arm = {"cntfrq": None, "launcher": False}
    with open(path, "rb") as f:
        for raw in f:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            m = RE_ARM_WS.search(line)
            if m:
                arm["cntfrq"] = int(m.group(1))
            if RE_ARM_LN.search(line):
                arm["launcher"] = True
            if RE_BAD.search(line):
                bad_t.add(last_t)          # attributed to the window being accumulated
            if "subprof-" not in line or "subprof-f" in line:
                continue
            seen += 1
            for rx, keys, tag in ((RE_A, A_KEYS, "a"), (RE_CL, CL_KEYS, "cl"), (RE_X, X_KEYS, "x")):
                m = rx.search(line)
                if m:
                    d = dict(zip(keys, map(int, m.groups())))
                    wins.setdefault(d["t"], {})[tag] = d
                    last_t = max(last_t, d["t"])
                    parsed += 1
                    break
    # A wedge line printed during window W appears BEFORE W's report, whose t is > last_t.
    # Re-attribute: a bad line belongs to the first window with t > the previous report.
    ts = sorted(wins)
    bad = set()
    for b in bad_t:
        nxt = [t for t in ts if t > b]
        if nxt:
            bad.add(nxt[0])
    return wins, bad, seen, parsed, arm


def ms(us, fr):
    return us / fr / 1000.0 if fr else 0.0


def summarize(path):
    wins, bad, seen, parsed, arm = parse(path)
    ts = sorted(wins)
    complete = [t for t in ts if len(wins[t]) == 3]
    print(f"== {path}")
    # Assert the arm from the program's own output, never from the command that was sent.
    if arm["cntfrq"] is not None and arm["launcher"]:
        print(f"   ARM: prof (winsys banner cntfrq={arm['cntfrq']}, stk-prof launcher)")
    else:
        print("   ********** ARM NOT ASSERTED: "
              f"winsys banner {'present' if arm['cntfrq'] is not None else 'MISSING'}, "
              f"stk-prof launcher line {'present' if arm['launcher'] else 'MISSING'} "
              "-- not a (complete) supertuxkart-prof run; do not grade it **********")
    if arm["cntfrq"] == 0:
        print("   ********** cntfrq read 0 at EL0: times assume 54 MHz -- check cg/wall **********")
    print(f"   subprof lines: {seen} seen, {parsed} parsed ({seen - parsed} unparsed residue); "
          f"windows: {len(ts)} with any line, {len(complete)} complete")
    if not complete:
        print("   NO complete windows -- was this the prof binary (look for 'SUBMIT PROFILE build')?")
        return

    def fps(t):
        a = wins[t]["a"]
        return a["fr"] * 1e6 / a["wall"] if a["wall"] else 0.0

    race = [t for t in complete if fps(t) > RACE_FPS]
    game = []
    if race:
        i0 = complete.index(race[0])
        run = []
        for t in complete[i0:]:
            if fps(t) > RACE_FPS:
                run.append(t)
            else:
                break
        game = run[1:-1]                      # drop the transition windows at both ends
    excl = []
    for t in list(game):
        cl = wins[t]["cl"]
        if cl["wedge"] or cl["l2tto"] or cl["tlbto"] or t in bad:
            excl.append(t)
            game.remove(t)

    hdr = ("t_s", "fps", "cpu", "prek", "bin", "hand", "rend", "post", "tfu", "bo", "flip", "rep",
           "lock", "cl/f", "gap", "sum/ioc", "cg/wall")
    print("   per-frame ms (all complete windows; * = gameplay window used):")
    print("   " + " ".join(f"{h:>7}" for h in hdr))
    for t in complete:
        a, cl, x = wins[t]["a"], wins[t]["cl"], wins[t]["x"]
        fr = a["fr"]
        prek = cl["pre"] + cl["tlb"] + cl["l2t"] + cl["fixa"]
        bo = x["create"] + x["close"] + x["mmap"] + x["other"] + x["unl"]
        row = (t / 1000.0, fps(t), ms(a["cpu"], fr), ms(prek, fr), ms(cl["bin"], fr), ms(cl["hand"], fr),
               ms(cl["rend"], fr), ms(cl["post"], fr), ms(x["tfu"], fr), ms(bo, fr), ms(a["flip"], fr),
               ms(a["rep"], fr), ms(a["lock"], fr), cl["n"] / fr if fr else 0.0,
               a["gap"] / a["gapn"] / 1000.0 if a["gapn"] else 0.0,
               cl["sum"] / cl["ioc"] if cl["ioc"] else 0.0, a["cg"] / a["wall"] if a["wall"] else 0.0)
        mark = "*" if t in game else ("x" if t in excl else " ")
        print(f"  {mark}" + " ".join(f"{v:7.2f}" for v in row))
    if excl:
        print(f"   excluded (wedge / spin-cap / TIMEOUT): {len(excl)} window(s) at t={[e/1000 for e in excl]}")
    if not game:
        print("   NO gameplay windows -- race never reached, or too short. No verdict.")
        return

    S = {k: 0 for k in ("fr", "wall", "cpu", "pre", "tlb", "l2t", "fixa", "bin", "hand", "rend", "post",
                        "tfupre", "tfuspin", "tfudiag", "tfupost", "csdpre", "csdspin", "csdpost", "bo",
                        "flip", "rep", "lock", "cln", "cg", "clsum", "clioc", "gap", "gapn")}
    for t in game:
        a, cl, x = wins[t]["a"], wins[t]["cl"], wins[t]["x"]
        S["fr"] += a["fr"]; S["wall"] += a["wall"]; S["cpu"] += a["cpu"]; S["cg"] += a["cg"]
        S["flip"] += a["flip"]; S["rep"] += a["rep"]; S["lock"] += a["lock"]
        S["gap"] += a["gap"]; S["gapn"] += a["gapn"]
        for k in ("pre", "tlb", "l2t", "fixa", "bin", "hand", "rend", "post"):
            S[k] += cl[k]
        S["cln"] += cl["n"]; S["clsum"] += cl["sum"]; S["clioc"] += cl["ioc"]
        for k in ("tfupre", "tfuspin", "tfudiag", "tfupost", "csdpre", "csdspin", "csdpost"):
            S[k] += x[k]
        S["bo"] += x["create"] + x["close"] + x["mmap"] + x["other"] + x["unl"]
    fr = S["fr"]
    F = S["wall"] / fr
    G = (S["bin"] + S["rend"] + S["tfuspin"] + S["csdspin"]) / fr
    M = (S["pre"] + S["tlb"] + S["l2t"] + S["fixa"] + S["hand"] + S["post"] + S["tfupre"] + S["tfupost"]
         + S["tfudiag"] + S["csdpre"] + S["csdpost"]) / fr
    C = S["cpu"] / fr
    B = S["bo"] / fr
    P = (S["flip"] + S["lock"]) / fr
    R = S["rep"] / fr

    def pc(v):
        return f"{v / 1000:7.2f} ms  {100 * v / F:5.1f} %"
    print(f"\n   GAMEPLAY: {len(game)} windows, {fr} frames, {fr * 1e6 / S['wall']:.2f} fps, "
          f"{S['cln'] / fr:.1f} CL submits/frame, mean inter-GPU-job gap "
          f"{(S['gap'] / S['gapn'] / 1000 if S['gapn'] else 0):.2f} ms")
    print(f"   frame                   {pc(F * 1.0)}")
    print(f"   G  GPU spins            {pc(G)}   (bin {S['bin'] / fr / 1000:.2f}, render {S['rend'] / fr / 1000:.2f}, "
          f"tfu {S['tfuspin'] / fr / 1000:.2f}, csd {S['csdspin'] / fr / 1000:.2f})")
    print(f"   M  cache/TLB maint.     {pc(M)}   (pre {S['pre'] / fr / 1000:.2f}, tlb {S['tlb'] / fr / 1000:.2f}, "
          f"l2t {S['l2t'] / fr / 1000:.2f}, fixA {S['fixa'] / fr / 1000:.2f}, hand {S['hand'] / fr / 1000:.2f}, "
          f"post {S['post'] / fr / 1000:.2f}, tfu/csd {(S['tfupre'] + S['tfupost'] + S['tfudiag'] + S['csdpre'] + S['csdpost']) / fr / 1000:.2f})")
    print(f"   C  CPU outside winsys   {pc(C)}")
    print(f"   B  BO/other ioctls      {pc(B)}")
    print(f"   P  flip + lock wait     {pc(P)}")
    print(f"   R  instrument prints    {pc(R)}")
    print("   checks: CL phase-sum/ioctl = %.3f (want 0.98..1.00); cg/counter wall = %.4f (want 0.99..1.01); "
          "R/frame = %.1f %% (want < 2)" % (S["clsum"] / S["clioc"] if S["clioc"] else 0,
                                             S["cg"] / S["wall"], 100 * R / F))

    # Upper-bound models (see the E2 doc, section "What each outcome means"). [inferred]
    cpu_side = C + B + P
    F1 = max(cpu_side, G + M)
    Gbr = G - min(S["bin"], S["rend"]) / fr
    F2 = max(cpu_side, Gbr + M)
    F3 = F - (S["fixa"] + S["tlb"]) / fr
    print("   models (UPPER bounds on fps gain, not predictions):")
    print(f"     U1 async submit, CPU||GPU overlap        : frame >= max(C+B+P, G+M) = {F1 / 1000:.2f} ms -> x{F / F1:.2f}")
    print(f"     U2 U1 + bin(N+1)||render(N) (CT0||CT1)   : frame >= max(C+B+P, G-min(bin,rend)+M) = {F2 / 1000:.2f} ms -> x{F / F2:.2f}")
    print(f"     U3 sync, drop fix-A + per-submit TLB flush: frame >= F-fixA-tlb = {F3 / 1000:.2f} ms -> x{F / F3:.2f}")
    share = {"G": G / F, "M": M / F, "C": C / F}
    if share["G"] >= 0.5 and share["C"] <= 0.25:
        v = ("GPU-BOUND: bin/render execution dominates; CPU overlap (U1) is capped by C -- the M1 lever is "
             "CT0||CT1 overlap (U2), in proportion to how much of G is bin.")
    elif share["M"] >= 0.25:
        v = "MAINTENANCE-BOUND: the waited flushes/TLB clears are the lever -- cache strategy (U3, E10), no M1 needed."
    elif share["C"] >= 0.5:
        v = "CPU-BOUND: the application/Mesa CPU side dominates; M1 will not help STK much."
    elif share["G"] + share["M"] >= 0.5 and share["C"] >= 0.25:
        v = ("SERIAL MIX: CPU and GPU are comparable and strictly serial; async submit (U1) is the lever, "
             "and U2 on top where bin is a substantial part of G.")
    else:
        v = "UNCLASSIFIED: read the components; no pre-registered branch applies."
    print(f"   VERDICT (pre-registered thresholds): G={share['G']:.0%} M={share['M']:.0%} C={share['C']:.0%} -> {v}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for p in sys.argv[1:]:
        summarize(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())

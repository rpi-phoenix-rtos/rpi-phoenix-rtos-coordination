#!/usr/bin/env python3
"""E2b: summarize the V3D performance-counter lines (`v3d-winsys: pctr-*`) of UART logs.

The analysis is PRE-REGISTERED in docs/gpu-new-lane/E2b-v3d-render-slowness.md (section
"Pre-registered measurement plan"); this script is that plan in executable form, written
before the first Pi run.

Lines (one set per 5 s window, all sharing t= with the E2 subprof-* lines of that window):
  pctr-w   t= fr= set= n= fail= bin= rend= pct= ovf= clk_arm= clk_core= clk_v3d= core_cfg=
  pctr-b   t= fr= set= v=<32 bin-phase sums>
  pctr-r   t= fr= set= v=<32 render-phase sums>
  pctr-j   t= fr= set= s=<slot> n= fail= bin= rend= bcl= rcl= notes= draws= note=<24 words>
  pctr-jsK t= fr= set= v=<32 render-phase sums of slot K>        (every Nth window only)
Every pctr-w/-b/-r/-j/-jsK line ends in ck=<FNV-1a 32 of the text from "pctr-" to the
space before " ck=">; a line whose checksum does not match is DROPPED and counted (the UART
corrupts ~1.3 % of lines, more of long ones, and a flipped digit would pass a format check).
plus once: pctr-ident, pctr-clk, "V3D_PHX_EZ knob", "pctr QRMAXCNT override",
"v3d-job-note:", and v3d-res-census lines from Mesa.

Gameplay windows are chosen exactly as in e2-summarize.py (fps > 3, contiguous race run,
first and last dropped, windows with a wedge / spin-cap exit / TIMEOUT line excluded).

Usage: e2b-summarize.py <uart.log> [more.log ...]
Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import re
import sys

RACE_FPS = 3.0
NQPU = 8                    # CORE0_IDENT1 0x81001422: 2 slices x 4 QPUs (checked vs pctr-ident)
V3D_HZ = 500e6

SET_A = ["cyc", "q_idle", "q_act_vtx", "q_act_frag", "q_valid", "q_wait_tmu", "q_wait_sb",
         "q_wait_vary", "q_stall_vtx", "q_stall_frag", "ic_hit", "ic_miss", "uc_hit", "uc_miss",
         "tmu_quads", "tmu_miss", "tmu_mru", "tmu_cfg", "tmu_active", "tmu_stalled",
         "cle_bin", "cle_rend", "l2t_hit", "l2t_miss", "vdw_stall", "vcd_stall",
         "fep_prim_nopix", "fep_prim_pix", "fep_ez_clip", "fep_quads", "tlb_q_failz",
         "tlb_q_passz"]
SET_B = ["cyc", "tlb_q_wr", "tlb_q_nz", "tlb_q_zero", "tlb_q_partial", "ptb_prims",
         "core_wr", "core_rd", "l2t_wr", "l2t_rd", "ptb_wr", "ptb_rd", "tlb_wr", "tlb_rd",
         "pse_rd", "gmp_rd", "ptb_words_wr", "tlb_words_wr", "pse_words_rd", "tlb_words_rd",
         "l2t_noid_stall", "l2t_cmdq_stall", "l2t_tmu_rd", "l2t_tmu_rd_miss", "l2t_cle_rd",
         "l2t_cle_rd_miss", "l2t_vcd_rd", "l2t_vcd_rd_miss", "l2t_tmucfg_rd", "l2t_slc0_rd",
         "l2t_slc0_rd_miss", "l2t_tmu_wr"]
TILING = ["RASTER", "LT", "UB1", "UB2", "UIF", "UIFX"]
EZ = ["undec", "GT_GE", "LT_LE", "OFF"]

RE_A = re.compile(r"subprof-a t=(\d+)ms fr=(\d+) wall=(\d+) ")
RE_CL = re.compile(r"subprof-cl t=(\d+)ms .* rend=(\d+) post=\d+ oom=\d+ wedge=(\d+) l2tto=(\d+) "
                   r"tlbto=(\d+)\s*$")
RE_CK = re.compile(r"(pctr-\S+ .*) ck=([0-9a-f]{8})\s*$")
RE_W = re.compile(r"pctr-w t=(\d+)ms fr=(\d+) set=([AB]) n=(\d+) fail=(\d+) bin=(\d+) rend=(\d+) "
                  r"pct=(\d+) ovf=0x([0-9a-f]+) clk_arm=(\d+) clk_core=(\d+) clk_v3d=(\d+) "
                  r"core_cfg=(\d+)\s*$")
RE_V = re.compile(r"pctr-(b|r|js\d+) t=(\d+)ms fr=(\d+) set=([AB]) v=([\d,]+)\s*$")
RE_J = re.compile(r"pctr-j t=(\d+)ms fr=(\d+) set=([AB]) s=(\d+) n=(\d+) fail=(\d+) bin=(\d+) "
                  r"rend=(\d+) bcl=(\d+) rcl=(\d+) notes=(\d+) draws=(\d+) note=([\d,]+)\s*$")
RE_BAD = re.compile(r"TIMEOUT|GPU wedged|DROPPED job")
RE_ONCE = re.compile(r"(pctr-ident .*|pctr-clk .*|V3D_PHX_EZ knob: .*|pctr QRMAXCNT override .*|"
                     r"v3d-job-note: .*|stk-\w+: DATADIR=.*|SUBMIT PROFILE build.*)$")
RE_CENSUS = re.compile(r"v3d-res-census:( b=.*)$")
RE_NEWRES = re.compile(r"v3d-res-census: new (.*)$")


def vals(s, n=32):
    v = [int(x) for x in s.split(",") if x != ""]
    return v if len(v) == n else None


def fnv1a(text):
    h = 2166136261
    for b in text.encode("utf-8", "surrogateescape"):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def parse(path):
    W = {}          # t -> dict
    once, census, newres, bad_t = [], None, [], set()
    last_t = 0
    stats = {"seen": 0, "ck_bad": 0, "parsed": 0}
    with open(path, "rb") as f:
        for raw in f:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if RE_BAD.search(line):
                bad_t.add(last_t)
            m = RE_ONCE.search(line)
            if m and len(once) < 40:
                once.append(m.group(1))
            m = RE_CENSUS.search(line)
            if m:
                census = m.group(1)
            m = RE_NEWRES.search(line)
            if m:
                newres.append(m.group(1))
            if "subprof-a" in line:
                m = RE_A.search(line)
                if m:
                    t = int(m.group(1))
                    W.setdefault(t, {})["a"] = (int(m.group(2)), int(m.group(3)))
                    last_t = max(last_t, t)
            elif "subprof-cl" in line:
                m = RE_CL.search(line)
                if m:
                    W.setdefault(int(m.group(1)), {})["cl"] = tuple(int(x) for x in m.groups()[1:])
            elif re.search(r"pctr-(w|b|r|j|js\d+) t=", line):
                stats["seen"] += 1
                mc = RE_CK.search(line)
                if not mc or fnv1a(mc.group(1)) != int(mc.group(2), 16):
                    stats["ck_bad"] += 1
                    continue
                line = mc.group(1)
                stats["parsed"] += 1
            if "pctr-w " in line and "ck=" not in line:
                m = RE_W.search(line)
                if m:
                    g = m.groups()
                    W.setdefault(int(g[0]), {})["w"] = {
                        "fr": int(g[1]), "set": g[2], "n": int(g[3]), "fail": int(g[4]),
                        "bin": int(g[5]), "rend": int(g[6]), "pct": int(g[7]), "ovf": int(g[8], 16),
                        "arm": int(g[9]), "core": int(g[10]), "v3d": int(g[11]), "corecfg": int(g[12])}
            elif "pctr-j " in line and "ck=" not in line:
                m = RE_J.search(line)
                if m:
                    g = m.groups()
                    note = vals(g[12], 24)
                    W.setdefault(int(g[0]), {}).setdefault("j", {})[int(g[3])] = {
                        "set": g[2], "n": int(g[4]), "fail": int(g[5]), "bin": int(g[6]),
                        "rend": int(g[7]), "bcl": int(g[8]), "rcl": int(g[9]), "notes": int(g[10]),
                        "draws": int(g[11]), "note": note}
            elif re.search(r"pctr-(b|r|js\d+) t=", line) and "ck=" not in line:
                m = RE_V.search(line)
                if m:
                    v = vals(m.group(5))
                    if v is not None:
                        W.setdefault(int(m.group(2)), {})[m.group(1)] = (m.group(4), v)
    ts = sorted(W)
    bad = set()
    for b in bad_t:
        nxt = [t for t in ts if t > b]
        if nxt:
            bad.add(nxt[0])
    return W, once, census, newres, bad, stats


def gameplay(W, bad):
    comp = [t for t in sorted(W) if "a" in W[t] and "cl" in W[t]]

    def fps(t):
        fr, wall = W[t]["a"]
        return fr * 1e6 / wall if wall else 0.0
    race = [t for t in comp if fps(t) > RACE_FPS]
    if not race:
        return [], [], fps
    i0 = comp.index(race[0])
    run = []
    for t in comp[i0:]:
        if fps(t) > RACE_FPS:
            run.append(t)
        else:
            break
    game, excl = run[1:-1], []
    for t in list(game):
        cl = W[t]["cl"]      # (rend, wedge, l2tto, tlbto)
        if cl[1] or cl[2] or cl[3] or t in bad or ("w" in W[t] and W[t]["w"]["fail"]):
            excl.append(t)
            game.remove(t)
    return game, excl, fps


def masks(b):
    s = ("Z" if b & 1 else "") + ("S" if b & 2 else "")
    s += "".join(f"C{i}" for i in range(8) if b & (4 << i))
    return s or "-"


def note_str(n):
    if not n or n[0] != 1:
        return "(no Mesa note: build without STKPROF_MESA?)"
    cb = n[19]
    zs = n[18]
    return (f"{n[1]}x{n[2]} tiles {n[3]}x{n[4]}@{n[5]}x{n[6]} rt={n[7]} bpp={n[8]} "
            f"msaa={n[10]} dbuf={n[11]} clr={masks(n[12])} load={masks(n[13])} "
            f"store={masks(n[14])} inval={masks(n[15])} ez={EZ[n[16]] if n[16] < 4 else n[16]} "
            f"cb0={TILING[cb & 0xff] if (cb & 0xff) < 6 else cb & 0xff}"
            f"{'/scanout' if cb & 0x100 else ''}/cpp{cb >> 16} "
            f"zs={'-' if not zs & 1 else (TILING[(zs >> 8) & 0xff] + '/cpp' + str(zs >> 16))}")


def pct(a, b):
    return 100.0 * a / b if b else float("nan")


def metrics_a(v, rend_us):
    d = dict(zip(SET_A, v))
    q = NQPU * d["cyc"] if d["cyc"] else 0
    out = {
        "clk_MHz": d["cyc"] / rend_us if rend_us else float("nan"),
        "qpu_idle%": pct(d["q_idle"], q),
        "frag_act%": pct(d["q_act_frag"], q),
        "vtx_act%": pct(d["q_act_vtx"], q),
        "valid_instr%": pct(d["q_valid"], q),
        "wait_tmu%": pct(d["q_wait_tmu"], q),
        "wait_sb%": pct(d["q_wait_sb"], q),
        "wait_vary%": pct(d["q_wait_vary"], q),
        "stall_frag%": pct(d["q_stall_frag"], q),
        "tmu_miss%": pct(d["tmu_miss"], d["tmu_quads"]),
        "tmu_stall/act%": pct(d["tmu_stalled"], d["tmu_active"]),
        "l2t_hit%": pct(d["l2t_hit"], d["l2t_hit"] + d["l2t_miss"]),
        "cle_rend%": pct(d["cle_rend"], d["cyc"]),
        "late_z_reject%": pct(d["tlb_q_failz"], d["tlb_q_failz"] + d["tlb_q_passz"]),
        "ez_clip%": pct(d["fep_ez_clip"], d["fep_quads"]),
        "icache_miss%": pct(d["ic_miss"], d["ic_hit"] + d["ic_miss"]),
    }
    return d, out


def metrics_b(v, rend_us):
    d = dict(zip(SET_B, v))
    out = {
        "clk_MHz": d["cyc"] / rend_us if rend_us else float("nan"),
        "tlb_words_wr": d["tlb_words_wr"], "tlb_words_rd": d["tlb_words_rd"],
        "core_rd": d["core_rd"], "core_wr": d["core_wr"],
        "l2t_rd": d["l2t_rd"], "l2t_wr": d["l2t_wr"], "tlb_rd": d["tlb_rd"], "tlb_wr": d["tlb_wr"],
        "ptb_rd": d["ptb_rd"], "pse_rd": d["pse_rd"], "gmp_rd": d["gmp_rd"],
        "l2t_tmu_miss%": pct(d["l2t_tmu_rd_miss"], d["l2t_tmu_rd"]),
        "l2t_cle_miss%": pct(d["l2t_cle_rd_miss"], d["l2t_cle_rd"]),
        "l2t_stall_noid%": pct(d["l2t_noid_stall"], d["cyc"]),
        "l2t_stall_cmdq%": pct(d["l2t_cmdq_stall"], d["cyc"]),
    }
    return d, out


def fmt(m):
    return " ".join(f"{k}={v:.1f}" if isinstance(v, float) else f"{k}={v}" for k, v in m.items())


def summarize(path):
    W, once, census, newres, bad, stats = parse(path)
    print(f"== {path}")
    print(f"   counter lines: {stats['seen']} seen, {stats['parsed']} checksum-ok, "
          f"{stats['ck_bad']} DROPPED (checksum mismatch or missing = UART corruption)")
    for o in once:
        print(f"   | {o}")
    arm_ok = any("SUBMIT PROFILE build" in o for o in once) and any("DATADIR=" in o for o in once)
    if not arm_ok:
        print("   ********** ARM NOT ASSERTED (no winsys banner or no launcher line) -- do not grade **********")
    if not any("pctr-ident" in o for o in once):
        print("   ********** no pctr-ident line: not an E2b build, or V3D_PCTR=0 **********")
    game, excl, fps = gameplay(W, bad)
    print(f"   windows: {len(W)} total, {len(game)} gameplay, {len(excl)} excluded (wedge/TIMEOUT/fail)")
    if census:
        print(f"   census (created; per class b/t/r/d = count/KiB for {','.join(TILING)}):{census}")
    rt = [r for r in newres if r.startswith(("r ", "d "))]
    print(f"   render targets / depth buffers created: {len(rt)}; non-UIF ones:")
    for r in rt:
        m = re.search(r"tiling=(\d+)", r)
        if m and int(m.group(1)) < 4:
            print(f"     {r}")
    if not game:
        print("   NO gameplay windows -- no verdict.")
        return
    clk = [W[t]["w"] for t in game if "w" in W[t]]
    if clk:
        print("   clocks (measured, MHz, gameplay min..max): arm %d..%d core %d..%d v3d %d..%d; core cfg %d"
              % (min(c["arm"] for c in clk) / 1e6, max(c["arm"] for c in clk) / 1e6,
                 min(c["core"] for c in clk) / 1e6, max(c["core"] for c in clk) / 1e6,
                 min(c["v3d"] for c in clk) / 1e6, max(c["v3d"] for c in clk) / 1e6,
                 clk[-1]["corecfg"] / 1e6))
        if any(c["ovf"] for c in clk):
            print("   ********** counter OVERFLOW latched in some window: those sums are low **********")
    res = {}
    for S in ("A", "B"):
        ws = [t for t in game if "w" in W[t] and W[t]["w"]["set"] == S and "r" in W[t] and "b" in W[t]]
        if not ws:
            continue
        fr = sum(W[t]["w"]["fr"] for t in ws)
        rend = sum(W[t]["w"]["rend"] for t in ws)
        binu = sum(W[t]["w"]["bin"] for t in ws)
        pc = sum(W[t]["w"]["pct"] for t in ws)
        R = [sum(W[t]["r"][1][i] for t in ws) for i in range(32)]
        B = [sum(W[t]["b"][1][i] for t in ws) for i in range(32)]
        print(f"\n   SET {S}: {len(ws)} windows, {fr} frames; render {rend / fr / 1000:.2f} ms/f, "
              f"bin {binu / fr / 1000:.2f} ms/f, counter reads {pc / fr / 1000:.3f} ms/f")
        f = metrics_a if S == "A" else metrics_b
        dR, mR = f(R, rend)
        dB, mB = f(B, binu)
        print(f"   render phase: {fmt(mR)}")
        print(f"   render /frame: " + " ".join(f"{k}={dR[k] / fr:.0f}" for k in (SET_A if S == 'A' else SET_B)))
        print(f"   bin phase:    {fmt(mB)}")
        res[S] = (mR, dR, fr)
        # per slot
        slots = {}
        for t in ws:
            for k, j in W[t].get("j", {}).items():
                sv = W[t].get(f"js{k}")
                if sv is None or j["set"] != S:
                    continue
                a = slots.setdefault(k, {"n": 0, "rend": 0, "bin": 0, "draws": 0, "bcl": 0, "v": [0] * 32,
                                         "note": None})
                if j["notes"] and j["notes"] != j["n"]:
                    a["notes_mismatch"] = a.get("notes_mismatch", 0) + 1
                a["n"] += j["n"]; a["rend"] += j["rend"]; a["bin"] += j["bin"]; a["draws"] += j["draws"]
                a["bcl"] += j["bcl"]
                a["v"] = [x + y for x, y in zip(a["v"], sv[1])]
                if j["note"] and j["note"][0] == 1:
                    a["note"] = j["note"]
        if slots:
            tot = sum(a["rend"] for a in slots.values()) or 1
            print(f"   per slot (windows with slot lines; share of render time):")
            for k in sorted(slots):
                a = slots[k]
                n = a["n"] or 1
                _, m = f(a["v"], a["rend"])
                d = dict(zip(SET_A if S == "A" else SET_B, a["v"]))
                extra = ""
                if S == "A" and a["note"]:
                    px = a["note"][1] * a["note"][2]
                    extra = f" quads/px={4 * d['fep_quads'] / n / px:.2f}" if px else ""
                if S == "B" and a["note"]:
                    px = a["note"][1] * a["note"][2]
                    extra = f" tlb_words_wr/px={d['tlb_words_wr'] / n / px:.3f}" if px else ""
                keys = (("clk_MHz", "qpu_idle%", "frag_act%", "valid_instr%", "wait_tmu%", "tmu_miss%",
                         "late_z_reject%", "ez_clip%") if S == "A" else
                        ("clk_MHz", "l2t_tmu_miss%", "l2t_stall_cmdq%"))
                print(f"     s{k}: {100 * a['rend'] / tot:5.1f}% rend {a['rend'] / n / 1000:6.2f} ms/job "
                      f"bin {a['bin'] / n / 1000:5.2f} draws/job {a['draws'] / n:6.1f} | "
                      + " ".join(f"{kk}={m[kk]:.1f}" for kk in keys) + extra)
                print(f"          {note_str(a['note'])}")
                if a.get("notes_mismatch"):
                    print(f"          ********** notes != jobs in {a['notes_mismatch']} window(s): some job in "
                          "this slot had no Mesa note (a non-v3d_job_submit CL job?) -- slot identity "
                          "may shift for later slots **********")

    # ---- pre-registered verdicts (thresholds fixed in the E2b doc before any run) ----
    print("\n   VERDICTS (pre-registered):")
    if "A" not in res:
        print("   no set-A gameplay windows -- no verdict on the cycle split")
        return
    m = res["A"][0]
    if not (480 <= m["clk_MHz"] <= 520):
        print(f"   V0 FAIL: counter cycles / render wall = {m['clk_MHz']:.0f} MHz (want 480..520): the GPU is not "
              "running (or not at 500 MHz) for the whole render spin -> read H5 (clock/external stall) first")
    else:
        print(f"   V0 ok: {m['clk_MHz']:.0f} MHz over the render spin (GPU clocked and counting throughout)")
    hits = []
    if m["late_z_reject%"] >= 30 and m["ez_clip%"] < 2:
        hits.append(f"H2 OVERDRAW/EZ: {m['late_z_reject%']:.0f}% of quads reach the TLB and fail Z there, "
                    f"{m['ez_clip%']:.1f}% are early-Z clipped -> run the V3D_PHX_EZ=1 arm")
    if m["wait_tmu%"] >= 25 or m["tmu_stall/act%"] >= 50:
        hits.append(f"H1 MEMORY/TMU LATENCY: QPUs wait on the TMU {m['wait_tmu%']:.0f}% of QPU-cycles, TMU stalled "
                    f"{m['tmu_stall/act%']:.0f}% of its active cycles -> the core-clock A/B (config.txt core_freq=500, "
                    "or V3D_PHX_CORE_HZ if the firmware allows it) and the --anisotropic=0 arm")
    if m["valid_instr%"] >= 60:
        hits.append(f"H6 SHADER ALU-BOUND: {m['valid_instr%']:.0f}% of QPU-cycles execute instructions -> the gap is "
                    "workload (shaders/settings), not the port; compare against the same settings on Pi OS")
    if m["qpu_idle%"] >= 50:
        hits.append(f"H3/H4 QPUs STARVED: {m['qpu_idle%']:.0f}% idle -> fixed-function bound (TLB load/store, tile "
                    "lists, CLE) or QPU reservation: read set B (TLB words vs pixels) and run V3D_PHX_QRMAXCNT=3")
    for h in hits or ["none of the pre-registered patterns matched -- read the components, no verdict"]:
        print(f"   {h}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        summarize(p)

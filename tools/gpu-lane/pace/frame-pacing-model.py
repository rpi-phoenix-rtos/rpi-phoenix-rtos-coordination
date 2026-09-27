#!/usr/bin/env python3
#
# Frame-pacing model of the new GPU lane's present path (docs/gpu-new-lane/frame-pacing.md):
# SDL 2.30.12 KMSDRM_GLES_SwapWindow -> Mesa GBM/EGL (DRI swap throttle on the previous frame's
# fence) -> rpi4-v3d-async (one job queue, serial) -> rpi4-kms (one commit in flight per CRTC,
# fence-gated, the fence polled every gate_us, a commit armed later than period - guard_us after
# the last vblank lands one vblank later: kms_main.c kms_try_apply()).
#
# Per frame k: the engine spends C ms on the CPU, then calls SDL_GL_SwapWindow, which
#   stock: waits for flip k-1, then eglSwapBuffers (submits job k), then drmModePageFlip(k);
#   pace:  eglSwapBuffers (submits job k), then waits for flip k-1, then drmModePageFlip(k)
#          (patches-pace/0001).
# Job k needs G ms of GPU after the GPU is free and it is submitted.
#
# Prints fps, the mean time inside SDL_GL_SwapWindow (the gamedrm `swapstat swap_us_avg`), the
# share of flips completing 1/2/3+ vblanks after their commit (KMS `flipstat vbl1/vbl2/vbl3p`)
# and the mean commit->arm time (`q2a_us_avg`). Deterministic (seeded). No Pi, no deps.
#
# Usage: frame-pacing-model.py                 the table in frame-pacing.md
#        frame-pacing-model.py C G [jC jG]     one workload, both orders
#        frame-pacing-model.py --check         assert the model reproduces mig-q2 and kmscube
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

import math
import random
import sys

PERIOD = 1000.0 / 60.0   # ms, 60.000 Hz (KMS srv vblank measured hz=60.000)
GUARD = 2.0              # ms, rpi4-kms guard_us=2000
GATE = 0.5               # ms, rpi4-kms gate_us=500 (fence poll once a commit is waiting)
SUBMIT = 0.3             # ms, eglSwapBuffers flush + SUBMIT_CL round trip [assumed]


def run(order, cpu, gpu, jcpu=0.0, jgpu=0.0, n=4000, seed=2):
    rnd = random.Random(seed)
    t = 0.0          # engine starts frame k
    fence_prev = 0.0  # job k-1 done
    flip_prev = 0.0   # flip k-1 on screen (its event)
    gpu_free = 0.0
    rows = []
    for _ in range(n):
        c = max(1.0, rnd.gauss(cpu, jcpu))
        g = max(1.0, rnd.gauss(gpu, jgpu))
        swap_call = t + c
        submit = (max(swap_call, flip_prev) if order == "stock" else swap_call) + SUBMIT
        fence = max(submit, gpu_free) + g
        gpu_free = fence
        # DRI throttle waits for job k-1; the pace order then waits for flip k-1.
        commit = max(submit, fence_prev) if order == "stock" else max(submit, fence_prev, flip_prev)
        arm = commit if fence <= commit else math.ceil(fence / GATE) * GATE
        j = math.floor(arm / PERIOD)
        target = (j + 1) if (arm - j * PERIOD) + GUARD < PERIOD else (j + 2)
        shown = max(target * PERIOD, flip_prev + PERIOD)
        vbl = round(shown / PERIOD) - math.floor(commit / PERIOD)
        rows.append((shown, commit - swap_call, vbl, arm - commit))
        flip_prev, fence_prev, t = shown, fence, commit
    rows = rows[n // 4:]
    fps = (len(rows) - 1) / ((rows[-1][0] - rows[0][0]) / 1000.0)
    swap = sum(r[1] for r in rows) / len(rows)
    vbl = {k: 0 for k in (1, 2, 3)}
    for r in rows:
        vbl[min(max(r[2], 1), 3)] += 1
    q2a = sum(r[3] for r in rows) / len(rows)
    return fps, swap, {k: round(100.0 * v / len(rows)) for k, v in vbl.items()}, q2a


def line(name, order, *wl):
    fps, swap, vbl, q2a = run(order, *wl)
    print(f"{name:26s} {order:5s} fps={fps:5.1f} swap_ms={swap:5.1f} "
          f"vbl1/2/3+={vbl[1]:3d}/{vbl[2]:3d}/{vbl[3]:3d}% q2a_ms={q2a:5.1f}")
    return fps, swap


TABLE = [
    ("quake2 mig-q2 (8.8, 14.8)", 8.8, 14.8, 0.0, 0.0),
    ("quake2, GPU +10%", 8.8, 16.3, 0.5, 0.5),
    ("quake2, GPU +20%", 8.8, 17.8, 0.5, 0.5),
    ("quake2, CPU 15 ms", 15.0, 14.8, 0.5, 0.5),
    ("quakespasm (10, 19.5+-3)", 10.0, 19.5, 1.5, 3.0),
    ("quakespasm light (10, 15+-3)", 10.0, 15.0, 1.5, 3.0),
    ("GPU-bound 25 ms", 8.8, 25.0, 1.0, 2.0),
    ("kmscube-like (1.5, 1.9)", 1.5, 1.9, 0.2, 0.2),
]


def main(argv):
    if argv[1:2] == ["--check"]:
        fps, swap = run("stock", 8.8, 14.8)[:2]
        assert abs(fps - 30.0) < 0.05 and abs(swap - 24.5) < 0.3, (fps, swap)   # mig-q2: 30.00, 24.5 ms
        assert abs(run("stock", 1.5, 1.9)[0] - 60.0) < 0.05                     # kmscube: 60.00
        assert abs(run("pace", 8.8, 14.8)[0] - 60.0) < 0.05
        print("model check OK: stock reproduces mig-q2 (30.00 fps, swap 24.5 ms) and kmscube (60.00)")
        return 0
    if len(argv) >= 3:
        wl = [float(a) for a in argv[1:5]]
        for order in ("stock", "pace"):
            line("workload", order, *wl)
        return 0
    for name, *wl in TABLE:
        for order in ("stock", "pace"):
            line(name, order, *wl)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Count frame REVERSALS in a recorded clip: an older frame shown again after a newer one.

A reversal is the capture sequence A, B, A: frame k is much closer to frame k-2 than to
frame k-1 while the picture is moving. Presentation-order bugs (a stale buffer shown after a
newer one) produce exactly that; a slow but ordered stream does not.

usage: count-frame-reversals.py <clip.mp4> <start_s> <dur_s> <w:h:x:y crop> [--move T]

  --move T   minimum mean abs difference (0-255 grey, on a 192x108 downscale) for a frame to count
             as "moving" (default 1.5; use ~0.3 for scenes with little motion)

Prints one tagged line: REVERSALS clip=... frames=N moving=M reversals=R pct=P
Validated 2026-09-30 on the windowed SuperTuxKart clip against the race timer read by OCR
(timer backwards in 447 of 1800 capture frames; this detector: 355).
"""
import subprocess
import sys

import numpy as np


def main():
	args = sys.argv[1:]
	move = 1.5
	if "--move" in args:
		i = args.index("--move")
		move = float(args[i + 1])
		del args[i:i + 2]
	if len(args) != 4:
		sys.exit(__doc__)
	clip, ss, dur, crop = args
	w, h = 192, 108
	cmd = ["ffmpeg", "-v", "error", "-ss", ss, "-t", dur, "-i", clip,
		"-vf", f"crop={crop},scale={w}:{h},format=gray", "-f", "rawvideo", "-"]
	raw = subprocess.run(cmd, capture_output=True, check=True).stdout
	f = np.frombuffer(raw, np.uint8).reshape(-1, h, w).astype(np.int16)
	d1 = np.array([np.abs(f[k] - f[k - 1]).mean() for k in range(2, len(f))])
	d2 = np.array([np.abs(f[k] - f[k - 2]).mean() for k in range(2, len(f))])
	moving = d1 > move
	rev = moving & (d2 < 0.5 * d1)
	pct = 100.0 * rev.sum() / max(1, moving.sum())
	print(f"REVERSALS clip={clip.split('/')[-1]} ss={ss} dur={dur} crop={crop} frames={len(f)} "
		f"moving={moving.sum()} reversals={rev.sum()} pct={pct:.1f}")


if __name__ == "__main__":
	main()

# W37 image lineage (archived from the weekly log)


## Delta of image 486acda5 vs 0ddcee67 (superseded by 23f46860)

**What changed vs the 21-boot-gated image** (`0ddcee67…`, now superseded at that path):
**exactly 4 artifacts**, each HW-verified, and the image's copies are byte-identical to the
tested ones — `Xphoenix-glamor-daemon` (the #4 fix; 4 trials, 0 faults, desktop
pixel-verified), the xlaunch launcher ×3 copies (adds the test-only `--quit-after`), and
rebuilt `python3` + `micropython` (both verified on HW this session). **All other 183 files in
`/bin`+`/sbin` are byte-identical** — same file sets, no additions or removals — so the five
game engines and their data keep their existing gate.


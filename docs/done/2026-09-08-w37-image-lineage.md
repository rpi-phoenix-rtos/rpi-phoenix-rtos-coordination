# W37 image lineage (archived from the weekly log)


## Delta of image 486acda5 vs 0ddcee67 (superseded by 23f46860)

**What changed vs the 21-boot-gated image** (`0ddcee67…`, now superseded at that path):
**exactly 4 artifacts**, each HW-verified, and the image's copies are byte-identical to the
tested ones — `Xphoenix-glamor-daemon` (the #4 fix; 4 trials, 0 faults, desktop
pixel-verified), the xlaunch launcher ×3 copies (adds the test-only `--quit-after`), and
rebuilt `python3` + `micropython` (both verified on HW this session). **All other 183 files in
`/bin`+`/sbin` are byte-identical** — same file sets, no additions or removals — so the five
game engines and their data keep their existing gate.



## §1b as it stood in the weekly log

## 1b. ✅ DONE (owner, 2026-09-08): full-project change study for Phoenix-RTOS maintainers

**Deliverable: [`docs/PHOENIX-RTOS-RPI4-CHANGES.md`](../PHOENIX-RTOS-RPI4-CHANGES.md)** (885 ln).
Opens with a 14-row ★ shortlist of defects in *upstream's* code that this port found, then four
stand-alone sections (kernel+plo+posixsrv · libphoenix+corelibs+tests · devices+USB+net+fs ·
ports+build), then caveats/open defects/licensing and a repo map. Scope stated honestly: ~1 540
commits / ~890 files / ~150k raw insertions, **minus** libphoenix `math/`+`libm/` (+28 472,
upstream's vendored libmcs) and `vkquake_shaders.c` (+30 506, generated SPIR-V) = **~90k
authored**. Built by four parallel subagents whose claims I spot-verified rather than trusted.

★ Best find, invisible to any single agent: kernel `5d8645f6` (pmap) and `usb/mem.c` `12c4fe8`
are **two instances of one class** — nothing retires cache lines when a cached page becomes
uncached DMA memory. Measured ~12 corrupt GPU control lists per 8 boots -> 0.

Reviewed for cross-section conflicts (`83b8579b3`): every multiply-cited commit stays inside one
section; the one real trap was the ports section listing the pthread 4 KiB stack / missing guard
page as an open gap while the shortlist calls it fixed — now cross-referenced both ways.

Method notes archived to `docs/done/2026-09-08-fork-vs-upstream-study.md`.


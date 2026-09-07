# Fork-vs-upstream change study (owner request, 2026-09-08)

Working notes for the study that produced `docs/PHOENIX-RTOS-RPI4-CHANGES.md`.
Kept for the method (how the scope was measured and how the agents were checked),
not because the conclusions live here — they live in the report.

## Brief

Deep study comparing **our forks vs upstream**, output a detailed **human-readable Markdown**
categorising everything we changed, for an audience of **original Phoenix-RTOS maintainers** —
so they can see what is here and what they might find useful. Categories the owner named:
general bug fixes · Pi4-specific porting (changes to existing Phoenix code) · Pi4 on-board
device drivers (new hardware support) · application ports · game ports · general
performance/stability/compatibility fixes · testing improvements. Cite commit IDs, files and
directories per repo where it helps, but **readability comes first**.

Owner: "Don't stop current work on pending tasks. Schedule this study and research step AFTER
current bug fixing effort." **STARTED 2026-09-08** (the X teardown fix was stopped and reverted;
the STK "link regression" was a false alarm — both closed out).

**Scope measured — and the raw total is misleading, so both figures go in the report.**
~**1 540 commits**, ~**890 files**, ~**150 000 raw insertions** vs `origin/master`. But ~59k of
that is **not fork-authored**:
- libphoenix `math/`+`libm/` = **+28 472** — upstream's own vendored libmcs and the math reorg,
  carried ahead of master (verified: 260 of its 338 files). Fork-authored libphoenix is
  **93 files, +5 874**.
- `vkquake/glue/vkquake_shaders.c` = **+30 506** — machine-generated SPIR-V bytecode as C arrays.

So the honest hand-written figure is **~90k insertions**, and the report will state raw vs
authored with the exclusions named rather than quote the flattering number. Spot-checked the
other big surface and it is genuine: `phoenix-rtos-devices` top dirs are all new Pi 4 drivers
(V3D 11.7k, WiFi 5.1k, xHCI 5.0k, pl011-tty 4.5k, EMMC2 4.1k).

**Four subagents running in parallel**, one per theme, each writing its section to
`docs/misc/study-section-*.md` and returning only a summary (so the synthesis stays tractable):
kernel+plo+posixsrv · libphoenix+corelibs+tests · devices+usb+lwip+filesystems ·
ports+project+build. Each is told to categorise per the owner's taxonomy, cite SHAs/paths,
mark items valuable **beyond** the Pi 4 with ★, and state completeness honestly rather than
inflate. I assemble them into the final document.

**2 of 4 sections landed** (`docs/misc/study-section-libc.md` 240 ln,
`study-section-drivers.md` 233 ln). Spot-checked their claims rather than trusting them:
upstream `phoenix-rtos-devices` genuinely has **no** `audio/ bt/ gpu/ misc/ video/ wifi/`
directories, and `usb/mem.c 12c4fe8` is a real 18-line `dc civac` fix. **16 new drivers
confirmed** by `--name-status A`, from the VL805 xHCI HCD and V3D 4.2 GPU stack down to HWRNG
and GPIO.

**3 of 4 landed** (+`study-section-kernel.md` 140 ln). It corrected my brief — posixsrv has
**2** commits in range, not the 3 I stated. Verified its two strongest claims: `6d8f40a5` does
carry the `execkstack` fix whose comment states it *"also silently disabled the kernel-stack
canary check in _threads_schedule"*, and `5d8645f6` is a real 21-line `hal/aarch64/pmap.c`
change retiring stale dirty lines on a NON-CACHED remap.

**★ Cross-section finding neither agent could see alone:** the kernel agent's `5d8645f6`
(pmap: retire dirty lines when remapping uncached) and the drivers agent's `12c4fe8`
(`usb/mem.c`: `dc civac` on recycled DMA pages) are **two instances of one class** — nothing in
Phoenix retires cache lines when a previously-cached page becomes uncached DMA memory. The
kernel one has the cleanest measurement in the whole study: **~12 corrupt GPU control lists per
8 boots -> 0**. The report will present them together.

The drivers agent ranked its top-5 by *"does this patch code upstream already ships?"* rather
than by size — so cross-target fixes (uncached-DMA cache maintenance, USB TRSTRCY + enum-failure
bounding, ext2 allocator locking, `FIONREAD` on every tty, three socket-layer defects) outrank
the larger but Pi-only new capabilities. That is the right axis for this audience.

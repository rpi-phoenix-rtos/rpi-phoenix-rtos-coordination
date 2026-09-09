# W37 detail snapshot 3 — 2026-09-10
Moved out of `docs/inprogress/WEEK-2026-W37.md` to keep that file short.
Everything here is SETTLED; nothing below needs a decision.

## 0. YOUR TWO REQUESTS — both done

- **✅ Your iPhone clip is in the reel.** ★ **WATCH:**
  `artifacts/hdmi-video/20260909-161316-phoenix-rtos-rpi4-showcase.mp4` (247 s, 11 segments).
  `IMG_8331.MOV` transcoded to the H.265 subset rpivid is verified bit-exact on and playing on
  hardware at **21.7 fps, 0 faults**, upright, full-screen — your recording of the Pi, decoded by the
  Pi. Re-encoded rather than stream-copied on purpose (Apple may use tiles/AMP/deblock offsets our
  decoder rejects, and there is no way to tell from outside). The X11 segment now comes from the
  **shipped image's** binaries and grades clean on all three artefacts you reported.
  ⓘ That segment's fps is a **framebuffer-bandwidth** number, not a decoder one: 41.5 ms of each
  46 ms frame is the blit to `/dev/fb0`; decode is ~4.5 ms.
- **✅ STK fps: 6 → 9 avg (~1.5×), shipped.** The deferred pipeline renders at 0.75 scale and
  upscales; the 1080p HUD stays crisp.

  | `scale_rtts_factor` | deferred RTT | FPS min/avg/max | picture |
  |---|---|---|---|
  | 1.0 (was default) | 1920×1080 | **5/6/6** | correct |
  | **0.75 (shipped)** | 1440×810 | **8/9/9** | **correct** |
  | 0.5 | 960×540 | **10/13/15** | ⚠ 3D scene **upside down** |

  **⛔ The further 2.2× is NOT available from the Y-flip gate — I measured it and was wrong earlier.**
  I had argued the flip at 0.5 was our size-gate bug and that fixing it would unlock 2.2×. A harness
  probed all three ways that could be true — one mismatched hop, a two-hop chain, and a genuinely
  scanout-backed destination read off the screen — and **every one preserves orientation**. So Mesa's
  per-FBO orientation handling is fine, STK's flip is STK-side, and the gate change would neither flip
  STK nor speed it up. **Recommending against** that change on performance grounds; see §1.

  Also measured: the cost is **fill-rate/bandwidth**, not per-pass overhead — disabling
  `light_scatter` (3 of 8 passes, but the *half-resolution* ones) gave **9/9/10 vs 8/9/9**, inside
  noise, so not shipped (it also costs the fog glow). Two record corrections: the old
  "shadows/dynamic-lights buys 1.7%" A/B is **void** (it predates the clock fix, when every frame was
  padded to a whole second), and upstream's own Broadcom texture-clamp never fires here because STK
  matches graphics restrictions by OS name. `docs/misc/2026-09-09-stk-fps-scale-rtts.md`.

## 2b. ✅ Fixed this week (all HW-verified, all in the image)

| fix | effect |
|---|---|
| glamor mirror artefacts — Mesa's ≥1024×768 `Y_0_TOP` gate applied to a non-scanout pixmap | your mirrored Clip **and** the GoL/`top` band bleed were **one** bug; MAD 6.85 → 74.98 |
| AF_UNIX sockets had a **one-page (4 kB)** ring, so a 1.2 MB `XPutImage` crossed in ~512 blocking round-trips | frame **395.9 → 112.6 ms** |
| the damage path was **dead** (`glamor_init` ran after `shadowSetup`) | HDMI **2.3 → 25.6** updates/s |
| xbill was hidden: it ignores its `-geometry` *position* | layout rebuilt around where it lands; 91% visible |
| desktop-exit Data Abort — glamor's `DestroyPixmap` never chained | 6/6 crashed before, **0/18** after |
| **desktop-exit WEDGE (~1 in 6)** — the kernel's own diagnostic `lib_printf` self-deadlocked the scheduler spinlock, and the fallback then asserted into BCM2711's `for(;;) halt` | **20/20 clean exits** |
| **`process_getName` faulted the KERNEL 2982× via `top`** — unvalidated `argv`, unbounded `argc`, and a write one byte *before* the caller's buffer | 0 faults, storm gone |

The three X artefact detectors are now **`scripts/grade-x-desktop-video.py`** (run with
`.venv/bin/python`), validated FAIL-on-your-video / PASS-on-current.

## 3. THE FOUR BUGS YOU REPORTED — status

| # | bug | status |
|---|-----|--------|
| #4 | Data Abort on exiting Window Maker | ✅ **FIXED** — kernel half + glamor `DestroyPixmap` chain. 6/6 → 0/18. |
| #3 | Quake III glitches early in gameplay | ✅ fixed — Mesa `u_vbuf` dropped-draw fix, in the image |
| #2 | Quake II underwater view Y-mirrored | ✅ fixed, but a **stopgap** (over-corrects at `viewsize <= 71`). Real fix in §1. |
| #1 | GPU X11 xterm resize artefacts | **not reproduced** by a sound measurement; resize *correctness* verified. Likely intermittent frames during a fast drag (no compositing here). Did they persist *after* you released the mouse? |

**STK, per your call:** races correctly, own overlay `FPS: 8/9/9`. Faults in ~40% of runs at varying
sites; libphoenix's allocator is **exonerated** of any internal defect by a 4.8 M-op harness, so it is
corruption in STK's own code.

**★ New, and useful: `addr2line` on STK's four distinct fault PCs shows two are inside the ALLOCATOR,
not STK's code** — `lib_listRemove` (`sys/list.c:71`) and `_malloc_chunkJoin` — with the other two in
`SkiddingAI::findNonCrashingPoint()` and `FontManager::loadFonts()`. So the allocator is the *victim*.
Both allocator sites are now guarded (libphoenix `ec3bf13`): free-bin links are value-checked before
the unlink dereferences them, and on garbage the bin is **abandoned** rather than followed — the same
remedy as the kernel scheduler fix earlier today, which was the identical defect class one layer down.
**STK is relinked against it** and races normally (FPS 7/9/9, 0 faults, 0 guard reports).

**STK crash-rate bench: 0 of 6 trials faulted** (baseline **2 of 5**). Under the old rate that is
p ≈ 0.05 — suggestive, and it is the threshold I set before running rather than after.
⚠ **But not evidence the guards fixed it:** there were **0 guard reports**, so the corruption simply
did not occur in those 6 runs, and the STK rebuild also rebuilt its whole dependency chain, which
perturbs heap layout and timing — the exact trap the 2026-09-02 heap pass fell into. Treat STK's crash
rate as *unmeasured* rather than improved.

⚠ **My first version of that guard was wrong in a way worth recording, because it failed *quietly*.**
It required `link + sizeof(chunk_t) <= heapHi`, which rejects a legitimate chunk at a heap's end
(`sizeof` includes the rbnode only large chunks use) — and the failure action is to abandon the bin, so
it **leaked live memory** on a healthy allocator instead of crashing. It passed both libc suites and a
desktop session; only a real application hit a chunk at a heap boundary. Also refuted its own premise:
I claimed `heap=0x2000` proved a garbage pointer since "no mmap returns page 2", but a real STK process
reports `heapLo=0x2000` — **the first heap does live at page 2 here**, so the earlier stray-free
conclusion does not follow from that evidence.

## 4b. ✅ Docs refreshed on your ask, and the reliability figure recomputed

`README.md`, `docs/PHOENIX-RTOS-RPI4-CHANGES.md`, `docs/KNOWN-ISSUES.md` and
`docs/pi4-hardware-support-matrix.md` are current. Audited rather than appended-to; the corrections
that mattered were statements that were **false**, not merely thin — CHANGES claimed "no decode-rate
figure exists for any clip" (one does: 21.7 fps), scoped STK's crash to the `--profile-time` path
(one of its four fault sites is at *startup*), and quoted post-fix AF_UNIX numbers under a pre-fix
heading. README asserted STK was unverified in-game in three places. The matrix still had H.265 as a
4–8-week owner-gated item that is complete.

**★ The headline reliability number is now reproducible and honest.** It had been hand-derived with a
detector blind to a board hang. `scripts/reliability-tally.sh --since 20260908`: **313 logs, 313
reached the prompt (100.0%)**, 36 fault-bearing, and **5 ending mid-print**. The prompt rate survives —
but all 5 of those runs *had* reached a prompt, because the hang came after boot, so "reached the
prompt" was structurally unable to see the class and "0 unexplained" was unsupported. All five are
attributed; the three real hangs are fixed.

## 4d. Artifact timestamps were on two different clocks — fixed

`test-cycle-netboot.sh` stamped log and HDMI names in **UTC** while
`test-cycle-psh-interact.sh` and `capture-rpi4b-uart.sh` stamped **local**. Tonight's netboot cycle
ran at 00:07 local on 2026-09-10 and was filed as `rpi4b-uart-20260909-220735-...` — two hours and
**one day** off. `reliability-tally.sh` parses that date out of the filename for its `--since`
window, so it would have silently dropped the cycle. The two clocks also made it impossible to
correlate a cycle's HDMI snapshots with its UART log by name. All four stamps are now local, with a
comment saying why.


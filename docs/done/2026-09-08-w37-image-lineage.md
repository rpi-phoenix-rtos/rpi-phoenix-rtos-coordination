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



## STK clock-fix block as it stood in the weekly log

**★★ ROOT-CAUSED AND FIXED: STK was capped at exactly 1 fps by a broken C++ clock — now 5.84 fps
(5.8×).** `phoenix-rtos-ports 6f08c26`.

libstdc++ for aarch64-phoenix is built with **none** of its time backends
(`_GLIBCXX_USE_CLOCK_MONOTONIC` / `_REALTIME` / `GETTIMEOFDAY` / `NANOSLEEP` / `SCHED_YIELD` all
`#undef` in `c++config.h`), so `std::chrono::steady_clock` falls back to `std::time()` and ticks
in whole **seconds**. STK's frame loop sits in `while (dt == 0) { StkTime::sleep(1); … }`
(`main_loop.cpp:265`) waiting for it to move — one frame per second, whatever is on screen. The
patch reads `CLOCK_MONOTONIC` directly under `__phoenix__`.

Measured on HW (counter in the V3D winsys at the page-flip): **1000 ms/frame → ~171 ms/frame**,
exactly the ~173 ms the frame budget predicted (150 ms GPU + 23 ms physics). GPU time is
unchanged, so STK is now **GPU-bound** — ~88% of the frame is command-list submits.

**What found it: precision, not magnitude.** 1000.0 ms/frame within 0.08% across two tracks,
1080p, the whole deferred pipeline disabled and sound off — a workload doesn't hold that still
while the camera moves; a clock does. Everything else was measured and refuted first: render
passes (1.7%), uncached BO stores (new `tools/v3dmemprobe`: uncached is *faster* for scattered
stores, 28 vs 89 ns), the page-flip mailbox (1–9 ms per *32* frames), audio, kart count, scene
complexity. Also: the earlier race-clock metric was never "saturated" — it was correctly
reporting 1 fps.

**⚠️ YOUR CALL — the general fix.** This is a toolchain defect, not an STK bug: **any** C++ code
here timing with `std::chrono` gets 1-second granularity, and `sleep_for`/`yield` are degraded
too. STK was just where it was catastrophic; elsewhere it fails silently (a coarse timeout, a
rate limiter that does nothing). Proper fix = rebuild libstdc++ with `--enable-libstdcxx-time=rt`
— and note `steady_clock::now()` lives in `libstdc++.a`, so defining the macros in a header
changes nothing. That's a whole-system C++ rebuild, so I have NOT started it mid-demo-prep; the
per-port workaround is in. Detail: `docs/misc/2026-09-08-stk-frame-budget.md`.



## STK launcher + profile-crash block as it stood in the weekly log

**Fixed:** `stk-launcher` silently ignored every caller option (`697cd21a3`). It appended user
args after its own and assumed "later wins"; STK's `CommandLine` *rejects* a duplicate, and
rejects it **non-fatally**, so `--screensize=640x480` was accepted on the command line and the
game ran at 1080p anyway. HW-verified: `invalid_param=0`, STK logs "You choose to use 640x480."

**New crash, not on the demo path:** `--profile-time` prints its report, then takes a Data Abort
(EL0) formatting the per-kart table — `esr=0x92000044` (write, translation fault L0), `far` and
x4/x5 hold a 16-bit ascending sequence dereferenced as a pointer, with a stack-range line printed
just before. Stack exhaustion is the first suspect. `--no-sound` normal-mode run did not crash.



## §4e (STK time-to-race) as it stood in the weekly log

## 4e. STK time-to-race (2026-09-08, post clock fix)

Same deterministic 1325-tick lap throughout (includes load + shaders + race):

| config | **first** run of a boot | second run, same boot |
|---|---|---|
| full deferred pipeline | 55.5 · 55.6 · 56.3 · 50.0 · 47.3 (n=5) | 20.0 |
| `--disable-dynamic-lights --shadows=0` | 22.7 · 22.4 (n=2) | 21.8 |

**Two separate effects.** The deferred pipeline costs **~30 s of one-time, per-boot warm-up**
(first run only) and **nothing at steady state** (20.0 warm vs no-deferred's 21.8 — marginally
*faster*). So the flags buy time-to-race, not frame rate.

⚠️ **I over-corrected last turn.** I reported a "2.54× render win", then retracted it as an
ordering artifact after reversing the arms. The retraction was too strong: the ordering effect
and the flag effect are both real and different. No steady-state win, but a genuine **~2.4×**
improvement in time-to-race on a fresh boot — which is the case a demo take actually hits.

**RAM-staging STK's assets: measured, and it's a LOSS.** 4120 files / 137 MiB copied in **73.0 s**
(1.88 MiB/s, latency-bound on small files) to save 2.7 s. Net −70 s. It works for Quake (~47 MiB,
mostly read) but STK reads a small fraction of a 149 MiB 40-track library. **Corollary: the ~30 s
is not asset-read latency** — with the whole art root in RAM the run still took 47.3 s. Keep using
`ram-stage-play` for Quake; don't for STK.

**For a demo take:** best-looking → keep the deferred pipeline, accept a one-time ~50 s before the
first lap (trivially cut in editing, same steady frame rate). Quick take → add
`--disable-dynamic-lights --shadows=0`, 22 s on any boot. Detail + the still-open question of what
the ~30 s actually is: `docs/misc/2026-09-08-stk-time-to-race.md`.



## Queued-polish list as it stood in the weekly log

**Queued polish, deliberately not started:** `ncurses --enable-overwrite`; micropython
golden files; ~37 dead `/srv/phoenix-rpi4-nfs` references in legacy `tools/*/build.sh`
(live export is `-gcc16` — treat staging through them as unverified); **TD-21** syscall
order revert, due at the next full rebuild; mc's `/etc/mc/sfs.ini` not staged.

## §2 and §4 as they stood before the 2026-09-08 consolidation

## 2. STATE / WHAT'S NEXT

**All four bugs you reported are resolved or not-reproduced — see §3.** Per your instruction I
fixed xorg-server 21.1.24 rather than reverting to 1.20.14.

✅ **Trusted root CAs (you asked 2026-09-06) — HW-VERIFIED.** 121 roots in
`/etc/ssl/certs/ca-certificates.crt`; `curl -sSI https://example.com` → `HTTP/1.1 200 OK`;
python3 loads 121 CAs and fetches HTTPS 200. (`curl` is at **`/usr/bin/curl`**, not `/bin`.)

**★ Where this stands:** the standing goal looks **met** — stable build, all five games + the X
desktop gated on HW (0 faults, pixels checked), a flashable image that also passes a QEMU
structural boot check, and one publishable 126 s reel. Unattended work is now hitting diminishing
returns: the remaining substantial items (bug #2's real `FlipY` fix, the libstdc++ `std::chrono`
rebuild) both invalidate the 6-app gate and want you present, and the two intermittent defects
(#67, q3dm7) **no longer reproduce**, so more trials can only tighten a bound, not root-cause
anything. I'll keep to low-risk verification and cleanup until you're back.

**Next, in priority order:****Next, in priority order:**
1. **SD-boot re-gate of the new image** — ⏸ blocked: no SD card in the host reader. The image is
   structurally verified instead (see the manifest).
2. **SuperTuxKart in-game** — **5.84 fps** after the C++ clock fix (was 1.0). Now GPU-bound. Still the weakest of the five for a recording.
3. **Post-demo engineering**, each with a written next step. **Needs you present** (both would
   invalidate the current 6-app HW gate): the Quake II FlipY **stopgap** → the real `FlipY` fix
   (§3), and the libstdc++ `std::chrono` rebuild (§4). **Still open, unattended-safe:** #67
   vkQuake torches not closed (16/16 clean, 95% UB 17%); the q3dm7 GPU wedge stays banked —
   **use `q3dm1` for demo takes**. ✅ V3D BO reaping is now closed (leak proven bounded).
4. Delete the two `*-nonfork-obsolete` GitHub repos (my token lacks `delete_repo`).

**⏸ OWNER ACTION — ★ FLASH THIS (re-cut 2026-09-08 to carry the STK clock fix):**
`artifacts/rpi4b/rpi4b-sd-2part.img`
SHA256 `ce9ae9f0764b4879f4cff00612ae83df3418911587d427381660f035f96a741c`,
manifest `manifests/2026-09-08-stk-clock-fix-demo-image.md`.

```
udisksctl unmount -b /dev/sda1 2>/dev/null || true
sudo dd if=artifacts/rpi4b/rpi4b-sd-2part.img of=/dev/sda bs=4M conv=fsync status=progress
sync
```

**vs the previous image (`486acda5…`, superseded at the same path):** it carries the **STK 1 fps →
5.84 fps** fix — confirmed from the image's *own* `etc/build-versions`, not just from when I cut
it: `phoenix-rtos-ports 6f08c26` (the `std::chrono` clock fix) and `phoenix-rtos-devices 45e3d06`
(the diagnostic reverted back out). It predates today's libphoenix `execve("")` merge, as noted. STK re-gated on HW this date: `--race-now`, 4 karts, **0 faults**, full race
rendered on HDMI.

✅ **GATE STATUS — image `ce9ae9f0` fully gated (2026-09-08). It can now reproduce the reel.**
Re-cut because the previous image had **no `browse` mode and no vkQuake demo hook** (ports
`6f08c26` vs the reel's `d7de9aa`), so it could not reproduce two of the seven segments. The new
one is current: ports `d7de9aa`, libphoenix `a595099` (upstream `execve("")`), `browse` present,
coordination recorded **clean** (no `+dirty`). Manifest:
`manifests/2026-09-08-reel-capable-demo-image.md`.

Gates, all on the image's own binaries: **contents PASS** ("safe to flash") · **QEMU structural
boot PASS** · **six HW gates, 0 faults each, every frame visually confirmed** — QuakeSpasm ·
Quake II · Quake III (q3dm1) · vkQuake (demo2, HUD 144/97) · SuperTuxKart · X desktop (Window
Maker + xterm + xclock + xcalc + xlogo; the clock hands advance between frames).

⚠️ I had this wrong twice before: I reported "exactly 4 artifacts changed", then that "quake3 + the
X desktop keep their gate". Both false — the `--variant sd` cut (12:55–12:59) rebuilt
**essentially all of userland** (busybox, coreutils, X apps, wmaker, python3, all five games,
`Xphoenix`), and my earlier game re-gates ran at ~11:1x–11:2x, *before* that relink. Source was
unchanged throughout (only STK's clock patch + the reverted diagnostic), so nothing was ever
suspected broken — the evidence had lapsed, and it is now genuinely current.

✅ **Presentation-length soak (2026-09-08): vkQuake 6 min, ~9 990 frames presented, 0 faults,
0 wedge/timeout markers.** Every game gate until now was ~2 min; a live demo may run one for ten.
This is the first evidence it survives that.

**#67 torches — 24/24 lit in the soak, but read it precisely.** `check-torch-rois.py --label`
scored 24 frames at the reference viewpoint (mae 3.6–3.9 vs a threshold of 8, so the viewpoint
check is valid) and all 24 pass. These are **temporal** samples inside one run, so they bound
*within-run* flicker over ~10 k frames — they add **nothing** to the per-boot bound, which stays
at 16/16 boots (95% UB 17%). #67 remains open.

⚠️ **One caveat on the six gates:** they ran over **netboot**, which serves the core from
`.buildroot/_fs` — and that core was rebuilt at 13:13 for the upstream libphoenix merge, *after*
the image was cut at 12:59. So the game/X binaries tested are the image's, but the kernel and
`sbin/*` servers under them are one libphoenix commit ahead (`execve("")`→ENOENT) and
byte-different. The image's own core has never been booted. Re-cutting to fix that would rebuild
userland and expire the six gates again — circular — so it wants your call, not mine.

⚠️ **Two checks that lied, worth not repeating:** (1) `fault_pattern_matches: 0` is *vacuously*
true when the program never ran — vkQuake's first attempt silently failed to exec (stale-nfsd
signature: command echoed, **zero** output) and still scored 0 faults; confirm the app ran (render
markers, or a real frame) before believing a clean fault count. (2) an ffmpeg `blackframe` check
passes on a console full of text, so it cannot answer "did the game render". Also: the harness
*did* flag the silent run, and I lost the warning by piping its output through `tail -2` — tail
enough to see the CAPTURE verdict.

⚠️ The image predates today's upstream libphoenix `execve("")`→ENOENT guard (a 4-line
correctness fix, irrelevant to the demo). Deliberately **not** re-cut: re-cutting relinks the
games and would expire the gate I just restored, for no demo benefit.

(`/dev/sda` is always the SD card on this host. Do **not** flash `rpi4b-sd.img` — FAT-boot-only.)

✅ **Contents gate re-run on THIS image (`23f46860`) — PASSES, safe to flash.** I had only ever
invoked it without the image argument, so it had never actually been run on this build. All five
game binaries + every data pak (`quake/id1/pak0`, `quake2/baseq2/pak0`, `quake3` pak0+pak1+q3key),
`Xphoenix`, `wmsetbg`, python3/bash/mc/nano — all present; positive marker (v3d submit mutex in
vkquake) present; negative marker (`gl3_discardfb` in yquake2) absent.

ℹ️ Its one warning — `1 repo(s) were DIRTY at build time` — is **benign and now resolved**:
`etc/build-versions` records `coordination <sha>+dirty`, and the dirt was two tracked agent-config
files under `.claude/` (the command allowlist + the rpi4-run skill), which cannot affect image
contents. They are committed and the tree is clean, so future images record coordination clean —
i.e. that warning means something again instead of being permanently on.

✅ **NEW: the image's loader now boots under QEMU** — `./scripts/qemu-boot-sdimage.sh` extracts
`loader.disk` from the image's FAT partition (mtools, no root), confirms the syspage really is the
SD variant (0 `nfs;/` refs, 4 `bcm2711-emmc`), and boots it against the live `plo.elf`. On
`23f46860`: **PASS — plo parses it and reaches kernel entry.** Deliberately narrow: it does *not*
prove the kernel runs (QEMU's rpi4 model stops it at once), that the ext2 root mounts, that EMMC2
works, or that userspace starts. **Not an SD-boot gate** — just proof the image isn't structurally
broken. Run it on every image cut.

⚠️ **The SD boot path itself was NOT re-verified:⚠️ **The SD boot path itself was NOT re-verified: there is no SD card in the host reader.**
All evidence is netboot on byte-identical binaries. Also: the rootfs volume is now sized from
content (881 MiB vs ~1.5 GiB), so free space on the Pi drops ~850 → ~179 MiB;
`RPI4B_ROOTFS_BLOCKS=1572864` restores the old size.

Cumulative contents and earlier image lineage: `docs/done/2026-09-08-w37-image-lineage.md`.

Also still owner-blocked: SD-boot port re-run; delete the two `*-nonfork-obsolete` GitHub repos
(my token lacks `delete_repo`). **SuperTuxKart in-game is no longer blocked** — see §4.

⚠️ **Harness note:** cutting a `--variant sd` image replaces the TFTP `loader.disk` with an
SD blob, so netboot cycles refuse until you restore it with
`./scripts/rebuild-rpi4b-fast.sh --scope project --variant nfsroot --skip-prepare`. The harness
says so rather than producing a silent empty run.

**Queued polish, not started:** `ncurses --enable-overwrite`; micropython extras. Low value.



## 4. WHAT GOT DONE THIS WEEK (short)

**★ SuperTuxKart in-game is NOT blocked — corrected.** `stk --race-now --track=hacienda
--numkarts=3` reaches a real race over netboot, **0 faults** — Hacienda dirt track, race timer,
minimap, opponents, full lighting and HUD. Screenshot-quality.

**★★ ROOT-CAUSED AND FIXED: STK was capped at exactly 1 fps by a broken C++ clock — now
5.84 fps (5.8×).** `ports 6f08c26`. libstdc++ for aarch64-phoenix is built with **none** of its
time backends, so `std::chrono::steady_clock` falls back to `std::time()` and ticks in whole
**seconds**; STK's frame loop sat in `while (dt == 0) { StkTime::sleep(1); … }` waiting for it.
Patch reads `CLOCK_MONOTONIC` directly. HW: 1000 → ~171 ms/frame, exactly the ~173 ms the measured
frame budget predicted; STK is now **GPU-bound** (~88% of the frame is CL submits). Found by
*precision, not magnitude* — 1000.0 ms/frame within 0.08% across two tracks, 1080p, pipeline off
and sound off is a clock, not a workload. Everything else was measured and refuted first (render
passes, uncached BO stores, the page-flip mailbox, audio, kart count).

**⚠️ YOUR CALL — the general fix.** This is a **toolchain** defect: any C++ code here timing with
`std::chrono` gets 1-second granularity, and `sleep_for`/`yield` are degraded too. Elsewhere it
fails *silently* (a coarse timeout, a rate limiter that never limits). Proper fix = rebuild
libstdc++ with `--enable-libstdcxx-time=rt`; note `steady_clock::now()` lives in `libstdc++.a`, so
defining the macros in a header changes nothing. That's a whole-system C++ rebuild, so I have NOT
started it mid-demo-prep. Detail: `docs/misc/2026-09-08-stk-frame-budget.md`.

**Also this week:** `stk-launcher` now honours caller options (it appended them and assumed
"later wins"; STK *rejects* duplicates, non-fatally, so every override was silently ignored) ·
a `--profile-time`-only crash in STK's per-kart report path (post-race, not on the demo path) ·
`tools/v3dmemprobe`. Detail in `docs/misc/2026-09-08-stk-*.md`.

**Your four bugs:** #4 fixed (kernel + glamor `DestroyPixmap` chain), #3 fixed, #2 fixed with a
documented stopgap, #1 not reproduced — see §3.

**Also fixed and HW-verified:** trusted root CAs (121 roots, real HTTPS) · a libphoenix stdio
partial-write bug that duplicated output · a V3D GPU leak of ~15.7 MB per X session · the
1-in-3-boots no-keyboard/mouse defect · a silently-dead `--timestamp` in the capture harness.

**Also verified:** USB input 8/8 boots. (Render/gate status is in the ✅ GATE STATUS block above.)

**★ SHOWCASE REEL v4 — 7 segments, 140 s.**
`artifacts/hdmi-video/20260908-162023-phoenix-rtos-rpi4-showcase.mp4` (72 MB, 1080p30 H.264 High,
limited-range/bt709, faststart). Rebuild: `./scripts/make-demo-reel.sh`.
· **QuakeSpasm** id1 `demo1` — ogre, blood, double-barrel shotgun
· **Quake II** `q2demo1` — machinegun combat, enemy soldiers, health 100→14
· **vkQuake** id1 `demo2` — muzzle flash, enemy projectiles, super-nailgun pickup
· **Quake III** — live **bot deathmatch** on q3dm1 (Grunt + Daemia)
· **SuperTuxKart** — AI-driven 4-kart race
· **NEW: Dillo web browser** — page fetched over TCP/IP, rendered under glamor X
· X desktop — Window Maker + xterm/xclock/xcalc
0 faults on every capture; full decode clean, 4200 frames. The browser and desktop segments are
static by nature; your "needs movement" point was about the games, which is fixed.

**NEW `browse` mode** (`84a8f7152`): `startx_gpu browse [url]` = Window Maker + Dillo. The
existing sole-client path (`startx_gpu /bin/dillo`) worked but gave a small undecorated window on
black — fine as proof, poor on a recording. HW-verified: titlebar shows the page's own `<title>`,
status bar reports "Page 1.2 kB" against the 1240 bytes served, and the log shows
`Nav_open_url` → DNS → `Connecting to 10.42.0.1:8000`. Page served from the host over HTTP for
reproducibility (a live public site also works, but that needs the host NAT+DNS chain).

**Quake III took three attempts — worth recording so nobody repeats them.** Its shipped demos are
the 1999 `.dm3` protocol while the engine only looks for `demos/<name>.dm_66/67/68/71`, so they can
never be found and that protocol is unsupported → demo playback is a netcode project. Bots work
instead (the demo pak ships `botfiles/` + `q3dm1.aas`). Two ways to get a *moving camera* failed:
`+team spectator`/`+follow 1` never took (the HUD still showed our health), and
`+set cg_thirdPerson 1 +set cg_cameraOrbit 2` **broke startup** — the game never left the main
menu, because those are cgame cvars that cannot be set before the game module loads. So the
segment is a 15 s clean stretch (151–166 s) with a bot running through frame: our own player is
stationary and gets fragged, and the scoreboard overlay then covers the screen.

**Fixed to make vkQuake's segment possible** (`ports d7de9aa`): the vkQuake port has **no argv
path** (#I2), so `+playdemo demo1` was silently ignored — it kept booting the map from
`phoenix-map.cfg` while the log echoed the demo request. Added `id1/phoenix-demo.cfg` beside it
(same convention): names a demo → `playdemo`, absent → unchanged map boot, so a flashed image
behaves as before. ⚠️ That file is **hand-staged on the NFS export**, not in the image.

Raw per-app captures remain in `artifacts/hdmi-video/` (local, gitignored).

Full engineering detail, measurements and the corrections I made along the way:
`docs/done/2026-09-08-w37-engineering-detail.md`.

**Queued, deliberately not started** (each with a written plan):
FlipY scanout-gate cleanup `docs/misc/2026-09-08-flipy-scanout-gate-work-order.md` ·
V3D per-client reaping via the exact RPC route `docs/misc/2026-09-08-v3d-bo-reaping-rootcause.md` ·
#67 vkQuake torches (16/16 clean, but n=16 only bounds the failure rate below 17%, so not closed) ·
q3dm7 GPU wedge — **re-measured 2026-09-08 and much better than recorded: 10/10 clean**
(no wedge, no BIN/RENDER TIMEOUT, 0 faults) and q3dm7 renders with full lightmaps. The
~50% rate measured on 08-22 is decisively excluded (P(10 clean) = 0.1%), and 30% too
(2.8%); the 95% upper bound is now 25.9%, so **not fixed, but usable for a demo take** —
q3dm7 is the far better-looking map, with `q3dm1` as the safe fallback and a re-run costing
one boot.


**STK first-run cost: closed as understood, deprioritised.** The ~27 s is **not** shader
compilation — a free probe showed 200 cache entries before a full-deferred first run and 200
after, with all 50 "compiles" being disk-cache *hits*. It is deferred-pipeline-specific, warms
per *boot* (not per process), and is not asset reads, shader sources, the cache, or steady-state
rendering — which rules out everything process-local. Leading hypothesis: the kernel's contiguous
physical allocator (every V3D BO is `mmap(MAP_CONTIGUOUS)`; the deferred pipeline wants many more
large 1080p render targets). Test path recorded, but not worth more Pi cycles: it's a one-time
wait, avoidable with two flags, and cut in editing.

**✅ X desktop leak is BOUNDED — the open 3-lifecycle measurement is closed.** Three full X
lifecycles in one boot, `/bin/mem` between each: after boot 91036 KB/197 entries → +60348/+169 →
**+2208/+15** → **+1504/+5**. Per-session cost **decays** (+60 MB → +2.2 MB → +1.5 MB); the +60 MB
is one-time setup, not per-session, which is why two sessions made the residue look worse than it
is. **0 faults**, 3 clean teardowns, reaper fired. So the BO-reaping fix holds over repeated
sessions rather than just reducing the slope — and it independently reproduces the 15.7 → 2.3 MB
improvement. Detail: `docs/misc/2026-09-08-v3d-bo-reaping-rootcause.md`.

**QEMU ceiling measured — it is plo-only; the kernel never starts.** Asked whether QEMU could
replace the Pi for iteration. It cannot: plo runs fully and hands off, then core 0 faults at once
(`--gdb`: running at `pc=0x200`, an exception vector with `VBAR` unset; cores 1-3 correctly parked
at `0x2001f0`). Cause is specific — plo needs a **firmware DTB** in x0 (`hal.c:229` checks
`0xd00dfeed`), VideoCore/armstub supply it on HW, nothing does under QEMU, so the kernel gets no
device tree and dies in early hal init. **`-dtb` does not help** (tested with the image's own DTB):
QEMU's `raspi4b` doesn't hand it to a bare-ELF `-kernel` entry. Two fix paths recorded, both plo
changes on the demo boot path → both need the full re-gate, so parked.
⇒ QEMU stays useful for **plo-level** checks, which is exactly what the new image gate uses.
`docs/misc/2026-09-08-qemu-boot-ceiling.md`.


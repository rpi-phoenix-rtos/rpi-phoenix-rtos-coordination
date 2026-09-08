# Week 2026-W37 — engineering detail (archived 2026-09-08)

Moved out of the weekly log, which the owner reads and which is meant to stay short.
Everything here is done, verified or superseded; the log keeps only what is actionable.


---

<!-- §4 progress entries -->

## 4. PROGRESS THIS WEEK

- **★ Architecture correction: the V3D stack is SPLIT, and it changes two written plans.**
  Measured from boot logs — the **GPU X desktop** runs against the **daemon** (`v3d-srv` in the
  log, no `v3d-winsys`), while the **games** (STK and vkQuake both checked) use the
  **in-process winsys** (`v3d-winsys`, no `v3d-srv`).
  Consequences: (1) the shipped BO-reaping fix is **daemon-only, i.e. X** — consistent with where
  it was measured and where it fires, and the games can't have that leak class at all since they
  never allocate through the daemon; (2) the FlipY work order's edit #1 ("clear the flag in the
  winsys, Mesa reads the same struct back") holds **only for games** — the daemon path needs an
  **RPC protocol change** (`v3d_rpc_resp_t` has no `flags` field and the client never writes back
  to `c->flags`), so that "one-liner" is really two edits, one of them touching a struct every
  GPU client uses. Both docs corrected.
  I found this by *implementing* the change and then checking whether it could actually take
  effect — it would have been live for games and silently inert for X. **Reverted** rather than
  ship a half-path change with no demo benefit.
  Also settled while there: the BO-cache risk that doc raised is **not real** —
  `v3d_bufmgr.c:141` correctly uses the request flag for the cache *lookup*, and the free-side
  guard exists because "scanout aliases a fixed framebuffer PA", which a *refused* BO does not.
- **★ SuperTuxKart verified after the V3D reaping change** — it was the one demo component not
  re-checked since that landed, and the fix is in the GPU daemon STK depends on. Full main menu,
  logo, all five mode icons, toolbar, crisp text; **0 faults**, and no spurious reaping (correct:
  STK is the sole live client). **All 6 components are now verified after BOTH core changes**
  (stdio partial-write + V3D BO reaping).
  Two notes: STK needed `--idle-secs 290` — 200 was enough on an earlier run but NFS asset load
  varies, and the short window produced a log ending at the `stk` echo, which looks exactly like
  "it never started". And **no STK footage was captured**: the recorder ran alongside the
  truncated first attempt, so the clip was boot console only and I deleted it rather than leave a
  misleading file. Reel still covers X+QuakeSpasm, Quake II and Quake III.
- **★ Fixed a silently-dead `--timestamp`** in `capture-rpi4b-uart.sh`: line 304 reassigned the
  numeric `timestamp` flag to a **date string** whenever no explicit `--log` path was given (the
  normal case), so the later `[ "$timestamp" -eq 1 ]` tests became bash *"integer expected"*
  errors evaluating false. The flag could never work for any normal caller, and each test wrote
  an error to stderr. Renamed the date variable. **Verified:** a capture with `--timestamp` now
  carries `[2026-09-08 06:52:34.510934]` prefixes on every line, 0 bash errors.
- **Bench boot-only multi-trial bug: two hypotheses eliminated, still open.** Not PID reuse in
  the capture watchdog (guard implemented, made no difference, reverted) and **not** the
  timestamp/pipeline path (standalone netboot with `--timestamp` returns 0, but the bench still
  dies after trial 1 with it). Also corrected: a `--timestamp` passthrough I added to the bench
  to test that theory **did not actually work** (0 timestamped lines), so I reverted it rather
  than keep an unverified feature. Workaround unchanged and reliable: give the bench a command,
  `-- "/bin/mem"`.
- **Decided NOT to re-cut the flash image**, with a concrete reason rather than caution: 132
  binaries now differ from `486acda5` (the two `--scope core` rebuilds), and
  **`bcm2711-emmc` — the SD card driver — is among them**. With no card reader I cannot test SD
  boot, so shipping a new image would put an untestable change on the critical boot path. The
  existing image has the smaller unverified delta and stays the flash target.
- **★★ FIXED: V3D per-session BO leak** (`devices 1eb8608`, manifest
  `manifests/2026-09-08-v3d-bo-reaping.md`). Each BO now records its creating client
  (`msg.pid`, reliable for client-*sent* messages) and the daemon sweeps owners that are gone,
  probing with `kill(pid, 0)`.
  **HW, `/bin/mem` across two X lifecycles in one boot:** second-session cost
  **15.7 MB → 2.3 MB** and **+85 → +8** map entries; daemon logged
  `reaped 80 BO(s) from exited client(s)`.
  No regression: X desktop 2 lifecycles 0 faults · vkQuake **2/2 torches** at reference
  viewpoint · QuakeSpasm renders · **0 faults throughout**. No reaping fires while a single
  client is alive, as expected.
  ⚠️ **I had deferred this last turn citing a hazard — "reaping could free memory a GPU job is
  still reading". That was wrong and I checked it instead of trusting it:** `ioc_submit_cl` waits
  for `FLDONE` then `FRDONE`, and `v3d_srv_thread` is the daemon's only thread, so at the top of
  the message loop no job can be in flight. Placing the sweep there makes it safe by
  construction. Both liveness error directions also under-reclaim (a zombie and a recycled pid
  both read as alive), so a live client's buffers can never be freed.
- **★ V3D per-session BO leak ROOT-CAUSED** (previously only "no disconnect handler").
  The daemon's `mtOpen`/`mtClose` arm is **dead code for the Mesa path**: the client never calls
  `open()` — `libv3d-client.c` does bare `msgSend`, and Mesa's DRM entry point discards the fd
  (`(void)fd;`). The kernel's *only* client-death signal is a synthesized `mtClose`, delivered
  solely to servers the dead process held an **fd** on
  (`process_destroy` → `posix_sweepFds` → `proc_close`). No fd ⇒ nothing sent ⇒ every BO a
  SIGTERM'd client didn't close leaks. Also confirmed **absent**: any kernel death notification
  to port owners.
  Two fixes need no kernel change — **A**: the established ptmx/ade9113 pattern (return a session
  id as positive `msg.o.err`, kernel stores it as `f->oid.id`, reap by session on `mtClose`);
  **B**: server-only, store the (reliable) `msg.pid` per BO and sweep with `kill(pid, 0)`.
  Note for whoever does it: **don't** key the death `mtClose` on `msg.pid` — it arrives as pid 1
  (in-tree FIXME at `usbwlan.c:571`).
  **No code landed, and the reason is specific:** reaping can free memory a GPU job is still
  reading, and the daemon can't tell whether the job retired — same class as the render wedges
  that took several sessions to chase. Against 15.7 MB/session on 4 GB that trade isn't worth it
  now. Plan + the ordering (do A, and drain the GPU before freeing):
  **[docs/misc/2026-09-08-v3d-bo-reaping-rootcause.md](../misc/2026-09-08-v3d-bo-reaping-rootcause.md)**
  ⚠️ Correction to my own note: whether the leak grows **linearly** is *not* established — the
  3-lifecycle run I attempted only completed 1 session (7 commands overran the capture window).
  Two sessions showed +15.7 MB for the second, so it is not purely one-time; beyond that, unknown.
- **★ USB input re-gated 8/8 after the core rebuild.** The rebuild included
  `phoenix-rtos-usb` (the xHCI Disable-Slot recovery), and the original defect was **1 boot in 3
  with no keyboard/mouse** — which would wreck a presentation you drive by hand. I had only seen
  single boots since. Now **8/8 boots** enumerated both `/dev/kbd0` and `/dev/mouse0`, **0
  faults** (the original gate was 6/6). Every one of the 8 logged a non-zero
  `xhci: transfer completion code`, so the recovery path was genuinely exercised each time and
  worked.
- ⚠️ **Harness: multi-trial `test-cycle-bench.sh` does not work on the BOOT-ONLY path** (no
  `-- <cmd>`); it stops after trial 1. **Use the psh path instead** — give the bench a command
  such as `-- "/bin/mem"`, which boots, enumerates USB and reaches psh, capturing the same
  evidence. Verified reliable across 2- and 3-trial runs.
  **Corrections to what I wrote last turn, both now checked:** (1) `rc=143` is *not* an external
  SIGTERM — it is the boot-only path's **own** exit status when the capture watchdog kills the
  serial tool, reproducible with `test-cycle-netboot.sh` standalone, and the capture it leaves is
  complete and usable (138-line boot). (2) It does **not** leave the Pi powered on — I queried
  the smart plug immediately after a 143 exit and it read OFF, so `ensure_powered_off` runs
  correctly. Why the bench then stops despite calling netboot with `|| true` is **not
  root-caused**; I tried a PID-reuse guard on the watchdog, it did not help, and I reverted it
  rather than leave an unproven change in the harness the whole test loop depends on.
- **★ All 6 demo components re-verified on hardware AFTER the core stdio change** — I had landed
  a `--scope core` libphoenix change having checked only python3, vkQuake and Quake III's
  *startup*, so this was owed. Every one **0 faults**, pixels inspected, not just logs:
  QuakeSpasm (start map, both torches lit, HUD) · Quake II (Outer Base fully textured, weapon +
  HUD) · Quake III q3dm1 (lightmaps, statues, HUD — no black-lightmap regression) · vkQuake
  (torches 17/17 at-reference frames) · SuperTuxKart (full main menu, all five mode icons) · GPU
  X desktop (stdio suite green) · python3 ALL-OK.
  Quake III footage added: `artifacts/hdmi-video/20260908-030847-demo-quake3.mp4` (260 s).
  ⚠️ Worth knowing: the **games are prebuilt and were NOT relinked** by the core build (sizes and
  mtimes unchanged), so they do not themselves contain the stdio fix — what this proves is that
  the new kernel + core daemons do not regress them.
- ⚠️ **The flash image `486acda5…` predates the stdio fix.** The fix affects newly-linked
  binaries (psh, daemons, test suite), not the prebuilt games. Re-cutting is best done when the
  SD reader is reattached, so the new image can actually be SD-boot-gated rather than only
  structurally verified.
- **⚠️ CORRECTION + fix: the 234k-line UART flood was a HOST CAPTURE artifact, not a Phoenix
  bug.** I attributed it to the libphoenix stdio defect last turn. That was wrong. Evidence, all
  from logs already on disk: it **recurred with the stdio fix already built in**
  (`stdiotest`, 235 121 lines); it appears in logs of runs that never ran the flooding program
  (`wmexit2`, a Window Maker test, flooded with 360 275 copies of the **vkQuake** line
  `vkvid: present 4170`); it sits **entirely before the boot banner** every time, i.e. in the
  window where the serial tool is open but the Pi is still powered off; and 7 489 copies of the
  spliced variant `vkvid: pvkvid: present 4170` prove the host is re-serving one buffer at a
  shifted offset. 7.5 MB of identical bytes is far more than any USB-UART adapter holds.
  **The stdio fix stands on its own** — proven by its regression test (fails without, passes
  with), not by the flood.
  **Mitigation shipped:** `scripts/collapse-uart-log-floods.py`, called non-fatally at the end of
  `capture-rpi4b-uart.sh`. Collapses runs of ≥200 identical lines to one line + an explicit
  repeat-count marker, so nothing is lost and the artifact labels itself. Verified on real
  flooded logs (7.79 MB → 386 KB; 7.32 MB → 16 KB with `uart-summary.sh` output unchanged) and a
  no-op on clean ones. **Not yet observed firing live** — the artifact is intermittent (~10 of
  ~60 recent captures) and did not recur in two deliberate attempts.
  This symptom was misread as a target bug twice. The cheap discriminator, available all along:
  *could the flooded line have come from the program that was actually running, and where does
  the flood sit relative to the boot banner?*
- **★★ FIXED + PROVEN: libphoenix stdio partial-write bug** (`libphoenix 9029813`,
  manifest `manifests/2026-09-08-stdio-partial-write-fix.md`). `__fflush_one()` treated a short
  write as "keep the whole buffer", so the bytes that had already gone out were **re-transmitted**
  on the next flush and the remainder was never resumed. `write_buffer()`, the other flush path
  ten lines below in the same file, already did it correctly — the two disagreed. `fflush()` now
  matches it.
  **Regression test proven to catch it** (`tests 9eb0343`): with the fix `3 Tests 0 Failures
  0 Ignored`; with the fix reverted the new test **FAILS** — `Expected 109 Was 97`, i.e. it
  received `'a'` where `'m'` was due, the duplication signature exactly.
  **Core gate passed** before pushing: clean build, healthy boot, python3 ALL-OK, Quake III
  renders on V3D, vkQuake 816 lines + **torches 17/17** at-reference frames, 0 faults across
  every run.

Earlier entries this week: `docs/done/2026-09-08-w37-progress-archive.md`.


---

<!-- §4a stability -->

## 4a. STABILITY (the goal says STABLE, so single runs are not enough)

Every demo component benched **3 trials** on the exact build in the SD image, each graded
on real criteria and confirmed on pixels, **0 faults and 0 dropped draws in every trial**:

| component | command | 3-trial |
|---|---|---|
| GPU X desktop | `startx_gpu deskapps` | ✅ 3/3 (5 clients, glamor up) |
| Quake III | `+devmap q3dm1` + bots, god mode | ✅ 3/3 (map loaded, world lit) |
| QuakeSpasm | `+map start` | ✅ 3/3 (pak found, `Host_Init`) |
| Quake II | `+map demo1` | ✅ 3/3 (GLES3 refresher, map loaded) |
| vkQuake | `+map start` | ✅ 3/3 (engine + server up) |
| SuperTuxKart | `stk` | ⚠️ not benched — see the open() finding below; **NOT** NFS-bound |

**15 boots, 0 faults.** Not claiming anything about vkQuake's intermittent torches (#67):
that needs `check-torch-rois.py --rate` over n>=8, and 3 frames is not a rate.

⚠️ **The first desktop bench scored 2/3 and the "failure" was mine.** Trial 2's log was
6.3 KB and ended at the command echo — a **truncated capture**, i.e. no evidence, not a
desktop failure. With a proper window all three pass. Two things came out of that:
- **Harness fixed** (`scripts/test-cycle-bench.sh`): each trial now runs
  `check-capture-complete.py` and is labelled **`VOID (capture truncated)`** instead of
  being lumped into "B (no output)". Benching a non-test command (a desktop, a game) was
  previously uninterpretable — a working run, a failing run and a cut-short run all produced
  the identical class-B line. That ambiguity has produced false conclusions here before.
- Class B is now "ran, no test output", which is what it actually means.


---

<!-- §4e bug #4 detail -->

## 4e. ✅ X desktop-exit crash FIXED (bug #4 closed) — root-caused and HW-verified

**Root cause: glamor's `DestroyPixmap` screen hook never chained.** `glamor_init()` saves the
previous `screen->DestroyPixmap` into `glamor_priv->saved_procs.destroy_pixmap` and installs
`glamor_destroy_pixmap()`, but that function ends in a hard `return fbDestroyPixmap(pixmap)` and
never calls the saved pointer — it is only read again to restore the hook at CloseScreen. So
anything wrapped **below** glamor was cut out of the chain.

Upstream xf86 never hits this: `glamor_init()` runs at ScreenInit and `DamageSetup()` later at
extension-init, so damage always wraps *above* glamor. Our DDX brings damage up through
`shadowSetup()` (`ddx/fbdev.c:591`) **before** `glamor_init()` (`:603`) — into the one slot
glamor doesn't honour. Consequence: `damageDestroyPixmap()` never ran for **any** pixmap, so
damage.c never destroyed `DamagePtr`s attached to dying pixmaps. glamor's stipple damage
outlived its drawable, and `FreeGC` (`dix/gc.c:781`) releases `gc->stipple` *before* the DDX
`DestroyGC` at `:783` → `glamor_invalidate_stipple` → `DamageUnregister()` on freed memory.

**Fix** (`bde9a44a3`, patch `xorg-server-21.1.24-glamor-destroypixmap-chain.patch`): a
screen-hook-only wrapper that does the same FBO teardown then calls
`saved_procs.destroy_pixmap`. The exported `glamor_destroy_pixmap()` is left alone — ~15
internal glamor callers use it to drop private pixmaps and must *not* traverse the screen chain.
Chosen over reordering `glamor_init` before `shadowSetup` because reordering risks glamor
bring-up itself, and the GPU X desktop is a demo component.

**HW: 3/3 trials, 0 faults** (the crash previously fired on *every* run):
`glamorchain` + `chain2` — full teardown (`quit-after` → SIGTERM → session ended → shutting down
X) then back to `(psh)%`; `chaindesk` — persistent desktop **pixel-verified**: wmaker root+dock,
xterm with a live BusyBox prompt, xclock with hands, xcalc's full button grid, xlogo, cursor. So
making `damageDestroyPixmap` run for every pixmap destroy did not disturb rendering.

⚠️ **The staged NFS export carries the fix; the SD image does NOT** — `rpi4b-sd-2part.img`
predates it. The image is still good (this was shutdown-only and the image was never gated on
it). To ship the fix on SD: full `--scope core --with-showcase --with-ports` rebuild, re-cut,
contents gate, re-gate the 6 components. That is a separate turn's work.


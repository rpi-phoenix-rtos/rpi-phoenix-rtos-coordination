# Week 2026-W37 — earlier progress entries (archived 2026-09-08)

Moved out of the weekly log to keep it short. Newest-first, as written.

- **★ Found a vacuous test while doing it** (`tests f54cdec`): `stdio_fflush_eagain` was guarded
  by `#ifdef _PHOENIX_POSIX_SOCKET_H_`, but libphoenix's `<sys/socket.h>` guard is
  `_SYS_SOCKET_H_` — so the body compiled to **nothing** and Unity reported PASS for an empty
  test. It had never run. Both guards widened; both now execute for real.
- **★★ NEW BUG root-caused in libphoenix stdio: a partial write duplicates output and wedges
  the stream.** Found via a Quake II soak whose log was **7 MB / 228k lines**, almost all one
  30-byte fragment repeated 234k times. Not a logging-gate bug (the vkQuake gate is provably
  correct — both compared trials show exactly the arithmetically expected 73 intact lines) and
  intermittent (~1 vkQuake run in 4). `Sys_Printf` is a bare `vprintf`, so it is **stdio, not
  port glue — any program printing under load can hit it**.
  Cause: `full_write` (`libphoenix/stdio/file.c:379`) returns a **short count on EAGAIN** — what
  a non-blocking tty does when a program outruns the UART — and `__fflush_one` (`:398`) treats
  short as "keep the whole buffer", never advancing `bufpos`. So the prefix that *did* go out is
  **re-transmitted** on the next flush, the remainder is never retried as a remainder, and
  `F_ERROR` sticks permanently on a stream that only hit backpressure.
  **Fix written but NOT implemented** — it is `--scope core` on the most-used path in the system,
  needs a soak across the games + desktop, and one step of the byte-level mechanism is still
  open (the observed symptom is the *tail* repeated; the proven mechanism explains the *prefix*).
  Plan, caveats and the instrumentation to close it:
  **[docs/misc/2026-09-08-libphoenix-stdio-partial-write.md](../misc/2026-09-08-libphoenix-stdio-partial-write.md)**
- **Quake II soaked ~150 s while recording** — `Map: demo1` on V3D 4.2.14.0, **0 faults**.
  Footage: `artifacts/hdmi-video/20260908-*-demo-quake2.mp4`.
- **#67 vkQuake torches: 7 more trials, all clean → 16/16 pooled. Still NOT closed.**
  `torchA/B/C/D` via the prescribed instrument (`test-cycle-bench.sh` + `check-torch-rois.py
  --rate`): 7/7 PRESENT, 7 at-viewpoint frames each, 125-137 lit px vs a threshold of 8. Pooled
  with the earlier 9/9 on the shipped build (vkQuake is V3DV/Vulkan, untouched by the glamor
  fix) = **16/16**.
  ⚠️ **The arithmetic says that is not enough to declare it fixed:** 0 failures in 16 trials
  gives a 95% upper bound on the failure rate of **17.1%**, which does *not* exclude the
  historically measured ~15%. (If the true rate were 15%, P(16 clean) = 7.4%.) Bounding it below
  5% needs **59** trials ≈ 3.5 h of Pi time — not worth it for a defect that currently renders
  correctly. **Verdict unchanged: reliable enough for a recording, #67 stays open.** This would
  have been the 6th false closure.
- **FlipY scanout-gate work order written, work NOT started** —
  [docs/misc/2026-09-08-flipy-scanout-gate-work-order.md](../misc/2026-09-08-flipy-scanout-gate-work-order.md).
  The Quake II stopgap's root cause is a **size** gate standing in for "is this FBO
  scanout-backed", so *any* offscreen FBO ≥1024×768 is wrongly flipped — every future GL port
  with a large FBO will need its own hunk. Investigation found the trap that makes the obvious
  fix wrong: `bo->scanout` records the *requested* flag, and the winsys never clears it when it
  *refuses* the scanout claim, so Q2's FBO looks scanout-backed to Mesa. Full 5-edit plan +
  the 5 compensators that must come out with it + the HW soak it needs are in the doc.
  **Deferred on purpose:** it touches every GL app's flip path and the BO-cache reuse path
  (documented corruption history) while the demo goal is already met with a cut, verified image.
- **✅ Trusted root CAs HW-VERIFIED** (your 2026-09-06 request; built 09-06, never tested until
  now). 121 roots present; `curl -sSI https://example.com` → `HTTP/1.1 200 OK`; python3
  `cert_store_stats {'x509': 121, 'x509_ca': 121}` from `/etc/ssl/cert.pem` + HTTPS 200. 0 faults.
- **Weekly log compacted 812 → 283 lines** (you asked for it SHORT). Resolved detail — the four
  bugs in full, the guard-page/STK/6-of-6 re-gate work, superseded images, the earlier upstream
  sweeps, and the xHCI no-input fix — moved verbatim to
  `docs/done/2026-09-08-w37-archived-detail.md`. §2 was stale (still called #4 open and #3
  untriaged) and is rewritten.
- **★ Desktop soak-tested; the #4 fix holds, and a pre-existing GPU-memory residue found.**
  The fix made `damageDestroyPixmap` run on *every* pixmap destroy, so short trials could not
  rule out a leak. `xsoak2`: 150 s desktop session (1.7× any previous run), clean teardown, **0
  faults**. `xleak`: **two full X lifecycles in one boot**, both started all 5 clients and both
  exited cleanly — the desktop is restartable. 0 faults throughout.
  `/bin/mem` at three points: 91040 → 151320 → 167044 KB (193 → 363 → 448 map entries), so the
  steady-state cost is **+15.7 MB / +85 entries per X session**.
  **Cause, by source inspection:** the V3D daemon frees a BO only via an explicit `gemClose`
  (`rpi4-v3d.c` → `v3d_gpu_closeBo`); it stores **no client identity** with a BO and has **no
  disconnect handler**, so anything a SIGTERM'd client didn't close stays allocated.
  **Pre-existing, not from the glamor fix** — the ownership model has no reaping in any path,
  which is why no attribution build was needed.
  **Not a demo risk** (a presentation starts the desktop once or twice; ~157 MB after ten
  restarts on 4 GB) and **deliberately not fixed before the demo**: per-client BO reaping
  touches `va_alloc`/`va_free` and the VA page tables, the code whose bugs previously caused
  render wedges and VA exhaustion. Good post-demo item.
  Detail + the harness lesson (`--idle-secs` is idle *detection*, so a silent soak looks idle —
  cost 2 cycles): **[docs/misc/2026-09-08-x-soak-and-v3d-bo-residue.md](../misc/2026-09-08-x-soak-and-v3d-bo-residue.md)**
- **★ FIRST DEMO RECORDING captured** (the standing goal names one).
  `artifacts/hdmi-video/20260908-004253-demo-x-and-quake.mp4` — H.264 1920x1080 @30 fps,
  **300.000 s**, 19.9 MB, sha256 `4cc114f0…`. One unbroken boot: Phoenix-RTOS boots → GPU X
  desktop (wmaker + xterm/xclock/xcalc/xlogo) → **clean X exit** (the bug-#4 fix on camera) →
  QuakeSpasm textured 3D on the start map. **0 faults.** Verified frames: t=110 s full desktop,
  t=265 s Quake with the carved QUAKE archway, both torches lit, lava fall, weapon + HUD —
  demo-quality. `artifacts/` is gitignored, so the file is local; re-take with
  `./scripts/record-hdmi.sh --label <l> --secs N` alongside a cycle run with
  `RPI4B_HDMI_INTERVAL=0`.
- **New SD image structurally verified** (no card in the reader, so it could not be booted;
  QEMU's `raspi4b` lane loads `plo.elf` directly and cannot exercise the firmware→FAT handoff).
  Checked: partition table, complete FAT boot set, `config.txt`, `loader.disk` is the SD variant
  carrying `-r /dev/mmcblk0p2:ext2` pointing at this image's own partition, and **`e2fsck -fn`
  clean**. Details in `manifests/2026-09-08-glamor-chain-demo-image.md`.
- **★ Fixed a build trap that made the image un-recuttable** — the ext2 volume was sized from
  the `--with-showcase` *flag* (256 MiB, or 1.5 GiB with it) while the content came from
  whatever was staged, so `--scope project --variant sd` gave a 256 MiB volume for a 682 MB
  rootfs and mke2fs died "Could not allocate block". `build-rpi4b-rootfs-ext2.sh` now measures
  the tree it is about to populate (`c406f3990`).
- **★ Fixed the flash instruction the owner will actually run** — the `dd` line in §2 still
  named `rpi4b-sd-2part-allfixes-20260907.img`, a file that no longer exists (only the header
  had been corrected). Now names the verified artifact, with unmount+sync. Image re-verified
  byte-intact (`0ddcee67…aaaa91e`), and its sha256/size/flash command are now recorded in
  `manifests/2026-09-07-drivable-demo-image.md` so what gets flashed can be checked against
  what was gated. Swept every other `.img` reference: the rest are historical manifests, correct
  as records.
- **`scripts/record-hdmi.sh`** — continuous HDMI capture to a shareable MP4
  (`artifacts/hdmi-video/`), for the owner's "screen recording published online". Only PNG
  snapshots existed. Single-opener card, so pair it with `RPI4B_HDMI_INTERVAL=0` on the cycle;
  the script fails early with that hint. Takes neither the UART nor the Pi lock.
- **★ Bug #4's kernel fault FIXED and HW-verified** (§3) — three kernel lifetime bugs, root
  cause decoded from the ASCII in the register dump.
- **★ Demo state restored and verified:** a full `--scope core --with-showcase --with-ports`
  rebuild, then a **pristine NFS export** regenerated from it. The export had lost every
  Quake binary; it now carries all five engines + their data + the new `/etc/ssl`, 17/17
  required paths. HW: `startx_gpu deskapps` renders wmaker + xterm + xclock + xcalc + xlogo
  **cleanly, 0 faults**, on the new kernel — and `/etc/ssl/certs/ca-certificates.crt` is
  present on the Pi (240216 B).
- **★ Bug #2 root-caused to OUR OWN Mesa patch** (§3).
- **`tools/x11-port/xresizer`** — the mouse-free resize probe that finally made #1 testable;
  it cleared the plain resize path on its first run.
- **Trusted root CA store shipped** (owner request, same evening) — see §2.5.
- **Weekly-log rollover W36 → W37**; the autonomous heartbeat cron was re-created (the
  previous one had expired, which is why progress had stalled between sessions).



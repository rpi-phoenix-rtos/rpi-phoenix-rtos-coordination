# Owner manual-test round 2 (2026-08-22) — issues to investigate (no-Pi analysis done; Pi-repro queued)

Owner tested Q1 (quakespasm) + vkQuake after the Tier-0 GL scanout fix. Q1 "works very
well". Four issues reported/observed. Analysis below done WITHOUT the Pi (owner was
re-testing Q2/Q3 on the shared UART). Each has a Pi-repro/fix step for when the Pi is free.

## 1. Q1 (quakespasm) — broken textures on SOME health/ammo boxes (minor glitch)
Overall Q1 render is excellent; narrow texture-specific corruption on some pickup boxes.
- Health/ammo boxes are brush-model pickups (`maps/b_*.bsp`: b_bh10/25/100, b_shell0/1,
  b_nail0/1, b_rock0/1, b_batt0/1) using small "+"-prefixed miptextures (some animated/toggled).
- HYPOTHESIS (most likely): a V3D **UIF tiling edge case for a specific small / NPOT texture
  dimension** — same family as the q3dm7 lightmap-black + quake2 floor-speckle. Small SAMPLED
  textures stay TILED (UIF) after the #12 fix (only large >=1024 RTs go RASTER); if a particular
  box texture's dims tile wrong, the TMU samples garbage. quakespasm sets gl_texture_NPOT (V3D
  advertises ARB_texture_non_power_of_two) so NPOT box textures upload directly and V3D tiles them.
  Alt hypotheses: texture-animation frame selection ("+0"/"+1", "+a"/"+b" toggle), or mipmap gen.
- NO-Pi done: texture path is external/quakespasm/Quake/gl_texmgr.c (TexMgr_Pad / NPOT at :748).
- PI-REPRO (Pi free): frame-dump a scene containing boxes (e1m1 has health+ammo near spawn) via the
  TCP-sink SSIM harness (quake-host-capture / quake-visual-compare), diff vs the host quakespasm
  reference → identify WHICH box texture is wrong + its dims → check that texture's tiling
  (RASTER vs UIF) + the miptex dims in libv3d/gl_texmgr. Likely a small-NPOT UIF fix in the same
  spot as [[project_quake3_lightmap_uif_xor]].

## 2. vkQuake — three sub-issues (a: fire pits, b: lighting, c: crash)
vkQuake torches ARE fixed (d3e329c, this session). Remaining:

### 2a. Fire-pit flames MISSING on start map (torches OK, fire pits not)
The archway torches render now, but the start-map fire pits' flames are absent. The fire pit
flame is a DIFFERENT effect than the torch flame model — likely a particle effect / different
sprite/model or a `flame2`/lava-pit entity. d3e329c fixed opaque alias models (alpha=1); the
fire-pit flame probably uses a separate path (particle system, additive sprite, or a
different-flagged model) that still renders invisible/absent. NEXT: identify the start-map
fire-pit entity + its render path in external/vkquake (r_part / r_sprite / r_alias) and what
differs from the now-fixed torch alias path.

### 2b. Lighting wrong at some spots (dark / unnatural / flicker)
Some walls/floors constantly dark or unnaturally lit; some places light flickers strangely.
Owner marked a bad spot: VIEWPOS `(63 1696 104)` on the start map. HYPOTHESIS: lightmap /
lightstyle handling in V3DV — static lightmap sampling wrong (dark/unnatural = lightmap texture
tiling/coord issue, possibly the same UIF class) and/or the animated lightstyle update path
(flicker = lightstyle animation writing the lightmap texture each frame via a
buffer->image copy that's mis-extent, cf. the #29 degenerate-copy work in the V3DV commits).
NEXT: check vkQuake's lightmap upload/update path on V3DV (R_UploadLightmaps / dynamic lightstyle)
+ whether the lightmap texture is tiled correctly.

### 2c. CRASH after playing a bit (memcpy/memmove overrun in stdio during Con_Printf)
Log: artifacts/rpi4b-uart/20260822-221322-live-test.log. addr2line'd against /tmp/vkquake-phoenix
(= the staged /bin/vkquake, not stripped):
- Fault backtrace: `CL_ParseServerMessage (cl_parse.c:2159) -> Con_LogCenterPrint (console.c:705)
  -> Con_Printf (console.c:503) -> write_buffer (libphoenix stdio/file.c:554) -> memmove/memcpy
  -> Data Abort (EL0), far=0x0fdef000` (a page boundary just past a ~0x0fdeef70 heap buffer).
- Then the signal delivery DOUBLE-FAULTS: `_signal_trampoline (signal.S:38)` -> PC alignment fault
  at pc=0xa000000000009036 (garbage) — same signal-on-corrupted-stack cascade as
  [[project_coreutils_cksum_od_dataabort]].
- MULTIPLE threads faulted: thread 58 (Data Abort in memcpy, main game loop) AND thread 61
  (Instruction Abort with x4..x28 = an incrementing byte pattern 0x0404..04,0x0505..05,…0x1c1c..1c
  and pc=lr=0x1514141414141414 = garbage → its stack/context was overwritten with a byte-pattern).
- write_buffer/buffer_data are bounded PER CALL (min(bufsz-bufpos, writesz)); the memmove at :554
  overruns only if `stream->bufpos` is corrupted. ⇒ HYPOTHESIS: **concurrent Con_Printf from
  vkQuake task-pool WORKER threads racing the same stdout FILE** → torn stream->bufpos → memmove
  overrun; OR a worker stack overflow overwriting an adjacent thread's context (the byte-pattern
  thread 61). Triggered by a CENTERPRINT server message while playing (item/trigger text).
- NO-Pi NEXT: audit libphoenix FILE locking (does every stdio write hold stream->lock? is the lock
  per-FILE and re-entrant-safe?) vs vkQuake calling Con_Printf/Sys_Printf from worker threads;
  check vkQuake's threaded logging. PI-REPRO: reproduce under libdbg/QEMU-gdb with the centerprint;
  confirm bufpos corruption + which thread. If libphoenix stdio isn't thread-safe for concurrent
  writers, that's a libc-level fix helping many multi-threaded ports.

## 3. USB enumeration FAILED after reboot (intermittent)
Log: artifacts/rpi4b-uart/20260822-222349-live-test.log.
- `xhci: capProbe OK; usb: New device 3.0 root hub; xhci: command completion code 36; usb: Fail to
  get device descriptor; (code 19 x2); usb: Enumeration failed despite 3 attempts.` Root hub comes
  up but the downstream VL805 device-descriptor fetch fails.
- HYPOTHESIS: **stale VL805/xHCI controller state after a WARM reboot** — [[project_pi4_xhci_crcr_stale_after_hcrst]]
  (HCRST does NOT clear the VL805's internal CRCR; a warm reboot leaves the controller poisoned so
  the next boot's USB init enumerates a bad controller). A full power-off/on clears it; warm reboot
  doesn't → matches intermittent-after-reboot. Also related: VL805 firmware (vl805.bin/.sig) is NOT
  served over TFTP (benign "Read failed" in every netboot log — fw loads from EEPROM), so it's the
  controller-state, not missing fw.
- PI-REPRO/FIX (Pi free): compare cold-boot (power-cycle) vs warm-reboot enumeration; if warm-only,
  make the USB/xHCI driver do a FULL controller reset (or the boot daemon BRIDGE-only init per the
  memory) so a warm reboot re-enumerates cleanly. Decode xhci completion code 36/19 exactly.

## Priority (my read; owner may reorder)
Owner explicitly asked for Q1 analysis+fix first. Ordering:
1. **Q1 box textures** (owner's headline ask; likely a small-NPOT UIF tiling fix, tractable).
2. **vkQuake 2c crash** (a crash is worse than a cosmetic gap; possible libphoenix stdio
   thread-safety fix with broad benefit).
3. **USB-after-reboot** (intermittent; workaround = power-cycle; real fix = warm-reboot controller reset).
4. **vkQuake 2a fire pits + 2b lighting** (cosmetic; V3DV render-path digs).

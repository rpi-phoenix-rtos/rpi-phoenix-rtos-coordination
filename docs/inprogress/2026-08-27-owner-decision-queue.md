# Owner decision queue — for Witold's return (as of 2026-08-27, autonomous session)

You've been away a while; the autonomous loop has kept working the master plan. This is the
**short list of decisions that need your call** (the board `autonomous-plan.md` has the full
chronological log; `pi4-hardware-support-matrix.md` has the capability state). Everything below
is at a clean stopping point — nothing is half-broken waiting on you.

## Shipped since you left (headline)
- **SuperTuxKart 1.4 PLAYS on the Pi4** — modern GLES3/SP renderer, one static ELF: boots → full
  engine init → main menu → **fully-lit in-game 3D race** on the V3D over netboot, 0 crashes. This
  killed the old "structurally blocked / defer multi-week" feasibility verdict.
- **Two real V3D driver fixes shipped** (found while bringing STK up, benefit all GPU apps):
  (1) GPU VA window 256 MiB→1 GiB (`GPUVA_PT_PAGES`) — fixes heavy-scene VA-exhaustion crash;
  (2) **QPU-interrupt-ack fix** — the CT1 render stage wedged per-frame under heavy fragment load
  because the poll-based winsys never cleared the QPU interrupt bits (Linux clears full INT status
  every IRQ); fix = Linux-parity clear+service → 330 wedges/run → 0, dark scene → fully lit.
- **Mesa on-disk shader cache implemented** (was stubbed) — GL apps no longer recompile shaders on
  the V3D every boot (HW-verified). Footgun documented: bump `V3D_PHX_CACHE_VERSION` on Mesa
  QPU-codegen changes (no build-id auto-invalidation).
- **ffmpeg → framework port** (P8) done; **python → framework port DONE** (HW-validated).
- **Port-finalization batch (all HW-verified + pushed, then integration-verified in a clean
  `--with-ports` image, 0 faults):** coreutils → **full tool set** (`stty` was the last skip);
  **jq regex builtins** (new `oniguruma` port); **Python `_blake2`/`_bz2`/`_lzma`** in both the dev
  and the shippable framework port (full `tarfile` gz/bz2/xz + blake2 hashlib); **busybox `awk`(+libm)
  / `xzcat`·`unxz` / seamless `tar`**; **curl gzip/deflate** decoding; **3 new reusable lib ports**
  (oniguruma, bzip2, xz). Also: **qemu 11.1** host toolchain + native `qemu-debug.sh`; a latent
  **stale-`.toolchain`-libphoenix ABI bug** found+fixed (would break any fresh port rebuild); README +
  TUTORIAL refreshed with STK + the new ports (per your docs request).

## DECISIONS AWAITING YOU (ranked)

1. **HEVC hardware video decode — pursue the full port, or leave banked?**
   Scoped + M0 HW-proven this session. Verdict: HW **H.264 decode is a VCHIQ/firmware wall** (~27k LOC,
   WiFi-scale) → banked. HW **H.265/HEVC via the `rpivid` block IS tractable** (directly-MMIO, no VCHIQ);
   `tools/hevc-probe` proved the block is reachable (version 0x202, clock via mailbox). **The catch:** it
   decodes **H.265, not the H.264** your e4-play clips use — so it's only worth ~4-8 weeks if your media
   target moves to H.265. **My call:** banked pending your content-strategy decision. Detail:
   `project_ffmpeg_hw_decode_scope` / the matrix "Video decode" row.

2. **gcc-16 promotion to the default toolchain?**
   The gcc-16.2.0 cross-toolchain builds core+ports clean AND boot-verified on HW (E10). Remaining = the
   *promotion* (swap gcc-16 → default `.toolchain/`, full-flow rebuild, manifest) — HIGH blast-radius, so
   I left it owner-attended (gcc-14 rollback kept). **Your call:** promote now, or keep gcc-14 default.

3. **WiFi data-plane — keep digging, or accept wired-only for now?**
   Control-plane is up (associates + 4-way-keyed to a real WPA2 AP). Data-plane doesn't carry traffic yet
   (TX reaches firmware, not the air — SDPCM seq/credit); it's banked at the firmware-opaque wall (I've
   avoided blind-coding SDPCM). **Your call:** prioritize a deeper WiFi dig, or stay on wired Ethernet.

4. **q3dm7 intermittent GPU binner wedge (CT0) — worth an attended dig?**
   Separate from the (now-fixed) CT1 render wedge; different signature, intermittent, owner-attended-adjacent.
   quake3 renders correctly (SSIM 0.989); this is a dropped-frame robustness issue, recovered by the winsys
   reset. **NOW MEASURED (2026-08-27): the QPU-int fix does NOT fix it.** Relinked quake3e against the
   QPU-int-fixed libv3d + ran a 7-trial q3dm7 bench: **wedge rate 3/7 (~43%)** — statistically the same as the
   historical ~50%, so the STK CT1-render fix leaves the q3dm7 CT0 binner wedge untouched (confirmed distinct
   bug). **Good news: all 7 trials RENDERED, 0 faults** — the winsys reset fully recovers it every time (a
   dropped-frame robustness issue, not a crash or corruption). **My call:** stays banked — it needs a genuine
   attended CT0-binner dig (the front-end pipeline-stall root-cause, see `project_quake3_lightmap_uif_xor`),
   not a side-effect of another fix. Worth it only if you want smooth (no-drop) q3dm7; correctness is fine.

5. **Ship CPython in the default image? (framework python is built + HW-validated, `if:false`)**
   The framework python port is feature-complete (zlib/bz2/lzma/ssl/hashlib+blake2/sqlite3/ctypes/decimal)
   and passes all selftests on HW. It is NOT in the default image (`if:false`) purely because of **size** —
   your call. Concrete footprint (measured): binary **9.5 MiB stripped** (or 57 MiB non-stripped, kept only
   for the `.so` C-extension `dlopen` recipe) + stdlib Lib tree **14 MiB trimmed** (drop test/idlelib/tkinter)
   or 52 MiB full. So on today's **66 MiB** image, shipping python adds **~+23 MiB (stripped+trimmed) up to
   +109 MiB (full+non-stripped)**. **My recommendation:** if you want python in-image, ship stripped binary +
   trimmed/pyc Lib (~+23 MiB); keep non-stripped only if you need on-device `.so` extension loading. Flip is a
   one-line `ports.yaml` `if:false`→`if:true`. **Your call:** ship it (which variant?) or keep it build-on-demand.

6. **Upstream B5** (the one deferred B-item) — lowest rpi4 value, cross-board/attended. Your call whether
   it's worth an attended pass.

7. **v3d-driver-port placement — the last P8 item.** Everything else in the "move tools/ → framework ports"
   directive is DONE (libpng…glib2, ffmpeg, python, all 4 game ports — every tools/ port is now a registered
   framework port). The ONLY holdout is the V3D driver itself: does it become a `phoenix-rtos-devices` GPU
   *component*, or stay a `tools/` build producing `tools/.gpu-libs/*.a`? The game ports currently anchor to
   `tools/.gpu-libs/` (the STK precedent); that anchor changes if V3D moves to devices, so I left the
   placement to you. **My call:** low urgency (it works as-is); decide when you next touch the GPU stack.


## Status of autonomous work (updated late 2026-08-27)
- **The unattended-tractable port backlog is now DRAINED and integration-verified.** Both survey rounds'
  tractable items are done (see the batch above), and a clean `--with-ports` build composed them all into a
  verified bootable image (manifest `2026-08-27-ports-batch-integration.md`), boot-tested on HW (jq regex,
  awk, xzcat, stty all work, 0 faults).
- **What's left is exactly the 7 decisions above — all need your input.** They're firmware-walled
  (WiFi/HW-H.264), high-blast-radius (gcc-16 promotion), policy (ship-python size, v3d placement), or
  attended-dig (q3dm7 wedge, B5). I've deliberately NOT forced low-value make-work or blind-coded past the
  firmware walls.
- Bounded residue I can still pick at without you (each investigated this session):
  - **QPU-int fix propagation to the GL stack** = just relinking the showcase apps against the shared
    (already-fixed) libv3d — mechanical, done implicitly by the next `--with-showcase` build.
  - **"Mesa NULL-BO-alloc crash" — re-scoped:** the winsys is ALREADY defensive (VA-exhaust and BO-table-full
    both log + return `-ENOMEM`, `v3d_phoenix_winsys.c:551/560`); the residual NULL-deref is one layer up in
    ported Mesa's BO-alloc caller (a Mesa-patch surface that touches every GPU app → needs a full showcase
    build + GPU boot to de-risk), and it's now rare given the 1 GiB VA window. Lower priority than its risk.
  - **q3dm7 CT0 wedge vs the QPU-int fix — DONE (2026-08-27):** measured, the fix doesn't help (rate 3/7 ≈
    historical 50%; all trials still render, 0 faults). Result folded into decision #4. It now needs a genuine
    attended CT0-binner dig, which is owner-gated — not a further unattended bench.
  - Otherwise I'm honestly at the "needs owner input" boundary and will not manufacture low-value churn.

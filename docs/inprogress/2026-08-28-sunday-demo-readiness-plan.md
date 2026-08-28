# Sunday demo-readiness plan (owner deadline)

**Owner (2026-08-28):** by **Sunday** needs a **stable system** to (a) capture screenshots of
different apps + games running and (b) record a screen-capture video of the system. Plan the
work for **Friday + Saturday**; on **Saturday night → Sunday** do a **full clean rebuild of
everything** so Sunday has a stable image. Earlier is better.

Timeline (today = Thursday):

| Day | Goal |
|---|---|
| **Thu (today)** | Establish the demo baseline: a clean `--with-showcase` build + boot, HDMI-verify each demo app/game renders. Triage anything demo-blocking. |
| **Fri** | Fix any demo-blockers; curate the demo set (which apps/games, launch commands); ensure everything is committed + pushed. Additive non-demo work (HEVC M1, WiFi) only if it can't destabilize the showcase image. |
| **Sat** | Final polish + a full dry-run: clean `--with-showcase` SD image build + boot + run the whole demo set from the real image. Freeze the demo-relevant repos (no risky merges after this). |
| **Sat night** | **Full CLEAN rebuild of everything** (nuke `.buildroot`, `--with-showcase`, gcc-16 default) → the authoritative stable Sunday image; flash to SD + keep a netboot copy. |
| **Sun** | Owner captures screenshots + video. I stay on standby for any quick fixes. |

## The demo set (candidate — curate Fri)

All GPU/HDMI, screenshot-worthy:
- **SuperTuxKart 1.4** — modern 3D kart racer on V3D (`stk`, `stk -N --track=olivermath`). Headline.
- **GLQuake** (`rpi4-quake`) — textured 3D on V3D, the flagship.
- **Quake II / Quake III** (`quake2` / `quake3 +devmap q3dm7`) — textured 3D. (q3dm7 wedge is reset-recovered = fine to demo; renders every boot.)
- **vkQuake** (`rpi4-vkquake`) — Vulkan/V3DV render (no input, but renders the map — good for a screenshot).
- **X11 desktop** — `startx` (WindowMaker) and `startx_gpu` (glamor GPU-accelerated), with xterm, `mc`, `dillo` (web page), xcalc/xclock/xeyes.
- **CLI ecosystem** — a terminal montage: `python3` REPL, `jq`, `sqlite3`, `redis-cli`, `coreutils`, `busybox awk`, `bash`.

## Stability rules until Sunday

- **No destabilizing merges** to the demo path (kernel/devices/lwip/libphoenix/Mesa/games/X) after the Sat freeze.
- HEVC M1 + WiFi work stays in **separate `tools/` probes / `if:false` ports** — never wired into the showcase image before Sunday.
- The Sat-night rebuild is CLEAN (`--full-clean` / nuked `.buildroot`) so the Sunday image has no stale-object surprises. gcc-16 is the default toolchain (certified).

## Status log
- (Thu) Plan created. Next: launch the `--with-showcase` baseline build + boot-verify the demo set.

## Update (Thu, owner clarifications)
- **Demo target = NETBOOT NFS ROOT, not SD** (SD tests are post-Sunday). So the Sat-night clean rebuild
  produces the netboot **kernel (TFTP)** + populates the **NFS root** (`/srv/phoenix-rpi4-nfs-gcc16`) with all
  demo apps/games; no SD flashing needed for Sunday.
- **Keep making progress on WiFi and/or HW video decode** — owner: important milestones, good if running by
  Sunday. So they continue IN PARALLEL as additive `tools/` work; still must not destabilize the netboot demo path.
- **Baseline build finding:** the `--with-showcase` image built + exported (Verification OK), core X desktop
  present (Xphoenix/wmaker/xterm/dillo/mc/xcalc/startx). One gcc-16 regression fixed: the ad-hoc x11-port's
  **libXt** failed on C23 `&true` → pinned `-std=gnu17` (confirmed). TODO: verify GLQuake/STK/Q2/Q3 are staged to
  the netboot NFS root (only vkQuake was in the showcase `_fs`; the others are netboot-staged separately).

## Baseline VERIFIED (Thu) — netboot NFS root is demo-ready NOW
The current `/srv/phoenix-rpi4-nfs-gcc16` already has the full demo set (built across this session):
- **Games:** `bin/stk` + `usr/bin/supertuxkart` (38MB) + assets; `usr/bin/rpi4-quake` (GLQuake); `usr/bin/quake2`;
  `usr/bin/quake3`+`quake3e`; `usr/bin/rpi4-vkquake`; `bin/quakespasm`. Data: quake/quake2/quake3/supertuxkart.
- **X desktop:** Xphoenix, wmaker, startx, startx_gpu, xterm, dillo, mc.
- **CLI:** the full --with-ports set (python/jq/sqlite/redis/coreutils/busybox/curl/…).

### ★ Sat-night clean-rebuild + restage sequence (the key demo gate — must be repeatable)
`--with-showcase` rebuilds the kernel + GLQuake + vkQuake + X + CLI, but NOT the extra games (STK/Q2/Q3 are
built-on-demand). So the Sat-night sequence is:
1. Nuke `.buildroot` → full clean `rebuild-rpi4b-fast.sh --with-showcase --with-ports` (gcc-16 default) → fresh
   netboot kernel + base NFS root (GLQuake, vkQuake, X, CLI).
2. Build the extra games: STK (`build-supertuxkart*`), quake2 (yquake2), quake3 (quake3e) — their build scripts.
3. Stage ALL demo games + launchers + assets into the netboot NFS root (the per-game deploy done this session).
4. **★ CLEAR THE MESA SHADER CACHE:** `sudo rm -rf /srv/phoenix-rpi4-nfs-gcc16/.mesa-shader-cache` — MANDATORY
   after any toolchain/Mesa change. The cache has NO build-id invalidation ([[project_v3d_shader_disk_cache]]); a
   stale gcc-14-compiled blob loaded by the gcc-16 Mesa makes the GPU run wrong QPU binaries → **green-speckle
   corruption** (HW-confirmed on STK's loading screen 2026-08-28; `rm -rf` → clean). The first boot after clearing
   pays a one-time cold recompile (STK ~52 blobs); subsequent boots are warm+clean.
5. **Do NOT restart nfs-server right before a boot** — the server's ~90 s post-restart GRACE period makes the Pi's
   `exec` fail with `err=-34` (ERANGE) mid-boot. The export is fine as-is once games have run on it; only restart
   nfsd if you hit genuine `NF4ERR_EXPIRED` staleness, then wait ≥120 s before booting.
6. Boot-verify the whole demo set over netboot (HDMI snapshots) before Sunday.
**Fri task:** script/checklist steps 2-3 so the Sat-night rebuild is one repeatable run (no ad-hoc per-game deploys).
**Fri task:** end-to-end boot-verify each demo item renders on the FRESH gcc-16 kernel (startx, stk, rpi4-quake, …).

## Thu (late) — X demo debugging
- **X server + WindowMaker come up on the fresh gcc-16 kernel** (`startx`), cursor drawn, 0 faults — but bare
  wmaker shows a black root (no apps/background); need a windows-visible launch for a good screenshot.
- **`startx deskapps` failed → root-caused:** the apps (xterm/xclock/xcalc/xeyes) connect fine, but **twm** (the
  WM, client[0]) fails `XOpenDisplay` and xlaunch tears the session down. Cause: the deployed **twm is a stale
  08-08 binary** whose old libX11 transport can't talk to the current Xphoenix (`SocketOpenCOTSClient: unable to
  open socket`); it wasn't rebuilt because the x11-port build died at the libXt/gcc-16 C23 `&true` error.
- **Fixes:** (1) libXt `-std=gnu17` (committed) — lets the x11-port build finish + rebuild twm/apps fresh;
  (2) xlaunch connect-probe + 800ms handshake settle (committed) for first-client robustness.
- **In progress:** full x11-port rebuild (fresh libXt + twm + apps against current libX11) → then re-test the
  desktop demo (`deskapps` / GPU `showcase`).

## Fri (early) — fresh-build demo verification + reproducibility fixes
- **★ ALL 5 HEADLINE GAMES render CLEAN on the fresh gcc-16 netboot rootfs** (HDMI-verified, evidence in
  `docs/inprogress/evidence/2026-08-28-fresh-build-*`): **GLQuake ✅ · Quake III q3dm7 ✅ · Quake II demo1 ✅ ·
  vkQuake (Vulkan) ✅ · SuperTuxKart ✅** (loading screen clean once the stale shader cache was cleared).
- **★ STK green-noise root-caused = STALE MESA SHADER CACHE** (not a GPU wedge). The gcc-16 promotion rebuilt the
  host Mesa → QPU codegen changed, but the persistent NFS-export cache still held old gcc-14 blobs → wrong QPU
  binaries → green speckle. `rm -rf .mesa-shader-cache` → clean. **Folded into the Sat-night recipe (step 4):
  clearing the cache is MANDATORY after any toolchain/Mesa change.** Also: do NOT restart nfsd right before a boot
  (grace period → exec err=-34).
- **★ Desktop fonts now REPRODUCIBLE** (was the biggest desktop blocker for the Sat-night rebuild): the base build
  produces no scalable TTF/fonts.conf/cache, so wmaker died "could not load any fonts" on a fresh export. Added
  `scripts/stage-desktop-fonts.sh` (host DejaVu + the self-contained fonts.conf at `tools/x11-port/fontconfig/` +
  `fc-cache --sysroot`), wired into `sync-netboot-tree.sh` so **every restage guarantees the fonts**. Verified
  fc-match resolves sans serif/Sans/monospace/serif → DejaVu.
- **REMAINING X-desktop blocker (next):** even with fonts, the wmaker root is black and app windows aren't visibly
  drawing (needs a Pi-cycle debug). `twm` as WM client[0] fails XOpenDisplay against the current Xphoenix (deskapps
  switched to wmaker as a workaround). This is the last thing standing between the demo and a good *apps* screenshot;
  games are done.

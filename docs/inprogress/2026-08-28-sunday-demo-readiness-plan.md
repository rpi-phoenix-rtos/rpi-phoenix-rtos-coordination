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

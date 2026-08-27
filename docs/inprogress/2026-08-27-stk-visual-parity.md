# SuperTuxKart 1.4 — Pi4/Phoenix vs host-Linux visual-parity comparison

Owner request (2026-08-27): compare STK rendering on the Pi4 (Phoenix-RTOS, V3D 4.2)
against a host-Linux reference (modern AMD GPU / Ubuntu), like the Quake host-vs-Pi
harness. Host = reference.

## Verdict

**The Pi4/V3D render matches the host AMD-GPU reference closely.** For the two genuinely
render-comparable scenes the structural similarity is strong:

| scene | SSIM | MAE (0-255) | assessment |
|---|---|---|---|
| **in-game race** (olivermath, behind-Tux) | **0.873** | **8.9** | strong match — the crux 3D scene |
| **loading screen** | **0.913** | 10.1 | strong match |
| main menu | 0.46* | — | layout-identical; *SSIM depressed by the Pi's first-run tutorial modal overlay, NOT a render difference |

Side-by-sides (Pi left \| host right), `docs/inprogress/evidence/2026-08-27-stk-parity-*.png`
(full-res composites in `artifacts/stk-compare/sidebyside/`):
- `…-race-pi-vs-host.png` — the in-game render
- `…-loading-pi-vs-host.png`
- `…-menu-pi-vs-host.png`

## Method (fair, same inputs both sides)

- **Same engine:** STK **1.4** on both — Pi from the framework port (`sources/phoenix-rtos-ports/supertuxkart`), host built from the same 1.4 source (apt's 1.5 rejected).
- **Same assets:** the host pointed at the Pi's live NFS export (`SUPERTUXKART_ASSETS_DIR=/srv/…/stk-assets`) = the identical mobile-reduced 1.4 asset set the Pi mounts — so the comparison isolates GPU/driver rendering, not asset differences.
- **Host GPU = real hardware GL:** `AMD Radeon 780M (radeonsi)`, OpenGL 4.6 Mesa — NOT llvmpipe.
- **Pi GPU:** V3D 4.2 via the ported Mesa gallium driver, GLES3/SP renderer.
- **Metric:** grayscale SSIM + RGB MAE + side-by-side composites, `scripts/stk-visual-compare.py`.

## The in-game race render (the key result)

Both frames are the behind-Tux start-grid view on Oliver's Math Class: the red "TUX" kart,
a 2nd kart, and the full classroom — LEGO-block edge walls, blue chairs, notebook/pencil
barriers, the MULTIPLES board, globes, the SUPER-TUX-KART poster, ceiling lights, HUD minimap.
They render near-identically (SSIM 0.873, MAE 8.9). This confirms the Pi's V3D pipeline
produces the same textured, lit 3D scene as the reference AMD GPU.

## Honest caveats

- **STK is not frame-deterministic** (unlike the Quake replay harness), so the race SSIM is
  an *indicator* and the side-by-side is the primary judgment. The frames were matched at the
  behind-kart start-grid (the most deterministic corresponding moment).
- **Cosmetic/state differences (not render differences)** that lower the raw SSIM:
  - AI opponent kart differs (purple on the Pi run, green on the host) — random per race.
  - Kart santa-hat cosmetic on the Pi (STK seasonal/xmas mode) vs plain on the host.
  - The Pi frame shows the ceiling start-light + bird; the host camera's slightly lower pitch cut them off.
  - Loading screen: the Pi shows the kart-icon row; the host loads too fast for icons to populate.
  - Main menu: the Pi shows STK's first-run "play tutorial?" modal (a UI state overlay); the menu
    itself — logo, the 5 buttons, toolbar, background — renders identically (visible around the modal).
- **Graphics preset not pinned:** the host auto-selected a moderate preset (dynamic lights on,
  shadows/bloom/glow off); the Pi's `config.xml` was not on the export (STK defaults). Output
  varies with these — a small residual source of difference beyond the GPU/driver.

## Bottom line

STK renders on the Pi4/V3D essentially the same as on a modern AMD GPU for the scenes tested —
strong quantitative parity on the in-game race + loading, and a layout-identical menu. The
differences are cosmetic/state, not rendering defects.

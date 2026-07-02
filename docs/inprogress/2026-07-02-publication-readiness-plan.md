# Publication-readiness plan — build Phoenix-RTOS RPi4 from an empty Ubuntu dir

**Date:** 2026-07-02  **Task:** #71  **Status:** plan for review (not yet executing)

## Goal / success criterion

A newcomer on a **stock Ubuntu x86_64 machine** with an **empty directory** can, following one
document, end-to-end:

1. clone the project,
2. install host dependencies,
3. fetch all source repos + external deps + build the toolchain,
4. build a bootable **SD-card image** of Phoenix-RTOS for the Raspberry Pi 4,
5. flash it to a card and boot it on real hardware —

**without any of the author's lab hardware** (smart plug, USB-UART, HDMI capture, dedicated netboot
NIC) and **without editing any script for host-specific paths.**

The acid test (Phase 5) is doing exactly that on a clean machine, from the docs alone.

## What already exists (we are hardening, not starting over)

The inventory (subagent, 2026-07-02) found the core machinery is already scripted:

- `scripts/bootstrap-linux-host.sh` — clones the 16 sibling repos (wires `fork` + `origin` remotes),
  sets up the `.venv`, sparse-checks-out `raspberrypi/firmware` blobs, installs apt deps.
- `scripts/build-phoenix-toolchain-linux.sh` — builds the `aarch64-phoenix` gcc-14.2.0/binutils-2.43
  cross toolchain into `.toolchain/` (idempotent).
- `scripts/prepare-buildroot.sh` → `phoenix-rtos-build/build.sh` → `scripts/build-rpi4b-rootfs-ext2.sh`
  → `scripts/assemble-rpi4b-sdimg.sh` — assemble buildroot, build core+ports+project, make the ext2
  rootfs, and produce the 2-partition SD image.
- `docs/knowledge/linux-host-bootstrap.md` — a good human-facing bootstrap description (but mixed
  with lab-hardware wiring).

So the deliverables below are: **consolidate + pin + de-hardcode + separate build-from-lab + document
+ validate on a clean machine.**

## Decision points that need the user (these gate the design)

**D1 — DECIDED (2026-07-02): personal-account forks under `github.com/houp/*`.** The bootstrap will
clone the 16 siblings + external pins from `github.com/houp/<repo>`. **Publishing (pushing) those forks
public is a user action** — it needs the user's GitHub push access; the agent cannot push to the
user's account. The agent prepares everything that *consumes* those URLs (bootstrap, pin manifest,
docs); the user does the actual `git push` to make `houp/*` public. (Original options kept below for
context.)

**D1 options (for context):** The Pi4 changes are committed only on local `houp/*`
forks. Users can't clone what isn't public. Options:

| Option | Pro | Con | Recommendation |
|---|---|---|---|
| **A. GitHub org with per-repo forks** (e.g. `github.com/<org>/phoenix-rtos-kernel` …) | Preserves per-repo git history + upstream-tracking + contribute-back path; bootstrap already assumes per-repo clones | 16 repos to publish + keep in sync; contributors juggle many repos | **Recommended** — matches the existing multi-repo build and phoenix-rtos upstreaming goal |
| **B. Single vendored monorepo snapshot** | Simplest for users (one clone); trivially reproducible | Loses per-repo history + clean upstream diffs; diverges from phoenix-rtos layout; harder to upstream | Fallback if simplicity ≫ upstreamability |
| **C. User-account forks** (`github.com/houp/*`) | Zero new infra | Ties project to a personal account; awkward for an org/community | Only as a stopgap |

D1 changes Phase 1's bootstrap (which URLs it clones, whether it clones 16 repos or 1).

**D2 — How much of the lab automation ships as "supported"?** Recommendation: publish everything,
but the docs present two tiers — **Tier 1 "Build & flash" (portable, required, zero special HW)** and
**Tier 2 "Author's test lab" (netboot/UART/HDMI/power automation, optional, hardware-specific)** — so a
newcomer is never blocked by hardware they don't have.

**D3 — Reproducibility bar.** Recommendation: ship a top-level **pinned manifest** (all repo SHAs of a
validated state, reusing the existing `manifests/*.md` mechanism) so a fresh clone reproduces a
known-good image bit-for-bit, plus a "latest/floating" mode for developers.

## Phases

### Source-acquisition reality (refined 2026-07-02 — answers to user Qs)

The external/port sources are **heterogeneous** — five distinct mechanisms, and the publication
bootstrap must handle each:

1. **`external/linux` (7.0 GB) — NOT needed for the build; the DTB is FETCHED, never compiled.**
   Per user directive (2026-07-02): the Pi4 device tree comes from the **`raspberrypi/firmware`**
   repo (https://github.com/raspberrypi/firmware) — which ships `boot/bcm2711-rpi-4-b.dtb` ready-made
   — **not** compiled from kernel `.dts`. The bootstrap already sparse-checks-out that repo for the
   boot blobs (`start4.elf`, `fixup4.dat`, …), so the **same fetch provides both the boot blobs and the
   DTB**; no `dtc`, no kernel source. → **Action:** in `prepare-rpi4b-dtb.sh`, make the firmware DTB the
   sole supported source and **remove the `dtc`-compile-from-`external/linux/.../bcm2711-rpi-4-b.dts`
   fallback branch** (it needs the 7 GB clone and never fires). Drop `external/linux` from the
   publication bootstrap entirely (mention it only as an optional, out-of-band *research* clone).
2. **`external/mesa` (1.1 GB) — REQUIRED for the build.** The V3D GPU / GL / Vulkan stack
   (`tools/v3d-driver-port`, `quakespasm-port`, `vkquake-port`) consumes `external/mesa/include` +
   `src/broadcom`. → **Clone + pin** in the bootstrap.
3. **`phoenix-rtos-ports` ports** (windowmaker, xterm, curl, busybox, openssl…): each `port.def.sh`
   **downloads its upstream tarball at build time** (pinned version+name; needs internet).
4. **`tools/x11-port/build-x11-phoenix.sh`**: **downloads X.org release tarballs at build time**
   (curl from x.org / xorg.freedesktop.org into `tools/x11-port/src`) + applies patches. Needs internet.
5. **`tools/ports/src/`** (dillo, fltk, glib, libffi, libiconv…): **vendored tarballs + extracted trees,
   currently UNTRACKED** (not gitignored, just never committed). The *build scripts* (`tools/ports/
   build-dillo.sh` …) are tracked, but the **sources are not** → a publication blocker: either commit
   (vendor) these tarballs, or replace with pinned-URL+checksum fetch scripts.

**Consequence:** the build is **not fully offline today** — it fetches port tarballs (phoenix-rtos-ports
+ X.org) at build time. For publication, decide: (a) accept build-time downloads (simplest; pin
versions, document that a network is required during build), or (b) add a `fetch-sources.sh` that
pre-downloads+checksums everything for offline/reproducible builds (more work, fully deterministic).
Recommend (a) first, (b) as a hardening follow-up.

### Phase 1 — Reproducibility foundation (one `bootstrap.sh`, pinned)
- Promote `bootstrap-linux-host.sh` to a single canonical `bootstrap.sh` that, from an empty dir:
  clones coord repo (if not already) → clones all siblings **and** the `external/` clones (linux, mesa,
  quakespasm, vkquake, rpi-eeprom — currently manual) → builds the toolchain → creates `.venv`.
- Make fork URLs **configurable** (one `REPOS` manifest / env, defaulting to the D1 location) so the
  same script serves the author, a fork-org, and contributors.
- Add a **pinned-SHA manifest** (`manifests/release-<date>.md` or a top-level `versions.lock`) and a
  `--pinned` mode that checks every repo out to the recorded SHA (D3). Reuse
  `restore-integration-state.sh`.
- Pin the `external/` deps too (raspberrypi/linux DTB, mesa, quakespasm, vkquake) — they currently have
  no auto-clone and no pin.
- **Exit test:** on a scratch dir, `./bootstrap.sh --pinned` yields a tree that builds.

### Phase 2 — De-hardcode + guard host/hardware assumptions
- Remove or relativize the macOS `/Users/witoldbolt/…` fallbacks in: `prepare-rpi4b-dtb.sh`,
  `create-rpi4b-first-trial-report.sh`, `qemu-debug.sh`, `export-rpi4b-fat-image.sh`,
  `export-rpi4b-sdimg.sh`, `write-sdimg.sh`, `rpi4_actled_probe_layout.py` (all derive repo root from
  the script location instead).
- Make every hardware handle an **env var with a safe default + a clear "not configured, skipping"
  message** (never a hardcoded absolute path): `MEROSS_PLUG_SCRIPT` (power),
  `RPI4B_SERIAL_DEV` (/dev/ttyUSB0), `RPI4B_HDMI_GRABBER` (/dev/video4),
  `RPI4B_NETBOOT_IFACE`/IP range, `RPI4B_SD_DEV`. Centralize in a committed `.env.example` +
  gitignored `.env.local`.
- **Secrets/PII scrub:** no committed tokens, personal IPs, MACs, or account names in tracked files.
- **Exit test:** `grep -rn '/home/houp\|/Users/witoldbolt' scripts/` returns nothing in the build path.

### Scope discovered during prep (2026-07-02 — refines Phases 2 & 3)

Executing the prep surfaced blockers the first inventory missed (this is what the VM test would have
hit at build time; found statically instead):

- **`tools/` also hardcodes `/home/houp/phoenix-rpi` — 36 files** (`tools/x11-port/build-*.sh`,
  `tools/ports/build-*.sh`, `tools/v3d-driver-port/*.py`, `tools/stress/*`, `tools/demo-apps/*`).
  Agent A's Phase-2 pass covered only `scripts/`. **Extend Phase 2 to `tools/`** (mechanical:
  derive repo root from script location at the right depth). Committed prep so far = `scripts/` only.
- **Automated build vs. manual tool-scripts (the big one).** The automated build
  (`rebuild-rpi4b-fast.sh` → `build.sh project image`) builds core + `phoenix-rtos-ports` (busybox,
  curl, windowmaker, xterm, …, which download tarballs at build time). But the **GPU/GL/Vulkan stack
  and the quake engines are built by manual `tools/v3d-driver-port|quakespasm-port|vkquake-port/*.py`
  runs** that are NOT in the bootstrap or the build; their `.gpu-libs/*.a` archives (~65 MB) are
  neither committed nor regenerated, and `sources/.../rpi4-quake/Makefile` `$(error)`s without them
  (with a hardcoded `GPU_LIBS := /home/houp/.../tools/.gpu-libs`). Likewise several X11 apps are built
  by `tools/x11-port/*.sh` outside the automated build. **Consequence:** a clean-clone `build.sh` today
  reproduces the *base* system and then errors at `rpi4-quake`. **Decision needed (Phase 3):** for the
  showcase apps (X11 extras, dillo, quake, vkquake), either (a) wire their `tools/*` build steps into
  the automated build (reproducible from source; needs external/mesa etc., already auto-cloned now), or
  (b) make `rpi4-quake`/`rpi4-vkquake` optional (base image builds; GPU/quake is an opt-in Tier-1.5
  step), or (c) commit the prebuilt archives. **Recommend (b) for the first release** (base image is
  the core deliverable; quake is a documented opt-in), with (a) as the eventual goal.
- **`tools/ports/src` vendoring:** the port build scripts `curl`-download a tarball if absent, then
  `tar -x` + patch. 315 MB extracted but only **31 MB of tarballs**. **Action:** commit the tarballs
  (offline/deterministic, survives URL-rot) + `.gitignore` the extracted trees; keep the download as a
  fallback. Otherwise a clean-clone build needs internet for these too.
- **`SIBLING_BRANCHES` array is stale** (points at old feature branches; all siblings are on `master`)
  → floating (non-`--pinned`) bootstrap reproduces a different tree. Refresh to `master`, or require
  `--pinned`.

**Revised expectation for the first VM test:** validate the **base image** ground-up (bootstrap →
toolchain → core → phoenix-rtos-ports → rootfs → SD image) — the hard, high-value part — with quake
made optional; the showcase-app build integration is a follow-on chunk.

### Phase 3 — Portable "build & flash" path (Tier 1, the core deliverable)
- Provide one obvious entry point — e.g. `make sd-image` or `./build-sd-image.sh` — that runs
  toolchain-check → buildroot → core+ports+project → rootfs ext2 → SD `.img`, on a stock Ubuntu box
  with **zero lab hardware**. (Composes existing scripts; no netboot/UART/power involved.)
- Document flashing the produced `.img` with `dd` or Raspberry Pi Imager, and the **Pi4 EEPROM
  boot-order** note (SD-boot vs the author's network-first EEPROM).
- Confirm host deps for this path are minimal: toolchain build prereqs, `mtools`/`dosfstools`/`parted`/
  `e2fsprogs`, `device-tree-compiler`, `python3`+`uv`, `git`.
- **Exit test:** the image builds and boots to psh on a real Pi flashed from the `.img` (no lab rig).

### Phase 4 — Documentation (human-facing, the other half of the ask)
- **`README.md`** (top level, currently missing): what this is (Phoenix-RTOS RPi4 port), status matrix
  (boots, SD/NFS root, USB HID, GENET net, V3D GPU + GLQuake, X11+WindowMaker, audio…), a screenshot or
  two, quick links.
- **`docs/BUILD.md` / `QUICKSTART.md`**: the step-by-step empty-dir → booting-Pi walkthrough (Phases
  1+3 as a user narrative), with exact commands and the apt one-liner.
- **`docs/HARDWARE.md`**: the optional Tier-2 lab rig (UART, HDMI capture, netboot NIC, smart-plug,
  self-flash-via-Linux) — clearly marked "not required to build."
- **`docs/KNOWN-ISSUES.md`**: consolidate the `TD-*` debt + current open items (#64 SD fs stack, #67
  torch artifacts, #68 MP-server hang, #69 xbill, #70 dillo TLS, #66 stale X lock).
- **`CONTRIBUTING.md`**: the fork/branch strategy from D1 + how to send changes upstream to phoenix-rtos.
- Leave `CLAUDE.md`/`AGENTS.md` as agent-facing; link them from README under "developing with agents."

### Phase 5 — Clean-machine validation (the real gate)
- Run the whole thing on a **fresh Ubuntu 24.04** (VM, container, or a clean user account) following
  **only** `README` + `BUILD.md` — no tribal knowledge. Every stumble = a doc/script fix.
- Produce an `.img`, flash it, boot a Pi. When a stranger's clean box builds a booting image from the
  docs alone, Phase 5 passes.

### Phase 6 — Release hygiene (optional, do before going public)
- License audit (Phoenix-RTOS is BSD/MIT-ish; ports carry their own — GPL/quake shareware caveats),
  `LICENSE`, per-file SPDX where practical.
- CI smoke (GitHub Actions: bootstrap-pinned → build image → QEMU boot smoke) so the build can't rot.

## Sequencing & effort (rough)

- **Phase 1 + 2** are the load-bearing, mostly-mechanical work (~a focused session each) and can run in
  parallel — they don't need lab hardware.
- **Phase 3** is small once 1+2 land (mostly a wrapper + flashing docs).
- **Phase 4** is writing (can draft alongside 1–3; finalize after Phase 5 surfaces gaps).
- **Phase 5** needs a clean machine (VM/container is fine) and is the true completion signal.
- **Phase 6** is polish.

## Who does what

**User-only (needs your GitHub account):** publish the `houp/*` forks public (`git push` each sibling
+ create the repos), and provide/authorize a clean Ubuntu machine or container for Phase 5.

**Agent (unattended, no lab HW, no GitHub push):** Phase 1 bootstrap consolidation + pin manifest
(pointing at `github.com/houp/*`), Phase 2 de-hardcoding + env-var-izing + PII scrub, Phase 3 portable
`build-sd-image` wrapper + flashing docs, Phase 4 all the docs (README/BUILD/HARDWARE/KNOWN-ISSUES/
CONTRIBUTING), Phase 6 license/SPDX. These can proceed *before* the forks are public (the bootstrap is
written against the `houp/*` URLs and clone-tested once you push). Only Phase-5 clone-and-build
end-to-end validation needs the forks actually published + a clean machine.

## Remaining confirmations before I execute

1. **D2 / D3** — OK to proceed with the two-tier docs framing ("build & flash" vs optional "lab") and a
   pinned-SHA release manifest? (both recommended above)
2. Shall I **start now** on the unattended parts (Phase 2 de-hardcoding + Phase 1 bootstrap + Phase 4
   docs), which don't need the forks public or any lab hardware?

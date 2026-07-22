# Publish plan → github.com/rpi-phoenix-rtos (first public release)

Goal: publish a **self-contained set** of repos into the new empty org
`rpi-phoenix-rtos` that anyone can inspect/study/clone to build a full Pi 4 image.
Abandon the legacy `github.com/houp/phoenix-rtos-*` + `houp/phoenix-rpi` (user
deletes those manually). NOTHING is pushed until the user approves this plan.

## 1. Repo set to publish (20 repos)

Authoritative build set per `scripts/bootstrap-linux-host.sh` (its EXTERNAL_DEPS +
SIBLING lists) and the Dockerfile.

**Coordination repo (1):** `phoenix-rpi` (currently `houp/phoenix-rpi`; the scripts +
`tools/` port glue + docs + manifests + Dockerfile). → `rpi-phoenix-rtos/phoenix-rpi`
(name TBD — see decisions).

**Phoenix-RTOS sibling forks (16):** libphoenix, plo, phoenix-rtos-{kernel, devices,
filesystems, lwip, usb, utils, corelibs, build, ports, posixsrv, tests, project, doc,
hostutils}. Names preserved. 14 carry our Pi4 commits; doc + hostutils are pristine
upstream (mirrored for self-containment + pinning).

**Third-party ports (1 repo + 1 patch-set):**
- **quakespasm** (GLQuake port; 12M) — publish as a fork repo. Names preserved.
- **mesa** — publish as a **PATCH SERIES**, not a 738M fork. Our entire delta over the
  immutable upstream tag `mesa-26.2.0-rc1` is only **598 insertions / 28 deletions, 23
  files** (10 Phoenix v3d/v3dv port commits + 7 upstream cherry-picks). Because the base
  is an IMMUTABLE tag, upstream drift cannot break the patches (fully answers the "target
  a stable tag" concern). Store `git format-patch mesa-26.2.0-rc1..HEAD` output in coord
  under `patches/mesa/*.patch`; the build clones upstream mesa @ the tag and `git am`s
  them. This keeps the org lean AND makes our GPU diff trivially inspectable. (Fallback if
  a build-flow change is unwanted: publish the full 738M mesa fork instead.)
- **vkquake — EXCLUDED from publish** (user 2026-07-22): non-functional + already opt-in
  (`--with-vkquake`, not in default showcase). Keep LOCAL-ONLY (like the WiFi WIP), resume
  later. The mesa **V3DV/Vulkan port commits STAY in the mesa patch-set** (part of the GPU
  stack); only the vkquake game repo is withheld.

Net publish set: **coord + 16 siblings + quakespasm = 18 repos**, plus mesa reconstructed
from `mesa-26.2.0-rc1` + coord patches. (Was 20; dropped vkquake + demoted mesa to patches.)

## 2. NOT published (reference-only, pristine upstream, publicly available)

- **external/linux** — 7 GB; research-only; the Pi4 DTB is fetched ready-made, the
  kernel is never compiled. Pristine `raspberrypi/linux`. (User agreed: no fork.)
- **external/rpi-eeprom** — 353 MB; Tier-2 lab/netboot only; `prepare-pi-eeprom-
  netboot.sh` self-clones it on demand. Pristine `raspberrypi/rpi-eeprom`.
- **external/vkquake** — non-functional Vulkan-Quake WIP; opt-in only. Keep local-only
  (preserve like the WiFi WIP); revisit when it works. `--with-vkquake` becomes a
  local-only / unsupported path in the public release.
- Build-time upstream fetches that stay at their canonical homes (standard, keeps the
  org lean): the GNU toolchain sources, X11/xorg libs (tools/x11-port fetches + patches
  them), and any ports tarballs. => The org hosts OUR forks + coord; a clean build still
  fetches a few pristine upstreams. NOTE: this is "self-contained forks", not "offline/
  air-gapped mirror". Full offline mirroring is a larger, separate phase if ever wanted.

## 3. Pre-push hygiene (per repo, BEFORE any push)

1. **lwip WiFi history** — `wi-fi/` was removed from the tip but is still in *history*.
   Publish a **filtered COPY** (do NOT rewrite the working repo, which must keep the
   `wifi-wip` branch + full history for resuming WiFi):
   `git clone --no-local sources/phoenix-rtos-lwip /tmp/lwip-pub && cd /tmp/lwip-pub &&
    git filter-repo --path wi-fi --invert-paths` → push /tmp/lwip-pub to the org.
   (Requires git-filter-repo; else `git filter-branch`.) The `wifi-wip` branch is NOT
   copied/pushed. Tarball preservation already in coord `.wifi-wip/`.
2. **coord review scratch history** — `docs/review/2026-07-06-pre-publication/{findings-
   low.md,PENDING-USER-TASKS.md}` were committed then untracked; still in coord history.
   Decision: leave (untracked at tip, only in history) OR filtered-copy-scrub coord too.
3. **Secret / large-blob scan** — before public push, scan every repo's history for
   accidental secrets (tokens/keys) and oversized blobs. pak0.pak was never committed
   (untracked) so it's not in history ✓.
4. **Legacy `houp` references** — update in Dockerfile (doc line, REPO_BASE) + scripts to
   the org (see §5).

## 4. LICENSE / attribution (user: "make sure LICENSE files are correct")

- **coord `phoenix-rpi`**: top-level `LICENSE` = BSD-3-Clause with a scope note that it
  covers first-party work and NOT third-party/patches, pointing to `LICENSING.md` for the
  per-component breakdown. ✅ present. TODO: verify `LICENSING.md` exists + is accurate;
  confirm `tools/quakespasm-port` (10 files) + `tools/vkquake-port` (16 files) are
  GPL-2.0-or-later per SPDX (they are); scripts BSD-3. Add a NOTICE/third-party section.
- **sibling forks**: keep upstream Phoenix `LICENSE` (BSD-3-Clause) — present ✓. Our
  commits inherit it. (lwip: our port code is BSD; the removed Cypress `wi-fi/` was the
  only third-party-licensed part and it's gone from the published copy.)
- **mesa fork**: MIT — upstream keeps it in `docs/license.rst` (no root LICENSE). Keep as
  upstream; our v3d-port commit inherits MIT. Verify a root pointer/NOTICE is adequate.
- **quakespasm / vkquake forks**: GPL-2.0(-or-later) — `LICENSE.txt` present ✓. Our port
  commits are GPL-compatible (marked GPL-2.0-or-later).
- Cross-check against memory `project_prepublication_licensing` (fbdev keycode BSD
  relicense done; Quake/vkQuake glue GPL; fork relocation was deferred — this IS that).

## 5. Build-script URL updates (point defaults at the org)

- `Dockerfile`: `ARG REPO_BASE=https://github.com/rpi-phoenix-rtos`; coord clone
  `${REPO_BASE}/phoenix-rpi.git`; doc example URL → org; decide `UPSTREAM_BASE` (keep
  `phoenix-rtos` as pristine fallback, OR set to org for full self-containment — since we
  mirror ALL siblings, recommend `UPSTREAM_BASE=…/rpi-phoenix-rtos` so a clone never
  depends on phoenix-rtos drift).
- `scripts/bootstrap-linux-host.sh`: `PHOENIX_FORK_BASE`, `EXTERNAL_FORK_BASE`,
  `PHOENIX_UPSTREAM_BASE` defaults → org; `EXTERNAL_DEPS` mesa/quakespasm/vkquake URLs →
  org (pins unchanged).
- `scripts/build-sd-in-docker.sh`, `serve-repos-for-docker.sh`: local-sim URLs stay for
  local builds; ensure the public default path resolves to the org.
- Commit these to coord and push coord LAST (so the published coord already points at the
  org).

## 6. Execution order (after approval)

1. `gh repo create rpi-phoenix-rtos/<name> --public` for each of the 20 (or push-to-create).
2. LICENSE/LICENSING.md verification pass (§4) — commit any fixes to each repo.
3. Hygiene scan (§3.3) + build lwip filtered copy (§3.1) [+ coord if chosen §3.2].
4. Add `org` remote to each repo; push `master`/`main` + tags. NOT `wifi-wip`.
5. Update coord build-script URLs (§5); commit; push coord.
6. Re-snapshot a manifest (lwip SHA changes if filtered) → `manifests/`.
7. Verify: clone-only build smoke test from the org (optional; big — mesa/toolchain).

## 7. DECISIONS (locked 2026-07-22, except mesa)

1. Coord repo name in org = **`rpi-phoenix-rtos-coordination`**.
2. All repos **PUBLIC**.
3. `UPSTREAM_BASE` → **the org** (`github.com/rpi-phoenix-rtos`) — clones never depend on
   phoenix-rtos drift.
4. **mesa = DEFERRED** (user analysis in progress) — patch-series vs full fork undecided;
   do NOT publish/rewrite mesa handling until decided. Everything else proceeds.
5. **History scrub = MINIMAL**: scrub only licensing-problematic content = the lwip
   Cypress `wi-fi/` subtree (filtered publish copy). In-progress / review docs may remain
   in coord history — NOT scrubbed.

## 7-orig. Decisions needed from the user (superseded by §7 above)

1. **Coord repo name** in the org: keep `phoenix-rpi`, or rename (e.g. `phoenix-rtos-rpi4`,
   `rpi4-port`)? (Everything else keeps its name.)
2. **Visibility**: all 20 repos PUBLIC? (implied by "inspect/study/clone" — confirm.)
3. **UPSTREAM_BASE**: point at the org too (full self-containment), or keep `phoenix-rtos`?
4. **mesa** — RESOLVED (recommend): patch-series against immutable `mesa-26.2.0-rc1`
   (598-line delta), stored in coord `patches/mesa/`, build clones upstream + `git am`s.
   Confirm OK, or prefer the full 738M fork (no build-flow change).
5. **vkquake** — RESOLVED (user): excluded, local-only.
6. **History scrub scope**: lwip wifi = yes (planned). Also scrub the coord pre-publication
   review docs from coord history, or leave them (tip-clean, history-only)?
7. **Self-containment level**: forks+coord only (a clean build still fetches pristine mesa
   base tag + X11/toolchain/DTB upstreams) — acceptable for v1, or full upstream mirroring
   later?
</content>

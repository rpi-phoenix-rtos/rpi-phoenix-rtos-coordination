# Consolidation + first-public-release prep (2026-07-22)

User decision: **consolidate the current playable Quake state, log the remaining glitch as a
known issue, then aim for a first public GitHub release**, and continue fixes after release.

## Consolidated state (rollback anchor)

- Manifest: `manifests/2026-07-22-quake-consolidated-playable.md` (all sibling SHAs).
- Quake: flicker fixed, ~40fps @1080p, smooth monster animation, full-speed no-capture build,
  staged to NFS. Validated playable by the user.
- Remaining glitch → **KNOWN-ISSUE #67** (docs/KNOWN-ISSUES.md), deferred post-release.

## Release-prep loose-ends triage (2026-07-22, per user direction) — DONE except where noted

1. **kernel `hal/riscv64/generic/config.h` stray edit** — ✅ REVERTED (uncommitted debug edit,
   "QEMU ext2-repro tweak", origin unknown — likely a broken upstream merge). Tree clean.
2. **lwip untracked WiFi blobs** (`port/wifi-fw-43455.{c,h}`, `wifi-nvram-43455.{c,h}`) — ✅
   DELETED (orphans: not referenced by any build, not tracked). WiFi documented as UNSUPPORTED
   in KNOWN-ISSUES ("Not started / unsupported"; use wired Ethernet). Firmware binaries were
   already non-vendored (staged to gitignored `.firmware/`).
   - ✅ **RESOLVED (user: keep local, don't publish, will resume WiFi soon).** The committed
     Cypress WHD WiFi subtree (`wi-fi/`, 116 files incl. `wifi_nvram_image.h`) was REMOVED from
     the lwip published tip (commit `bf8d405`) and PRESERVED two ways (belt+suspenders):
       - lwip branch **`wifi-wip`** at the pre-removal HEAD (116 wi-fi/ files) — **do NOT push it**;
       - tarball **`.wifi-wip/lwip-wifi-subtree-2026-07-22.tar.gz`** (coord, gitignored).
     Build-safe: Pi4 sets `LWIP_WIFI_BUILD=no` so `wi-fi/` is never compiled; the trivial upstream
     `include/wifi-api.h` (BSD-3-Clause, one decl) is KEPT so `port/main.c` still compiles
     (`init_wifi()` is `#if LWIP_WIFI`=0). **Restore to resume WiFi:**
     `git -C sources/phoenix-rtos-lwip checkout wifi-wip -- wi-fi` (or untar the archive).
     **Publish rule:** push lwip `master` only, NOT `wifi-wip`. For a fully wifi-free public
     HISTORY (master history still contains wi-fi/), scrub at publish time:
     `git filter-repo --path wi-fi/ --invert-paths` — this rewrites lwip SHAs, so re-snapshot
     the manifests afterward. (Not done now — publish-time decision.)
3. **Quake `pak0.pak`** — ✅ gitignored in the project fork (`**/share/quake/**/*.pak`, targeted
   so tracked ia32 overlay assets are unaffected). ✅ auto-download wired: new
   `scripts/fetch-quake-shareware-pak.sh` (md5-verified freely-redistributable shareware) +
   `build-sd-in-docker.sh` now falls back to the public shareware mirror when no local pak0 is
   present (unset PAK0_URL = auto; ="" = no game data; =url = own copy). Docker `PAK0_URL` path
   already existed. No permission prompt needed — shareware is free to redistribute; scripts
   print a clear license notice.
4. **external/quakespasm host binary + artifacts** — ✅ host `Quake/quakespasm` binary removed;
   added `external/quakespasm/.gitignore` (`*.o`, `*.d`, binaries) so the fork won't publish
   build artifacts. (coord already ignores `external/`.)
5. **coord scratch** — ✅ gitignored `docker-out/`, `docs/review/2026-07-06-pre-publication/`,
   `session-id-cl` (none were tracked; now can't be).

### Still-open decisions for you (from the triage)
- **project `_user/ext2conc/`** — untracked (so it will NOT publish either way). It's a
  diagnostic stress-test that reproduced a Pi4 ext2 concurrent-allocator crash (Data Abort in
  `ext2_block_destroyone` during bitmap write-back) — **that bug was already FIXED** (ext2
  fs-global lock, commit `463aec13`), so the repro is a closed/disproved-hypothesis tool.
  Recommend DROP (rm) as tidiness; kept in place for now (untracked = release-safe) pending
  your OK — deleting an untracked file is unrecoverable, so not done unilaterally.

## Release readiness references (already in repo)

- `docs/KNOWN-ISSUES.md` — user-facing known issues (just updated: #67 refined, date bumped).
- `docs/inprogress/pi4-hardware-support-matrix.md` — per-peripheral status.
- `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` — TD registry (most RESOLVED).
- Pre-publication licensing work: see memory `project_prepublication_licensing` (fbdev keycode
  BSD relicense done; Quake/vkQuake glue marked GPL-2.0-or-later; fork relocation deferred).
- Upstream-readiness review: `docs/review/2026-06-06-...` (17-area review; B1–B14 NEEDS-HW bugs).

## Suggested release-prep sequence (for the next phase, with the user)

1. Triage the 6 loose ends above (revert stray edits, sort licensing on fw blobs + pak0,
   gitignore build artifacts).
2. Licensing/attribution sweep (LICENSE files, third-party notices: Mesa MIT, Quake GPL,
   libmcs, etc.) — coordinate with the user (fork relocation was deferred to them).
3. README + build/run instructions for the public repo.
4. Decide repo scope: coordination repo + which sibling forks, and how to reference them.
5. Tag the release against the consolidation manifest.

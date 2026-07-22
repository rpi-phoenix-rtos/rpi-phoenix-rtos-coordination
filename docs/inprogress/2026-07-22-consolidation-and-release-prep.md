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
   - ⚠️ **DECISION NEEDED (user):** a large **committed** Cypress WHD WiFi driver subtree exists
     in the lwip fork — `wi-fi/whd/*`, `wi-fi/lwip/*`, `include/wifi-api.h`, and notably
     `wi-fi/whd/wifi_nvram_image.h` (an NVRAM blob-as-C-header). It is third-party (Cypress/
     Infineon license) + non-functional. For a clean public release, recommend REMOVING the
     whole `wi-fi/` subtree (WiFi is unsupported anyway) — but deleting committed third-party
     code is consequential, so left for your call. If kept, needs a licensing/attribution pass.
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
- (2) remove the committed `wi-fi/` WHD subtree entirely, or keep + license it?
- **project `_user/ext2conc/`** — untracked test/util; keep (commit) or drop? (leaning drop.)

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

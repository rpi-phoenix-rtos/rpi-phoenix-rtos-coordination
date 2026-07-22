# Consolidation + first-public-release prep (2026-07-22)

User decision: **consolidate the current playable Quake state, log the remaining glitch as a
known issue, then aim for a first public GitHub release**, and continue fixes after release.

## Consolidated state (rollback anchor)

- Manifest: `manifests/2026-07-22-quake-consolidated-playable.md` (all sibling SHAs).
- Quake: flicker fixed, ~40fps @1080p, smooth monster animation, full-speed no-capture build,
  staged to NFS. Validated playable by the user.
- Remaining glitch → **KNOWN-ISSUE #67** (docs/KNOWN-ISSUES.md), deferred post-release.

## Release-prep loose ends found during consolidation (NOT yet resolved)

Pre-existing uncommitted / stray state in the tree that should be cleaned or decided before a
public release (surfaced 2026-07-22 via `git-siblings.sh status`):

1. **kernel `hal/riscv64/generic/config.h`** — uncommitted stray debug edit (`#define NUM_CPUS 1`,
   comment "QEMU ext2-repro tweak"). RISC-V, not built for Pi4; harmless but cruft. Decide:
   revert (likely) or commit with rationale.
2. **lwip `port/wifi-fw-43455.{c,h}` + `wifi-nvram-43455.{c,h}`** — untracked WiFi firmware/NVRAM
   blobs for BCM43455. **Licensing-sensitive for a public release** (redistributable? Broadcom
   fw license). Decide: gitignore/exclude, or include with proper license/attribution. WiFi is
   unresolved (#91) so these aren't load-bearing for the release.
3. **project `_projects/.../rootfs-overlay/usr/`** — untracked; holds Quake shareware `pak0.pak`
   + data. **Copyrighted — must NOT be committed.** Ensure `.gitignore` covers it; document how
   users supply their own pak0.
4. **project `_user/ext2conc/`** — untracked test/util. Decide: keep (commit) or drop.
5. **external/quakespasm `Quake/quakespasm`** — host-built binary artifact (should be
   gitignored, not committed); `Quake/history.txt` is upstream doc.
6. **coord repo**: ~22 changes = mostly untracked `docs/review/2026-07-06-pre-publication/*`
   file-inventory lists + `docker-out/` + `session-id-cl`. Triage before release.

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

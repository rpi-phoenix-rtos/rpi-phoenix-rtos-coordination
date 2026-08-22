# gcc-16 RELEASE PLAN — align the whole build system to gcc-16 and ship a gcc-16 release

**Owner directive (2026-08-23):** "Align ALL build instructions + build scripts to gcc-16.
Remember we have a Docker-based reproducible/host-independent build — verify it against gcc-16.
Ship a gcc-16-based release into the github org." This authorizes the gcc-16 **promotion**
(previously held as owner-attended). Keep gcc-14 as a tagged rollback throughout.

## Key realization (drives the whole plan)
Every build script references the toolchain **by path** (`.toolchain/aarch64-phoenix/bin`), NOT by
version — this includes `rebuild-rpi4b-fast.sh`, all `tools/*` GPU/game build scripts (they hardcode
`.toolchain`), `build-showcase-apps.sh`, and the Docker build. So the gcc-16 alignment is mainly a
**promotion**: make the default `.toolchain/` be gcc-16 (built by the one toolchain-build script the
Dockerfile also invokes), and everything downstream picks up gcc-16 with *no per-script edits*. The
two version pins are `scripts/build-phoenix-toolchain-linux.sh` (builds gcc-14.2.0 today) and the
`Dockerfile` (invokes it). gcc-16.2.0 is already proven buildable (`.toolchain-gcc16/` exists + boots).

## Current status (2026-08-23)
- gcc-16 cross-toolchain BUILT (`.toolchain-gcc16/`); core gcc-16-clean under -Werror (4 fixes pushed);
  `-std=gnu17` pinned (Makefile.common 8b0c29c, for gcc-16's C23 default).
- gcc-16 CORE rootfs BOOT-VERIFIED over NFS (session ~223) — owner-testable now.
- Tarball distfiles cache + artfiles mirror done (xorg_libs 2cb8a62; cache populated 23/23).
- **RUNNING:** full-clean gcc-16 `--with-ports` build (pid 2457574) — first --with-ports gcc-16 build
  (G1.1 below).

## Phases (execution order; each committed + verified)

### G1 — gcc-16 build completeness
- **G1.1** full-clean gcc-16 `--with-ports` build clean → fix any gcc-16 `-std`/`-Werror` issues in
  ports/X not seen in the core-only builds. [IN PROGRESS, pid 2457574]
- **G1.2** GPU/games under gcc-16: the `tools/*` scripts use `.toolchain` by path, so once G2.2 makes
  `.toolchain` = gcc-16 they build under gcc-16 automatically. Rebuild libv3d/libGL/SDL2-glue/vkquake/
  quakespasm/yquake2/quake3 under gcc-16; expect + fix Mesa-under-gcc-16 issues (big C/C++ base).
  Boot + HW-verify a game. [after G2.2]

### G2 — align toolchain + scripts to gcc-16 (the promotion)
- **G2.1** Bump the toolchain version pins to **gcc-16.2.0 + binutils-2.47** (latest, owner request —
  was gcc-14.2.0 + binutils-2.43). The pins live in the upstream helper
  `sources/phoenix-rtos-build/toolchain/build-toolchain.sh:65-66` (`BINUTILS=` / `GCC=`);
  `scripts/build-phoenix-toolchain-linux.sh` is just the wrapper that invokes it. READY: the
  gcc-16.2.0 Phoenix patches exist (toolchain/gcc-16.2.0-*.patch, 4 aarch64 ones) and the
  binutils-2.47 aarch64-phoenix patch is staged + verified-clean (toolchain/binutils-2.47-04-aarch64-phoenix.patch,
  sibling 96f5697); both tarballs are on the mirrorservice.org GNU mirror as .tar.bz2/.tar.xz (the
  formats the script fetches) — so no download-URL change needed. The script globs
  `binutils-${VER}-*.patch` / `gcc-${VER}-*.patch`, so only the aarch64 subset applies (same as the
  gcc-16 pattern; i386/sparc/arm binutils patches unported — not needed for aarch64-phoenix). STEP:
  edit lines 65-66, do a from-scratch toolchain build, verify `aarch64-phoenix-gcc --version`=16.2.0 +
  `aarch64-phoenix-ld --version`=2.47 + a test compile + a full Phoenix build boot. Multi-hour — run
  AFTER the current full gcc-16 build finishes (CPU) and commit the version bump only once verified.
- **G2.2** PROMOTE: replace the default `.toolchain/` with gcc-16 (rebuild via G2.1, or atomically
  swap in `.toolchain-gcc16`). HIGH-BLAST-RADIUS — before: tag + manifest the gcc-14 state; after:
  full rebuild + boot-verify before treating as default. Owner-authorized (this directive).
- **G2.3** Confirm all path-based consumers now use gcc-16: `rebuild-rpi4b-fast.sh`, every `tools/*`
  build script (build-v3d-phoenix, build-quakespasm-*, build-yquake2, build-quake3e, build-vkquake,
  build-x11*, build-showcase-apps), and `build-port.sh`. No edits expected (path-based) — verify by
  string-checking a produced binary or the compiler `--version` used. Fix any that hardcode a version.
- **G2.4** Verify the `-std=gnu17` pin is in the sibling `phoenix-rtos-build/Makefile.common` (not only
  the buildroot copy) so clean builds inherit it. Commit/push if missing.

### G3 — Docker reproducible build to gcc-16
- **G3.1** Ensure the Dockerfile's cross-toolchain step produces gcc-16 (it invokes G2.1's script).
  Check `UBUNTU_TAG` (24.04) suffices for gcc-16.2.0 deps; bump only if needed.
- **G3.2** Run the authoritative `scripts/build-sd-in-docker.sh` (`--no-cache`) under gcc-16 → require
  BUILD_RC=0 (the clean-build release gate, [[project_clean_build_release_gate]]). This is the
  host-independent proof.
- **G3.3** Boot-verify the Docker-produced gcc-16 image (SD self-boot + netboot to psh + a game).

### G4 — docs aligned to gcc-16
- **G4.1** Update build instructions to gcc-16: `docs/BUILD.md`, `TUTORIAL.md`, `TUTORIAL-NETBOOT.md`,
  `README.md`, `docs/KNOWN-ISSUES.md`, `docs/HARDWARE.md`, `docs/inprogress/pi4-hardware-support-matrix.md`.
- **G4.2** Update `docs/AI-DRIVEN-PORT-JOURNEY.md` if it pins gcc-14.

### G5 — ship the gcc-16 release to the org
- **G5.1** Authoritative gcc-16 clean rebuild via Docker (G3.2) → boot-verified gcc-16 SD image +
  netboot rootfs.
- **G5.2** Snapshot a gcc-16 integration manifest (all sibling SHAs) + tag a known-good gcc-16 across
  siblings + coord (e.g. `known-good/gcc16-release-YYYY-MM-DD`).
- **G5.3** Publish the gcc-16 release to the rpi-phoenix-rtos org (git-topology publish flow): push the
  tag, attach the built image artifact / release notes, update the org README to state the release is
  gcc-16.2.0-based.

## Follow-up: extend the distfiles cache to the other X ports
The persistent distfiles cache + artfiles mirror was added to `xorg_libs` (the acute 24-lib
re-downloader). `xorg_fonts` and `xorg_server` have their OWN `_fetch_extract` (no cache) pulling from
mixed sources (sourceforge freetype/libpng, x.org libfontenc/libXfont2/libXft, freedesktop fontconfig,
cairographics, github expat, ijg jpeg) — so a clean build re-downloads those. Owner's general cache
ask applies: add the same cache-check-then-mirror pattern (or a shared helper) to `xorg_fonts` +
`xorg_server`. Non-urgent (fewer libs each; several tarballs are committed in-dir; x.org is currently
up), but do it for a fully offline-reproducible clean build. Ideal end state: one shared distfiles
cache used by all ports (framework-level fetch hook).

## Follow-up: Docker game-data fetch for all Quakes (owner request)
The Dockerfile fetches only **Quake 1** data (`PAK0_URL` = quake106.zip → id1/pak0.pak). A
from-scratch reproducible build therefore ships the quake2/quake3/vkquake engines WITHOUT their game
data (baseq2/pak0.pak, demoq3/pak0.pk3) — those are currently hand-staged, not fetched. For the
release the Docker build should fetch all games' **freely-redistributable demo/shareware** data:
add `PAK0Q2_URL`/`PAK0Q3_URL` (+ sha256) args and download/extract/stage steps mirroring the Q1
`PAK0_URL` logic (optional; "" skips; a non-empty URL that fails to verify fails the build).
Sources to pin (verify redistribution terms first, like Q1's shareware): Quake II shareware demo
(baseq2 pak), Quake III Arena demo (demoq3/pak0.pk3 from the linux Q3 demo). Also worth a general
`fetch-quake-data.sh` covering all three so SD + Docker + netboot builds share one path. Licensing:
demo/shareware data only — no full retail paks in the public build.

## Follow-up: C++23 `import std` module (libstdc++ std.gcm) — owner request
The gcc-16.2.0 toolchain build installs fine and classic C++ works (libstdc++.a; verified with a
std::vector test → static aarch64 ELF), but the **C++23 std module** fails to build:
`Cannot compile std module … failed to read compiled module 'gcm.cache/std.gcm' … imports must be
built before being imported` (then std.compat). Harmless today (Phoenix ports use classic
`#include`, not `import std;`), but the owner wants it working for future modern-C++ ports. LEAD: the
error is a build-ORDERING one (std.compat imported std before std.gcm existed) — quite possibly a
parallel-build race exposed by the `-j$(nproc)` bump (the old -j9 may have serialized it). Check
whether the libstdc++ modules build declares the std→std.compat dependency, whether building the std
module needs `-j1`/an explicit order, or a libstdc++ patch; verify with `import std;` compiling to a
static aarch64 ELF. Not on the release critical path (classic C++ is what the release needs).

## Disk hygiene note
Toolchains are large (.toolchain gcc-14 5.8G, .toolchain-gcc16 binutils-2.43 8.6G,
.toolchain-gcc16-247 ~0.3G after removing its 6.7G _build scratch). Once .toolchain-gcc16-247 is
promoted/validated, **.toolchain-gcc16 (binutils-2.43) is superseded and removable** (frees ~8.6G).
Keep .toolchain (gcc-14) as the promotion rollback until the gcc-16 release is HW-signed-off.

## Rollback
Keep gcc-14 `.toolchain/` (rename, don't delete) + a gcc-14 tag/manifest until the gcc-16 release is
HW-signed-off. Any regression → restore gcc-14 `.toolchain` + rebuild.

## Dependencies
G1.1 (running) → G2.1 → G2.2 → {G2.3, G2.4, G1.2} → G3 → G4 → G5. G4 (docs) can run in parallel once
G2 lands. Each phase commits to the relevant repo + pushes when verified.

# Game builds: one source of truth, one build path (owner directive, 2026-09-02)

Owner: *"a) there is only one way to build the games; b) no parts of the patches are
lost — maybe we need to integrate the changes from the forks and from the patches
into one place."*

Companion: `2026-09-02-game-source-of-truth-audit.md` (per-game subset numbers —
which changes exist only in a fork, and the dangerous inverse: shipped changes that
exist in no fork). **Step 1 below must not run until those numbers exist**, or a
shipped fix can be dropped silently.

## What is wrong today (verified directly)

Two definitions of the same game, built two different ways:

| | source | built by | artifact |
|---|---|---|---|
| showcase path | `external/<game>` = **our fork** | `scripts/build-showcase-apps.sh:337,381` → `tools/.gpu-libs/lib<game>.a` | `rpi4-quake` (image, boot-launchable) via `_user/rpi4-quake/Makefile:41` |
| ports path | **pinned upstream tarball** + one local patch | `sources/phoenix-rtos-ports/<game>/port.def.sh` | `/usr/bin/<game>` (`port.def.sh:186`) |

- All four recipes pin upstream, not us: quakespasm `6baceeac`, vkquake `9be3a5ad`,
  yquake2 `8.71`, quake3e `1.32` — each plus exactly one patch.
- The forks are a build input for **quakespasm and vkquake only**. For yquake2 and
  quake3e nothing builds from `external/`, so their forks are documentation today.
- The deltas differ in size: shipped quakespasm patch +97/-17 vs fork +522/-17. The
  fork's surplus is not only the capture harness — it includes `#67` alias-model,
  `#26` LAN play, and the `r_quadparticles`/`r_oldwater` defaults. Until the audit
  says otherwise, **the ports-built game may lack fixes we HDMI-verified**.

## Target

**The fork is the single source of truth; the ports recipe builds from it; there is
one artifact per game.** This satisfies both halves of the directive and keeps the
owner's two constraints: GitHub still shows our delta against the upstream project
(fork compare view), and upstream stays trackable (merge upstream into the fork).

## Steps

1. **Make each fork a strict superset.** For every change the audit finds only in a
   shipped patch, commit it to `phoenix-rpi4-port` with a message saying it came
   from the port patch. After this, deleting a patch cannot lose anything.
2. **Pick the surviving artifact per game.** `rpi4-quake` and `/usr/bin/quakespasm`
   are the same engine; keep the one that is actually launched and verified, and
   delete the other from the image so no second build exists. Record which, and why.
3. **Repoint the ports recipe at the fork** — `source=` our fork archive at a pinned
   commit + sha256 (same reproducibility contract as now, different owner), and drop
   `patches/` for that game. Bumping = re-pin to fork HEAD after a green Pi cycle.
   Rejected alternative: generate the patch from the fork delta on every build. It
   keeps two representations in sync by machinery where one representation will do.
4. **Guard against regression:** a check that the pinned fork commit is an ancestor
   of the fork's `phoenix-rpi4-port` (i.e. the pin is real history, not a rewrite),
   run where `publication-audit.sh` runs.
5. **Verify on hardware, per game, before pushing** — step 2 or 3 can change what the
   binary contains. HDMI-confirm each game that changed source.

## Known risk — checked, and it is small

Building from the fork ships the capture harness. Checked whether it is inert:

- **quakespasm: safe.** The harness defaults off (`gl_screen.c:114` `scr_capture "0"`,
  plus `scr_capture_max "0"`) and the GL-blit path is compile-gated (`gl_screen.c:928`
  `#ifdef QSS_PHOENIX`). Dormant unless a cvar is set.
- **vkquake: one line to fix during step 1.** The texture trace is env-gated
  (`gl_texmgr.c:1256`, `getenv("VKQ_TEXDBG")`), but our delta leaves one **ungated**
  `fprintf(stderr, "vkq-tex-fix: '%s' region0 extent now ...")` in the #29 fix path.
  Gate it behind `VKQ_TEXDBG` or drop it (it is diagnostic-only, and the fix it
  reports is proven) before the fork becomes the shipped source. Not done yet on
  purpose: the audit is diffing that working tree right now.

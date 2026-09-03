# `tools/` dedup: what is genuinely left (measured audit, 2026-09-03)

Owner rule: `tools/` is only for our own build tooling that cannot easily live
elsewhere. A normal port belongs in `phoenix-rtos-ports`. *"We don't need multiple
ways to build mv or nano or quake or sdl or python."*

The migration is reported done, so this audit asks the harder question: **what still
sits in `tools/` that duplicates a framework port, and is any of it still wired in?**
Method: for each candidate directory, count references from the **live build paths
only** (`scripts/`, `Makefile*`, `.github/`), then read every reference to separate a
real invocation from a comment.

| dir | files | refs in live paths | verdict |
|---|---|---|---|
| `python-port` | 27 | **0** | **DELETE** — pure duplicate; framework port is self-contained |
| `sdl2-port` | 7 | **0** | mixed — build scripts duplicate; the gltest/audiotest **sources** are genuine test apps |
| `ffmpeg-port` | 14 | **0** | mixed — `build-ffmpeg-phoenix.py` duplicates; the `e4_*.c` demos are genuine |
| `quake3-port` | 4 | 2 | **KEEP** — `quake3-launcher.c` is compiled into `/usr/bin/quake3`; `demos/cap.dm_68` is capture test data |
| `yquake2-port` | 3 | 2 | **KEEP** — `quake2-launcher.c` → `/usr/bin/quake2` |
| `supertuxkart-port` | 3 | 1 | **KEEP** — `stk-launcher.c` → `/bin/stk` |
| `quakespasm-port` | 17 | 2 | **BLOCKED** — see below |
| `vkquake-port` | 21 | 3 | **BLOCKED** — see below; `gen-vkquake-shaders.py` is a real out-of-band tool |
| `ports` | 16 | 1 | **KEEP** — only `teken-src` remains; `rebuild-rpi4b-fast.sh` walks `tools/ports/src` for stale-object cleanup |
| `v3d-driver-port` | 46 | 2 | **KEEP** (owner-gated D2 — the V3D → devices move) |

## Why `python-port` is safe to delete (checked, not assumed)

`sources/phoenix-rtos-ports/python/port.def.sh` consumes **its own** copies —
`${PREFIX_PORT}/config.site`, `${PREFIX_PORT}/Setup.local`,
`${PREFIX_PORT}/curses_shim.h` (`port.def.sh:115,135,279`), and
`sources/phoenix-rtos-ports/python/curses_shim.h` exists as a real file. The three
mentions of `tools/python-port/build.sh` / `build-curses.sh` (`port.def.sh:39,48,52`)
are **provenance comments**, not invocations. So nothing executes anything under
`tools/python-port/`. Those comments must be reworded in the same commit, or the
deletion leaves dangling references.

## Why quakespasm-port / vkquake-port cannot simply be removed

`build-showcase-apps.sh:186-189` lists both as **mtime freshness inputs** to
`archive_fresh()`, and that function **deliberately dies** when an input directory is
missing:

> *"A missing input silently reads as 'nothing is newer' => the stale archive would be
> reused forever."*

That guard was added 2026-09-03 precisely because the tools/→ports migration had
already caused this failure once. So deleting the directories trips a loud, correct
error. The fix is to first decide whether they are still *meaningful* freshness inputs
for the **GPU** archives (`libv3d`/`libGL`/`libv3dv`) — they look vestigial, since
neither directory builds those any more — and remove the list entries in the same
commit. `gen-vkquake-shaders.py` is genuine build tooling (generates SPIR-V headers
out of band, `build-showcase-apps.sh:433`) and stays regardless.

## Order of execution

1. `tools/python-port/` — delete, reword the three provenance comments. Validate with
   a **stock `--scope core`** build (project rule for any dead-code deletion: an
   `auto` build reuses cached objects and can hide a red build).
2. `sdl2-port` / `ffmpeg-port` — delete only the duplicate *build scripts*; keep the
   test/demo `.c` sources. Decide their home: they are diagnostics, so `tools/` is
   arguably the right place for the sources alone.
3. `quakespasm-port` / `vkquake-port` — audit `archive_fresh`'s input list first, drop
   the vestigial entries, keep `gen-vkquake-shaders.py`, then remove the dead
   full-build scripts.

Not started here on purpose: an authoritative clean image build was mid-flight, and a
dead-code deletion must be validated by its own `--scope core` build rather than
sharing one.

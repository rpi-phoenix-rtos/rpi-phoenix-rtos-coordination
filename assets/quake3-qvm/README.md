# Quake III VM pak (`pak1-ioq3-vms.pk3`)

Three QVMs — `vm/ui.qvm`, `vm/cgame.qvm`, `vm/qagame.qvm` — built from
**ioquake3** (GPL-2.0) on 2026-08-05 as part of the C5 Quake III port work.

## Why the image needs it

The free Quake III **demo** `pak0.pk3` carries the 1999 QVMs, whose UI reports
API version 3. quake3e (`code/ui/ui_public.h`: `UI_API_VERSION 6`) rejects
those with:

    ERROR: User Interface is version 3, expected 6

so with demo data alone the game dies before the menu. These QVMs are version 6
and make Quake III run **on the free demo data** — no retail content, no retail
CD key (see `q3key` below).

Losing this pak in the ports migration is what made Quake III look like it
"needs retail data". It does not; see coord commit `98162de9d` (the C5 capstone),
which records the original working configuration.

## `q3key`

The ioq3 UI enforces `Com_CDKeyValidate`, which only checks the FORMAT: 16
characters from `{2,3,7,A,B,C,D,G,H,J,L,P,R,S,T,W}` and no checksum. A generated
key of that shape satisfies it. This is the free demo, not a retail key.

## Reproducibility — how to rebuild these

**Recipe in the repo:** `tools/quake3-vm/build-quake3-vms.sh`.

```
./tools/quake3-vm/build-quake3-vms.sh            # -> tools/quake3-vm/build/
./tools/quake3-vm/build-quake3-vms.sh --install  # ALSO overwrites this pak
```

What it does, and where each piece comes from:

* **VM sources + QVM compiler: ioquake3**, pinned to
  `588393618dbc82e7207c21c6ddecca229944a03a` ("Read and write CD key in
  lowercase", 2026-07-19). ioquake3 publishes no release tags, so a SHA is the
  only immutable pin available. Self-cloned into `external/ioquake3` on demand —
  it is not in `bootstrap-linux-host.sh`'s `EXTERNAL_DEPS`, because only this one
  asset needs it.
* **Host tools:** `lburg` → `dagcheck.c` → `q3rcc`, plus `q3cpp`, `q3lcc`,
  `q3asm`, all built with host gcc. They are x86 host binaries; the QVMs
  themselves are architecture-independent bytecode. All four land in one
  directory because `q3lcc` locates `q3cpp`/`q3rcc` next to its own `argv[0]`,
  not via `PATH`.
* **Module source lists and `q3asm` link order** are transcribed verbatim from
  ioquake3's `cmake/basegame.cmake` (`add_qvm SOURCES`). At this pin ioquake3 has
  migrated to CMake, so there is no top-level Makefile to read them from any
  more. The order is load-bearing: `vmMain` must sit in the first object
  (`cg_main.c` / `g_main.c` / `q3_ui/ui_main.c`), and the hand-written
  `*_syscalls.asm` must be last.
* No id content is involved at any step.

### Correction: `external/quake3e` does NOT carry the VM sources

An earlier version of this file claimed the QVMs could be built from
`external/quake3e`'s `code/{game,cgame,ui}`. That is wrong. quake3e is
engine-only: those three directories contain nothing but the four interface
headers (`ui_public.h`, `cg_public.h`, `g_public.h`, `bg_public.h`) — no module
`.c` files — and `code/tools/{lcc,asm}` are absent too. Both the sources and the
compiler have to come from ioquake3, which is also how the 2026-08-05 pak was
built.

So the API-match guarantee is a **header cross-check**, not a shared source tree,
and the script's pre-flight asserts it: `UI_API_VERSION`, `GAME_API_VERSION` and
`CGAME_IMPORT_API_VERSION` are expanded by the real preprocessor out of *both*
ioquake3's headers (what the QVMs were compiled against) and quake3e's headers
(what the engine was compiled against), and required to agree — 6, 8 and 4.

### Reproduction result (2026-09-03)

From a clean ioquake3 clone the recipe reproduces this pak **bit-for-bit except
for four bytes**:

| module | fresh | committed | verdict |
| --- | --- | --- | --- |
| `vm/qagame.qvm` | 488108 | 488108 | 4 bytes differ |
| `vm/cgame.qvm` | 343304 | 343304 | byte-identical |
| `vm/ui.qvm` | 307164 | 307164 | byte-identical |

The four bytes are the `__DATE__` string that `g_main.c` bakes into the
`gamedate` cvar (`"Aug  5 2026"` vs the day of the rebuild). The staged asset is
therefore *explained* by the recipe, not merely superseded by it — which is also
the strongest evidence available that the recipe is right.

## What is still unverified

* **The freshly built QVMs have not been HDMI-verified on the Pi.** The
  committed `pak1-ioq3-vms.pk3` is still what ships and what
  `scripts/stage-game-data.sh` stages; `--install` is opt-in and was not used.
  Given the bit-for-bit match above the risk is small, but "small" is not
  "measured" — if you install a rebuild, re-run the Quake III showcase and check
  `artifacts/hdmi/` before committing it.
* **API compatibility is checked at the three version macros only.** quake3e
  drops the legacy traps 100–106 (`MEMSET`/`MEMCPY`/`STRNCPY`/`SIN`/`COS`/
  `ATAN2`/`SQRT`) and adds engine extensions above them, while keeping `FLOOR`
  at 107 so the numbering is unchanged. The ioquake3 VM sources at this pin
  reference none of 100–106 (checked by hand), and `entityShared_t`'s leading
  field differs only in name (ioq3 `unused` vs quake3e `s`) with identical type
  and layout. Both facts need re-checking if the pin is bumped.
* **The version number is read out of headers, not decompiled out of the
  bytecode.** There is no cheap static way to read a return-constant out of a
  QVM; a direct check means running the VM.

## Licensing

ioquake3 and the resulting QVMs are GPL-2.0. That is fine here — this is game
data plus a host-tools recipe in the coordination repo. It must never be copied
into a `sources/phoenix-rtos-*` core repo. The QVMs belong in the IMAGE as game
data (this directory / `stage-game-data.sh`).

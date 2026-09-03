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

## Reproducibility — open gap

These QVMs are a build artifact whose build recipe is NOT yet in the repo, so
this file is currently staged rather than rebuilt. The proper fix is a port step
that compiles them from ioquake3 source with its own `q3lcc`/`q3asm`. Until then
this directory is the source of truth, and `scripts/stage-game-data.sh` stages it.

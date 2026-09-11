# SuperTuxKart's AI-pathfinding crash — root cause (2026-09-11)

This is the **app-side** half of what used to be one `STK-crash` row. The other half (free-bin
corruption in libphoenix's allocator) is separate and contained; see
`docs/misc/2026-09-10-stk-audio-heap-corruption-source-sweep.md`. Conflating them is what made the
row read as a single intermittent mystery.

## The fault as observed

```
Exception #36: Data Abort (EL0)  esr=0x92000004  pc=0x8c6e60
far=0x800000010d009d78
pc -> DriveGraph::getAngleToNext(int, int) const
in thread 60, process "/usr/bin/supertuxkart"
```

`far` has **bit 63 set**, which a valid aarch64 userspace pointer never has, so this is a garbage
value being dereferenced — not a page-boundary overrun. One STK cycle in three faults this way; the
other two reach ~1880 frames. It fires with **no allocator report at all**, which is what separates
it from the free-bin class.

## The chain, all in STK's own code

1. `Graph::UNKNOWN_SECTOR = -1` (`src/tracks/graph.cpp:40`).
2. `AIBaseLapController::steerToAngle(const unsigned int sector, const float add_angle)`
   (`src/karts/controller/ai_base_lap_controller.cpp:246`) takes the sector as **`unsigned int`**.
   A `-1` sector therefore converts to **`0xFFFFFFFF`** at the call boundary, silently.
3. It calls `DriveGraph::get()->getAngleToNext(sector, getNextSector(sector))`
   (`drive_graph.cpp:713`), which is `getNode(n)->getAngleToSuccessor(j)`.
4. `DriveGraph::getNode(unsigned int j)` (`drive_graph.cpp:744`) bounds-checks with
   **`assert(j < m_all_nodes.size())`** — and `assert` is **compiled out under `NDEBUG`**, which is
   how the shipped build is built. So the check does not exist at runtime.
5. `m_all_nodes[0xFFFFFFFF]` reads ~32 GiB past the vector, yielding whatever is there as a
   `Node*`.
6. `dynamic_cast<DriveNode*>(that)` dereferences the garbage pointer's **vptr** → the Data Abort.
   `assert(n != NULL)` on the next line is also compiled out, so even a null result would not be
   caught.

`SkiddingAI` is the caller that matters in practice (`skidding_ai.cpp:433,438,446,451` all call
`steerToAngle(next, …)`), which is why the originally-recorded PC
`SkiddingAI::findNonCrashingPoint()` is the same subsystem.

## Why it is intermittent, and why it is fatal *here*

The out-of-range index is a **latent upstream STK bug on every platform** — with asserts enabled it
would abort loudly, and on a desktop OS with a large mapped heap the wild read often lands on mapped
memory and merely produces nonsense steering. It becomes a hard fault only when the computed address
is unmapped, which depends on the process's memory layout. So the same defect is invisible on
desktop builds and a crash here.

It needs the AI to reach an unknown/negative sector, which happens when a kart leaves the drive
graph (rescue, off-track, first frames after a respawn) — hence "1 run in 3" rather than always.

## Candidate fixes, cheapest first

1. **Guard the conversion at the boundary** — make `steerToAngle` take a signed sector, or return
   early when `sector` is `UNKNOWN_SECTOR` / `>= DriveGraph::getNumNodes()`. Smallest change, and
   the only one that fixes the actual defect (an invalid sector should not reach the graph).
2. **Make `getNode()`'s bounds check survive `NDEBUG`** — a real `if` rather than `assert`. Broader
   protection, but changes a hot accessor and needs a defined behaviour for the failure case.
3. **Operational, no code:** run with fewer AI karts. Under test — `--numkarts=1` removes the AI
   controllers entirely.

⚠ Any code fix belongs in the **port patch** (`sources/phoenix-rtos-ports/supertuxkart/patches/`),
not in a Phoenix core repo: STK is GPL-3 and the owner's policy keeps GPL out of core.

## A FOURTH route, found while auditing the callers (2026-09-11, not yet guarded)

`LinearWorld::getRescuePositionIndex()` (`src/modes/linear_world.cpp:821`) is declared
**`unsigned int`** but returns `getTrackSector(kart_id)->getCurrentGraphNode()`, which is
`Graph::UNKNOWN_SECTOR` (-1) precisely when the kart is off the drive graph. So the same
signed→unsigned conversion happens here, and then:

* `Graph::get()->getQuad(index)->isIgnored()` is indexed with it **before any check**, inside the
  function itself; and
* the caller passes it to `getRescueTransform(index)` (`linear_world.cpp:843`), which does
  `getNode(index)` twice and `Track::getCurrentTrack()->getAngle(index)` — and `Track::getAngle`
  (`track.cpp:2922`) is `DriveGraph::get()->getAngleToNext(n, 0)`.

**That reaches the exact observed fault site**, `getAngleToNext`, so the rescue path can produce the
same Data Abort as the AI path. It is also the *most* likely moment for an invalid sector, since a
rescue is triggered by the kart leaving the graph.

⚠ The three guards committed in `0014-stk-guard-unknown-sector.patch` do **not** cover this route.
Whether they were sufficient in practice is a separate question from whether this route is sound: it
is not. Fix in the same style — reject an out-of-range index at
`getRescuePositionIndex()` (fall back to `findOutOfRoadSector()`, which the function already does
for the ignored-quad case) rather than inside the graph accessors.

Deliberately not added mid-verification: a 6-trial bench was running on the three-guard build, and
changing the patch under it would have invalidated the test. Sequence the change after that result.

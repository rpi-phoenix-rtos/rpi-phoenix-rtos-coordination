# The intermittent GPU-app stall is a V3D clock race, not a libc startup hang

*2026-09-11. Root cause + fix. Supersedes the pre-`main` framing for THIS failure; see
`docs/misc/2026-09-11-premain-hang-hunt.md` for how the hunt got here.*

## What was actually happening

Storming `/usr/bin/quakespasm -loadbench` (85 launches/cycle) caught a stalled launch with the
**startup trace live**. The trace settles the old hypothesis outright:

```
spawn-storm: launch 24/85
libc-init: enter … atexit … errno … malloc … env … signals … file … pthread -> init_array
quakespasm: main() entered (argc=2)          <- reached main
… heap 96 MB committed … found pak0.pak … Host_Init … Console initialized …
v3d-winsys: winsys_init pid=49 (power-on + map regs + install PT)
v3d_phoenix_powerOn: PM_GRAFX 0x00001040->0x00001040 asb M=ok S=ok
v3d-coldstate: clk_v3d cfg=500000000 Hz meas=0 Hz delta=-500000 kHz clkstate=0x0 …
                                                                    ^^^^^^^^^^^^^ clock OFF
<nothing, ever>
```

**All eight libc initialisers completed — including `file`, the `isatty`→`tcgetattr` suspect — and
`main()` was entered.** So this failure is not pre-`main` at all. It dies in V3D bring-up.

It is a stall, not the harness stopping: 23 completed launches at ≤3095 ms each is ≤71 s, while the
harness reported the command still alive at its 300 s cap ⇒ **launch 24 hung ≥229 s**, and its last
line is complete, not truncated.

## The mechanism

The statement immediately after the cold-state probe reads **V3D core MMIO**
(`W.core0[0]`, `W.hub[0x0c/4]`). An MMIO read of an **unclocked** block never completes, and SError
is masked on this target (**TD-10**), so there is no abort to take — the process hangs forever having
printed nothing more. That is the silent, unkillable stall.

Why the clock was off: `v3d_phoenix_powerOn()` deliberately switches the V3D clock **OFF** around the
`PM_V3DRSTN` deassert and back **ON** afterwards (canonical `bcm2835_asb_power_on`). Every one of
those calls went through the **direct mailbox FIFO** with its result thrown away by a `(void)` cast.
The BCM2711 property FIFO has **no hardware arbitration** — a concurrent client (the vcmbox server,
thermal, usb, genet) can pop our response. Lose that race on the final enable and the clock stays
off, silently. Routing mailbox traffic through the serialized `/dev/vcmbox` is an established rule on
this target; this path was still violating it.

And the caller discarded the result too: `v3d_phoenix_powerOn();` at `v3d_phoenix_winsys.c:576`, so
even an outright failure walked straight into the hanging read.

### The evidence that picks out the clock

In the 24-launch run, **7 launches had anomalous cold-state readings** — but they are two different
things, and the distinction is the whole argument:

| reading | meaning | stalled? |
|---|---|---|
| `4294967295` (`0xffffffff` = `MBOX_FAIL`) ×6 | the *query* failed | no |
| `meas=0 Hz, clkstate=0x0` ×1 | the query **succeeded** and reported the clock **off** | **yes** |

The one launch whose firmware said "clock off" is the one that hung.

## The fix (`phoenix-rtos-devices` `9f9ec65` + retry reporting)

1. Enable the clock through the **serialized `/dev/vcmbox`** and **read the state back**, retrying
   until the firmware agrees it is running (20 tries, ~4 ms).
2. **Check `v3d_phoenix_powerOn()`'s return at the call site** and bail with `-ENODEV` instead of
   reading MMIO. This half converts *any* future power-on failure — clock or otherwise — from an
   unkillable silent hang into a clean, reported error, independently of the clock theory.
3. Report the retry count, so a caught race is observable rather than invisible.

## Status: well-supported, NOT yet proven

| | launches | stalls | `clk=on` | retries needed | direct-FIFO GET failures |
|---|---|---|---|---|---|
| before fix | ~220 | **1** | — | — | ~7 in 24 |
| after fix | **170** | **0** | 170/170 | **0** | 8 in 85 (~9%) |

170 clean launches against a prior rate of ~1-in-220 is only ~0.8 expected events, so it does **not**
prove the fix on its own. What strengthens it: the *diagnostic* GET queries still use the racy direct
FIFO and **still fail ~9% of the time in the very same runs**, so the contention is demonstrably
present and ongoing — yet the vcmbox-routed clock enable has never once needed a retry. Contention
real, serialized path unaffected, is exactly what the fix predicts.

## ⏭ Remaining

- ✅ **Propagated to all five games**, each verified by `strings … | grep 'refusing to read V3D MMIO'`.
  ⚠ A GPU-archive change does **not** invalidate port state, so every port had to be force-rebuilt.
- ✅ **Regression-checked on rendering**, not just startup: Quake II ran **4743 frames at 38.8 fps**
  with `clk=on`, 0 refusals, 0 retries, 0 faults.
  ⚠ Launch `/usr/bin/quake2` (the ram-stage launcher), never `/usr/bin/yquake2` — the bare ELF dies on
  `GetPCXPalette: Couldn't load pics/colormap.pcx`, which looks like a regression and is not one.
- Route the diagnostic GET queries through `/dev/vcmbox` too, to stop ~9% of cold-state lines being
  garbage.
- Keep accumulating launches for a statistical case.
- The netboot export still carries the **diagnostic traced** libc build; it must be rebuilt clean
  before any demo image is cut.

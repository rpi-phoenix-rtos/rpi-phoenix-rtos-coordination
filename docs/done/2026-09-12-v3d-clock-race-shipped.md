# V3D clock race — root cause, fix, and the image it shipped in

*Archived from WEEK-2026-W37 on 2026-09-12. Fixed and shipped; kept for the detail.*

### ★★★ ROOT-CAUSED — a **V3D clock race**, FIXED and SHIPPED

Caught a stalled launch **with the startup trace live**, and the trace settles it: **all 8 libc
initialisers completed — including `file`, the `isatty`/`tcgetattr` suspect — and `main() entered`
printed.** The process then loaded the game and died in **V3D bring-up**, last line
`v3d-coldstate: … meas=0 Hz … clkstate=0x0` — the clock **off**. (A stall, not a cutoff: 23 completed
launches at ≤3095 ms = ≤71 s, yet the command was alive at the harness's 300 s cap ⇒ **≥229 s hung**.)

**Mechanism.** The next statement reads **V3D core MMIO**. A read of an *unclocked* block never
completes, and SError is masked here (**TD-10**), so there is no abort to take — the process hangs
forever, silently. The clock was off because `v3d_phoenix_powerOn()` deliberately toggles it OFF/ON
around the reset deassert via the **direct mailbox FIFO with the result discarded** (`(void)` cast).
That FIFO has **no hardware arbitration**, so a concurrent client (vcmbox server, thermal, usb,
genet) can pop our response — the established rule here is to use the serialized `/dev/vcmbox`, and
this path was still violating it. The **caller discarded the return too**, so a failed power-on walked
straight into the hanging read.
**The evidence that picks out the clock:** of 7 anomalous cold-state readings in that run, six were
`0xffffffff` = `MBOX_FAIL` (the *query* failed) and **none stalled**; the one reading `clkstate=0x0`
(the query *succeeded* and said "off") is the one that hung.

**Fix** (`phoenix-rtos-devices` **`9f9ec65`**): enable the clock through `/dev/vcmbox` and **read the
state back** with bounded retry; **check `v3d_phoenix_powerOn()`'s return** and bail `-ENODEV` rather
than touching MMIO; report the retry count. The second half turns *any* future power-on failure into
a clean error instead of an unkillable hang, whatever the cause.

| | launches | stalls | `clk=on` | retries needed | racy direct-FIFO GET failures |
|---|---|---|---|---|---|
| before | ~220 | **1** | — | — | 7 in 24 |
| after | **170** | **0** | 170/170 | **0** | 8 in 85 (**~9%**) |

⚠ **Well-supported, not yet proven:** 170 clean launches is only ~0.8 expected events at the old rate.
What strengthens it — the *diagnostic* queries still use the racy FIFO and **still fail ~9% in the
very same runs**, so contention is demonstrably live, yet the vcmbox-routed enable has never needed a
retry. Contention real + serialized path unaffected is exactly what the fix predicts.
✅ **Audited the tree for the same pattern — and found it once more.** `/sbin/rpi4-v3d` (the
standalone daemon) ships a **verbatim copy** of the power-on sequence, clock toggled off/on with
every result discarded. It is **not launched**, so latent rather than live, but it ships and this
code is heading for publication. It cannot reach `/dev/vcmbox` without adding a link dependency to a
driver nothing starts, so it now **reads the clock state back and retries** (guarding `MBOX_FAIL`,
whose bit0 is set, from reading as "on"); its call site already checked the return. Boot-verified:
`clk=on`, 0 faults, Quake II **9812 frames @ 38.8 fps**. `phoenix-rtos-devices` **`1292480`**,
manifest `2026-09-12-v3d-daemon-clock-guard.md`.
ⓘ Rest of the audit is clean: thermal, genet and xhci already go through `/dev/vcmbox`; the vcmbox
server and `plo` are the two legitimate direct-FIFO users (the server owns it; plo runs before any
server exists).

✅ **Propagated to ALL FIVE games** (quakespasm · yquake2 · quake3e · vkquake · supertuxkart), each
verified by content — a GPU-archive change does **not** invalidate port state, so every port had to be
forced. ✅ **Regression-checked on a second game that actually renders:** Quake II ran **4743 frames at
38.8 fps, `clk=on`, 0 refusals, 0 retries, 0 faults** — the fix does not break GPU bring-up.
✅ **Also fixed on the RUNTIME path** (`phoenix-rtos-devices`): both in-job reset call sites
discarded `v3d_phoenix_reset()`'s result and then wrote V3D MMIO via `apply_core_regs()` — the same
forever-hang, but striking **mid-render** instead of at startup. Dormant in normal use (it only runs
on a binner wedge), so low risk, but it could not have been worse than hanging.
✅ **Rebuilt CLEAN** (no trace) and re-verified by content: **trace gone from every binary** (games +
psh) and the V3D fix present in **all five games**. ⚠ Turning a `-D` knob *off* does not invalidate
objects any more than turning it on does — a dropped flag would have relinked the traced `init.o` and
exited 0, i.e. **silently shipped a diagnostic build**. `rebuild-rpi4b-fast.sh` now stamps the knob's
state and forces the rebuild on **any** transition.
✅ **Six-app gate PASSED on the clean build: 6/6, rc=0, prompt reached, 0 faults each**, every app
reporting `clk=on`, 0 "refusing" bails. Pixel check (`check-hdmi-content.py`, not just the log):
**stk 100% nonblack / 46152 colours · x 89.6% · vkq 91.4% · q3 42.1% · qspasm 30.5% — CONTENT**;
`q2` grades SUSPECT at 14.6%/5983/14.7, but that is **its known signature** — three earlier *passing*
q2 runs scored 14.5%/5964/14.9 — and its UART shows **38.81 fps, 0 faults**. Not a regression.
✅ **SHIPPED as `rpi4b-sd-2part-gated-1f493117.img`** (§2). **340 post-fix quakespasm launches, 0
stalls**, `main() entered` + `clk=on` on every one.
✅ **Re-gated on the SHIPPED code: 6/6, 0 faults, and now 6/6 CONTENT on pixels too.**
best-tick grades: stk 98.8%/55229 · x 89.7%/10273 · q3 62.2%/19952 · vkq 53.6%/22224 · qspasm
50.6%/22863 · q2 47.7%/6332. Six cold boots, six cold first-launches, **no pre-`main` stall**.
⚠ **The pixel gate was mis-grading, and it cost a scare.** It judged only the **last** tick, but
QuakeSpasm has no controlled viewpoint (it falls through to the demo attract loop), so that frame is
chance: one run graded 11.4% while an earlier tick of the *same* run was 100% and it had drawn 9569
frames at 31 fps with 0 faults. `check-hdmi-content.py` now grades the best **passing** tick and
reports the last alongside — which also retires **q2's long-standing false SUSPECT** (CONTENT at
47.7%). Negative control: a known-faulted run still grades SUSPECT across every tick.
**Boot-scoped hunt for the reopened `premain-hang`: NOT reproduced. 20 cold boot-launches, 0 faults.**
One cold quakespasm launch per boot (the original failing scenario), on a **traced** build.
**11/12 clean at a 60 s window + 8/8 clean at 240 s.**
⚠ **The one ambiguous trial was my experiment's fault, not a bug.** T12 reproduced the symptom exactly
— command echoed, no output, prompt never returned, no libc-init markers — but the window was
`--idle-secs 60`, and a large **cold exec over NFS** is a documented ~**68 s** class here (the
`vm/object.c` read-ahead fix). A slow *data* load would still have printed the markers and `main()
entered`; their absence points at **ELF paging**, which that class covers. It did not recur in 8
trials at 240 s. **Rule: a boot-scoped app-launch bench needs `--idle-secs` ≥ 240** — 60 s sits below
a known slow-start class and manufactures false positives.
ⓘ The harness called it `VOID (capture truncated)` and said "re-run with a longer window before
bisecting". It was right, and I nearly reported a reproduction anyway.
⚠ **Two build-system corrections, both measured** — and the first means a protection I added
yesterday was **inert**: (1) *touching* the guarded source does **not** force the rebuild (the
buildroot's `init.c` was already newer than `init.o` and the core stage still skipped it), so the
"don't ship a diagnostic build" net printed a reassuring message and did nothing; (2) deleting
`libphoenix.a` to force it is **too blunt** — `libc.a`/`libm.a`/`libpthread.a` are **symlinks** to it,
so every port link then failed with `cannot find …/libpthread.a` and a `--scope core` run was needed
to repair the tree. Now it deletes **only `init.o`**, which is sufficient and safe.
ⓘ **A storm is the wrong instrument for the reopened pre-`main` bug.** That one was ~1 in 30 *boots*,
and a storm's launch 1 is the only cold-after-boot sample per cycle — so 340 launches bought only ~4
relevant samples. Many *boots* is the efficient probe, which is what the showcase gate does anyway. Re-validated with the three follow-up
commits in: **85/85** quakespasm launches `clk=on`, 0 retries, 0 faults · Quake II **10588 frames @
38.8 fps** · and the cold-state probe's bogus `MBOX_FAIL` readings **~9% → 0%** — measured proof the
mailbox race was real and the serialized path removes it.
⚠ One scare worth recording: a `/usr/bin/quake2` cycle produced **no output at all** — the exact
"looks like the hang" signature. It was a **too-short window**; the same command with `--idle-secs
240` rendered 10588 frames. The harness had already said so ("Re-run with a longer --idle-secs before
bisecting").
Detail: [`docs/misc/2026-09-11-v3d-clock-race-mmio-hang.md`](../misc/2026-09-11-v3d-clock-race-mmio-hang.md).

Detail, traps and the unapplied candidate fix:
[`docs/misc/2026-09-11-premain-hang-hunt.md`](../misc/2026-09-11-premain-hang-hunt.md).


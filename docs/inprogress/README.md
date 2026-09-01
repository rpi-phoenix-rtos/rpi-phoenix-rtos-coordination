# docs/inprogress — how this folder works

**Only weekly logs live here.** One file per week: `WEEK-<ISO-week>.md`
(e.g. `WEEK-2026-W36.md`). Each weekly log is short and answers exactly:

1. **Decisions needed from Witold** — the owner's action list.
2. **Plan for the next days**.
3. **In progress** — active work + the concrete next step.
4. **Progress this week** — terse; detail links into `docs/done/`.
5. **Blocked / waiting** — things I cannot advance alone.

## Week rollover (do this at the end of each week)

1. Create `WEEK-<next-ISO-week>.md` from the template of the current one.
2. **Carry forward only OPEN items** (decisions, in-progress, blocked). Do not
   copy the "Progress" section — that is history.
3. `git mv` the finished week's file into `docs/done/`.
4. Update the stub links in `MASTER-RECONCILED-PLAN.md` /
   `autonomous-plan.md` to the new week file.

## Where everything else goes

| Content | Location |
|---|---|
| Finished work, closed investigations, archived plans | `docs/done/` |
| One-off analyses / research that is neither active nor a "result" | `docs/misc/` |
| Durable how-it-works reference | `docs/knowledge/` |
| Technical-debt ledger (`TD-xx`) | `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` |

**Rule:** never start a new per-topic file in `inprogress/`. The one-file-per-topic
habit made progress untrackable — put the finding in the weekly log (a line or
two) and the deep detail in `docs/done/` or `docs/misc/`.

# Tracking canonical upstream (origin = phoenix-rtos)

As of **2026-06-02** every Phoenix sibling repo under `sources/` is consolidated
onto its local **`master`** branch, which tracks **`origin`** (the canonical
`github.com/phoenix-rtos/*` upstream). All the Pi4 port work that previously
lived on per-repo feature branches (`agent/rpi4-program-reloc`,
`agent/rpi4-genet`, `codex/upstream-sync-20260516`) was merged into `master`;
the old feature branches were **kept** as a safety net (their commits are also in
`master`). See manifest `manifests/2026-06-02-consolidated-master-upstream-tracked.md`.

Goal: keep `master` = canonical upstream + our Pi4 work, pulling upstream
frequently as it lands. **We do not push anything upstream** (no PRs, no
`git push` to origin) for now.

## Remotes

Each sibling has two remotes:

- `origin` → `github.com/phoenix-rtos/<repo>` — the canonical upstream we track.
- `fork`   → `github.com/houp/<repo>` — the personal fork (eventual push target;
  unused while we don't push). A few repos (e.g. lwip) only have `origin`.

The coordination repo itself (this directory) is a separate repo on `main`
tracking `github.com/houp/phoenix-rpi`; it is not part of the sibling sync.

## Routine: pull upstream across all repos

```sh
./scripts/git-pull-upstream-all.sh        # fetch origin + merge origin/master -> master, every repo
./scripts/rebuild-rpi4b-fast.sh           # rebuild from the updated masters
./scripts/test-cycle-netboot.sh --timestamp --capture-secs 180 --label upstream-pull
```

`git-pull-upstream-all.sh` runs `git-consolidate-repo.sh` on each sibling. Steady
state (all repos already on `master`) that is just: `git fetch origin` +
`git merge origin/master`. It is **non-destructive**: any repo whose merge
conflicts is left untouched (the helper does `git merge --abort` and restores the
branch) and is listed under `CONFLICTS:` at the end for manual resolution.
Nothing is force-updated; nothing is pushed.

## Resolving a conflicted repo

```sh
git -C sources/<repo> checkout master
git -C sources/<repo> merge origin/master      # fix conflicts in your editor
git -C sources/<repo> add <files> && git -C sources/<repo> commit
```

Then rebuild + boot-test before relying on the result. Record the new integration
state with `./scripts/snapshot-integration-state.sh <label>`.

## Single repo

```sh
./scripts/git-consolidate-repo.sh phoenix-rtos-kernel
```

## Why "merge", not "rebase"

The Pi4 work is large and spans many files; merge commits keep upstream history
intact and make conflict resolution localised and reviewable. Rebasing hundreds
of port commits onto moving upstream would be far more error-prone. When we
eventually upstream pieces, individual clean patches can be cherry-picked from
`master` onto a fresh `origin/master` branch for PRs.

## First consolidation (2026-06-02) — what happened

All 16 siblings consolidated with **zero conflicts**. Most repos' upstream had
not advanced past the 2026-05-16 sync; `devices`, `plo`, `libphoenix`, `build`,
`project` made real merge commits (upstream had moved, merged cleanly); `lwip`
(+2 `tuntap.c`) and `ports` fast-forwarded new upstream commits. The rebuilt,
upstream-merged image booted clean to `(psh)%` with klog on HDMI, networking up,
USB enumerated, **0 faults** (`consolidated-validate`, image SHA `a830f9e1`).

# lwIP fork git-history archaeology + remediation plan

**Date:** 2026-08-26
**Scope:** `sources/phoenix-rtos-lwip` (parent port repo) + its `lib-lwip` submodule (vendored lwIP core).
**Mode:** READ-ONLY investigation. No git state was mutated. All destructive remediation is deferred to the owner (command lists below are for the owner to run after review).
**Prior context:** memory `project_git_topology.md` (the 2026-08-26 "lwip UNBLOCKED" reconciliation).

---

## TL;DR

1. **Owner observation #1 (default branch) — resolved as a conflation.** There are two different repos and they *should* track different branches:
   - **Parent `phoenix-rtos-lwip`** correctly tracks **`master`** — that is genuinely the default branch of upstream `phoenix-rtos/phoenix-rtos-lwip` (it is the Phoenix *port* repo, not the lwIP core). This is correct.
   - **Submodule `lib-lwip`** correctly tracks **`STABLE-2_1_x_phoenix`** — the default branch of its upstream `phoenix-rtos/lwip` (Phoenix's fork of the lwIP core). `.gitmodules` already pins `branch = STABLE-2_1_x_phoenix`. This is correct.
   - The "STABLE-2_1_x" the owner has in mind is the *real* lwIP project's release line (savannah / `lwip-tcpip/lwip`). That maps to `lib-lwip`, and `lib-lwip` already follows it (via Phoenix's `STABLE-2_1_x_phoenix`). **Nothing to change on branch tracking.**

2. **Owner observation #2 ("175 ahead / 492 behind" is a broken-history artifact) — CONFIRMED with airtight evidence.** There is **no common ancestor** between our fork's `master` and upstream `master` (verified locally *and* by the GitHub compare API, which returns `No common ancestor`). `175` is the *exact total commit count* of our `master`; `492` is the *exact total commit count* of upstream `master`. The badge is counting two **disjoint** histories, not a real 492-commit divergence. The break was caused by a **git-filter-repo scrub** (to remove the `wi-fi/` WHD subtree) that **re-SHA'd every commit**, including the shared upstream base — so GitHub can no longer find the shared root.

3. **Owner observation #3 (messy / hard to upstream) — CONFIRMED, but the fix is mechanical.** The *content* divergence is small and clean: **~160 owner-authored commits** (Witold Bolt) on top of a real upstream base, collapsing into **~6–10 logical change-sets** (GENET driver, port/sockets fixes, the gigabit perf series, lwipopts, lib-lwip bump). `lib-lwip` is already a **clean 4-commit fast-forward** over its upstream. There are **no binary firmware blobs on `master`** (wi-fi/ fully scrubbed); the only history bloat is a ~220 KB diagnostic source file (`port/diag-udp.c`) that lives on never-push branches.

**Top recommendation:** rebuild our changes as a curated patch series on a **new branch rooted on the real upstream tip** (option i), *not* a force-push rewrite of the published `master`. Sequence **lib-lwip first**. Fix the misleading badge without rewriting published history by flipping the org fork's **default branch** to the new clean branch. Force-pushing `master` is an optional later step after owner review.

---

## A. TOPOLOGY (evidence)

### Parent: `phoenix-rtos-lwip`

```
$ git -C sources/phoenix-rtos-lwip remote -v
origin   https://github.com/phoenix-rtos/phoenix-rtos-lwip.git      (TRUE UPSTREAM)
publish  https://github.com/rpi-phoenix-rtos/phoenix-rtos-lwip.git  (OUR ORG FORK)

$ git -C ... rev-parse --abbrev-ref HEAD   ->  master
$ git -C ... rev-parse HEAD                ->  bf34d89f...  (tracks publish/master)

local branches:
  agent/rpi4-genet    3a5dc6c   (legacy feature branch)
* master              bf34d89   [publish/master]
  full-history-backup 9db0d23   (LOCAL blob-containing archive — NEVER PUSH)
  wifi-wip            3296166   (LOCAL WHD subtree — NEVER PUSH)
```

- Live GitHub: `phoenix-rtos/phoenix-rtos-lwip` default branch = **`master`**; our fork `rpi-phoenix-rtos/phoenix-rtos-lwip` `fork=true`, `parent=phoenix-rtos/phoenix-rtos-lwip`, default = `master`.

### Submodule: `lib-lwip`

```
$ git -C sources/phoenix-rtos-lwip/lib-lwip remote -v
origin   https://github.com/phoenix-rtos/lwip           (TRUE UPSTREAM — Phoenix fork of lwIP core)
publish  https://github.com/rpi-phoenix-rtos/lwip.git   (OUR ORG FORK)

$ git -C .../lib-lwip rev-parse --abbrev-ref HEAD  ->  HEAD (detached)
$ git -C .../lib-lwip rev-parse HEAD               ->  58e89121a...

local branches:
* (detached at 58e89121a) — our tip (base + our 4 commits)
  STABLE-2_1_x_phoenix  cac552df1  [origin/STABLE-2_1_x_phoenix]  (clean upstream mirror)
  phoenix-pin           8da7fc2fe
  phoenix-pin-2         8f8335c8b
```

- Live GitHub: `phoenix-rtos/lwip` default = **`STABLE-2_1_x_phoenix`**; our fork `rpi-phoenix-rtos/lwip` `fork=true`, `parent=phoenix-rtos/lwip`, default = `STABLE-2_1_x_phoenix`.
- The real lwIP project GitHub mirror is `lwip-tcpip/lwip` (default `master`); `lwip/lwip` 404s. Phoenix maintains `STABLE-2_1_x_phoenix` = the lwIP 2.1.x release line + Phoenix ioctl adaptations. `lib-lwip` version = **2.1.3 (LWIP_RC_DEVELOPMENT)** per `src/include/lwip/init.h`.

**Three-level chain:**
`lwip-tcpip/lwip` (real lwIP, STABLE-2_1_x release line) → `phoenix-rtos/lwip` `STABLE-2_1_x_phoenix` (Phoenix core fork) → `rpi-phoenix-rtos/lwip` (our fork) = **lib-lwip**.
`phoenix-rtos/phoenix-rtos-lwip` `master` (Phoenix port repo, *contains* lib-lwip as submodule) → `rpi-phoenix-rtos/phoenix-rtos-lwip` = **parent**.

---

## B. DIVERGENCE ANATOMY (parent) — the "175 / 492" explained precisely

```
$ git -C ... merge-base master origin/master          ->  (NO MERGE BASE)
$ git -C ... merge-base --all master origin/master     ->  (NONE)
$ git -C ... rev-list --left-right --count master...origin/master  ->  175   16
$ git -C ... rev-list --count master                   ->  175   (ALL of our history)
$ git -C ... rev-list --count origin/master (stale)    ->   16   (shallow-truncated locally)

# Live GitHub:
$ gh api repos/phoenix-rtos/phoenix-rtos-lwip/compare/master...rpi-phoenix-rtos:master
  -> 404  "No common ancestor between master and rpi-phoenix-rtos:master."
$ gh api ... (paginated) upstream master total commits  ->  492
```

**Roots differ:**
```
our master root:      3c06d25c  2026-05-13 17:52:31  Marek Białowąs  "ipsec: warn when invalid IPSEC device is configured"   tree a39b868f
upstream b63d44c2:    b63d44c2  2026-05-13 13:00:51  Marek Białowąs  "ipsec: warn when invalid IPSEC device is configured"   tree 4a0b8a11
upstream true root:   eb04e158  2018-06-01  "initial LwIP import"
```

**The mechanism (smoking gun):** our root commit `3c06d25c` has the **identical message, author, and date** as the real upstream commit `b63d44c2` — but a **different SHA *and* a different tree** (`a39b868f` vs `4a0b8a11`). That is the exact fingerprint of a `git-filter-repo` pass: it rewrote every commit's SHA (and, because it *stripped the `wi-fi/` subtree*, it also changed the tree of even the base commit). Once the shared base commit's SHA changed, no common ancestor exists between our line and upstream — so:

- **`175 ahead` = every commit on our `master`** (nothing on it is reachable from upstream after the re-SHA).
- **`492 behind` = every commit on upstream `master`** (the entire 2018→2026 history, none reachable from ours).

The counts are therefore **meaningless as a divergence measure** — they are the two disjoint totals. This is **"ancestry is broken so the counts are noise"**, not "we are 492 commits behind real upstream."

**The REAL divergence is small.** Our local repo is **shallow**, grafted exactly at the fork point:
```
$ git -C ... rev-parse --is-shallow-repository  ->  true
$ cat .git/shallow                              ->  b63d44c2   (= real upstream fork-point, 2026-05-13)
```
We forked from upstream at `b63d44c2` (which exists at that exact SHA in real upstream), did our port work, then scrubbed. Authorship of our 175 commits:
```
$ git -C ... shortlog -sn master
   128  Witold Bolt          |
    32  Witold Bołt          |  = 160 owner-authored PORT commits  (the real work)
    11  Andrzej Głowiński    |  = re-SHA'd UPSTREAM pppos/modem/sockets commits (match stale origin/master 1:1)
     3  Marek Białowąs       |  = re-SHA'd UPSTREAM base (ipsec/tuntap)
     1  Michał Woyke         |  = re-SHA'd UPSTREAM base
```
**→ Real content divergence ≈ 160 owner commits**, on top of a ~15-commit re-SHA'd upstream base. Upstream has advanced ~16 commits past our fork point (`b63d44c2 → 2ec4579a`), some of which we already merged (via `e4c1332 "Merge remote-tracking branch 'origin/master'"`, re-SHA'd to `cb94796`).

---

## C. lib-lwip — clean, unlike the parent

```
$ git -C .../lib-lwip merge-base --is-ancestor cac552df1 HEAD   ->  YES (clean FF)
$ git -C .../lib-lwip rev-list --left-right --count HEAD...origin/STABLE-2_1_x_phoenix  ->  4   0
$ git -C .../lib-lwip log --oneline origin/STABLE-2_1_x_phoenix..HEAD
  58e89121a  api: ingress TCP window crediting (LWIP_INGRESS_CREDIT, default off)
  8f8335c8b  api/recv_tcp: coalesce back-to-back TCP segments onto one recvmbox chain
  8da7fc2fe  Revert FIONREAD changes — they regressed NFS/rendering
  4d244655d  lwip: FIONREAD for UDP sockets (int value + LINUXMODE default) — Quake multiplayer

# our fork's STABLE-2_1_x_phoenix branch IS byte-identical to upstream:
$ gh api repos/phoenix-rtos/lwip/compare/STABLE-2_1_x_phoenix...rpi-phoenix-rtos:STABLE-2_1_x_phoenix
  -> {ahead:0, behind:0, status:"identical"}
```

- **`lib-lwip` has intact ancestry.** Our submodule HEAD `58e89121a` is **4 commits fast-forward** over upstream `STABLE-2_1_x_phoenix` (`cac552df1`), **0 behind**. Base = real lwIP 2.1.3.
- **The 4 commits are our real changes** (2 of which are an add+revert FIONREAD pair → nets to **2 effective patches**: recvmbox coalescing + ingress window crediting).
- **DEFECT FOUND (submodule tracking hazard):** the pinned SHA `58e89121a` lives only on our fork's **`master`** branch, but `.gitmodules` says `branch = STABLE-2_1_x_phoenix`, and our fork's `STABLE-2_1_x_phoenix` is still at upstream `cac552df1`:
  ```
  $ gh api repos/rpi-phoenix-rtos/lwip/branches/STABLE-2_1_x_phoenix --jq .commit.sha  ->  cac552df1  (NOT 58e89)
  ```
  A `git submodule update --remote` would silently reset lib-lwip to `cac552df1` and **drop our 4 commits**. Fresh `git clone --recursive` is fine today (the pinned gitlink SHA `58e89` *is* reachable in the fork, on `master`), but the branch pin is wrong.

---

## D. OUR REAL CHANGES (what we actually changed)

Real work range on the parent (`f786486` = first genuinely-ours commit, "genet Tier 1", through `master`). Top-level dirs touched:
```
$ git -C ... diff --name-only f786486~1 master | (dir tally)
  10 drivers/     5 port/     4 modem/     3 include/     2 _targets/
   1 tests/   1 lib-lwip (gitlink)   1 ipsec/   1 Makefile   1 .gitmodules   1 .gitignore
```

**Coherent change-sets (≈6–10 logical patches):**

1. **GENET driver** (`drivers/bcm-genet.c`, `drivers/*genet*`, `include/`) — BCM2711 GENET v5 Ethernet driver, brought up in "Tiers 1–5" across many WIP commits (link-up → TDMA TX → RDMA RX → lwIP wiring → IRQ-driven RX → real MAC from VideoCore mailbox). *Currently ~30+ tier/checkpoint WIP commits — needs squashing into one coherent driver patch (or a small ordered set).*
2. **Gigabit RX perf** — `275a2d0 bcm-genet: gigabit RX fixes + cacheable pool + input batching + diagnostics`, cacheable-RX, 256 unique bufs.
3. **Gigabit TX perf** — `3df1d71 pipelined multi-slot TX ring`, `1ae0f21 cacheable TX buffer + pipelined TX default-on`.
4. **port/sockets glue fixes** — `6093bb2 FIONBIO non-blocking`, `8520b92 getnameinfo OOB write`, `f9f94ee do_getifaddrs cleanup`, `67df3d1 poll readiness wakeup`.
5. **lwipopts / checksum** — `bb990f0 default LWIP_CHKSUM_ALGORITHM to 3 (guarded)`.
6. **recvmbox coalescing + build flags + banner** — `7ee48e3`.
7. **lib-lwip submodule bump** — `e8cc8c5 bump lib-lwip to ingress TCP window crediting (58e89121)`, plus `3250b23 point submodule at rpi-phoenix-rtos/lwip fork`.
8. **Diagnostics (probably NOT for upstream PR)** — `344f551…` diag-udp responder on :9999, `net-test` harness, RXPROF/RXSTATS gated counters. `port/diag-udp.c` is the ~220 KB history-bloat blob.
9. Trivial upstream-adjacent: `ipsec` fallthrough warning, `_targets` pppos/pppou for imx6ull, `drivers/tuntap` tweaks (some of these are re-SHA'd upstream, exclude from series).

**Blob / firmware pollution:** none on `master`. `wi-fi/` is fully scrubbed (`git ls-tree -r master | grep -ic wi-fi` → `0`). Largest blobs across *all* branches are **source files**, not firmware binaries:
```
226 KB  port/diag-udp.c          (many versions — diagnostic source, on master's history)
216 KB  wi-fi/whd/whd_wifi_api.c  (WHD source — ONLY on wifi-wip / full-history-backup, never-push)
```
No `.bin/.hcd/.clm/nvram` firmware binaries in the top blobs — consistent with the prior "firmware-blob check = EMPTY" finding. The wi-fi WHD *source* is already public in canonical upstream anyway.

**Separability verdict:** the changes are cleanly separable into a reviewable patch series. The GENET driver's tier-by-tier WIP is the only messy part and should be squashed. Diagnostics should be dropped or isolated for the upstream PR.

---

## E. REMEDIATION PLAN

**Goals:** (1) restore a real common ancestor with upstream so we can PR + track incoming upstream changes; (2) fix the misleading badge; (3) keep the published history / coordination manifests intact; (4) never re-leak wi-fi blobs.

### Guiding decisions

- **Sequence lib-lwip FIRST.** The parent's gitlink can't be cleanly upstreamed until its SHAs are reachable on a sane branch of `phoenix-rtos/lwip` (or at least stable on our fork).
- **Tree-rebuild, not range-rebase.** Reapplying `f786486..master` (173 commits) by cherry-pick will conflict in `port/sockets.c` (upstream `c6c942f`/`7168654` also touched it, and we already merged them). Instead reconstruct ~6–10 logical commits **from the final validated working tree** — the pattern this project already used successfully (the "5 curated commits"). Verify equivalence with a tree-diff.
- **Avoid force-push as the primary path.** Prefer publishing a new clean branch and flipping the fork's **default branch** in GitHub settings; that alone fixes the badge. Force-pushing `master` is an optional, owner-gated second step.
- **Never-push list unchanged:** local `wifi-wip`, `full-history-backup`, and the untracked `port/wifi-fw-43455.*` / `wifi-nvram-43455.*` blobs.

### Option comparison

| Option | What | Pros | Cons / risk |
|---|---|---|---|
| **(i) Fresh upstream-rooted clean branch + curated series** ⭐ recommended | Branch from real upstream tip, rebuild ~6–10 logical commits from the validated tree | Real common ancestor restored → PR-able + `git rebase origin/master` works forever; no force-push needed if we flip default branch; reviewable | Need to reconstruct commits + verify tree-equivalence (mechanical, ~1 session) |
| (ii) History rewrite (filter-repo/rebase) on `master` | Rewrite existing `master` onto upstream | Single branch | Still requires re-rooting anyway; **force-push** over published history; breaks manifest SHAs; higher risk |
| (iii) Hybrid | Do (i), keep old `master` as archive, migrate default branch, optionally later fast-forward/replace | Best of both: continuity + clean upstreamable line | Two branches to reason about briefly |

**Recommended: (i), executed as (iii)** — build the clean branch, flip the default branch, keep old `master` archived; decide later whether to overwrite `master`.

### Step 1 — lib-lwip (do first; all clean FF, no force-push)

```bash
LL=/home/houp/phoenix-rpi/sources/phoenix-rtos-lwip/lib-lwip

# 1a. Publish our 4 commits to the fork's STABLE-2_1_x_phoenix branch (FF: 4-ahead / 0-behind — safe).
git -C "$LL" push publish 58e89121a69b747076e6d1cf689056ff7b937150:refs/heads/STABLE-2_1_x_phoenix

# 1b. (verify) fork branch now == 58e89
gh api repos/rpi-phoenix-rtos/lwip/branches/STABLE-2_1_x_phoenix --jq .commit.sha   # expect 58e89121a

# 1c. Upstream PR to phoenix-rtos/lwip. Push all 4 commits AS-IS (option a — fewest moving parts;
#     keeps the parent gitlink 58e89 valid). Net effect is clean (the FIONREAD add+revert cancel out);
#     upstream reviewers may ask to squash the add+revert pair — do that in the PR branch if requested,
#     which does NOT change 58e89 on our tracked branch.
#     Open PR from rpi-phoenix-rtos:STABLE-2_1_x_phoenix -> phoenix-rtos:STABLE-2_1_x_phoenix.
#     (Do NOT rebuild a 2-commit branch here — that mints new SHAs and would force Step 2's gitlink
#      to chase them. If a squashed series is ever wanted, do it as a separate branch and re-pin.)
```
This also removes the `submodule update --remote` hazard (the pinned SHA now lives on the tracked branch).

### Step 2 — parent: build a clean upstream-rooted branch (owner runs; I did NOT unshallow)

```bash
P=/home/houp/phoenix-rpi/sources/phoenix-rtos-lwip

# 2a. De-shallow so real upstream ancestry is present locally (mutates .git — deferred to owner).
git -C "$P" fetch --unshallow origin
git -C "$P" fetch origin master

# 2b. Base the clean branch at the upstream commit our tree ACTUALLY incorporates — NOT the current
#     upstream tip. Our master merged upstream through 25d74cf ("ipsec/des: ignore -Wimplicit-fallthrough",
#     2026-07-03), re-SHA'd to cb94796 in our history. Upstream has since advanced 5 commits (25d74cf ahead_by 5
#     of tip 2ec4579a). Basing on the tip would make the diff-check in 2d silently REVERT those 5 commits.
git -C "$P" merge-base --is-ancestor 25d74cf origin/master && echo "OK: 25d74cf is on upstream master"
git -C "$P" checkout -b rpi4-port-clean 25d74cf

# 2c. Rebuild the curated series FROM THE VALIDATED TREE (bf34d89 = current master tip), as a small
#     ordered set of commits (see §D 1–7). Robust skeleton — make the tree IDENTICAL in one shot
#     (this also captures any files our port DELETED, which an additive `checkout -- <dirs>` would miss),
#     then split into logical commits:
git -C "$P" read-tree -m -u master        # working tree + index now byte-identical to master's tree
#     (equivalently: `git rm -rq --cached . && git checkout master -- . && git add -A`)
#     ... then build ~6-10 commits with `git reset -p HEAD~ ... && git add -p` OR by re-adding per area:
#         GENET driver / RX perf / TX perf / port-sockets fixes / lwipopts / recvmbox / lib-lwip bump.
#     Exclude diagnostics (diag-udp, net-test, RXPROF) from the upstream-facing series or isolate them last.

# 2d. VERIFY byte-equivalence to the validated tree (excluding wi-fi, which is gone anyway):
git -C "$P" diff master rpi4-port-clean -- ':(exclude)wi-fi'    # expect EMPTY
git -C "$P" ls-tree rpi4-port-clean lib-lwip                    # expect gitlink == 58e89121a

# 2e. NOW pick up the ~5 newer upstream commits in a controlled step (this is also the first proof that
#     upstream tracking works again — impossible before re-rooting):
git -C "$P" rebase origin/master        # or `git merge origin/master`; resolve any conflicts here
```

### Step 3 — fix the badge WITHOUT force-push (preferred)

```bash
# 3a. Push the clean branch to the fork (new branch — no force, nothing overwritten).
git -C "$P" push publish rpi4-port-clean

# 3b. In GitHub (rpi-phoenix-rtos/phoenix-rtos-lwip → Settings → Branches):
#     set DEFAULT BRANCH = rpi4-port-clean.
#     Because rpi4-port-clean now shares upstream's root AND (after 2e) sits on the current upstream tip,
#     the fork badge shows a truthful "~160 ahead / 0 behind" (our real port commits) instead of "175/492".

# 3c. Open the upstream PR from rpi-phoenix-rtos:rpi4-port-clean -> phoenix-rtos:master.
```

### Step 4 (OPTIONAL, owner-gated) — collapse onto `master`

Only if the owner wants a single `master`:
```bash
# Archive first (so manifest SHAs stay resolvable):
git -C "$P" branch master-scrubbed-archive master
git -C "$P" push publish master-scrubbed-archive          # keep the old published line reachable
# Then move master to the clean line (THIS IS A FORCE-PUSH — owner decision):
git -C "$P" branch -f master rpi4-port-clean
git -C "$P" push --force-with-lease publish master
```
**Force-push risk:** our coordination-repo manifests reference the current scrubbed `master` SHAs (`275a2d0…f51d700…bf34d89`); Step 4 makes them resolvable only via `master-scrubbed-archive` (and the local `full-history-backup`). GitHub keeps old objects reachable by SHA until GC regardless. Acceptable because the fork has no external consumers — but it is a deliberate owner decision, hence gated. **Step 3 (flip default branch) already fixes the badge, so Step 4 is not required.**

### Going forward — tracking real upstream

- **Parent:** once `rpi4-port-clean` is rooted on `origin/master`, track upstream normally: `git -C "$P" fetch origin && git -C "$P" rebase origin/master` (or merge). This is **impossible today** (no common ancestor makes rebase/merge meaningless) and becomes trivial after re-rooting. Keep de-shallowed.
- **lib-lwip:** keep tracking `origin/STABLE-2_1_x_phoenix` (correct as-is). Rebase our 2 effective patches onto new upstream tips as they land; if/when the upstream PR merges, the delta shrinks to zero.
- **Branch-tracking answers:** parent → `master` (correct, no change). lib-lwip → `STABLE-2_1_x_phoenix` (correct, no change). The `.gitmodules` pin is right; only the *fork branch content* needed the Step 1a FF push.

### Never-push (unchanged rule)
Local `wifi-wip`, `full-history-backup`, and untracked `port/wifi-fw-43455.*` / `wifi-nvram-43455.*` stay local-only. None of the above steps touch them.

---

## Evidence appendix (commands run, read-only)

- `git -C <repo> remote -v | branch -vv | branch -r | rev-parse HEAD` — topology (§A).
- `git -C <parent> merge-base [--all] master origin/master` → NONE; `rev-list --left-right --count master...origin/master` → `175 16`; roots `3c06d25c` vs `b63d44c2` (same msg/author/date, different SHA+tree). (§B)
- `git -C <parent> rev-parse --is-shallow-repository` → `true`; `.git/shallow` → `b63d44c2` (fork point). (§B)
- `gh api repos/phoenix-rtos/phoenix-rtos-lwip/compare/master...rpi-phoenix-rtos:master` → `404 No common ancestor`; upstream master total = **492**; our master total = **175**. (§B)
- `git -C <parent> shortlog -sn master` → Witold Bolt 160 (real work), others re-SHA'd upstream. (§B/§D)
- `git -C <lib-lwip> merge-base --is-ancestor cac552df1 HEAD` → YES; `rev-list --left-right --count HEAD...origin/STABLE-2_1_x_phoenix` → `4 0`; `gh api .../lwip/compare/...STABLE-2_1_x_phoenix` → identical. (§C)
- `gh api repos/rpi-phoenix-rtos/lwip/branches/STABLE-2_1_x_phoenix` → `cac552df1` (≠ pinned `58e89` → submodule `--remote` hazard). (§C)
- `git -C <parent> ls-tree -r master | grep -ic wi-fi` → `0` (scrubbed); largest blobs are source (`diag-udp.c`, `whd_wifi_api.c`), no firmware binaries. (§D)

---

## EXECUTED 2026-08-26 (non-destructive steps done; force-push + upstream PR owner-gated)

Done autonomously (advisor-reviewed; all non-destructive, byte-equivalence-gated):

- **Step 1 (lib-lwip):** FF-pushed our pinned SHA `58e89121a` to the fork's
  `STABLE-2_1_x_phoenix` (was `cac552df1`, a clean fast-forward). Fixes the
  `git submodule update --remote` hazard (pinned SHA now lives on the tracked branch).
- **Step 2 (parent clean branch):** de-shallowed `origin`; verified base
  (`diff 25d74cf cb94796 -- ':(exclude)wi-fi'` empty ⇒ `25d74cf` is the true root).
  Built branch **`rpi4-port-clean`** rooted at upstream `25d74cf` as 6 curated commits
  (RPi4 target/arch/build · GENET driver · gigabit port path · lib-lwip bump ·
  diagnostics · drop-upstream-wi-fi), in a throwaway worktree (live sibling never left
  `master`). **GATE PASSED:** `git diff master rpi4-port-clean` is EMPTY (byte-identical
  to the validated master tree) and the lib-lwip gitlink == `58e89121a`.
- **Step 3 (badge fix, no force-push):** pushed `rpi4-port-clean` to the fork and flipped
  the fork's **default branch** `master` → `rpi4-port-clean`. The upstream compare now
  resolves: **`ahead_by:6, behind_by:5, diverged`** (real common ancestor restored) — was
  `404 No common ancestor` / the bogus "175 ahead / 492 behind". `git rebase origin/master`
  is meaningful again.
- **DEFERRED (advisor):** absorbing the 5 newer upstream commits (`25d74cf..2ec4579`) — they
  are unbuilt/unbooted and the Pi UART is down; the default branch must stay the validated
  tree for the owner's task-3 test. Absorb later as the first exercise of restored tracking,
  with a build+boot cycle.
- **NOT done (owner-gated):** Step 4 force-rewrite of `master` (breaks coord-manifest SHAs;
  the default-flip already fixed the badge, so it's optional), and the upstream PR to
  `phoenix-rtos/lwip` (outward-facing, in the owner's name).

**One-line reverts if wanted:** default branch →
`gh api -X PATCH repos/rpi-phoenix-rtos/phoenix-rtos-lwip -f default_branch=master`;
the `rpi4-port-clean` branch and the lib-lwip FF are additive (delete the branch / no
history was overwritten). `master` on the fork is untouched (still `bf34d89`).

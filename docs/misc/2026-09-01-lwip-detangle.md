# lwip fork de-tangle — method and result (2026-09-01)

Owner's ask: *"Can we somehow fix the lwip repo to escape from this history
rewrite / cherry picking / filter scrubbing mess? If needed we can recreate the
lwip clean from upstream and apply our changes onto it and just remove the old,
crazy-history fork."* Plus: *"I don't think removing parts of upstream code is
the right approach."*

Both are now done. The fork's history is rooted in real upstream, `wi-fi/` is
restored, and the result is boot-proven on the Pi 4.

## Why it was broken

A past `git-filter-repo` scrub of the `wi-fi/` subtree re-SHA'd every commit, so
our fork and upstream had **no common ancestor**. Consequences: `git pull` was
impossible, GitHub showed a meaningless "175 ahead / 492 behind", and the port
could not be offered upstream.

## Method (reusable for any disjoint fork)

1. **Find the true fork base by blob fingerprint**, not by date:
   `git rev-list --max-parents=0 master`, then for a few upstream-owned files
   `git log origin/master --find-object=$(git rev-parse <root>:<file>)`.
   Base = upstream **`b63d44c`**, *proved* by `git diff b63d44c <root> -- . ':(exclude)wi-fi'`
   being **empty**.
2. **Two approaches that look right and are wrong.** Patching endpoint-to-endpoint
   (`diff origin/master master`) **reverts upstream's newer work**, because our
   fork lagged upstream on `ephy.c`, `pppos.c` and the `gpio_*`→`net_gpio*`
   rename. Grafting a fake ancestor and merging keeps exactly the history we were
   asked to remove.
3. **Do a real three-way *tree* merge** with plumbing — no fake ancestry, correct
   by construction:
   ```
   git switch -c rebase-clean origin/master
   git read-tree -m -u b63d44c origin/master master   # base, ours, theirs
   git merge-index -o git-merge-one-file -a
   ```
4. **Undo the wi-fi deletion the merge inherits** (theirs deleted it):
   `git restore --source=origin/master --staged --worktree -- wi-fi`.
5. **One conflict only** — `drivers/pppos.c`, upstream's `const char *const *at_cmd`
   vs our older `const char **`. Kept upstream's.
6. Recommitted as **11 clean, upstreamable commits** (build/target plumbing,
   checksum algorithm, mbox coalescing, `dmammap_cached`, netif-driver hook,
   the GENET driver, the BCM54213PE PHY, socket fixes, `/dev/ipstats`, bench/tests,
   submodule pin).

## Result

| | before | after |
|---|---|---|
| upstream is an ancestor | **no** | **yes** |
| behind upstream | 492 (artifact) | **0** |
| our commits on top | 175 (rewritten) | **11 (clean)** |
| divergence | 31 files / 3324+ / 144− | **21 files / 3257+ / 19−** |
| `wi-fi/` | deleted (49k lines) | **restored, identical to upstream** |

Deletions fell 144 → 19 because the merge stopped reverting upstream work: the
`net_gpio*` rename, the `create_mutexcond_bulk` fix, the ephy arg-parsing fix and
two upstream-new files (`Makefile.aarch64a53-zynqmp`, `quectel_rm500u/pppos_modem.h`)
all came back for free.

## Safety order (keep this order on any repeat)

1. **Push archive refs first** — `archive/pre-detangle-2026-09` (old master
   `bf34d89`) and `archive/full-history-backup`. Old manifests pin lwip SHAs, and
   `scripts/restore-integration-state.sh` must still be able to fetch them.
2. Build `--scope core` (lwip is core — the stale-core hazard applies) and
   **boot-test on hardware**.
3. Only then `git push --force-with-lease publish master`.

## Hardware proof (label `lwip-detangle`)

`lwip started` · `genet link up` · `netif has IP` 10.42.0.12 ·
`nfs-fs: mounted 10.42.0.1:/ via v4` · `nfs-fs: registered / (takeover)` ·
**0 faults**. NFS root over genet is itself the end-to-end TCP/IP proof.

Manifest: `manifests/2026-09-01-lwip-detangled-on-upstream.md`.
The org fork's default branch is back to `master`; the superseded
`rpi4-port-clean` experiment branch is left in place (its tree was byte-identical
to the archived old master) and can be deleted on request.

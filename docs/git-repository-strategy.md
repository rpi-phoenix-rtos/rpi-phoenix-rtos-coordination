# Git Repository Strategy

Phoenix RTOS is split across multiple upstream repositories. The Raspberry Pi port should preserve that structure locally instead of trying to force a monorepo.

## 1. Working Model

This repository is the coordination repository.

Its role:

- store long-lived documentation
- store agent playbooks
- store integration manifests and session state
- describe how the upstream Phoenix repositories fit together

The actual implementation should happen in local clones of the upstream repositories.

## 2. Recommended Local Layout

Recommended workspace shape:

```text
phoenix-rpi/
  AGENTS.md
  docs/
  skills/
  manifests/
  sources/
    phoenix-rtos-kernel/
    plo/
    phoenix-rtos-devices/
    phoenix-rtos-filesystems/
    phoenix-rtos-build/
    phoenix-rtos-project/
    phoenix-rtos-tests/
```

Notes:

- keep the upstream repos as sibling directories under `sources/`
- do not copy their contents into this coordination repo
- do not use git submodules initially unless there is a specific operational need that plain sibling clones cannot satisfy
- on this workstation, keep `sources/` on the macOS filesystem and mount it into the Linux VM rather than hiding the primary working copy inside the VM disk

## 3. Why Not Start With Submodules

Submodules would add friction early:

- each repo advances on a different schedule
- early bring-up will often touch only one or two repos
- AI-driven sessions benefit from simple, independent commits
- submodule pointer churn creates extra noise before a stable integration cadence exists

Recommendation:

- use plain sibling clones
- track tested integration states in this coordination repo via manifest files
- reconsider submodules only if later CI or distribution needs them

## 4. Remote Strategy

For each upstream repository:

1. clone the official Phoenix repository into `sources/`
2. add the official repo as `upstream` if the initial clone uses a fork
3. use `origin` for the writable fork if one exists
4. keep the default branch clean
5. do feature work on `codex/...` branches

Recommended branch naming:

- `codex/rpi4-phase1-dtb`
- `codex/rpi4-phase2-plo-uart`
- `codex/rpi4-phase3-kernel-boot`
- `codex/common-aarch64-virt-qemu`

## 5. Commit Discipline

This is mandatory for the implementation.

### Definition of a "step"

A step is a narrow, validated unit of progress such as:

- generic DTB parser refactor
- new Pi 4 `plo` UART output path
- new build target definition
- first boot smoke test for one board target

### Rule after every successful step

After the step passes its success criteria:

1. commit in each touched upstream repository
2. update the coordination repo with:
   - the tested commit SHAs
   - what was validated
   - any new constraints or follow-up tasks
3. commit that coordination-repo update

Do not allow large piles of validated but uncommitted work to accumulate.

## 6. Multi-Repo Change Order

When one logical step spans multiple repositories, commit in dependency order where possible:

1. low-level implementation repo
2. build glue repo
3. target composition repo
4. test repo
5. coordination repo

Typical examples:

- kernel HAL change:
  `phoenix-rtos-kernel` -> `phoenix-rtos-build` -> `phoenix-rtos-project` -> `phoenix-rtos-tests` -> coordination repo
- loader bring-up:
  `plo` -> `phoenix-rtos-build` -> `phoenix-rtos-project` -> `phoenix-rtos-tests` -> coordination repo

## 7. Integration Manifest Discipline

Use `manifests/` to record tested integration states.

For each validated state, record:

- repository name
- remote URL
- branch
- exact commit SHA
- board or emulator target tested
- validation result
- date
- notes

Use [manifests/integration-state-template.md](/Users/witoldbolt/phoenix-rpi/manifests/integration-state-template.md) as the starting format.

This makes it possible for a future agent to reconstruct the exact multi-repo state after context compaction or a later session.

## 8. Worktrees

When parallel branches are needed, prefer `git worktree` inside each upstream repository rather than mixing unrelated experiments in one checkout.

Good uses:

- compare two DTB parser approaches
- keep a stable bring-up branch while investigating a risky PCIe change
- isolate Pi 5 preparation from ongoing Pi 4 stabilization

## 9. Recovery and Rollback

Always preserve at least one known-good integration state in the manifests.

If a new step regresses hardware boot:

1. identify the last known-good integration manifest
2. restore the affected upstream repos to those commits in separate worktrees or clean checkouts
3. reproduce the regression from that known-good baseline

Do not use destructive git cleanup commands on unrelated work.

## 10. Practical Recommendation

The first time implementation begins:

1. create `sources/`
2. clone all Phoenix repos there
3. create the first integration manifest with their clean baseline SHAs
4. begin work on the first narrow milestone only after that baseline is recorded

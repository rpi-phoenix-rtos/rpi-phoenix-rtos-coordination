# Git Repository Strategy

Phoenix RTOS is split across multiple upstream repositories. The Raspberry Pi port should preserve that structure locally instead of trying to force a monorepo.

## 1. Working Model

This repository is the coordination repository.

Its role:

- store long-lived documentation
- store agent playbooks
- store step-tracking state
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
    libphoenix/
    phoenix-rtos-kernel/
    phoenix-rtos-corelibs/
    plo/
    phoenix-rtos-doc/
    phoenix-rtos-devices/
    phoenix-rtos-filesystems/
    phoenix-rtos-build/
    phoenix-rtos-hostutils/
    phoenix-rtos-lwip/
    phoenix-rtos-ports/
    phoenix-rtos-posixsrv/
    phoenix-rtos-project/
    phoenix-rtos-tests/
    phoenix-rtos-usb/
    phoenix-rtos-utils/
```

Notes:

- keep the upstream repos as sibling directories under `sources/`
- do not copy their contents into this coordination repo
- do not use git submodules initially unless there is a specific operational need that plain sibling clones cannot satisfy
- on this workstation, keep `sources/` on the macOS filesystem and mount it into the Linux VM rather than hiding the primary working copy inside the VM disk
- treat `sources/phoenix-rtos-project/.gitmodules` as the authoritative repo inventory for local build prerequisites and re-check it before assuming the sibling set is complete

## 2.1 Local buildroot rule

Do not run the main `phoenix-rtos-project` build directly in the upstream `sources/phoenix-rtos-project` working copy.

Reason:

- `phoenix-rtos-project` still expects component paths shaped like populated submodules
- the sibling-clone workflow should remain the canonical editable source state
- build artifacts should not spill into the upstream working copy unnecessarily

Use the generated disposable buildroot instead:

- prepare it with `scripts/prepare-buildroot.sh`
- choose the component mode deliberately:
  - `scripts/prepare-buildroot.sh --link-components`
    use this for the normal sibling-clone workflow when linked component directories are sufficient
  - `scripts/prepare-buildroot.sh --copy-components`
    use this when the build must have a fully writable project tree, including copied component sources
- default output paths:
  - `--link-components`
    - writable repo checkout: `buildroots/phoenix-rtos-project`
    - read-only shared checkout in the Linux VM: `~/phoenix-buildroots/phoenix-rtos-project`
  - `--copy-components`
    - writable repo checkout: `buildroots/phoenix-rtos-project-copy`
    - read-only shared checkout in the Linux VM: `~/phoenix-buildroots/phoenix-rtos-project-copy`
- treat the buildroot as disposable and reproducible
- re-run the script after changing `phoenix-rtos-project` itself or after the repo inventory changes

Current practical rule:

- the linked buildroot is sufficient for the verified `host-generic-pc` baseline build
- the copied buildroot is required for the Phoenix toolchain build and should be treated as the safe default for current AArch64 validation work because the present `libphoenix` AArch64 flow still generates files inside its source tree

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
5. do feature work on `agent/...` branches (historical branches on `fork/` may still use the `codex/...` prefix; keep them as-is and use `agent/...` for new work)

Recommended branch naming:

- `agent/rpi4-phase1-dtb`
- `agent/rpi4-phase2-plo-uart`
- `agent/rpi4-phase3-kernel-boot`
- `agent/common-aarch64-virt-qemu`

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

Before each upstream-repo commit, run the local quality checklist from `docs/knowledge/code-quality-and-upstreaming.md`.

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

### Tooling

Two helpers automate this:

- `scripts/snapshot-integration-state.sh <slug> [--note "..."]`
  Generates `manifests/YYYY-MM-DD-<slug>.md` from the current sibling SHAs.
  Run this after every validated step, before starting the next one.
- `scripts/restore-integration-state.sh <manifest.md> [--dry-run] [--force]`
  Checks out every sibling repository to the SHAs recorded in the manifest.
  Refuses to run when any sibling has uncommitted changes unless `--force` is
  passed. Leaves sibling repos in detached-HEAD state — create a working
  branch in each repo before resuming edits.

Each manifest contains a machine-parseable `integration-state-v1` fenced block
alongside its human-readable table. Do not hand-edit the block; regenerate
the manifest instead.

### Procedure

If a new step regresses hardware boot:

1. identify the last known-good integration manifest
2. run `scripts/restore-integration-state.sh <manifest.md>` against it
3. reproduce the regression from that known-good baseline

Do not use destructive git cleanup commands on unrelated work.

## 10. Practical Recommendation

The first time implementation begins:

1. create `sources/`
2. clone all Phoenix repos there
3. create the first integration manifest with their clean baseline SHAs
4. begin work on the first narrow milestone only after that baseline is recorded

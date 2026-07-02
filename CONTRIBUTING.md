# Contributing

Thanks for your interest in the Phoenix-RTOS Raspberry Pi 4 port. This document
explains the multi-repo layout, the fork/branch model, how to build and test a
change, and how the work is intended to flow back to Phoenix-RTOS upstream.

## The multi-repo layout

This repository (`phoenix-rpi`) is a **coordination repo** — it holds the docs,
build scripts, and integration manifests, but no Phoenix-RTOS source code. The
actual OS source lives in **16 sibling repositories** cloned under `sources/`
(kernel, devices, usb, lwip, filesystems, libphoenix, plo, project, and more),
plus build-required external dependencies under `external/` (Mesa, Quakespasm,
vkQuake).

The bootstrap script (`scripts/bootstrap-linux-host.sh`) clones all of them and
wires up the remotes for you. The siblings are ordinary git repositories, **not
submodules** — you edit and commit in each one directly.

## Fork/branch model

Each sibling repo is cloned with **two remotes**:

| Remote | Points at | Use it for |
|---|---|---|
| `origin` | the Phoenix-RTOS upstream (`github.com/phoenix-rtos/<repo>`) | pulling upstream changes |
| `fork` | the work fork (`github.com/houp/<repo>`) | pushing your work |

> Note the convention is inverted from the common mental model: **`origin` is
> upstream, `fork` is where you push.** This lets the same tree pull from
> Phoenix-RTOS and push to the work fork without ambiguity. Pull upstream with
> `git -C sources/<repo> pull origin`; push your branch with
> `git -C sources/<repo> push fork <branch>`.

The Pi 4 work forks live under [`github.com/houp/*`](https://github.com/houp).
All siblings currently track their `master` branch. Publishing (pushing) the
forks public is a repo-owner action performed with the owner's GitHub access.

## Reproducible builds and the pin manifest

Because the port spans many repos, a "known-good" state is a set of SHAs across
all of them. That set is recorded in
[`manifests/release-pin.md`](manifests/release-pin.md): every sibling SHA plus
the pinned `external/` dependency commits.

- Build against the pinned state:
  `./scripts/bootstrap-linux-host.sh --pinned` — this checks every sibling out
  to the SHA in the manifest before building, reproducing a validated image.
- Without `--pinned`, siblings track `master` (a floating "latest" build for
  active development).

The manifest mechanism is shared with the rollback tooling:
`scripts/snapshot-integration-state.sh` writes a manifest from the current
sibling state, and `scripts/restore-integration-state.sh <manifest.md>`
restores all siblings to a recorded state. Prefer these over ad-hoc
`git checkout` across the repos.

## Building and testing a change

1. Make your change in the relevant sibling repo under `sources/<repo>/` and
   commit it there.
2. Rebuild and produce a fresh SD image:
   ```bash
   ./scripts/rebuild-rpi4b-fast.sh --variant sd
   ```
   See [docs/BUILD.md](docs/BUILD.md) for the full build-&-flash walkthrough
   and troubleshooting (including the stale-sysroot clean-rebuild note — after
   a committed core change, do a clean rebuild so you don't ship a stale cached
   image).
3. Flash and boot the image on a Pi 4 to validate. If you have the optional lab
   rig ([docs/HARDWARE.md](docs/HARDWARE.md)), the netboot test-cycle scripts
   make this fast and hands-off.
4. When your change reaches a validated state, record the integration state
   with `scripts/snapshot-integration-state.sh` (writes a new manifest under
   `manifests/`), then commit that in the coordination repo.

## Upstreaming to Phoenix-RTOS

The intent is for the Pi 4 changes to flow back to the Phoenix-RTOS upstreams.
Code should be written for readability and upstreamability — see
[docs/knowledge/code-quality-and-upstreaming.md](docs/knowledge/code-quality-and-upstreaming.md).
Transitional Pi-4 bring-up shortcuts are tracked as `TD-NN` items in
[docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md);
each carries a matching `TODO(TD-NN):` marker in the source so it can be found
and cleaned up before upstreaming.

When a change is ready for upstream, open a pull request against the relevant
`phoenix-rtos/<repo>` from your `fork`, keeping the diff clean of Pi-4-only
diagnostics and transitional markers.

## Developing with agents

Much of this port was built using AI coding agents. If you work that way, the
agent-facing rules and session conventions are in [AGENTS.md](AGENTS.md) and
[CLAUDE.md](CLAUDE.md). They are workflow documents, not a prerequisite for
contributing by hand.

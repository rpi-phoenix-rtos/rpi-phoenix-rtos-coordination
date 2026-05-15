# Claude Code Instructions

This is the **Phoenix-RTOS Raspberry Pi port** coordination repository.

The authoritative rules for this project live in [AGENTS.md](AGENTS.md) and the files it references. This file is only the thin boot sequence Claude Code needs on session start; do not duplicate policy here.

## Session boot sequence

Read these three, in order, before any code change:

1. [AGENTS.md](AGENTS.md) — rules, conventions, mandatory reading list
2. [docs/status.md](docs/status.md) — current boot progress and active focus
3. [tracking/current-step.md](tracking/current-step.md) — the single active implementation step

Everything else in AGENTS.md's "Mandatory Reading Order" is **conditional** — read it when the task touches that area. Do not read all 14 docs on every session; that burns context with no gain.

## Project layout you should know

- **Coordination repo** (this directory): docs, scripts, tracking, manifests — no Phoenix source code
- **Sibling upstream repos**: `/Users/witoldbolt/phoenix-rpi/sources/<repo>/` — e.g. `phoenix-rtos-kernel`, `plo`. These are separate git repos, not submodules. Edit and commit there, then record the tested integration state in a new `manifests/*.md` here.
- **Active kernel branch**: `agent/rpi4-program-reloc` in `sources/phoenix-rtos-kernel`. Known-good rollback tag: `known-good/2026-04-19-map-relocation-complete`.
- **Build loop**: `./scripts/rebuild-rpi4b-fast.sh` → `./scripts/capture-rpi4b-uart.sh` → `python3 scripts/summarize-rpi4b-uart-log.py <log>`. Do not improvise alternate paths — fix the helper if broken.

## Rollback discipline

This project depends on **deterministic rollback** when a step regresses boot progress.

- Every validated step should produce a manifest in `manifests/` recording all sibling SHAs.
- `scripts/snapshot-integration-state.sh` generates a manifest from current sibling state.
- `scripts/restore-integration-state.sh <manifest.md>` restores all siblings to the recorded SHAs.
- Use these rather than ad-hoc `git checkout` across multiple repos.

## Technical debt

The Pi 4 bring-up is currently accepting known shortcuts to reach a first full boot. Each shortcut has an ID (e.g. `TD-01`) in [docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md) and a matching `TODO(TD-xx):` comment in source code. When touching transitional code, keep those markers; when resolving a debt item, remove both the marker and its entry.

The code will be **published publicly**. Optimize for readability and upstreamability (see [docs/code-quality-and-upstreaming.md](docs/code-quality-and-upstreaming.md)). Remove diagnostic-only code whose hypothesis was disproved before closing a step.

## Working-style defaults

- One active step at a time. Do not widen scope mid-step.
- Prefer small, reviewable commits in the touched upstream repo, then a coordination-repo commit that records the integration state.
- For any runtime question answerable by QEMU gdbstub, try that before adding source-level probes.
- Surface warnings from builds, DTB tooling, or scripts rather than letting them scroll past.
- You are running in a git worktree under `.claude/worktrees/`. Coordination-repo commits you make here land on the worktree branch; sibling repos are outside the worktree and are committed in place.

## Shell-command discipline (applies to subagents too)

The harness's static command validator can't allowlist piped, chained, or `cd`-prefixed commands; every such command becomes a permission prompt that interrupts the user. The project ships wrapper scripts whose entire surface is statically allowlisted in `.claude/settings.json`. Use them.

**Never write:**
- `cd <sibling-repo> && git ...` — the harness flags this as "untrusted hook risk"
- `find <dir> -exec grep ...` — flagged as "command execution"
- `ls -t … | head | sed …` and other multi-stage pipelines — not allowlistable
- `sed -n '<range>p' <file>` to read a file slice — `p` flag trips the validator

**Always use instead:**
- `./scripts/git-siblings.sh <subcmd>` for any git read across coord + sibling repos
- `./scripts/git-siblings.sh in <repo> <git-subcmd> [args...]` for repo-scoped git reads (covers `log <range>`, `show <sha> -- <paths>`, `show --stat <sha1> <sha2>`, etc.)
- The **Read tool** (with `offset` + `limit`) for any file slice — never `sed -n`, `head`, or `tail` of source files
- `./scripts/uart-summary.sh [label|path]` for UART log analysis instead of grep/sed/wc chains
- `./scripts/uart-list.sh [N] [label]` to list recent UART logs
- `grep -r <pat> <dir>` (allowlisted as `grep:*`) or `rg <pat>` (allowlisted as `rg:*`) — never `find -exec grep`
- `./scripts/rebuild-rpi4b-fast.sh` / `./scripts/test-cycle-netboot.sh` / `./scripts/test-cycle-psh-interact.sh` for build + Pi cycles
- `./scripts/qemu-debug.sh` (with `--gdb` for state capture) for any QEMU rpi4b iteration

The allowlist in `.claude/settings.json` covers every script above plus `grep`, `rg`, `git add:*`, `git commit:*`. If you need an operation not covered, **add a script wrapper for the operation, then add the script to the allowlist** — do not inline the pipeline. This keeps the permission surface stable across sessions.

## Python tooling

The host macOS Python is PEP 668-managed — **never** run `pip install --user`, never `pip --break-system-packages`. If a helper script needs an additional package (e.g. `pyserial` for `psh-interact.py`), create a local venv with `uv` and install into that:

```
uv venv .venv
.venv/bin/python -m pip install pyserial
```

Then point any wrapper script's shebang or `PYTHON` variable at `.venv/bin/python`. Do not modify the system Python install.

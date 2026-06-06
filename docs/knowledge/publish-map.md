# Publish Map

This document records how the Raspberry Pi port work is published to GitHub.

Use it to keep future pushes, syncs, and PRs consistent across the multiple
Phoenix repositories involved in the port.

## Strategy

Current publishing model:

- keep the official Phoenix repositories as `origin`
- keep personal GitHub forks as `fork`
- keep long-running port work on `fork/codex/rpi4-port`
- do not push the porting work to fork default branches unless there is an
  explicit reason to do so
- keep this coordination repository separate from the upstream Phoenix repos

Why this model is preferred:

- upstream sync stays simple because `origin` still points to `phoenix-rtos/*`
- fork pushes stay explicit and low-risk
- the long-running Raspberry Pi work is isolated from fork `master`
- later PRs can be split repo-by-repo from one well-known branch name

## Coordination Repository

- local repo:
  - `/Users/witoldbolt/phoenix-rpi`
- GitHub repo:
  - <https://github.com/houp/phoenix-rpi>
- tracked branch:
  - local `main`
  - remote `origin/main`

Note:

- this repository is the coordination and knowledge-base repo
- it is not a fork of a Phoenix upstream repository

## Phoenix Fork Branch Map

The repositories below currently contain local Raspberry Pi port work and are
published to personal forks.

| Repo | Local path | Upstream `origin` | Personal fork | Local branch | Published fork branch |
| --- | --- | --- | --- | --- | --- |
| `libphoenix` | `/Users/witoldbolt/phoenix-rpi/sources/libphoenix` | <https://github.com/phoenix-rtos/libphoenix> | <https://github.com/houp/libphoenix> | `master` | `codex/rpi4-port` |
| `phoenix-rtos-build` | `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-build` | <https://github.com/phoenix-rtos/phoenix-rtos-build> | <https://github.com/houp/phoenix-rtos-build> | `master` | `codex/rpi4-port` |
| `phoenix-rtos-devices` | `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-devices` | <https://github.com/phoenix-rtos/phoenix-rtos-devices> | <https://github.com/houp/phoenix-rtos-devices> | `master` | `codex/rpi4-port` |
| `phoenix-rtos-filesystems` | `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-filesystems` | <https://github.com/phoenix-rtos/phoenix-rtos-filesystems> | <https://github.com/houp/phoenix-rtos-filesystems> | `master` | `codex/rpi4-port` |
| `phoenix-rtos-kernel` | `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel` | <https://github.com/phoenix-rtos/phoenix-rtos-kernel> | <https://github.com/houp/phoenix-rtos-kernel> | `codex/common-aarch64-platform-makefiles` | `codex/rpi4-port` |
| `phoenix-rtos-project` | `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project` | <https://github.com/phoenix-rtos/phoenix-rtos-project> | <https://github.com/houp/phoenix-rtos-project> | `master` | `codex/rpi4-port` |
| `phoenix-rtos-usb` | `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-usb` | <https://github.com/phoenix-rtos/phoenix-rtos-usb> | <https://github.com/houp/phoenix-rtos-usb> | `master` | `codex/rpi4-port` |
| `phoenix-rtos-utils` | `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-utils` | <https://github.com/phoenix-rtos/phoenix-rtos-utils> | <https://github.com/houp/phoenix-rtos-utils> | `master` | `codex/rpi4-port` |
| `plo` | `/Users/witoldbolt/phoenix-rpi/sources/plo` | <https://github.com/phoenix-rtos/plo> | <https://github.com/houp/plo> | `codex/common-aarch64-platform-makefiles` | `codex/rpi4-port` |

## Current Remote Convention

For the Phoenix sibling repositories that have published fork branches:

- `origin`:
  official upstream Phoenix repo
- `fork`:
  personal `houp` fork

This convention should be preserved. Do not rename `origin` to the fork.

## Normal Future Workflow

### Upstream sync

1. fetch from `origin`
2. merge or rebase onto the local working branch
3. validate locally
4. push the updated branch to `fork/codex/rpi4-port`

### New implementation work

1. keep working in the current local branch for that repo
2. commit small validated steps locally
3. push to the same published fork branch:
   - `git push fork HEAD:refs/heads/codex/rpi4-port`

### PR creation later

Preferred later PR shape:

- split the work into smaller reviewable PRs by subsystem, not one giant PR
- use the fork `codex/rpi4-port` branch as the long-running integration branch
- when needed, branch off smaller PR branches from that integration branch in
  the individual fork repos

Examples:

- `codex/pi4-dtb-parser-cleanup`
- `codex/pi4-generic-timer`
- `codex/pi4-plo-hdmi`
- `codex/pi4-pcie-xhci`

## Re-verify

Before future publish work:

- confirm GitHub account `houp` is still the intended fork owner
- confirm each `fork` remote still points to the expected GitHub URL
- confirm the long-running branch name is still meant to be
  `codex/rpi4-port`

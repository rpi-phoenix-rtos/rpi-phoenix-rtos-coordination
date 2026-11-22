# Upstream bug reports (drafts)

**Status: draft, not submitted.** These are drafts only. None of them has been filed, proposed or posted anywhere
upstream (GitHub, GitLab, mailing lists, e-mail). The owner reviews each one, rewrites it where a project requires
that, and decides whether and when it goes out; each is laid out the way the receiving project's tracker expects.

**Purpose.** The defects that we found, **and mostly fixed in our fork**, that the upstream projects also have. Most
are in Phoenix-RTOS itself (kernel, libphoenix, filesystems, devices, usb, posixsrv, lwip, plo, ports, build); a few
are in third-party projects we port (Mesa, libnfs, libX11). This file complements
[CONTRIBUTION-PLAN.md](CONTRIBUTION-PLAN.md), which plans the pull requests: here are the reports that go with them,
or instead of them where we have no fix to offer. General defects come first; a brief section near the end covers
Raspberry Pi 4 / BCM2711 hardware.

**IDs.** Entries reuse the contribution plan's IDs (`K..` kernel, `L..` libphoenix, `F..` filesystems, `D..`
devices, `U..` usb, `P..` posixsrv, `W..` lwip, `B..` plo, `R..` ports), so a report and its pull request can be
matched. `K47`, `L49` and `W04` are new here; `F04a`, `F04d` and `F04w` split the plan's ext2 series `F04`; `T..`
(toolchain), `M..` (Mesa), `N..` (libnfs) and `X..` (X.org) are new series. Security-relevant entries have opaque
IDs `S01`…`S23` (see below).

**How it was compiled (2026-11-21).** Candidates came from the contribution plan (its tables and draft issues), the
"★ Start here" table and the "General bug fixes" sections of
[PHOENIX-RTOS-RPI4-CHANGES.md](../PHOENIX-RTOS-RPI4-CHANGES.md), [KNOWN-ISSUES.md](../KNOWN-ISSUES.md) and the weekly
logs. We read the fix commit behind every candidate, then settled its upstream status by **reading upstream's source**,
not our notes: the defective lines were looked up in the upstream trees fetched on
2026-11-21, at these revisions.

| repository | upstream revision checked |
|---|---|
| phoenix-rtos-kernel | `origin/master` `4c7564cd` (2026-11-18) |
| libphoenix | `origin/master` `fc60045` (2026-11-06) |
| phoenix-rtos-filesystems | `origin/master` `52ae003` (2026-10-30) |
| phoenix-rtos-devices | `origin/master` `d1a7e764` (2026-11-20) |
| phoenix-rtos-usb | `origin/master` `6397a27` (2026-11-17) |
| phoenix-rtos-corelibs | `origin/master` `5d676e7` (2026-11-18) |
| phoenix-rtos-posixsrv | `origin/master` `4673928` (2026-10-07) |
| phoenix-rtos-lwip | `origin/master` `3adc368` (2026-11-04) |
| plo | `origin/master` `6e97833` (2026-11-06) |
| phoenix-rtos-ports | `origin/master` `23101f9` (2026-11-17) |
| phoenix-rtos-build | `origin/master` `46a69e2` (2026-11-16) |
| phoenix-rtos-tests | `origin/master` `7d48d34` (2026-11-18) |
| phoenix-rtos-project | `origin/master` `46bbc34` (2026-11-18) |
| Mesa | `main` `98fc8b8f62a` (2026-11-21); we ship 26.2.0 |
| libnfs | `master` `e6ed2b1` (2026-11-21, 8.1.0-5); we ship 6.0.2 |
| libX11 | `master` `1b16905` (2026-11-19, 1.8.14-4); we ship 1.8.7 |

Every `file:line` in an entry is at the revision above, and every entry repeats the date it was checked. Open
upstream pull requests were listed on 2026-11-21 and are named where one already addresses an entry. Where upstream
has fixed a defect meanwhile, the entry moved to [Already fixed upstream](#already-fixed-upstream-no-report-needed).
The upstream check is code reading only; **no entry was run on an upstream target**.

**Scope of the list.** A candidate is listed when upstream code misbehaves: it crashes or hangs, loses or corrupts
data, gives a wrong result, opens a security hole, wastes time because of a defect (not a missed optimisation), or
fails to build. Our new drivers, features and optimisations (the plan's kind **c**) are left out, however useful
upstream might find them. Code that exists only in our fork (the Pi 4 drivers,
`usb/xhci/`, GENET, our NFS server, the GPU stack, fork-only files such as `hal/aarch64/pl011.c`) is not an upstream
bug, and neither is a defect our own changes introduced. Behaviour that upstream documents as intended (for example
"advisory record locking is not supported", `phoenix-rtos-doc` `libc/functions/fcntl/fcntl.md:232`) is listed under
[Checked and not reported](#checked-and-not-reported).

**Scope column.** *general*: all targets, argued from the code in each entry; *MMU*: targets with an MMU; *64-bit*:
aarch64 and riscv64; *aarch64*: any aarch64 target (upstream builds `aarch64a53-zynqmp`); *Pi 4*: BCM2711 hardware
only.

**Full and short entries.** An entry is *full* when we have a reproducer or a test, a fix of our own, and an
upstream status read from the code. Everything else is *short*: findings from reading the code, defects that
upstream's default configuration or callers never reach, issues upstream already has, and minor items. A short entry
gives the defect, its severity, our fix, the upstream status and the reason it is short. The table's "Form" column
says which form each entry has.

**Security-relevant entries are withheld.** An entry is withheld when, from the code, an ordinary program could
corrupt, read, crash or hang the kernel; could read another process's data or the kernel's; could corrupt the memory
of a system server with values it chooses (a request, a device descriptor); or when a security mechanism is weakened
(randomness, stack canaries, register or memory clearing). Such an entry appears only as an opaque ID (`S01`…`S23`)
marked "held for a private report", with its repository and nothing else: no component, location, mechanism, fix
commit or reproducer. The owner sends the withheld entries to the maintainers privately; upstream has no
`SECURITY.md`, so the first step is to ask the maintainers how they want them reported. Defects where a server
crashes or corrupts its files through its own internal state (for example [F04a](#f04a), [F01](#f01), [F15](#f15),
[W04](#w04)) are reliability bugs and are public here. The classification is from
code reading only; exploitability was not tried, and some may turn out to be ordinary bugs.

**Duplicate search.** Phoenix-RTOS accepts issues only in `phoenix-rtos/phoenix-rtos-project`; the other
repositories have issues turned off. The titles of all 689 issues there (open and closed) were downloaded on
2026-11-21 and searched by keyword, and open pull requests of the main repositories were listed. An entry records
one of three things: "searched '<terms>': none found", the matching issue or PR, or "not searched" and why. The last
is a statement about our search, not about the tracker. Third-party trackers were not searched. Search by hand again
before sending.

**Written with AI help.** An AI agent wrote these drafts from our commits, tests and logs, and AI agents read the
upstream code to decide each status. Phoenix-RTOS publishes no rule about AI-assisted contributions; Mesa's rule is
strict (see the [checklist](#before-sending-checklist-for-the-owner)). The owner should check every statement, put
the text in their own words wherever a project expects that, and follow each project's disclosure rule.

**Entry layout.** Each heading reads `ID — component: title`. Under it, a list gives the issue fields to fill in,
then our own notes: severity, scope, our fix (repository and commit in our fork,
[github.com/rpi-phoenix-rtos](https://github.com/rpi-phoenix-rtos)), whether that commit can be sent as it is, where
the defective code is upstream (`file:line`) and what the duplicate search found. The text after the list uses
upstream's own issue headings (Description, Expected behavior, Actual behavior, POSIX requirement when relevant,
Reproduction, Environment) and adds root cause, fix and evidence. Reproduction snippets were written from the code
for these reports and were not run on an upstream target; the evidence we have is the named test or harness, run on
our fork or on the host. Paths `tools/...` are in this coordination repository.

**Environment of every Phoenix-RTOS entry.** Our fork's target `aarch64a72-generic-rpi4b` on a Raspberry Pi 4 Model B
4 GB (4× Cortex-A72); fork repositories merged with upstream v3.4.0 and later `master` (2026-11-18); GCC 16.2
toolchain (upstream builds GCC 14.2). Host harnesses run on Linux x86_64. Before filing, reproduce each entry on an
upstream target: `ia32-generic-qemu` for general entries, `aarch64a53-zynqmp-qemu` for aarch64 entries.

## Start here: the ten most valuable general bugs

1. [K01](#k01) — aarch64: dirty cache lines of a page's previous owner survive into a new non-cacheable mapping and
   are later written back over DMA data.
2. [F04a](#f04a) — ext2: truncating a sparse file frees the wrong blocks, other files' blocks included.
3. [F15](#f15) — ext2: a write that runs out of space can later overwrite block 0 (at 4 KiB blocks, the superblock):
   the filesystem no longer mounts.
4. [F05](#f05) — `fsync()` and `sync()` flush nothing: three independent breaks in libphoenix, the kernel and ext2
   (short form: found by reading, no reproducer).
5. [K31](#k31) — vm/page: freed blocks never merge back into large ones; contiguous allocations fail with gigabytes
   free.
6. [K16](#k16) — a dying process waits forever on a server that has received its request; killed clients of a hung
   server never exit.
7. [F01](#f01) — ext2: block and inode allocators have no lock; any server with two worker threads corrupts the
   bitmaps.
8. [L01](#l01) — `strncmp()`/`strncasecmp()` read one byte past `n` (intermittent faults at a page boundary).
9. [L49](#l49) — default libm: `pow(-2, 3)` returns `+8`; `pow()` and `fmod()` are inaccurate.
10. [T01](#t01) — toolchain: libstdc++ is built without its clock and sleep backends, so
    `std::chrono::steady_clock` ticks once a second and `sleep_for()` oversleeps.

## Summary

87 entries: 64 public (37 full, 27 short) and 23 withheld as security-relevant. Per project: Phoenix-RTOS kernel 14
public + 13 withheld, libphoenix 19 + 1, filesystems 13 + 3, devices 3 + 1, usb 1 + 2, plo 1, posixsrv 1 + 1, lwip
3 + 1, build 1 + 1, ports 1; Mesa 5; libnfs 1; libX11 1. A further 26 candidates are already fixed upstream (or being
fixed in an open upstream pull request), and about 40 were checked and not reported (see the sections at the end).

The severity column names the worst effect: crash, hang, data corruption, wrong output, performance or build.
"Upstream" is the status read from the code on 2026-11-21: "open" means the defective code is unchanged at the revision in the table above;
"dup #N" means an upstream issue already reports it. "Patch": *clean* = our commit cherry-picks onto upstream as it
is (text merge only; compiled for aarch64 only, see the plan); *conflicts* = it must be re-derived (the entry says
why); *n/a* = we have no patch for upstream's code.

| ID | Project | Component | Title | Severity | Scope | Our fix | Upstream | Patch | Form |
|---|---|---|---|---|---|---|---|---|---|
| [K01](#k01) | Phoenix kernel | hal/aarch64/pmap | Dirty lines of a page's previous owner survive into a non-cacheable mapping | data corruption | aarch64 | kernel `5d8645f6` + `90544244` | open | squash | full |
| [K10](#k10) | Phoenix kernel | posix/uchannel | SCM_RIGHTS descriptors not bound to their message (SEQPACKET/DGRAM) | wrong output | general | kernel `39609195` | open | conflicts | full |
| [K14](#k14) | Phoenix kernel | vm/map | `meminfo()` credits every piece of a split mapping with its whole amap | wrong output | MMU | kernel `e802ac0e` | open | conflicts | full |
| [K16](#k16) | Phoenix kernel | proc/msg | A dying process waits forever on a server that received its request | hang | general | kernel `3c5f210b`, `07bd1384`, `a8880d9d` | open | conflicts | full |
| [K18](#k18) | Phoenix kernel | posix | `ioctl()` on an AF_UNIX socket (e.g. `FIONBIO`) always fails with `EINVAL` | wrong output | general | kernel `99cb2ab4` | open | conflicts | full |
| [K28](#k28) | Phoenix kernel | vm/map | `mprotect()` rejects a length that is not a page multiple | wrong output | MMU | kernel `fc21dab0` | open | clean | full |
| [K31](#k31) | Phoenix kernel | vm/page | Freed pages never re-merge into large blocks | performance | MMU | kernel `2c003860`, `66454137` | open | clean | full |
| [K33](#k33) | Phoenix kernel | proc, vm/map | A fault whose signal is blocked or ignored re-faults forever | hang | general | kernel `d36f9967` | open | conflicts | full |
| [K11](#k11) | Phoenix kernel | posix/usocket | One-page default ring rejects framed messages ≥ 4089 B | wrong output | general | kernel `c0431a84` | open | conflicts | short |
| [K17](#k17) | Phoenix kernel | proc/threads | `SA_RESETHAND` ignored | wrong output | general | kernel `4b1975d8` | dup #844 | clean | short |
| [K25](#k25) | Phoenix kernel | posix | `p->lock` held across a blocking close in the exit/exec descriptor sweeps | hang | general | kernel `2c489b75` | open | conflicts | short |
| [K27](#k27) | Phoenix kernel | vm/object | A backing-store read error is reported as `-ENOMEM` | wrong output | MMU | kernel `0620f2e9` | open | conflicts | short |
| [K32](#k32) | Phoenix kernel | proc/process | `waitpid()` returns before the child's memory is freed | wrong output | general | kernel `286f8b8d` | open | conflicts | short |
| [K47](#k47) | Phoenix kernel | vm/object | A short read before end of file is not retried when paging in | data corruption | MMU | kernel `f145658f` | open (EOF part: PR kernel#872) | conflicts | short |
| [L01](#l01) | libphoenix | string | `strncmp()`/`strncasecmp()` read one byte past `n` | crash | general | libphoenix `f2d2789` | open | clean | full |
| [L05](#l05) | libphoenix | pthread | Default thread: one-page stack and no guard page | crash | general | libphoenix `02ab4e0`, `bad2009` | open | clean / trivial conflict | full |
| [L10](#l10) | libphoenix | unistd, sys/stat | A signal makes `access()` report `ENOENT` and `stat()` `EINTR` | wrong output | general | libphoenix `b33e8ec` | open | conflicts | full |
| [L14](#l14) | libphoenix | stdio | `setvbuf(f, NULL, _IOLBF, 0)` fails; mode change clears no flag | wrong output | general | libphoenix `fa8521d` | open | clean | full |
| [L17](#l17) | libphoenix | stdio | `fflush()` after a short write sends the written part again | wrong output | general | libphoenix `9029813` | open | clean | full |
| [L19](#l19) | libphoenix | ctype.h | ctype macros evaluate their argument more than once | wrong output | general | libphoenix `156422a` | open | conflicts | full |
| [L20](#l20) | libphoenix | stdio | `fclose(stdout)` frees a stream that is still referenced | crash | general | libphoenix `bf35aaf` | open | conflicts | full |
| [L22](#l22) | libphoenix | stdio/format | `%'d` is printed literally and shifts every later argument | wrong output | general | libphoenix `a41d8d5` | open (see #707) | split | full |
| [L23](#l23) | libphoenix | stdio/scanf | `%i`/`%x` with a sign before `0x`; bare `0x` accepted | wrong output | general | libphoenix `5020478` | open | conflicts | full |
| [L27](#l27) | libphoenix | posix/stubs | `fchdir()` returns success without changing directory | data corruption | general | libphoenix `1d86e24` | open | clean | full |
| [L29](#l29) | libphoenix, kernel, filesystems | unistd | `unlink()` removes an (empty) directory | data corruption | general | libphoenix `ae9801e` (workaround) | open | n/a | full |
| [L34](#l34) | libphoenix | pthread | `pthread_self()` is NULL in a forked child | crash | general | libphoenix `f36de2b`, `d67a879` | open | conflicts | full |
| [L49](#l49) | libphoenix | libm | `pow(-2, 3)` returns `+8`; `pow()`/`fmod()` inaccurate | wrong output | general | libphoenix `6bb0bdc`, `6bf27ab` (msun) | open (`fmod`: dup #1229) | conflicts | full |
| [L51](#l51) | libphoenix | sys/socket | `recvmsg()` into several iovecs leaves `msg_controllen` (and `msg_flags`, `msg_namelen`) as passed in | data corruption | general | libphoenix `84d3656` | open | clean | full |
| [L11](#l11) | libphoenix | pthread | `pthread_setspecific()` calls `malloc()` under the key lock | hang | general (latent) | libphoenix `052b590` | open | trivial conflict | short |
| [L30](#l30) | libphoenix | sys/stat | `rename()` onto an existing file fails with `EEXIST` | wrong output | general | libphoenix `c01f7a7` (do not send) | dup #409 | n/a | short |
| [L38](#l38) | libphoenix | termios.h | `c_oflag` constants invisible to `#ifdef` | build | general | libphoenix `e9bb8c4` | open | clean | short |
| [L39](#l39) | libphoenix | string | `gai_strerror()` says "Unknown error" for every code | wrong output | general | libphoenix `40e3a55` | open | conflicts | short |
| [L45](#l45) | libphoenix | pthread | Thread-stack unmap race; cancelled detached thread never released | crash | general | libphoenix `ad2af81`, `1a7753d` | open | conflicts | short |
| [F01](#f01) | Phoenix filesystems | ext2 | Block and inode allocators have no lock | data corruption | general | filesystems `463aec1` | open | conflicts | full |
| [F02](#f02) | Phoenix filesystems | ext2 | 32-bit byte offsets (> 4 GiB) and a wrong first data block (> 1 KiB blocks) | data corruption | general | filesystems `7618a7d`, `505c4a3` | open | clean | full |
| [F04a](#f04a) | Phoenix filesystems | ext2 | Truncate of a sparse file frees the wrong blocks | data corruption | general | filesystems `680c9b2` | open | conflicts | full |
| [F04d](#f04d) | Phoenix filesystems | ext2 | `setattr(atSize)` truncates a directory | data corruption | general | filesystems `d46ba88` | open | clean | full |
| [F04w](#f04w) | Phoenix filesystems | ext2 | Stale indirect cache, split runs, in-memory link counts, root freed at unmount | data corruption | general | filesystems `c49d0e2`, `74823c9`, `9e1ff6a`+`ba9149a`, `d4ff499` | open | partly clean | full |
| [F13](#f13) | Phoenix filesystems | ext2 | Unmount deletes cached files; unlink frees no blocks; a hole reads as block 0 | data corruption | general | filesystems `269306a`, `8a35d91`, `62d513a` | open | clean (order matters) | full |
| [F14](#f14) | Phoenix filesystems | ext2 | Reading a hole allocates indirect blocks | data corruption | general | filesystems `43c00d7` | open | conflicts | full |
| [F15](#f15) | Phoenix filesystems | ext2 | A failed indirect-block allocation is later written to block 0 | data corruption | general | filesystems `28e26d7` | open | clean | full |
| [F16](#f16) | Phoenix filesystems | ext2 | A `write()` that runs out of space part way fails and leaves the inode inconsistent | data corruption | general | filesystems `9e1ea4e` | open | conflicts | full |
| [F05](#f05) | Phoenix filesystems, libphoenix, kernel | ext2, sync, posix | `fsync()`/`sync()` flush nothing | data corruption | general | filesystems `017eb3a`, `391da7e`; libphoenix `40a1efb`; kernel `482b54c2` | open | partly clean | short |
| [F10](#f10) | Phoenix filesystems | ext2 | Crash ordering: pointers to new blocks written before the bitmap and the indirect blocks | data corruption | general | filesystems `40807bc`, `13f4808` | open | conflicts | short |
| [F11](#f11) | Phoenix filesystems | dummyfs | `link()`/`unlink()` set the wrong timestamps, also on failure | wrong output | general | filesystems `8cf0a25` | open (ext2: #1443) | clean | short |
| [F18](#f18) | Phoenix filesystems | ext2 | A symlink whose target cannot be written leaves a dangling entry | data corruption | general | filesystems `6c8c117` | open | clean | short |
| [D01](#d01) | Phoenix devices | tty/libtty | `ioctl(FIONREAD)` on a tty fails with `EINVAL` | wrong output | general | devices `b247643a` | open | conflicts | full |
| [D03](#d03) | Phoenix devices | storage/umass | A command that fails all retries is reported as success with no data | data corruption | general | devices `cf83ee6f` (+ usb `00e9ac4`) | open | conflicts | short |
| [D07](#d07) | Phoenix devices | storage/* | A read that crosses the end of a device fails with `EINVAL` | wrong output | general | none upstream (our Pi driver only) | open | n/a | short |
| [U08](#u08) | Phoenix usb | usb/hub | Over-current and suspend changes never acknowledged | hang | general | usb `182d146` | open | clean | short |
| [B01](#b01) | plo | cmds/kernel | `.bss` of the loaded kernel not zeroed | data corruption | general | plo `9551073` | dup #1733 | clean | short |
| [P03](#p03) | Phoenix posixsrv, kernel | pipe, pty, posix_poll | Pipe/pty readiness found only by re-polling (up to 100 ms); spurious `EAGAIN` | performance | general | posixsrv `7e6cf77` (+ fork-only syscall series) | open | partly clean | short |
| [W01](#w01) | Phoenix lwip | port/sockets | `ioctl(FIONBIO)` on an inet socket never sets non-blocking mode | wrong output | general | lwip (one hunk of a larger commit) | open | hunk only | full |
| [W02](#w02) | Phoenix lwip | port/sockets | `socket(AF_INET6)` returns an IPv4 socket when built without IPv6 | wrong output | general | lwip `d080252`, `83a56d5` | open | clean | full |
| [W04](#w04) | Phoenix lwip | port/threads | 512-byte thread-collector stack overflows inside `free()` | crash | general | lwip `356ae98` | open | clean | short |
| [T01](#t01) | Phoenix build | toolchain (libstdc++) | libstdc++ built without clock/sleep backends: `steady_clock` resolution 1 s | wrong output | general | build `95d9fca` (GCC 16 only) | open | no | full |
| [R01](#r01) | Phoenix ports | jq | jq 1.8.2 recipe installs libjq and oniguruma into the build host's `/usr/local` | build | host | none (we use our own recipe) | open | n/a | full |
| [TS01](#ts01) | Phoenix tests | libc/stdio | `stdio_fflush` leaves its test file behind; the next run's `stdio_fopenfclose_file` fails | test | general (persistent root) | tests `793e190` | open | clean | short |
| [M01](#m01) | Mesa | gallium/auxiliary (u_vbuf) | NULL translate object dereferenced; NULL cached in the translate cache | crash | general | ports `mesa_drm/patches/0005` (incomplete) | open | applies | short |
| [M02](#m02) | Mesa | egl/drm, gbm | GBM back buffer without a DRI image dereferenced in `eglMakeCurrent` | crash | general | ports `mesa_drm/patches/0009` | open | applies | short |
| [M03](#m03) | Mesa | broadcom/compiler | Register-allocation passes do work proportional to blocks times temporaries | performance | general | ports `mesa_drm/patches/0020`–`0022` | open (partly changed) | partly | short |
| [M04](#m04) | Mesa | v3dv | Tiles outside a render area that does not start on a supertile boundary are overwritten | data corruption | general | ports `mesa_drm/patches/0023` | open | needs rebase | short |
| [M05](#m05) | Mesa | v3d | Every shader-variant lookup computes a BLAKE3 digest | performance | general | ports `mesa_drm/patches/0024` | open | applies | short |
| [N01](#n01) | libnfs | nfs_v4 | NFSv4 `st_blocks` counted in 4096-byte units | wrong output | general | ports `libnfs/patches/02` | open | needs re-anchoring | full |
| [X01](#x01) | libX11 | om/generic, XDefaultOMIF | Font-set error path frees the caller's base-name string | crash | general | ports `xorg_libs/patches/libX11-1.8.7-…-58.patch` | open | applies | short |
| [S01](#security-relevant-entries-held-for-private-reports)–[S13](#security-relevant-entries-held-for-private-reports) | Phoenix kernel | — | Held for a private report (13 entries) | security | — | fixed in our fork | open | — | withheld |
| [S14](#security-relevant-entries-held-for-private-reports) | libphoenix | — | Held for a private report | security | — | fixed in our fork | open | — | withheld |
| [S15](#security-relevant-entries-held-for-private-reports) | Phoenix posixsrv | — | Held for a private report | security | — | fixed in our fork | open | — | withheld |
| [S16](#security-relevant-entries-held-for-private-reports) | Phoenix lwip | — | Held for a private report | security | — | fixed in our fork | open | — | withheld |
| [S17](#security-relevant-entries-held-for-private-reports)–[S19](#security-relevant-entries-held-for-private-reports) | Phoenix filesystems | — | Held for a private report (3 entries) | security | — | fixed in our fork | open | — | withheld |
| [S20](#security-relevant-entries-held-for-private-reports) | Phoenix devices | — | Held for a private report | security | — | partly fixed in our fork | open | — | withheld |
| [S21](#security-relevant-entries-held-for-private-reports)–[S22](#security-relevant-entries-held-for-private-reports) | Phoenix usb | — | Held for a private report (2 entries) | security | — | one fixed in our fork | open | — | withheld |
| [S23](#security-relevant-entries-held-for-private-reports) | Phoenix build | — | Held for a private report | security | — | fixed in our fork | open | — | withheld |

## 1. Phoenix-RTOS

Issues go to [phoenix-rtos/phoenix-rtos-project](https://github.com/phoenix-rtos/phoenix-rtos-project/issues) with
upstream's layout (Category, Description, Expected behavior, Actual behavior, POSIX requirement, Minimal reproduction
code, Environment; see #1817 and #1818 for examples) and the labels upstream uses (`bug`, `POSIX`, `kernel`,
`libphoenix`, `filesystems`, `posixsrv`, `plo`, `ports`). Pull requests go to the repository named in "Our fix", with
the repository's PR template; a test goes to `phoenix-rtos-tests` as a separate PR. Our fix commits are on `master`
of the same repository in [github.com/rpi-phoenix-rtos](https://github.com/rpi-phoenix-rtos) unless an entry says
otherwise.

### 1.1 Kernel (phoenix-rtos-kernel)

<a id="k01"></a>

#### K01 — hal/aarch64/pmap: dirty cache lines of a page's previous owner survive into a new non-cacheable mapping

- **Issue fields:** Category: BUG · labels `bug`, `kernel` · title "hal/aarch64: dirty cache lines survive when a freed page is mapped non-cacheable"
- **Severity:** data corruption (DMA buffers silently overwritten at cache-line granularity)
- **Scope:** aarch64 (`hal/aarch64` only; `hal/armv7a/pmap.c:356` already maintains the cache when mapping NC/Device)
- **Our fix:** kernel `5d8645f6` "clean+invalidate a page when it becomes non-cacheable" + `90544244` "flush a page
  made non-cacheable by its physical address" (2026-11-21; send squashed)
- **Patch:** the pair compiles for aarch64; `90544244` is the rework the first version needed (see Fix)
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `hal/aarch64/pmap.c:227`
  `_pmap_cacheOpBeforeChange()` returns at once when the old entry is invalid (`if ((oldEntry & DESCR_VALID) == 0U)
  return;`), and `_pmap_cacheOpAfterChange()` (`:255-269`) only invalidates the I-cache. A page freed by
  `_pmap_destroy()` gets no cache maintenance either.
- **Tracker:** searched 'cache', 'uncached', 'non-cach', 'dirty': none found.

**Description.** When a process exits, its pages are freed without cleaning their data-cache lines. When one of those
pages is mapped again with the Normal non-cacheable memory type (`MAP_UNCACHED`, DMA buffers), the new mapping's
entry is written over an invalid entry, so no cache maintenance runs. Dirty lines left by the previous owner stay in
the cache and are evicted at some later moment, on top of whatever the new owner wrote through its non-cacheable
mapping (often data a device already consumed or produced).

**Expected behavior.** A page reaches a non-cacheable mapping with no dirty lines in the data cache.

**Actual behavior.** 64-byte pieces of DMA buffers are overwritten with stale data, intermittently and depending on
which physical pages are reused.

**Reproduction.** No standalone reproducer. A program that dirties many pages through a cacheable mapping and exits,
followed by a program that maps `MAP_UNCACHED` memory, fills it through that mapping and checks it after a delay, will
see stale 64-byte lines when it gets the same pages; the probability depends on the cache size and the page reuse.

**Environment.** See the section header; Raspberry Pi 4 (Cortex-A72, PIPT data cache).

**Root cause.** `hal/aarch64/pmap.c:223-229`: the only cache maintenance on a memory-type change is in
`_pmap_cacheOpBeforeChange()`, which needs a valid old entry. Neither the free path nor a fresh non-cacheable mapping
cleans the page.

**Fix.** Our commit cleans and invalidates the page when the new entry's memory type is `MAIR_IDX_NONCACHED`
(`hal_cpuFlushDataCache(vaddr, vaddr + SIZE_PAGE)` in `_pmap_cacheOpAfterChange()`); Device mappings are left alone.
The first version flushed by virtual address in the current address space, which is wrong when the mapping is made in
another one (`vm_mapCopy()` for a fork child, while the parent's tables are live). `90544244` cleans the page through
its physical address instead, via the scratch mapping `_pmap_cacheOpBeforeChange()` already uses (helper
`_pmap_flushPage()`); Pi gate 2026-11-21 incl. a new fork test, `kh_vm.uncached_private_survives_fork`.

**Evidence.** On the Pi, GPU control lists written through non-cacheable buffers were corrupted at 64-byte
granularity (zeros or the previous owner's data), about 12 corrupt lists per 8 boots. A clean+invalidate of each new
buffer in the GPU driver took that to 0, and a rendering defect that depended on it from 0/15 to 7/7 correct runs;
this kernel change moved the same guarantee into the kernel, after which the driver workaround was removed. Only the
GPU case was measured; other DMA users (USB, SD, network) have the same exposure by construction.

<a id="k10"></a>

#### K10 — posix/uchannel: SCM_RIGHTS descriptors are not bound to the message that carried them

- **Issue fields:** Category: BUG · labels `bug`, `kernel`, `POSIX` · title "AF_UNIX SEQPACKET/DGRAM: descriptors passed with one message are delivered with another"
- **Severity:** wrong output (descriptors arrive with the wrong message, or are closed)
- **Scope:** general
- **Our fix:** kernel `39609195`; tests `f4fb172` (`fd_per_message`, `fd_not_carried_over`)
- **Patch:** conflicts: upstream `usocket.c` gained `MSG_TRUNC` and `getsockname()` since; re-derive
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `posix/uchannel.c:165`,
  `:188` (`LIST_ADD(&ch->fdpacks, fdpack);`) keep one list of descriptor packs per channel; `:220-224` and `:313-314`
  hand the whole list to any read that consumes a frame; `posix/usocket.c:1270-1271` (`fdpass_discard(&packs);`)
  closes what does not fit.
- **Tracker:** searched 'SCM', 'descriptor', 'ancillary', 'fd pass': none found.

**Description.** On `SOCK_SEQPACKET` and `SOCK_DGRAM` AF_UNIX sockets, the descriptors sent with `sendmsg()` go onto
one list per channel, not onto the frame they came with. The first `recvmsg()` with a control buffer takes all of
them.

**Expected behavior.** Each `recvmsg()` returns exactly the descriptors sent with that message (POSIX/Linux
semantics; descriptors that do not fit the control buffer are closed and `MSG_CTRUNC` is set).

**Actual behavior.** Two messages sent back to back with one descriptor each: the first `recvmsg()` gets both, the
second none.

**Reproduction.**
```c
int sv[2], a = open("/dev/null", O_RDONLY), b = dup(a);
socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv);
send_fd(sv[0], a, "1");   /* sendmsg() with one SCM_RIGHTS descriptor */
send_fd(sv[0], b, "2");
/* recvmsg() on sv[1] twice with room for two descriptors:
 * expected 1 + 1 descriptors, actual 2 + 0 */
```

**Environment.** See the section header.

**Root cause.** The descriptor packs are a property of the channel (`posix/uchannel.c:188`), and frames carry no
reference to their pack.

**Fix.** Mark a frame that carries descriptors in its frame header, keep the packs in frame order, and on a read take
only the pack of the frame being read; close the descriptors of a frame that is dropped or truncated.

**Evidence.** WebKit's IPC (one descriptor per message) dropped its connection; the two Unity cases fail on the old
kernel and pass with the fix (Pi).

<a id="k14"></a>

#### K14 — vm/map: `meminfo()` credits every piece of a split mapping with the whole amap

- **Issue fields:** Category: BUG · labels `bug`, `kernel` · title "meminfo(): anonymous size of split map entries counted several times"
- **Severity:** wrong output (memory use over-reported, by GBs for big processes)
- **Scope:** MMU
- **Our fix:** kernel `e802ac0e`; test `meminfo_anonsz_of_split_entries` (tests `413ac20`)
- **Patch:** conflicts (small; `vm/map.c` moved)
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `vm/map.c:1355`
  (`for (i = 0; (unsigned int)i < e->amap->size; ++i)`, also `:1392`, `:1446`) counts the whole amap for every entry;
  split entries share one amap (`:411` `amap_ref`, `:426` `new->aoffs += len;`).
- **Tracker:** searched 'meminfo', 'anonsz': none found.

**Description.** `mprotect()` of part of an anonymous mapping, or a partial `munmap()`, splits the map entry; the
pieces share the amap at different offsets. `meminfo()` counts every page of the shared amap for each piece.

**Expected behavior.** Each entry reports the anonymous pages in `[aoffs, aoffs + size)`.

**Actual behavior.** A mapping split into three pieces is reported three times over.

**Reproduction.** `mmap()` 16 pages anonymous, touch all, `mprotect()` the middle 4 pages read-only, then read the
process's entries with `meminfo()` (or `psh mem <pid>`): the three entries report 16 pages each.

**Environment.** See the section header.

**Root cause / fix.** `vm/map.c:1355` iterates `e->amap->size`; iterate only the entry's window of the amap.

**Evidence.** WebKit's memory-pressure monitor read the inflated sum and stayed in its strict mode; the test fails on
the old kernel and passes with the fix (7/0 on the Pi).

<a id="k16"></a>

#### K16 — proc/msg: a dying process waits forever on a server that has received its request

- **Issue fields:** Category: BUG · labels `bug`, `kernel` · title "Killed process never exits while a server holds its message"
- **Severity:** hang (unkillable processes; their resources are never freed)
- **Scope:** general (MMU and NOMMU: `proc/msg.c` and `proc/msg-nommu.c:66`)
- **Our fix:** kernel `3c5f210b`, `07bd1384`, `a8880d9d`; a Unity test in our phoenix-rtos-tests (6/0 with the fix,
  2 cases fail without)
- **Patch:** conflicts; a design choice is involved, so open the issue first
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `proc/msg.c:391-397`: in
  the `msg_received` state the sender waits with `proc_threadWait()` (uninterruptible), under a `FIXME` that says the
  thread "cannot be properly killed".
- **Tracker:** searched 'kill', 'SIGKILL', 'unkillable', 'exit hang': no matching issue (#1084, closed, is a different
  case: a process sleeping in `accept()`).

**Description.** Once a server has received a request (`msgRecv()` returned it), the sender sleeps uninterruptibly
until `msgRespond()`. If the sender's process is killed or calls `exit()` from another thread, that thread never
leaves the kernel, so the process is never reaped. A server that is slow, stuck, or simply keeps requests (an event
queue) leaves unkillable clients behind.

**Expected behavior.** `SIGKILL` or `exit()` ends the process; the late `msgRespond()` fails cleanly.

**Actual behavior.** The process stays forever (seen as WebKit processes reparented to init, about 85 MB each, after
their UI process quit).

**Reproduction.** Server: `msgRecv()` a request and never respond. Client: a thread sends to the server; the main
thread calls `exit(0)`. The client process never disappears from `ps`.

**Environment.** See the section header.

**Root cause.** `proc/msg.c:391-397`: the kernel message lives on the sender's kernel stack, so the sender cannot
leave while the server may still write the response.

**Fix (ours).** A thread of a dying process takes back a received request whose payload is not mapped into the server
(the server's later `msgRespond()` gets `-ENOENT`); requests with a mapped payload still wait, and the kernel names
them once. Upstream may prefer a different shape (allocate the kernel message off the stack), hence an issue first.

**Evidence.** The test passes 6/0 on the Pi; WebKit processes no longer linger.

<a id="k18"></a>

#### K18 — posix: every `ioctl()` on an AF_UNIX socket fails with `EINVAL`

- **Issue fields:** Category: BUG · labels `bug`, `kernel`, `POSIX` · title "ioctl(FIONBIO) on an AF_UNIX socket returns EINVAL"
- **Severity:** wrong output
- **Scope:** general
- **Our fix:** kernel `99cb2ab4`; test `socketpair_nonblock` (tests `90265ab`)
- **Patch:** conflicts (`posix.c` changed upstream)
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `posix/posix.c:1862`
  sends every ioctl to `f->oid.port`, which for a UNIX socket is the placeholder `posix/usocket.h:25` `#define
  USOCKET_PORT 0xffffffffU /* FIXME */`; `proc/msg.c:362-364` then returns `-EINVAL`.
- **Tracker:** searched 'FIONBIO', 'nonblock': none found.

**Description.** `posix_ioctl()` has no in-kernel path for UNIX sockets. `fcntl(F_SETFL, O_NONBLOCK)` works
(`posix.c:1683`), `ioctl(FIONBIO)` does not.

**Expected behavior.** `ioctl(fd, FIONBIO, &on)` makes the socket non-blocking and returns 0.

**Actual behavior.** `-1`, `errno == EINVAL`; the socket stays blocking. Python's `asyncio` (`setblocking(False)` on
its self-pipe socketpair) cannot start an event loop.

**Reproduction.**
```c
int sv[2], on = 1;
socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
printf("%d %d\n", ioctl(sv[0], FIONBIO, &on), errno);   /* expected 0, actual -1 EINVAL */
```

**Environment.** See the section header.

**Root cause / fix.** Handle UNIX-socket requests in `posix_ioctl()`: `FIONBIO` sets or clears the socket's
non-blocking flag through the same path as `F_SETFL` (its argument read as an `int`, as on Linux, the BSDs and lwIP);
any other request fails with `ENOTTY` instead of `EINVAL`.

**Evidence.** `socketpair_nonblock` fails on the old kernel, passes with the fix (Pi).

<a id="k28"></a>

#### K28 — vm/map: `mprotect()` rejects a length that is not a page multiple

- **Issue fields:** Category: BUG · labels `bug`, `kernel`, `POSIX` · title "mprotect(): EINVAL for a length that is not a multiple of the page size"
- **Severity:** wrong output
- **Scope:** MMU
- **Our fix:** kernel `fc21dab0`; test `mem/test_mprotect` (tests `fbfc47e`, 6/0)
- **Patch:** clean
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `vm/map.c:883` fails with
  `EINVAL` when `(len & (SIZE_PAGE - 1U)) != 0U` (and when `len == 0`).
- **Tracker:** searched 'mprotect': only #1010 (a different, ia32 page fault).

**Description / POSIX requirement.** POSIX `mprotect()` requires `addr` to be page-aligned, not `len`; the call
covers every page in `[addr, addr + len)`. Linux and the BSDs round `len` up and accept `len == 0`.

**Expected behavior.** `mprotect(p, 100, PROT_READ)` changes the first page and returns 0.

**Actual behavior.** `-1`, `EINVAL`. Quake III's JIT logged `mprotect(RX) failed` and ran from an RWX mapping.

**Reproduction.**
```c
char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
printf("%d\n", mprotect(p, 100, PROT_READ));   /* expected 0, actual -1 (EINVAL) */
```

**Environment.** See the section header.

**Fix.** Round `len` up to whole pages (`ENOMEM` if that wraps), keep `EINVAL` for an unaligned `addr`, and treat
`len == 0` as a successful no-op. This does not widen what `mprotect()` may grant (`map_checkProt()` stays).

**Evidence.** `mem/test_mprotect` 6/0 on the Pi; it fails on the old kernel.

<a id="k31"></a>

#### K31 — vm/page: freed pages never merge back into large blocks

- **Issue fields:** Category: BUG · labels `bug`, `kernel` · title "vm/page: buddy merge stops after the first merge whose block is a right half"
- **Severity:** performance (contiguous allocations fail with most of the memory free)
- **Scope:** MMU (`vm/page.c`; NOMMU uses `page-nommu.c`)
- **Our fix:** kernel `2c003860` (merge at every order), `66454137` (no boot-time block across a memory hole); host
  simulation `tools/pagealloc-hosttest`
- **Patch:** clean
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `vm/page.c:137-160`:
  after a merge only one neighbour pointer is recomputed (`:154-159` sets `lh` *or* `rh`), so the next iteration
  compares against the stale other pointer, whose `idx` was just reset at `:148`, and the loop stops.
- **Tracker:** searched 'fragment', 'buddy', 'contiguous', 'page alloc': none found.

**Description.** `vm_pageFree()` merges a freed block with its buddy and should continue with the merged block's
buddy at the next order. Because it recomputes only the left or only the right neighbour, it continues only while the
merged block keeps being a left half; a merged block that is a right half stops the merge.

**Expected behavior.** After a large process exits, its memory is available again as large blocks.

**Actual behavior.** On the Pi, after a process used and gave back 3 GB and a browser then ran, 2.9 GB was free but
the largest free block was 2 MB, then 128, 64 and 32 KB, and every GPU buffer allocation failed.

**Reproduction.** `tools/pagealloc-hosttest` runs `vm/page.c`'s allocator on the host: allocate every page singly,
free them in random order, then ask for large blocks. With upstream's merge loop 3.3 GB is free in physically
contiguous runs of 1 GB, yet the largest free block is 128 KB and no 4 MB block can be allocated.

**Environment.** See the section header; also host (Linux x86_64) simulation.

**Root cause.** `vm/page.c:154-159`.

**Fix.** Recompute both `lh` and `rh` from the merged block after every merge. Separately (`66454137`), the boot-time
sizing at `vm/page.c:222` can build one free block that spans a hole in physical memory; split blocks at holes.

**Evidence.** Host simulation: with the fix, the same freed memory gives 828 blocks of 4 MB (or 207 of 16 MB), as
before the pages were taken.

<a id="k33"></a>

#### K33 — proc, vm/map: a fault whose signal is blocked or ignored re-faults forever

- **Issue fields:** Category: BUG · labels `bug`, `kernel`, `POSIX` · title "Synchronous SIGSEGV/SIGILL that is blocked or ignored makes the thread spin forever"
- **Severity:** hang (a process spins at 100 % CPU, printing a register dump per fault)
- **Scope:** general (every HAL)
- **Our fix:** kernel `d36f9967`; test cases `segv_blocked_kills`, `segv_ignored_kills`, `segv_in_handler_kills`,
  `sigill_blocked_kills`
- **Patch:** conflicts (upstream TLS and lazy-fault changes in the same functions)
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21. `vm/map.c:845` "FIXME: In
  case signal is ignored or blocked, the process should be terminated"; the same for `SIGILL` at
  `proc/process.c:307`.
- **Tracker:** searched 'SIGSEGV', 'fault loop', 'blocked signal', 'ignored': no matching issue.

**Description / POSIX requirement.** A data or instruction abort posts `SIGSEGV`, an undefined instruction `SIGILL`,
and the exception returns to the faulting instruction. If the thread blocks the signal (as it does inside its own
handler for that signal) or the process ignores it, the signal is not delivered and the instruction faults again at
once. POSIX leaves the result of ignoring or blocking a synchronous `SIGSEGV`/`SIGILL` undefined; Linux
(`force_sig`) unblocks it and restores the default action, so the process dies.

**Expected behavior.** The process is terminated by the signal.

**Actual behavior.** The thread re-faults forever.

**Reproduction.**
```c
sigset_t s; sigemptyset(&s); sigaddset(&s, SIGSEGV);
sigprocmask(SIG_BLOCK, &s, NULL);
*(volatile int *)0 = 1;   /* expected: killed by SIGSEGV; actual: spins */
```

**Environment.** See the section header.

**Fix.** If a fault signal is blocked or ignored, unblock it and reset its disposition to `SIG_DFL` before posting
it, as Linux's `force_sig` does.

**Evidence.** The four cases fail (time out) on the old kernel and pass with the fix (Pi).

#### Short form

<a id="k11"></a>

##### K11 — posix/usocket: the one-page default ring rejects framed messages of 4089 bytes or more

- **What:** a SEQPACKET/DGRAM message must fit the receive ring plus an 8-byte frame header; the default ring is one
  page, so `send()` of a 4089-byte SEQPACKET message (4081 B for DGRAM) fails with `EMSGSIZE` (on 512-byte-page
  NOMMU targets the limit is ~500 B). Linux accepts much larger datagrams by default.
- **Severity / scope:** wrong output; general.
- **Our fix:** kernel `c0431a84` (64 KiB default for framed sockets; streams keep one page); test `message_4096`
  (tests `f4fb172`). Note for a PR: every framed socket pair then allocates two 64 KiB rings.
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21: `posix/usocket.c:56`
  `#define USOCKET_DEF_BUFFER_SIZE SIZE_PAGE`, `:157`; 64 KiB is only the `SO_RCVBUF` maximum (`:58`). Tracker:
  searched 'EMSGSIZE', 'SEQPACKET', 'DGRAM', 'rcvbuf': none found.
- **Short form because:** a capacity limit with a workaround (`setsockopt(SO_RCVBUF)`); a sizing choice for upstream.

<a id="k17"></a>

##### K17 — proc/threads: `SA_RESETHAND` is ignored

- **What:** a handler installed with `SA_RESETHAND` is not reset to `SIG_DFL` on delivery, so a fault inside a crash
  handler re-enters the same handler until the stack runs out (redis's crash reporter nested ~190 times).
- **Severity / scope:** wrong output; general.
- **Our fix:** kernel `4b1975d8`; tests `sigaction resethand_*` (tests `a43e7fa`, which also carries an unrelated
  pthread-fork test: split it).
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21: `include/signal.h:80`
  `#define SA_RESETHAND (1U << 4) /* FIXME: implement */`, `proc/threads.c:1952` "TODO: Handle other sa_flags".
  Tracker: **duplicate of #844** (open, "sigaction(): SA_SIGINFO, SA_RESETHAND, SA_RESTART and SA_NOCLDWAIT flags
  are ignored").
- **Short form because:** already reported upstream; offer the PR and test against #844.

<a id="k25"></a>

##### K25 — posix: `p->lock` is held across a blocking close in the exit and exec descriptor sweeps

- **What:** process exit and exec walk the descriptor table with `p->lock` held and call `posix_fileDeref()`, whose
  last-reference path sends a blocking `proc_close()`. A server that does not answer stalls every other thread (and a
  child's `posix_died()`) that needs the lock.
- **Severity / scope:** hang; general.
- **Our fix:** kernel `2c489b75` (`posix_sweepFds()`: clear the slot under the lock, dereference outside it).
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21: `posix/posix.c:461-468`
  (exec) and `:481-498` (exit); the blocking close is at `:137`. Tracker: searched 'close block', 'exit lock': none
  found.
- **Short form because:** found by review; no reproducer.

<a id="k27"></a>

##### K27 — vm/object: a backing-store read error is reported as `-ENOMEM`

- **What:** `object_fetch()` returns a page pointer with no error channel; `vm_objectPage()` returns `EOK` with a NULL
  page, and `vm/map.c:777-778` turns that into `-ENOMEM`. A transient I/O error at `exec` or on a file-backed fault
  is reported as out of memory.
- **Severity / scope:** wrong output (misleading error); MMU.
- **Our fix:** kernel `0620f2e9` (propagates the real error; it sits on our fork-only read-ahead code, so it must be
  re-derived).
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21: `vm/object.c:248`. Tracker:
  searched 'ENOMEM', 'out of memory': none found.
- **Short form because:** minor; the upstream fix needs a signature change.

<a id="k32"></a>

##### K32 — proc/process: `waitpid()` returns before the child's memory is freed

- **What:** `process_destroy()` calls `posix_died()` (which wakes the parent) before `vm_mapDestroy()`; a parent that
  reaps a large child and allocates at once can fail although the child is gone.
- **Severity / scope:** wrong output (spurious allocation failure); general.
- **Our fix:** kernel `286f8b8d` (free the memory first, as Linux does).
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21: `proc/process.c:95`
  (`posix_died()`) before `:104` (`vm_mapDestroy()`). Tracker: searched 'waitpid', 'wait()': no matching issue.
- **Short form because:** timing-dependent; our evidence is a timing-based test only.

<a id="k47"></a>

##### K47 — vm/object: a short read before end of file is not retried when a page is fetched

- **What:** `object_fetch()` issues one `proc_read()` per page and treats only a negative return as failure. A server
  may legally return fewer bytes than asked before end of file (our NFS client does), and the rest of the page is
  then wrong.
- **Severity / scope:** data corruption (demand-paged text or data); MMU.
- **Our fix:** kernel `f145658f` (loop until the page is full or the read returns 0, then zero the tail).
- **Upstream status:** open; checked against phoenix-rtos-kernel `4c7564cd` on 2026-11-21: `vm/object.c:192`. The
  end-of-file half (zero the tail after a short read) is in upstream's open PR
  [phoenix-rtos-kernel#872](https://github.com/phoenix-rtos/phoenix-rtos-kernel/pull/872) (2026-11-17), which does
  not retry a short read. Tracker: searched 'short read', 'object_fetch', 'mmap file': none found.
- **Short form because:** only servers that return short reads before EOF trigger it; comment on PR #872 rather than
  open a separate issue.

### 1.2 libphoenix

<a id="l01"></a>

#### L01 — string: `strncmp()` and `strncasecmp()` read one byte past `n`

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "strncmp()/strncasecmp() read s1[n] before checking the bound"
- **Severity:** crash (intermittent `SIGSEGV` when `s1[n-1]` is the last byte of a mapped page)
- **Scope:** general
- **Our fix:** libphoenix `f2d2789`; tests `a82e0e4`, `7c80324` (a string ending flush against an unmapped page);
  host harness `tools/libstring-hosttest` (66 cases equal to glibc)
- **Patch:** clean
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `string/string.c:113`
  (`strncmp`) and `:87` (`strncasecmp`): `for (p = s1, k = 0; *p && k < n; p++, k++)` reads `*p` before testing
  `k < n`.
- **Tracker:** searched 'strncmp', 'strncasecmp', 'over-read': none found.

**Description.** The loop condition dereferences `p` first, so when the first `n` bytes compare equal it reads
`s1[n]`. C 7.24.4.4 lets `strncmp()` read at most `n` characters of each array; a caller may pass a buffer that is
exactly `n` bytes long and not terminated.

**Expected behavior.** `strncmp(p, "a", 1)` returns 0 for `p` pointing at `'a'` in the last byte of a mapping.

**Actual behavior.** Data abort at the next page (`far` = page base).

**Reproduction.**
```c
char *m = mmap(NULL, 2 * 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
munmap(m + 4096, 4096);
m[4095] = 'a';
printf("%d\n", strncmp(m + 4095, "a", 1));   /* expected 0, actual SIGSEGV */
```

**Environment.** See the section header.

**Fix.** Test `k < n` first (as `strncpy()`/`strncat()` in the same file already do).

**Evidence.** Seen as an intermittent `Data Abort (EL0)` in SuperTuxKart's script tokenizer (`n = 1`, `s1` at the end
of a page). Both tests fail before and pass after on the Pi; the host harness goes from 2 faults to 0.

<a id="l05"></a>

#### L05 — pthread: a default thread has a one-page stack and no guard page

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "pthread: default stack is PTHREAD_STACK_MIN (one page) with no guard page"
- **Severity:** crash / memory corruption (ordinary library code overflows the stack silently into a neighbour)
- **Scope:** general (MMU targets for the guard page)
- **Our fix:** libphoenix `02ab4e0` (one guard page by default; `mprotect()` best-effort so NOMMU targets keep
  working), `bad2009` (`PTHREAD_STACK_DEFAULT`, opt-in per architecture; aarch64 256 KiB); tests `adcceba`
  (`default_attr_has_guard_page`), `291708a`
- **Patch:** `02ab4e0` clean; `bad2009` trivial conflict (a renamed constant on the next line)
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `pthread/pthread.c:119`
  `.stacksize = ALIGN(PTHREAD_STACK_MIN, PAGE_SIZE),` and `:120` `.guardsize = 0`;
  `include/arch/aarch64/limits.h:57` `#define PTHREAD_STACK_MIN 256`.
- **Tracker:** searched 'guard', 'stack': no matching issue.

**Description.** A thread created with a NULL attribute (every `std::thread`, most libraries) gets a 4 KiB stack and
no guard page. `PTHREAD_STACK_MIN` is the smallest stack an implementation accepts, not a usable default, and POSIX
leaves the default guard size to the implementation.

**Expected behavior.** A default thread can run ordinary code (a few KiB of locals); an overflow faults at once.

**Actual behavior.** An overflow writes silently into the memory below the stack (another thread's stack or the
heap) and shows up later as a bad return address or a fault inside `malloc()`.

**Reproduction.** `pthread_attr_init(&a); pthread_attr_getstacksize(&a, &s); pthread_attr_getguardsize(&a, &g);`
prints `s = 4096, g = 0`. A thread that puts an 8 KiB array on its stack corrupts memory instead of faulting.

**Environment.** See the section header.

**Fix.** Default guard size of one page; a per-architecture default stack size (`PTHREAD_STACK_DEFAULT`, falling back
to the current value so other architectures do not change until they opt in).

**Evidence.** SuperTuxKart's audio decoder wrote a 44 100-byte buffer on a default stack (proven byte-exact), and
libjpeg's Huffman setup overflowed one; with the guard page the second hit the guard instead of corrupting memory.
Review note on our patch: making the `mprotect()` best-effort also hides a failure for an explicitly requested
guard on MMU targets; a PR should keep the error there.

<a id="l10"></a>

#### L10 — unistd, sys/stat: a signal makes `access()` report `ENOENT` and `stat()` report `EINTR`

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix`, `POSIX` · title "access()/stat()/path resolution fail when a signal interrupts a lookup"
- **Severity:** wrong output (an existing file is reported missing)
- **Scope:** general
- **Our fix:** libphoenix `b33e8ec`; test `libc/sa_restart` (tests `ae146ef`); host model of the code (6 of 11 failing
  positions → 0)
- **Patch:** conflicts (`unistd/dir.c` reworked upstream in `d0bf010`); re-derive
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. The kernel aborts a queued message
  with `-EINTR` when a signal arrives (phoenix-rtos-kernel `proc/msg.c:406-409`). Only `safe_lookup()`
  (`unistd/dir.c:44`) retries; `access()` turns the error into `ENOENT` (`unistd/file.c:484`), `stat()` returns it
  (`sys/stat.c:40`, `:59`), and so does path resolution (`unistd/dir.c:734`, `:761`).
- **Tracker:** searched 'access(', 'EINTR', 'ENOENT': no matching issue.

**Description / POSIX requirement.** POSIX gives `access()` and `stat()` no `EINTR` error, and callers that install
handlers with `SA_RESTART` do not expect one. When a signal arrives while a lookup message is still queued at a busy
filesystem server, the kernel takes the message back and returns `-EINTR`; libphoenix passes that on (or maps it to
`ENOENT`).

**Expected behavior.** The call is retried; an existing file is found.

**Actual behavior.** `access()` returns `ENOENT`, `stat()` returns `EINTR`. WindowMaker reported "cannot find" an
existing image in 3 of 8 desktop starts: a `SIGCHLD` from its own child landed during the search.

**Reproduction.** The `sa_restart` test installs a `SIGCHLD` handler with `SA_RESTART`, forks 300 children that exit
at once, and after each fork queries `/etc/passwd` with `access()`, `stat()` and `realpath()`, while three threads do
the same to keep the filesystem server's queue non-empty. Every query must succeed. The failure needs a signal to land
while a message is still queued, so it is probabilistic (likeliest with a slow server such as an NFS root).

**Environment.** See the section header.

**Fix.** Resend a lookup the kernel aborted with `-EINTR`: such a message was removed from the queue unseen, and one a
server has taken is waited for uninterruptibly, so a resend is never a duplicate. Reads, writes and waits keep their
`EINTR`.

**Evidence.** Host model of the real code, with a signal on each of the 11 IPCs one `access()` makes: 6 of 11
positions failed before, none after; `sa_restart` 1/0 on the Pi.

<a id="l14"></a>

#### L14 — stdio: `setvbuf(f, NULL, _IOLBF, 0)` fails, and a mode change clears no flag

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix`, `POSIX` · title "setvbuf(): NULL buffer rejected; F_LINE/F_USRBUF never cleared"
- **Severity:** wrong output
- **Scope:** general
- **Our fix:** libphoenix `fa8521d`; test `setvbuf` (tests `6e7a7f6`)
- **Patch:** clean
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `stdio/file.c:1343`
  `stream->flags &= ~(F_USRBUF & F_LINE);` (the mask is `~0`, so nothing is cleared); `:1350-1357` `buffAlloc(0)`
  returns NULL and the call returns -1.
- **Tracker:** searched 'setvbuf', 'setlinebuf', 'buffer': none found.

**Description / POSIX requirement.** POSIX allows `buf == NULL` with any mode; the implementation then allocates the
buffer. `setvbuf(f, NULL, _IOLBF, 0)` is what glibc's `setlinebuf()` does and what bash calls. Separately, the flag
reset uses `&` where `|` was meant.

**Expected behavior.** `setvbuf(stdout, NULL, _IOLBF, 0)` returns 0 and the stream is line-buffered; switching a
stream to `_IOFBF` clears its line-buffered flag.

**Actual behavior.** -1; a stream switched from line to full buffering keeps `F_LINE`, and a user buffer keeps
`F_USRBUF`.

**Reproduction.** `printf("%d\n", setvbuf(stdout, NULL, _IOLBF, 0));` prints `-1`.

**Environment.** See the section header.

**Fix.** Allocate `BUFSIZ` when `buf` is NULL and `size` is 0; use `~(F_USRBUF | F_LINE)`.

**Evidence.** The test fails before and passes after; the libc suite (694 tests) passes on the Pi.

<a id="l17"></a>

#### L17 — stdio: `fflush()` after a short write sends the already-written part again

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "fflush(): after EAGAIN the written prefix is sent again"
- **Severity:** wrong output (duplicated output on terminals and pipes)
- **Scope:** general
- **Our fix:** libphoenix `9029813`; tests `9eb0343`, `f54cdec`
- **Patch:** clean
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `stdio/file.c:364-368`: when
  `full_write()` returns less than `bufpos`, the stream gets `F_ERROR` but `bufpos` is not reduced; `full_write()`
  returns a short count on `EAGAIN` on purpose (`:341`). `write_buffer()` (`:579-588`) already handles the same case
  correctly.
- **Tracker:** searched 'fflush', 'duplicat': no matching issue.

**Description.** When a flush meets back-pressure (a non-blocking tty or pipe returns `EAGAIN` part way), the bytes
already written stay in the buffer and the next flush writes them again from the start; the unwritten remainder is
never resumed on its own, and `F_ERROR` sticks.

**Expected behavior.** The next flush continues after the bytes already written.

**Actual behavior.** The same bytes appear twice (or many times).

**Reproduction.** Make a pipe's write end non-blocking, `fdopen()` it with a full buffer, fill the pipe so a flush
writes only part of the buffer, drain the pipe, `fflush()` again: the reader sees the first part twice.

**Environment.** See the section header.

**Fix.** On a short write, drop the written prefix from the buffer (`memmove()`, `bufpos -= written`), as
`write_buffer()` does.

**Evidence.** A game printing at frame rate produced 234 113 identical copies of one 31-byte buffer tail on the UART;
both tests fail before and pass after. The fix explains a repeated *prefix*; the observed repetition of a *tail* may
have a second cause, as the commit message says.

<a id="l19"></a>

#### L19 — ctype.h: the ctype macros evaluate their argument more than once

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "ctype.h: isdigit(*p++) advances p twice"
- **Severity:** wrong output
- **Scope:** general
- **Our fix:** libphoenix `156422a` (each macro calls a `static inline` function; same values for every input
  −128…255); test `ctype single_evaluation` (tests `0bdaa1a`)
- **Patch:** conflicts (upstream reworked `__isspace`); small re-derive
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `include/ctype.h:50-51`
  `#define __isdigit(c) (((c) >= '0' && (c) <= '9') ? 1 : 0)`; the same pattern for the other classes and for
  `tolower`/`toupper` (`:27-112`, e.g. `:97`, `:103`).
- **Tracker:** searched 'ctype', 'isdigit', 'macro': no matching issue (#782, closed, asked for the inlining that
  introduced the macros).

**Description / standard requirement.** C17 7.1.4 requires a library function implemented as a macro to evaluate
each argument exactly once. The fast macros expand the argument two or three times.

**Expected behavior.** `isdigit(*p++)` advances `p` by one.

**Actual behavior.** It advances `p` by two. xorg-server's config scanner (`isdigit(c = buf[pos++])`) read
`DefaultDepth 24` as `2`.

**Reproduction.** `const char *p = "12"; isdigit(*p++); printf("%td\n", p - "12");` prints `2` (expected `1`).

**Environment.** See the section header.

**Fix.** Implement each macro through a `static inline` helper.

**Evidence.** The test fails before and passes after; host check of every input value.

<a id="l20"></a>

#### L20 — stdio: `fclose(stdout)` frees a standard stream that is still referenced

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "fclose(stdout/stderr) frees the FILE object; a later flush uses freed memory"
- **Severity:** crash (use after free at exit)
- **Scope:** general
- **Our fix:** libphoenix `bf35aaf` (keep the three standard objects; on close empty them: no buffer, fd −1, I/O fails
  with `EBADF`, off the list, as glibc and musl do); test `stdlib_exit closed_std_streams` (tests `c4b23a8`);
  `tools/libstdio-hosttest` (ASan)
- **Patch:** conflicts (it builds on our double-`fclose()` change, `5fa3847`)
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `stdio/file.c:184` calls
  `file_free()` (`:154-166`, `free(file)`) for every stream, the standard ones (`:1544-1546`) included.
- **Tracker:** searched 'fclose', 'stdout', 'stderr': no matching issue (#133, closed, is about `fclose(stdin)`
  failing).

**Description.** Pointers to `stdin`/`stdout`/`stderr` outlive `fclose()`: libstdc++'s `std::cout`/`std::cerr` cache
them and flush them from an exit-time destructor. A failed `freopen()` releases the stream the same way.

**Expected behavior.** Using a closed standard stream fails with `EBADF`; nothing is freed.

**Actual behavior.** The exit-time flush reads freed memory; SuperTuxKart (which ends `main()` with
`fclose(stderr); fclose(stdout);`) faulted in `file_rawSeek` ← `fflush` ← `ios_base::Init::~Init`.

**Reproduction.** A C++ program that writes to `std::cout`, calls `fclose(stdout)` and returns from `main()`: the
exit-time flush uses the freed `FILE`. It faults only when the freed block has been reused meanwhile, so the test
(`closed_std_streams`) checks the defined behaviour (`EBADF`) instead, and the host harness runs under ASan.

**Environment.** See the section header.

**Evidence.** The test fails before and passes after; SuperTuxKart exits cleanly on the Pi.

<a id="l22"></a>

#### L22 — stdio/format: `%'d` is printed literally and shifts every later argument

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "printf: the ' flag leaves its argument on the va_list"
- **Severity:** wrong output
- **Scope:** general
- **Our fix:** libphoenix `a41d8d5` (accept the flag; with only the C locale, whose `thousands_sep` is empty, it has
  no visible effect, as in glibc under `LC_NUMERIC=C`); test `flag_grouping` (tests `fba1f8e`);
  `tools/libfmt-hosttest` (169 differences → 0)
- **Patch:** split: the `stdio/format.c` hunk merges cleanly; the commit also touches our fork-only `swprintf.c`
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. The flag loop
  (`stdio/format.c:977-998`) has no `'` case, and the unknown-conversion path (`:1222-1225`, `feed('%'); feed(fmt);`)
  prints the directive and consumes no argument.
- **Tracker:** **#707** (closed 2026-11-12: "printf() with ' flag prints format instead of number", closed as
  documented behaviour, since locales are not planned). This report is narrower: even unsupported, the directive
  must not desynchronise the arguments.

**Description.** Supporting grouping is a locale feature upstream declined. The defect reported here is that the
conversion's argument stays on the `va_list`, so every later conversion in the same call prints the wrong argument.

**Expected behavior.** `printf("%'lu files, %s\n", 73UL, "x")` prints `73 files, x` (C locale).

**Actual behavior.** `%'lu files, ` followed by `73` interpreted as a string pointer (a fault or garbage). Thunar's
status bar showed `73 files: 15.4 GiB (%'lu bytes)`.

**Reproduction.** The line above.

**Environment.** See the section header.

**Fix.** Accept `'` in the flag loop and ignore it (C locale), or at least consume the argument of a directive it
cannot print.

**Evidence.** `flag_grouping` fails before and passes after; `test-libc-printf` 136/0 on the Pi.

<a id="l23"></a>

#### L23 — stdio/scanf: `%i`/`%x` reject a sign before `0x`; a bare `0x` converts as 0

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "sscanf(\"-0x10\", \"%li\") stores 0"
- **Severity:** wrong output
- **Scope:** general
- **Our fix:** libphoenix `5020478`; test `stdlib_inttypes` group (tests `1c15941`); `tools/libscanf-hosttest`
  (20 of 1934 cases differed from glibc → 0)
- **Patch:** conflicts (upstream rewrote scanf in `fb200b5`); re-derive
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `stdio/scanf.c:1180`
  `if (((flags & PFXOK) != 0) && (len == 1))`: `len` counts the sign, so the `x` is accepted only without one;
  `:1202-1209` accept a bare `0x`.
- **Tracker:** searched 'scanf', '0x', 'hex': no matching issue (many closed scanf issues, none about this).

**Description / standard requirement.** C11 7.21.6.2 and `strtol()` accept an optional sign before the `0x` prefix;
a `0x` with no hex digit after it is a matching failure.

**Expected behavior.** `sscanf("-0x10", "%li", &v)` stores −16 and returns 1; `sscanf("0x", "%x", &u)` returns 0.

**Actual behavior.** The first stores 0 (it stops at the `x`); the second returns 1 with `u == 0`.

**Reproduction.** The two calls above.

**Environment.** See the section header.

**Evidence.** Unity group on the Pi; host harness against glibc.

<a id="l27"></a>

#### L27 — posix/stubs: `fchdir()` returns success without changing the directory

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix`, `POSIX` · title "fchdir() is a stub that returns 0"
- **Severity:** data corruption (callers act in the wrong directory: gnulib's `unlinkat()` emulation unlinked in the
  wrong place)
- **Scope:** general
- **Our fix:** libphoenix `1d86e24` (fail with `ENOSYS`); the real implementation (`2e46a0a`) needs a kernel call our
  fork added, so it is a design discussion; tests `d4edee9`, `e1fa434`
- **Patch:** `1d86e24` clean
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `posix/stubs.c:100-102`
  `int fchdir(int fildes) { return 0; }`.
- **Tracker:** searched 'fchdir': none found.

**Description.** Code that saves the current directory with `open(".")` and returns with `fchdir()` (gnulib
`save_cwd`, fts, `rm -r`) believes the call worked and continues in the wrong directory.

**Expected behavior.** `fchdir(fd)` changes the directory, or fails.

**Actual behavior.** Returns 0; `getcwd()` is unchanged.

**Reproduction.** `int fd = open("/tmp", O_RDONLY); chdir("/"); fchdir(fd); getcwd(buf, sizeof(buf));` gives `/`.

**Environment.** See the section header.

**Fix.** Fail with `ENOSYS` until the kernel can map a descriptor to its path; then implement it.

**Evidence.** gnulib's `unlinkat()` emulation deleted in the wrong directory during an `rm -r`; the tests fail before
and pass after.

<a id="l29"></a>

#### L29 — unistd, kernel, filesystems: `unlink()` removes an empty directory

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix`, `filesystems`, `POSIX` · title "unlink() of a directory succeeds"
- **Severity:** data corruption (directory removed past `rmdir()`'s checks)
- **Scope:** general
- **Our fix:** libphoenix `ae9801e` (client-side `lstat()` check: a workaround; the right fix is in the servers or the
  kernel); test `7c678ee`
- **Patch:** n/a (report only; the workaround applies cleanly but is not the fix to send)
- **Upstream status:** open; checked against libphoenix `fc60045`, phoenix-rtos-kernel `4c7564cd` and
  phoenix-rtos-filesystems `52ae003` on 2026-11-21. `unlink()` (`unistd/file.c:328`) and `rmdir()`
  (`unistd/dir.c:836`) both reach the same `mtUnlink` (kernel `posix/posix.c:1307`); dummyfs checks only that a
  directory is empty (`dummyfs/dummyfs.c:664`), so `unlink("dir")` succeeds and skips `rmdir()`'s `EBUSY` check
  (`unistd/dir.c:822`).
- **Tracker:** searched 'unlink', 'rmdir', 'directory': no matching issue.

**POSIX requirement.** `unlink()` fails with `EPERM` when the path names a directory and the implementation does not
support unlinking directories (or the caller lacks the privilege to).

**Expected behavior.** `mkdir("d", 0755); unlink("d");` returns -1.

**Actual behavior.** Returns 0 and the directory is gone; `remove()` inherits this.

**Reproduction.** The two calls above.

**Environment.** See the section header.

**Fix (suggested).** Pass the caller's intent (file or directory) in `mtUnlink`, or give `rmdir()` its own message, so
the filesystem servers can refuse.

**Evidence.** The test fails before and passes with the workaround.

<a id="l34"></a>

#### L34 — pthread: `pthread_self()` is NULL in a forked child

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "After fork(), pthread_self() returns NULL in the child"
- **Severity:** crash (any pthread call that dereferences the current thread, e.g. `pthread_setcancelstate()`)
- **Scope:** general
- **Our fix:** libphoenix `f36de2b` (re-key the forking thread's record to the child's thread id, drop the records of
  threads that do not exist in the child), `d67a879` (`pthread_setcancelstate()` returns `ESRCH` for an unknown
  thread); test `test-libc-pthread-fork` (tests `a43e7fa`, 3 cases)
- **Patch:** `d67a879` clean; `f36de2b` merges but needs our fork's retired-thread list: re-derive
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `pthread/pthread.c:1828-1843`
  (`_pthread_atfork_child`) only runs the handlers; `:555` looks the thread up by `gettid()`, which is new in the
  child; `unistd/sys.c:433-435` then calls `_pthread_ctx_get()` with NULL.
- **Tracker:** searched 'pthread_self', 'fork': no matching issue.

**Description.** The child inherits the parent's thread list, keyed by kernel thread id; the forking thread has a new
id in the child, so it finds no record.

**Expected behavior.** `pthread_self()` in the child returns a valid handle; pthread calls work.

**Actual behavior.** NULL; redis's `--daemonize` crashed in `pthread_setcancelstate()`.

**Reproduction.** `if (fork() == 0) { printf("%p\n", (void *)pthread_self()); pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); _exit(0); }`

**Environment.** See the section header.

**Evidence.** The three cases fail before and pass after on the Pi.

<a id="l49"></a>

#### L49 — libm (default implementation): `pow(-2, 3)` returns `+8`; `pow()` and `fmod()` are inaccurate

- **Issue fields:** Category: BUG · labels `bug`, `libphoenix` · title "libm: pow() drops the sign for a negative base and an integer exponent"
- **Severity:** wrong output
- **Scope:** general (the default `libm/phoenix`, `LIBM_USE_LIBMCS ?= n` at `libm/Makefile:12`)
- **Our fix:** libphoenix `6bb0bdc`, `6bf27ab` replace the transcendental functions with FreeBSD msun (≤ 2.75 ULP
  against MPFR; an enhancement to discuss); test `math_accuracy` (tests `c5a1ab5`); `tools/libm-hosttest`. The sign
  bug alone is a one-line fix worth sending on its own.
- **Patch:** conflicts (upstream reorganised libm into `mathd/`/`mathf/` in `f879e15`)
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21. `libm/phoenix/mathd/powd.c:70`
  computes the sign `s`, but `:74-75` (`return quickPow(base, (int)exponent);`) returns without it; other exponents
  go through `exp(y * log(x))` (`:78-79`) built on series `exp`/`log` (`expd.c`, `logd.c`). `fmod()`
  (`mathd/fmodd.c:34-37`) computes `x - trunc(x / y) * y`, wrong once the quotient reaches 2^53.
- **Tracker:** `fmod`: **#1229** (open, "fmod error", large arguments); `pow`: searched 'pow', 'libm', 'math': none
  found.

**Description.** For a negative base and an integer exponent, `pow()` takes the absolute value of the base and then
returns the integer-power result without the sign it computed. For other arguments the `exp(y·log x)` route loses
precision.

**Expected behavior.** `pow(-2, 3) == -8`, `pow(-2, -3) == -0.125`, `pow(7, 0.5) == sqrt(7)` to the last bit,
`fmod(1e17, 3) == 1`.

**Actual behavior.** `+8`, `+0.125`; `pow(7, 0.5)` differs from the 9th significant digit; `fmod(1e17, 3) == 0`,
`fmod(1, DBL_TRUE_MIN) == -inf` (host run of upstream's own files).

**Reproduction.** `printf("%g %g %.17g %g\n", pow(-2, 3), pow(-2, -3), pow(7, 0.5), fmod(1e17, 3));`

**Environment.** See the section header; also upstream's `mathd/` sources compiled on Linux x86_64.

**Fix.** Apply `s` on the `quickPow()` path; for accuracy, an established implementation (we used FreeBSD msun,
BSD/Sun licences). Upstream's optional libmcs is not affected.

**Evidence.** JavaScript's SJCL derives SHA-256's round constants with `Math.pow`; JetStream's crypto tests computed
wrong hashes (31 of 72 constants wrong; 3.6 M ULP error). The host harness compares every function with MPFR.

#### Short form

<a id="l51"></a>

#### L51 — sys/socket: `recvmsg()` into more than one iovec does not report `msg_controllen`, `msg_flags` or `msg_namelen`

- **Issue fields:** Category: BUG · labels `bug`, `POSIX`, `libphoenix` · title "recvmsg() with msg_iovlen > 1 leaves msg_controllen/msg_flags/msg_namelen as passed in"
- **Severity:** data corruption (a receiver decodes the previous read's `SCM_RIGHTS` again and uses a descriptor it
  already has as a new one)
- **Scope:** general
- **Our fix:** libphoenix `84d3656`; tests `d06d930` (`unix_socket.fd_recvmsg_iovecs_controllen`)
- **Patch:** clean (the test conflicts with neighbouring fork tests in `unix-socket.c` and uses the helpers of
  `fd_order_stream`, tests `f575a9a`: re-derive)
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-22. `sys/socket.c`, `recvmsg()`: with
  `msg_iovlen > 1` it receives through a local `struct msghdr _msg` into a bounce buffer and copies back only the
  data (`copy_to_iov`); `_msg.msg_controllen`, `_msg.msg_flags` and `_msg.msg_namelen` never reach the caller.
- **Tracker:** searched 'recvmsg', 'msg_controllen', 'SCM_RIGHTS': none found.

**Description.** POSIX `recvmsg()` sets `msg_controllen` to the length of the control data received (0 when there
is none), `msg_flags` to the flags of the message (`MSG_CTRUNC`, `MSG_TRUNC`) and `msg_namelen` to the address
length. With one iovec libphoenix passes the caller's `msghdr` to the kernel and all three are right. With two or
more it copies the caller's values into a local `msghdr`, lets the kernel update that one, and leaves the caller's
fields as they were passed in.

**Expected behavior.** After a read that carries no descriptors, `msg_controllen` is 0 and `CMSG_FIRSTHDR()` is NULL,
whatever the number of iovecs.

**Actual behavior.** `msg_controllen` keeps the buffer size the caller passed in. A caller that reuses its control
buffer (libwayland's `wl_connection_read()` keeps it on the stack across its loop and reads into two iovecs whenever
its ring buffer wraps) decodes the previous read's `SCM_RIGHTS` a second time and queues that descriptor again; the
next request that carries a descriptor is given the wrong one.

**POSIX requirement.** `recvmsg()`: "Upon successful completion, the msg_controllen member of the msghdr structure
shall be set to the length of the control data received" and "the msg_flags member ... shall be set" on return.

**Reproduction.**
```c
int sv[2], fd = open("/tmp/f", O_CREAT | O_RDWR, 0600);
union { struct cmsghdr h; char b[CMSG_SPACE(sizeof(int))]; } c;
char a[1], b[4];
struct iovec iov[2] = { { a, 1 }, { b, 4 } };
struct msghdr m = { .msg_iov = iov, .msg_iovlen = 2, .msg_control = c.b, .msg_controllen = sizeof(c.b) };
socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
/* send "A" with fd in SCM_RIGHTS, then "B" with no control data */
recvmsg(sv[1], &m, 0);                    /* "A" + one descriptor: fine */
m.msg_controllen = sizeof(c.b);
recvmsg(sv[1], &m, 0);                    /* "B" */
printf("%u\n", (unsigned)m.msg_controllen);  /* expected 0, actual CMSG_SPACE(sizeof(int)); the old SCM_RIGHTS decodes again */
```

**Environment.** See the section header.

**Fix.** Copy `msg_namelen`, `msg_controllen` and `msg_flags` from the local `msghdr` back to the caller's after the
call, in both bounce-buffer branches.

**Evidence.** A Wayland compositor (labwc/wlroots) intermittently imported a client's `wl_shm` pool descriptor as the
client's dma-buf and killed it with a protocol error: 2-3 of every 6 windowed video starts. A libwayland trace showed
the client sending the right descriptors; the compositor's request got the descriptor of an earlier request. On the
Pi the test fails with the old library (66 tests, 1 failure) and passes with the fix; 12 of 12 windowed starts then
played with no wrong import (2026-11-22).

<a id="l11"></a>

##### L11 — pthread: `pthread_setspecific()` calls `malloc()` under the key lock

- **What:** the list node is allocated (and freed in `pthread_key_delete()`/thread exit) with the non-recursive
  `pthread_key_lock` held; an allocator that uses thread-specific data itself (mimalloc) re-enters the lock and the
  thread sleeps on itself.
- **Severity / scope:** hang; general, but latent with upstream's own `malloc_dl`.
- **Our fix:** libphoenix `052b590`; test `pthread_tsd_alloc` (tests `23593b1`); `tools/pthread-key-hosttest`. Review
  note: dropping the lock around `malloc()` lets a concurrent `pthread_key_delete()` free the key; a PR must close
  that window.
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21: `pthread/pthread.c:1683` lock,
  `:1695` `malloc()`. Tracker: searched 'setspecific', 'key': no matching issue.
- **Short form because:** latent with the default allocator.

<a id="l30"></a>

##### L30 — sys/stat: `rename()` onto an existing file fails with `EEXIST`

- **What:** `rename()` is emulated as `link()` + `unlink()` and fails when the destination exists, breaking the
  write-to-temporary-then-rename idiom.
- **Severity / scope:** wrong output; general.
- **Our fix:** libphoenix `c01f7a7`. **Do not send it:** it can lose data (`rename(x, x)` unlinks `x`; a failure of
  the retried `link()` leaves the destination destroyed). A correct fix checks `st_dev`/`st_ino` first, or better, a
  real rename message to the server.
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21: `sys/stat.c:278-279`. Tracker:
  **duplicate of #409** (open since 2022); see also #1273 (rename of directories).
- **Short form because:** already reported; our fix is not suitable.

<a id="l38"></a>

##### L38 — termios.h: the `c_oflag` constants are invisible to the preprocessor

- **What:** `OPOST` and the other output flags exist only as enum members, so `#ifndef OPOST / #define OPOST 0`
  (xterm) compiles output processing out: newlines lose their CR.
- **Severity / scope:** build (silently wrong configuration); general.
- **Our fix:** libphoenix `e9bb8c4` (self-referential `#define OPOST OPOST`, as the header already does for the baud
  rates). `c_iflag`, `c_cflag` and `c_lflag` have the same shape upstream and in our fork.
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21: `include/termios.h:88-89`. Tracker:
  related **#1279** (open, `CSIZE` not a symbolic constant) and #1437; this one is about `c_oflag`.
- **Short form because:** minor; same class as #1279, add it there.

<a id="l39"></a>

##### L39 — string: `gai_strerror()` answers "Unknown error" for every code

- **What:** the build rule that generates the message table from `<netdb.h>` matches only non-negative numbers, and
  every `EAI_*` code is negative, so the table is empty.
- **Severity / scope:** wrong output; general.
- **Our fix:** libphoenix `40e3a55` (accept a sign; glibc's texts for all 11 codes); test (tests `de21235`, 11/0).
  Minimal upstream fix: add `-?` to the pattern.
- **Upstream status:** open; checked against libphoenix `fc60045` on 2026-11-21: `string/Makefile:11` (`[[:digit:]]+`),
  `include/netdb.h:51` `#define EAI_NONAME -2`. Tracker: searched 'gai_strerror', 'strerror': none found; upstream
  libphoenix PR #284 (open since 2023, "strerror, strerror_r, gai_strerror, perror: Add full implementation") touches
  the same function.
- **Short form because:** minor; coordinate with PR #284.

<a id="l45"></a>

##### L45 — pthread: a thread's stack can be unmapped while it still runs on it; a cancelled detached thread is never released

- **What:** (1) `_pthread_release()` keeps the previous thread's stack for later release and drops the list lock; the
  next releaser `munmap()`s it while that thread may still be running on it before `endthread()`. (2)
  `pthread_cancel()` of a detached thread never releases its record and stack.
- **Severity / scope:** crash (1), leak (2); general.
- **Our fix:** libphoenix `ad2af81`, `1a7753d` (on our fork's own retired-thread list; must be re-derived).
- **Upstream status:** open (each read once); checked against libphoenix `fc60045` on 2026-11-21: (1)
  `pthread/pthread.c:342` (stack kept in `to_cleanup`), `:154` (unlock), `:352` (`munmap(prev_stack, ...)`), `:595`
  (`endthread()`); (2) `:486-532` (`pthread_cancel()` never calls `_pthread_release()` for a detached target),
  `:572-575`. Tracker: searched 'pthread_join', 'detach', 'cancel': no matching issue (#1645 is different).
- **Short form because:** found while fixing our own version; not reproduced on upstream code.

### 1.3 Filesystems (phoenix-rtos-filesystems)

All ext2 entries were found with libext2 running unmodified on a Linux host against `mke2fs` images, with `e2fsck`
as the oracle (`tools/libext2-hosttest/`: a randomised stress with a shadow model of the file contents, plus focused
programs per defect; ~182 000 random operations over 115 runs at 1 KiB and 4 KiB blocks are clean with the fixes).
The harness would fit in `phoenix-rtos-tests`. Upstream's own sequential create/write/delete tests never produce a
hole, which is why most of these survived. **Order matters when sending:** several fixes depend on each other (see
"Patch" and the plan's section 6).

<a id="f01"></a>

#### F01 — ext2: the block and inode allocators have no lock

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: concurrent allocations corrupt the block/inode bitmaps"
- **Severity:** data corruption (blocks handed out twice; a data abort in the server)
- **Scope:** general (every server that runs libext2 handlers on more than one thread)
- **Our fix:** filesystems `463aec1` (one per-filesystem mutex taken at the `libext2_*` entry points, the outermost
  lock)
- **Patch:** conflicts in `ext2/libext2.c` (one hunk: upstream added `mode` to `libext2_write()`); small re-derive
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21. `ext2/block.c:173-180`
  reads the bitmap, toggles a bit, writes it and updates `fs->gdt[group].freeBlocks` with no lock; the only locks in
  `ext2/` are per object (`obj->lock`) and per object table (`objs->lock`). Two handler threads run at once in
  devices `storage/umass/umass.c:51` (`UMASS_N_POOL_THREADS 2`) and corelibs `libstorage/storage.c:217-222`
  (`zynq7000-sdcard` starts two).
- **Tracker:** searched 'ext2', 'bitmap', 'concurrent': no matching issue.

**Description.** Allocating or freeing a block or inode is a read-modify-write of an on-disk bitmap plus the in-memory
group descriptors and superblock counts, and nothing serialises it. Two writers in two handler threads interleave.

**Expected behavior.** Concurrent writes to different files leave a consistent filesystem.

**Actual behavior.** Blocks marked in use twice ("multiply-claimed blocks" in `e2fsck`), counts off, and on the Pi a
deterministic data abort in `ext2_block_destroyone()` under a concurrent-write stress.

**Reproduction.** Two threads (or two processes) writing and deleting different files on one ext2 volume served by
umass or libstorage; run `e2fsck -fn` on the image afterwards. On the host: `tools/libext2-hosttest/concurrent.c`
with its lock removed.

**Environment.** See the section header; ext2 on a USB stick (umass) and on the SD card.

**Fix.** A per-filesystem mutex taken at every `libext2_*` entry point (they are never re-entered, so it is trivially
outermost: `fs->lock` > `obj->lock`, `objs->lock` > storage).

**Evidence.** Pi stress: data abort before, clean after; host harness in real-mutex mode clean with the lock.

<a id="f02"></a>

#### F02 — ext2: byte offsets wrap at 4 GiB, and the first data block is wrong for blocks larger than 1 KiB

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: 32-bit offset overflow (> 4 GiB) and block/bitmap off-by-one at 2/4 KiB blocks"
- **Severity:** data corruption
- **Scope:** general
- **Our fix:** filesystems `7618a7d` (64-bit offsets), `505c4a3` (use `s_first_data_block`)
- **Patch:** `7618a7d` clean; `505c4a3` applies after it
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21. Offsets:
  `ext2/block.c:28` `fs->strg->start + bno * fs->blocksz` with both operands `uint32_t` (also `:33`, `:49`, `:54`).
  First data block hard-coded as 1: `ext2/block.c:195` `*res = group * fs->sb->groupBlocks + offset;`, `:260`, and
  `(bno - 1) / fs->sb->groupBlocks` at `:68`, `:82-83`, `:215-216`, `:522-523`; `sb.h:175` has `fstBlock` but it is
  not used there.
- **Tracker:** searched 'ext2', '4 GiB', 'large': no matching issue.

**Description.** (1) `bno * blocksz` is computed in 32 bits, so any block past 4 GiB from the partition start is read
from and written to the wrong place. (2) The first data block is 1 only at 1 KiB blocks; at 2 and 4 KiB it is 0, so
bitmap bit N hands out block N + 1 and frees block N − 1.

**Expected behavior.** ext2 works on volumes over 4 GiB and with 2 or 4 KiB blocks.

**Actual behavior.** (1) Data written past 4 GiB lands below it; (2) on a 4 KiB-block volume, allocations are off by
one block and `e2fsck` finds bitmap differences and blocks claimed by two inodes.

**Reproduction.** (2) Create a 64 MiB image with `mke2fs -b 4096`, write and delete files with libext2, run
`e2fsck -fn`. (1) Write a file that crosses 4 GiB on a larger partition and read it back.

**Environment.** See the section header; (1) a 27.5 GiB USB partition on the Pi; (2) host images and an SD card.

**Fix.** Widen to 64 bits before multiplying; use `s_first_data_block` everywhere a block number and a bitmap bit are
converted.

**Evidence.** (2) host harness (`blockmath.c` and the stress) at 4 KiB blocks, and a 1 GiB write/read-back on the Pi
checked with `e2fsck`; (1) Pi only: the host images (≤ 64 MiB) cannot reach the defect.

<a id="f04a"></a>

#### F04a — ext2: truncating a sparse file frees the wrong blocks

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: truncate across a hole frees blocks of other files (uint32 underflow)"
- **Severity:** data corruption (other files' blocks freed; a server crash)
- **Scope:** general
- **Our fix:** filesystems `680c9b2`; a target test in our phoenix-rtos-tests (to be split per defect before
  sending)
- **Patch:** conflicts when applied alone; it applies in our order, on top of our earlier ext2 commits (`fd4379a` and
  the indirect-block truncate fixes `282a46f`, `244f1cf`, `844198a`): send those first
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21. `ext2/file.c:207`
  `if (!lbno || (*bno == lbno + 1))` and `:211`, `:220` `ext2_block_destroy(fs, lbno + 1 - n, n)`. Also `:229`
  subtracts from `i_blocks` on a growing truncate.
- **Tracker:** searched 'truncate', 'sparse', 'hole': no matching issue (#498, closed 2022, "ext2: truncate is broken",
  predates this code).

**Description.** `_ext2_file_truncate()` gathers runs of consecutive physical blocks by remembering the last block
number. A hole gives block number 0, which resets `lbno` and makes `!lbno` extend the run across the hole. A run that
ends in a hole is then released at `lbno + 1 - n` = `1 - n`, a uint32 underflow near 2^32, which crashes the storage
server; a run that ends on a real block is released starting at the wrong block, freeing blocks that belong to other
files.

**Expected behavior.** Truncating a sparse file frees exactly its own blocks.

**Actual behavior.** `e2fsck` exit status 12 (uncorrected errors) and a SEGV of the storage server; other files lose
blocks.

**Reproduction.** Write 1 block at offset 0 and 1 block at offset 1 MiB of a new file (a hole between), then
`truncate(path, 0)`; run `e2fsck -fn`.

**Environment.** See the section header; host harness.

**Fix.** Track whether a run is open separately from the block number, and close the run at a hole.

**Evidence.** `tools/libext2-hosttest/run-stress.sh`: `e2fsck` rc 12 plus a SEGV on 5 of 5 seeds before, clean after.

<a id="f04d"></a>

#### F04d — ext2: `setattr(atSize)` truncates a directory

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: setattr(atSize) on a directory succeeds and frees its contents"
- **Severity:** data corruption (all entries of the directory lost)
- **Scope:** general
- **Our fix:** filesystems `d46ba88`; a host test in `tools/libext2-hosttest/`
- **Patch:** clean
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21. `ext2/ext2.c:455-456`
  (`case atSize:` → `_ext2_file_truncate()`) bypasses the `-EISDIR` check that only `ext2_truncate()` has
  (`:258-260`); there is no guard for device nodes or fast symlinks on this route either.
- **Tracker:** searched 'truncate', 'directory', 'setattr': no matching issue (#573, closed, is about the errno of
  `truncate()` on a directory).

**Description.** The size attribute is a second route to the truncate primitive that skips the type checks.

**Expected behavior.** `-EISDIR` (and no change) for a directory.

**Actual behavior.** Success; a directory of 40 entries had 0 left.

**Reproduction.** Send `mtSetAttr` with `atSize = 0` for a directory's oid (any client that can name the oid), or call
`libext2_setattr()` on the host; list the directory.

**Environment.** See the section header; host harness.

**Fix.** Apply the same checks in `ext2_setattr(atSize)` as in `ext2_truncate()`, and refuse device nodes and fast
symlinks (their block array holds data, not block numbers).

**Evidence.** The host test fails before and passes after.

<a id="f04w"></a>

#### F04w — ext2: stale indirect-block cache, split runs, in-memory link counts, and the root freed at unmount

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: four write-back and lifetime defects found by randomised stress"
- **Severity:** data corruption
- **Scope:** general
- **Our fix:** filesystems `c49d0e2` (stale `ind[]`), `74823c9` (run after a discontiguity), `9e1ff6a` + `ba9149a`
  (link counts; squash them), `d4ff499` (`fs->root` at unmount)
- **Patch:** `74823c9` and `9e1ff6a` clean alone; `c49d0e2`, `ba9149a`, `d4ff499` apply in our order (see F04a)
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21:
  1. `ext2_iblock_destroy()` (`ext2/block.c:589-631`) frees indirect blocks without clearing the cached `obj->ind[]`,
     which `block.c:347` and `obj.c:215` later write back over the block's new owner.
  2. `block.c:502-503` (`lbno = 0; i = j++;`) loses the first block of a run that follows a discontiguity, so two
     separate blocks are written as one contiguous run.
  3. `ext2.c:625-629` decrements the link counts of the object and the directory without marking either dirty, so
     the on-disk counts stay one too high.
  4. `obj.c:297-299` frees the objects in tree order; the root (inode 2) goes first, and `ext2_inode_destroy()` then
     reads `fs->root->id` (`inode.c:96`) from freed memory.
- **Tracker:** searched 'ext2', 'link count', 'unmount': no matching issue.

**Description / actual behavior.** (1) A file's data lands in a block that another file now owns. (2) A write with a
hole in the middle stores one block at the wrong place. (3) `e2fsck` reports wrong link counts for every directory
that held a subdirectory. (4) Unmount reads freed memory.

**Expected behavior.** `e2fsck -fn` clean after any sequence of operations; no use of freed memory.

**Reproduction.** `run-stress.sh` (random create/write/truncate/unlink with a shadow model) for (1) and (2);
`dirstress.c` and `linkstress.c` for (3); `uaf.c` under AddressSanitizer for (4).

**Environment.** See the section header; host harness.

**Evidence.** Stress mismatches 10 → 2 with (1), 2 → 0 with (2) (20 seeds × 800 operations, both block sizes); (3)
and (4) fail before and pass after.

<a id="f13"></a>

#### F13 — ext2: unmount deletes cached files, unlink frees no blocks, and a hole reads as block 0

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: unlinked files keep their blocks; unmount destroys inodes; sparse read returns block 0"
- **Severity:** data corruption
- **Scope:** general
- **Our fix:** filesystems `269306a`, `8a35d91`, `62d513a`
- **Patch:** each clean alone, but `8a35d91` routes every unlink through truncate, so it must go **after** F04a
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21:
  1. unmount: `ext2/obj.c:299` calls `_ext2_obj_destroy(fs, ..., true)`, which destroys the inode (`obj.c:55`) of
     every cached object, live files included;
  2. unlink: `obj.c:192-193` destroys an object whose link count reached 0 without truncating it, and the kernel's
     `posix_unlink()` (phoenix-rtos-kernel `posix/posix.c:1280-1331`) sends only `mtUnlink`, so the blocks stay
     allocated;
  3. sparse read: `block.c:649` `return ext2_block_read(fs, *bno, buff, 1);` with `*bno == 0` reads block 0.
- **Tracker:** searched 'ext2', 'unlink', 'unmount', 'hole': no issue names these. Possibly related: **#507** (open
  since 2022, "pc-ata: saving large files seems not to work properly": writing a 1 MB file and removing it, a few
  times over, hangs). That fits (2), deleted files keeping their blocks until the volume is full, but was not
  verified.

**Description / actual behavior.** (1) After unmount and mount, files that were open or cached are gone. (2) Deleted
files never give their space back: the volume fills up. (3) Reading a hole returns the contents of block 0 (the boot
block, or at 4 KiB blocks the block holding the superblock) instead of zeros.

**Expected behavior.** Unmount writes cached inodes back; unlink of the last link frees the blocks; holes read as
zeros.

**Reproduction.** (2) Write a 10 MiB file, unlink it, compare `statvfs()` free blocks before and after. (3) Write one
byte at offset 1 MiB of a new file and read offset 0.

**Environment.** See the section header; host harness.

**Evidence.** `e2fsck` host suites and `holes.c` fail before and pass after.

<a id="f14"></a>

#### F14 — ext2: reading a hole allocates indirect blocks

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: a read through a hole allocates indirect blocks"
- **Severity:** data corruption (blocks allocated by a read and not accounted; `ENOSPC` on a read)
- **Scope:** general
- **Our fix:** filesystems `43c00d7` (lookup-only `ext2_block_find()` for reads and scans); host test `holes.c`
- **Patch:** conflicts in `block.c`/`file.c`: it sits on the F04 series and on F10 (`40807bc`)
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21. Reads look blocks up
  with `ext2_block_get()` (`file.c:52`, `:66`, `:74`; `block.c:408`, `:646`), which always walks the indirect blocks
  with allocation on (`block.c:351-357`).
- **Tracker:** searched 'ext2', 'hole', 'sparse': no matching issue.

**Description.** Each missing indirect block on the path of a read is created. Reading an 8 MiB hole at 1 KiB blocks
took 31 blocks and raised `i_blocks` by as much, without marking the inode dirty, so what reached the disk depended on
the next write of the inode. On a full volume the read failed with `ENOSPC` instead of returning zeros. A truncate
that scans holes allocates the same way.

**Expected behavior.** A read of a hole returns zeros and changes nothing on disk.

**Reproduction.** Create a file with one byte at offset 64 MiB, read it from offset 0 to the end, compare the free
block count before and after (or fill the volume first and see `ENOSPC` from `read()`).

**Environment.** See the section header; host harness; Pi SD card (ext2 root), 2026-11-21.

**Evidence.** `holes.c` at both block sizes, `e2fsck` clean with the fix. Pi: after filling the root to 100 %, reads
and small writes fail cleanly, and `e2fsck -fn` after a power cut finds nothing (with F15 and F16).

<a id="f15"></a>

#### F15 — ext2: a failed indirect-block allocation is later written to block 0

- **Issue fields:** Category: BUG · labels `bug`, `filesystems` · title "ext2: ENOSPC while filling an indirect block corrupts block 0 (superblock at 4 KiB blocks)"
- **Severity:** data corruption (the filesystem no longer mounts)
- **Scope:** general
- **Our fix:** filesystems `28e26d7` (drop the cache slot when it cannot be filled); host test `enospc.c`
- **Patch:** clean
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21. In
  `ext2_block_readind()` the cache slot gets a buffer (`ext2/block.c:343`), and when `ext2_block_createone()` fails
  (`:352-353`) the function returns with the buffer still in the slot and the slot's block number still 0; the next
  sync of the object writes it to block 0 (`obj.c:215`). After a failed read, the half-read buffer overwrites the
  previous block the same way.
- **Tracker:** searched 'ext2', 'ENOSPC', 'superblock', 'full': no matching issue (#927, closed, "Ext2 unresponsive
  during filling whole file system", is a different symptom).

**Description.** One `write()` that runs out of space at the moment it needs a new indirect block, followed by any
sync of the file, overwrites block 0: the boot block at 1 KiB blocks, and at 4 KiB blocks the block that holds the
superblock.

**Expected behavior.** The write fails (or is short) with `ENOSPC`; nothing else changes.

**Actual behavior.** After a power cut, `e2fsck` cannot open the filesystem: "Bad magic number in super-block".

**Reproduction.** On a 4 KiB-block volume, fill the volume so that the last free block is used just before a file
needs its first indirect block (12 direct blocks = 48 KiB), write one more block to that file, `fsync()`, then read
block 0 of the device (or cut the power and run `e2fsck`).

**Environment.** See the section header; host harness; Pi SD card (ext2 root), 2026-11-21.

**Evidence.** `enospc.c` at both block sizes: "Bad magic number" after ENOSPC and a simulated power cut before, clean
after. Pi 2026-11-21: root filled to 100 % with 1 MiB writes, then 4 KiB and 1 000-byte writes fail at once with
`ENOSPC`, and after a power cut without unmount `e2fsck -fn` on the host finds nothing to fix.

<a id="f16"></a>

#### F16 — ext2: a `write()` that runs out of space part way fails and leaves the inode inconsistent

- **Issue fields:** Category: BUG · labels `bug`, `filesystems`, `POSIX` · title "ext2: partial write on ENOSPC returns -ENOSPC; i_size, i_blocks and bitmap inconsistent"
- **Severity:** data corruption
- **Scope:** general
- **Our fix:** filesystems `9e1ea4e`; host test `enospc.c` (5 write sizes × 2 block sizes × unmount or power cut)
- **Patch:** conflicts in `block.c`/`file.c`: needs F14 and F10 first
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21. `ext2/file.c:138-141`
  returns the error of `ext2_block_sync()` and drops the count of bytes written; the scan ahead in
  `block.c:470-476` allocates indirect blocks for the whole request before writing.
- **Tracker:** searched 'ext2', 'ENOSPC', 'partial write': no matching issue.

**POSIX requirement.** `write()` that transfers some bytes returns their number; it fails with `ENOSPC` only when no
byte could be written.

**Description / actual behavior.** The blocks written lie past `i_size`, which is not updated; `i_blocks` grows in
memory only; indirect blocks allocated ahead for the whole request stay allocated (pointed to or leaked). Filling a
32 MiB volume with 1 MiB writes left a wrong size, a wrong `i_blocks` and 39 blocks in use that nothing used.

**Expected behavior.** A short count with matching size and `i_blocks`; a call that wrote nothing leaves the inode,
free counts and bitmap as they were.

**Reproduction.** Fill a small volume with `write()` calls of 1 MiB until one returns less (or fails); unmount and run
`e2fsck -fn`.

**Environment.** See the section header; host harness; Pi SD card, 2026-11-21 (see F15).

**Evidence.** `enospc.c`: `e2fsck` errors before, clean after, for every size, block size and ending.

#### Short form

<a id="f05"></a>

##### F05 — ext2, libphoenix, kernel: `fsync()` and `sync()` flush nothing

- **What:** three independent breaks, each enough to lose data that an application was told is durable:
  1. libphoenix: `sync()` is an empty stub (`posix/stubs.c:152-154`).
  2. kernel: `posix_fsync()` sends `mtSync` with the file's oid only in `msg.i.raw`, under "FIXME: Replace this
     hack, pass oid via msg_t root struct" (`posix/posix.c:1567-1572`); every `mtSync` handler reads `msg->oid`
     (devices `storage/zynq7000-sdcard/sdstorage_srv.c:186`, corelibs `libstorage/fs.c:176`), so it syncs object
     id 0.
  3. ext2: `libext2_handler()` has no `mtSync` case and returns the caller's zeroed error, so `fsync()` reports
     success (`ext2/libext2.c:215-288`); on the libstorage route the filesystem's `.sync` is NULL
     (`libext2.c:370`) and `libstorage/fs.c:172-173` answers `-ENOSYS`; when a `.sync` exists, its result is ignored
     (`fs.c:176`).
- **Severity / scope:** data corruption (on power loss); general. SQLite, for one, relies on `fsync()`.
- **Our fix:** libphoenix `40a1efb`; kernel `482b54c2`; filesystems `017eb3a` (handler case) and the `.sync` wiring
  inside `391da7e` (split it out: on the handler route `fs->strg` is NULL, `libext2.c:317`, so `017eb3a` alone turns
  silent success into an error but cannot flush). The libstorage result is ignored in our fork too.
- **Patch:** `40a1efb`, `482b54c2`, `017eb3a` clean; the `.sync` part of `391da7e` must be split out.
- **Upstream status:** open; checked on 2026-11-21 against libphoenix `fc60045`, phoenix-rtos-kernel `4c7564cd`,
  phoenix-rtos-filesystems `52ae003`, phoenix-rtos-corelibs `5d676e7` and phoenix-rtos-devices `d1a7e764` at the
  lines above. Tracker: searched
  'sync()', 'fsync', 'mtSync': no matching issue.
- **Short form because:** found by reading the code while measuring SD writes; we have no reproducer (no harness
  compiles `libext2.c`). One issue for the three repositories, with three PRs, fits upstream's single tracker.

<a id="f10"></a>

##### F10 — ext2: crash ordering: pointers to new blocks reach the disk before the bitmap and the indirect blocks

- **What:** (1) `ext2_block_create()` stores the new block numbers (`ext2/block.c:253`, `:260`) before it writes the
  bitmap that marks them (`:268`), and the cache write-back (`:347`) can push the pointers to disk first: a power cut
  in between leaves blocks in use but free on disk, to be handed out again. (2) `_ext2_obj_sync()` writes the inode
  (`obj.c:208-213`) before the indirect blocks (`:214-223`): a newly allocated indirect block still holds its
  previous owner's data, so a cut leaves the inode pointing at garbage block numbers.
- **Severity / scope:** data corruption on power loss; general.
- **Our fix:** filesystems `40807bc` (bitmap and descriptor written once per call, before anything that can point to
  the blocks), `13f4808` (lower levels first, inode last).
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21 at the lines above.
  Tracker: searched 'ext2', 'power', 'crash', 'order': no matching issue.
- **Short form because:** our evidence is a replay of every prefix of one `write()`'s device writes (1 MiB write:
  bad at 4 of 28 cut points; one-block write into the double-indirect range: 2 of 7; none after), recorded in the
  commit messages; the replay tool is not committed.

<a id="f11"></a>

##### F11 — dummyfs: `link()`/`unlink()` set the wrong timestamps, also on failure

- **What:** they set the directory's mtime and atime and the file's mtime, never a ctime, and do so even when the
  operation failed. POSIX: on success only, the directory's `st_mtime` and `st_ctime`, and the file's `st_ctime`.
- **Severity / scope:** wrong output; general.
- **Our fix:** filesystems `8cf0a25`; test `stat_dir_times` (tests `99b024e`).
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21: `dummyfs/dummyfs.c:611-613`
  and `:679-681`. Tracker: the same defect for ext2 is **#1443** (open); none for dummyfs.
- **Short form because:** minor; add it to #1443.

<a id="f18"></a>

##### F18 — ext2: a symlink whose target cannot be written leaves a dangling directory entry

- **What:** `libext2_create()` makes the inode and its entry, then writes the target; when that write fails, the undo
  destroys the inode but keeps the entry (`e2fsck`: "entry has deleted/unused inode").
- **Severity / scope:** data corruption (small); general.
- **Our fix:** filesystems `6c8c117` (anything short of the whole target fails, and the undo unlinks the name). With
  targets longer than 60 bytes it needs `8a35d91` (F13) to free the target's block.
- **Upstream status:** open; checked against phoenix-rtos-filesystems `52ae003` on 2026-11-21: `ext2/libext2.c:117-119`.
  Tracker: searched 'symlink': no matching issue.
- **Short form because:** no committed reproducer (only reachable when the target write fails, e.g. `ENOSPC`).

### 1.4 Devices, usb, plo, posixsrv

<a id="d01"></a>

#### D01 — tty/libtty: `ioctl(FIONREAD)` on a terminal fails with `EINVAL`

- **Issue fields:** Category: BUG · labels `bug` · title "libtty: FIONREAD not handled (EINVAL on every tty)"
- **Severity:** wrong output
- **Scope:** general (every tty driver built on libtty)
- **Our fix:** devices `b247643a` (return `fifo_count(rx_fifo)`)
- **Patch:** conflicts (upstream changed the ioctl locking); re-derive from our current code
- **Upstream status:** open; checked against phoenix-rtos-devices `d1a7e764` on 2026-11-21. `tty/libtty/libtty.c:705`
  `_libtty_ioctl()` has no `FIONREAD` case; the default (`:815`, `log_warn("unsupported ioctl: 0x%lx", cmd);`)
  returns `-EINVAL`. `FIONREAD` is defined in libphoenix `include/sys/ioctl.h:49`.
- **Tracker:** searched 'FIONREAD', 'tty': related **#1426** (open, FIONREAD for *pipes*); none for ttys.

**Description.** Programs that ask how much input is pending (readline, bash, many terminal UIs) get a failed ioctl
and an undefined count.

**Expected behavior.** `ioctl(0, FIONREAD, &n)` returns 0 and the number of bytes waiting.

**Actual behavior.** -1, `EINVAL`.

**Reproduction.** `int n; printf("%d %d\n", ioctl(STDIN_FILENO, FIONREAD, &n), errno);` on a serial console.

**Environment.** See the section header; PL011 console via libtty.

**Evidence.** `tools/ttyprobe` on the Pi: `errno 22` before, the pending count after. No test in phoenix-rtos-tests
yet.

#### Short form

<a id="d03"></a>

##### D03 — storage/umass: a command that fails all its retries is reported as success with no data

- **What:** `_umass_transmit()` sets `bytes = 0` on each CHECK CONDITION retry and returns `bytes` when the retries
  run out, so a failed read or write reads as success with no data; TEST UNIT READY passes the same way. A transport
  error leaves the device and the data toggles in an unknown state: there is no Bulk-Only reset recovery (BOT 1.0
  §5.3.4) and no failure limit.
- **Severity / scope:** data corruption (a failed read returns success); general.
- **Our fix:** devices `cf83ee6f`, which needs usb `00e9ac4` (`usb_clearFeatureHalt()` sends ClearFeature(HALT) to
  the endpoint, USB 2.0 §9.4.1; upstream `libusb/driver.c:115` addresses the device, latent because nothing calls it).
  On upstream EHCI the bulk data toggle must also be reset after a clear (phoenix-rtos-devices
  `usb/ehci/ehci.c:353`).
- **Upstream status:** open; checked against phoenix-rtos-devices `d1a7e764` and phoenix-rtos-usb `6397a27` on
  2026-11-21: `storage/umass/umass.c:245` (retry loop), `:289-292` (`ret = -1; bytes = 0; continue;`), `:302`
  (`return bytes;`), `:339` (TEST UNIT READY). Tracker: searched 'umass', 'mass storage', 'storage': no matching
  issue.
- **Short form because:** our test (`test_usb_dmapressure`, 1 GiB, 0 bad sectors) exercises the fixed path but does
  not provoke CHECK CONDITION; the patch conflicts with our other umass changes.

<a id="d07"></a>

##### D07 — storage drivers: a read that crosses the end of the device fails with `EINVAL`

- **What:** the storage servers reject any request with `offs + len > size`, so a device cannot be read to its end:
  `dd if=/dev/<partition> of=img bs=1M` with no `count` stops before the last partial block, prints no summary, and
  `e2fsck` of the result reports a size mismatch that looks like corrupt media. POSIX `read()` at the end returns 0
  and a read that crosses it returns the bytes that are there.
- **Severity / scope:** wrong output; general.
- **Our fix:** only in our Pi SD driver (`822e9337`), not in any upstream driver. The write side (short write or
  `ENOSPC`) is a design choice.
- **Upstream status:** open; checked against phoenix-rtos-devices `d1a7e764` on 2026-11-21: `storage/zynq7000-sdcard/sdstorage_srv.c:46`,
  `storage/zynq-flash/zynq-flash.c:47`, `storage/flashdrv/flashsrv.c:70`, `storage/imx6ull-flash/flashsrv.c:487-489`,
  `storage/grlib-nandfctrl2/flashsrv.c:442`, `storage/virtio-blk/vblksrv.c:62` (which makes the clamp in
  `vblk.c:145-149` unreachable), `storage/umass/umass.c:484`, `storage/host-flash/host-flash.c:43`. `pc-ata/atasrv.c:319-331`
  already clamps; `gr716-flash/flashsrv.c:313-319` clamps a crossing read but rejects `offs == size`. Tracker:
  searched 'storage', 'EOF', 'dd': no matching issue.
- **Short form because:** found on our own driver; the upstream drivers were checked by reading only.

<a id="u08"></a>

##### U08 — usb/hub: over-current and suspend changes are never acknowledged

- **What:** `hub_portstatus()` clears only the connection, enable and reset change bits. An over-current or suspend
  change stays set, so the hub reports the port on every status poll forever.
- **Severity / scope:** hang (a busy status loop; the over-current itself goes unreported); general.
- **Our fix:** usb `182d146` (clear both, log an over-current).
- **Upstream status:** open; checked against phoenix-rtos-usb `6397a27` and phoenix-rtos-devices `d1a7e764` on
  2026-11-21: usb `usb/hub.c:340-352`; the EHCI root hub raises the over-current change (devices
  `usb/ehci/ehci-hub.c:383`, `:175-176`) and even implements its clear (`:260-261`), but nothing sends it. Tracker: searched 'hub', 'over-current': no matching issue.
- **Short form because:** found by reading; no over-current event was provoked on hardware.

<a id="b01"></a>

##### B01 — plo `cmds/kernel`: the loaded kernel's `.bss` is not zeroed

- **What:** `cmd_kernel()` copies `p_filesz` bytes of each `PT_LOAD` segment and leaves `[p_filesz, p_memsz)` as RAM
  held it; no upstream kernel `_init.S` clears `.bss` (aarch64 re-zeroes one variable by hand,
  `hal/aarch64/_init.S:208`). 17 upstream targets load the kernel this way (`_targets/*/user.plo.yaml`).
- **Severity / scope:** data corruption (uninitialised kernel globals, depending on the boot ROM and reset path);
  general.
- **Our fix:** plo `9551073` (zero the tail of each segment; plo's own `hal/ia32/efi/efi.c:698` already does this),
  and a debug command `poison` (plo `4509be9`, `3818c83`) that fills free RAM with a pattern before `kernel`.
- **Upstream status:** open; checked against plo `6e97833` on 2026-11-21: `cmds/kernel.c:111` copies, `:124` jumps.
  Tracker: **duplicate of #1733** (open, 2026-08-05, "plo: cmd_kernel() does not zero the .bss part of loaded kernel
  segments", with a patch attached).
- **Short form because:** already reported, with a patch; offer our PR and the `poison` command as the test (with the
  fix, 10/10 boots reach the shell with 3.7 GiB of free RAM poisoned first). We never saw a failure without it: a
  probe of two `.bss` words read 0 on 5/5 Pi boots.

<a id="p03"></a>

##### P03 — posixsrv, kernel: pipe and pty readiness is found only by re-polling; non-blocking pipe I/O can fail with a spurious `EAGAIN`

- **What:** (1) `poll()`/`select()` on a pipe or pty re-queries the server every `POLL_INTERVAL` (100 ms) instead of
  being woken, so a program that wakes its main loop through a pipe (GLib, WebKit) waits up to 100 ms per wake-up.
  (2) `pipe.c` takes its lock with `mutexTry()`, so non-blocking pipe I/O fails with `EAGAIN` when another thread
  merely holds the lock (a lost wake-up for GLib).
- **Severity / scope:** performance (1), wrong output (2); general.
- **Our fix:** (2) posixsrv `7e6cf77` (standalone, applies cleanly); (1) posixsrv `6432961`, `d2a3b7a`, `fb46fa8` and
  kernel `ee5939fc`, which need a readiness-notification system call our fork added: a design discussion.
- **Upstream status:** open; checked against phoenix-rtos-posixsrv `4673928` and phoenix-rtos-kernel `4c7564cd` on
  2026-11-21: kernel `posix/posix.c:42` (`#define POLL_INTERVAL 100000`), `:2611-2612`; posixsrv `pipe.c:49`
  (`return mutexTry(lock);`). Tracker: searched 'poll', 'pipe', 'EAGAIN': no matching issue.
- **Short form because:** (1) is a design limit with an enhancement as the fix; (2) is small.

### 1.5 lwIP port (phoenix-rtos-lwip)

<a id="w01"></a>

#### W01 — port/sockets: `ioctl(FIONBIO)` on an inet socket never makes it non-blocking

- **Issue fields:** Category: BUG · labels `bug`, `POSIX` · title "lwip port: ioctl(FIONBIO) passes NULL to lwip_ioctl, socket stays blocking"
- **Severity:** wrong output (and it clears an earlier `O_NONBLOCK`)
- **Scope:** general
- **Our fix:** lwip, the `FIONBIO` hunk of a larger commit in our fork; test `03dfc9a`
- **Patch:** send only the `FIONBIO` hunk
- **Upstream status:** open; checked against phoenix-rtos-lwip `3adc368` on 2026-11-21. `port/sockets.c:301-304`
  (`case FIONBIO: ... return map_errno(lwip_ioctl(sock, request, out_data));`): `FIONBIO` is an `_IOW` request, so
  `out_data` is NULL (`:683`), and lwIP reads NULL as "off".
- **Tracker:** searched 'FIONBIO', 'nonblock': none found.

**Expected behavior.** `ioctl(s, FIONBIO, &on)` with `on = 1` makes the socket non-blocking.

**Actual behavior.** The socket stays blocking; an earlier `fcntl(F_SETFL, O_NONBLOCK)` is undone.

**Reproduction.** `int s = socket(AF_INET, SOCK_STREAM, 0), on = 1; ioctl(s, FIONBIO, &on);` then
`fcntl(s, F_GETFL) & O_NONBLOCK` is 0, and `accept()` on a listening `s` blocks.

**Environment.** See the section header.

**Fix.** `FIONBIO` is an input request: pass `in_data` to `lwip_ioctl()` for it (`do_socket_ioctl()`, `:683-686`,
already unpacks it); `FIONREAD` keeps `out_data`.

**Evidence.** The test fails before and passes after (Pi).

<a id="w02"></a>

#### W02 — port/sockets: `socket(AF_INET6)` returns an IPv4 socket when lwIP is built without IPv6

- **Issue fields:** Category: BUG · labels `bug`, `POSIX` · title "socket(AF_INET6, ...) succeeds without IPv6 support and returns an IPv4 socket"
- **Severity:** wrong output (programs that probe IPv6 with `socket()` conclude it works, then fail with `EIO`)
- **Scope:** general (the default `lwipopts` has `LWIP_IPV6=0`)
- **Our fix:** lwip `d080252`, `83a56d5` (fail with `EAFNOSUPPORT`, comparing with the system's `AF_INET6`); test
  `test-libc-inet-socket` (tests `354c97b`, 8/0)
- **Patch:** the pair applies cleanly
- **Upstream status:** open; checked against phoenix-rtos-lwip `3adc368` on 2026-11-21. `port/sockets.c:1179`
  `lwip_socket(smi->socket.domain, type, ...)`; with IPv6 off, lwIP's `DOMAIN_TO_NETCONN_TYPE` ignores the domain,
  and lwIP's `sockets.h:242` redefines `AF_INET6` as `AF_UNSPEC` (0), so a check against lwIP's constant never
  matches the system's 10.
- **Tracker:** searched 'AF_INET6', 'IPv6': none found.

**POSIX requirement.** `socket()` fails with `EAFNOSUPPORT` for an unsupported address family.

**Expected behavior.** `socket(AF_INET6, SOCK_DGRAM, 0)` returns -1, `EAFNOSUPPORT`; programs fall back to IPv4
(quake3e, xtrans, lighttpd and redis test exactly this).

**Actual behavior.** A descriptor; the first `bind()`/`sendto()` with a `sockaddr_in6` fails with `EIO`.

**Reproduction.** The call above, then `bind()` to `in6addr_any`.

**Environment.** See the section header.

**Evidence.** The test passes on the Pi with both commits; the first commit alone did nothing (it compared with lwIP's
redefined constant), which the host test against glibc could not show.

#### Short form

<a id="w04"></a>

##### W04 — port/threads: the 512-byte thread-collector stack overflows inside `free()`

- **What:** `thread_waittid_thr` runs on a static 512-byte stack and frees exited threads' stacks; `free()` goes
  through the allocator's tree (`_malloc_chunkJoin` → … → `rb_transplant`, 512–528 bytes), so a return address lands
  on the static data after the stack. On our aarch64 build that was the route table, and lwIP (with the NFS root)
  died at boot.
- **Severity / scope:** crash (memory corruption in the network server); general, depth depends on architecture and
  compiler.
- **Our fix:** lwip `356ae98` (4 KiB on aarch64; a PR should size it for every architecture: riscv64 also stays at
  512 B in our fix); host check `tools/elf-stack-depth/check-lwip-thread-stacks.sh` (worst call path of every
  static-stack thread: exit 1 on the old binary, 0 now).
- **Upstream status:** open; checked against phoenix-rtos-lwip `3adc368` on 2026-11-21: `port/threads.c:39`
  `char collector_stack[512] __attribute__((aligned(8)));`, used at `:212`. Tracker: searched 'lwip', 'stack': none
  found.
- **Short form because:** our evidence is a static stack-depth check and one boot failure.

### 1.6 Toolchain (phoenix-rtos-build)

<a id="t01"></a>

#### T01 — toolchain: libstdc++ is built without its clock and sleep backends

- **Issue fields:** Category: BUG · labels `bug` · title "toolchain: libstdc++ for *-phoenix configured with no clock_gettime/nanosleep: steady_clock has 1 s resolution"
- **Severity:** wrong output (C++ time and threading features silently degraded)
- **Scope:** general (every C++ program on every target built by `build-toolchain.sh`)
- **Our fix:** build `95d9fca` (`gcc-16.2.0-12-libstdcxx-phoenix-features.patch`, which hard-codes the configure
  answers for `*-phoenix*` as GCC does for RTEMS and FreeBSD). It targets GCC 16, and some of its answers
  rely on libphoenix functions our fork added; an upstream fix belongs in upstream's
  `gcc-14.2.0-05-libstdcpp.patch` with answers that match upstream libphoenix.
- **Patch:** no (different GCC version; re-derive)
- **Upstream status:** open; checked against phoenix-rtos-build `46a69e2` on 2026-11-21. `toolchain/build-toolchain.sh:65-66`
  builds `gcc-14.2.0`; of its GCC patches only `toolchain/gcc-14.2.0-05-libstdcpp.patch:170-223` (the
  `*-phoenix*` stanza) touches libstdc++'s configure, and it sets only `hypot`, `strtof`, float math and `strtold`;
  no `--enable-libstdcxx-time` (`:234-242`). An aside in the same file: the `getexecname` hunk at `:251` sets
  `have_fcntl`, which looks like a copy-paste slip.
- **Tracker:** searched 'libstdc', 'chrono', 'toolchain', 'gcc': no matching issue.

**Description.** libstdc++'s configure checks are link tests in C++ mode run before `libstdc++.a` exists, so all of
them fail (`cannot find -lstdc++`); the clock checks are also wrapped in `#if _POSIX_TIMERS > 0 &&
defined(_POSIX_MONOTONIC_CLOCK)`, which libphoenix does not define. The resulting `c++config.h` has
`_GLIBCXX_USE_CLOCK_MONOTONIC`, `_GLIBCXX_USE_CLOCK_REALTIME`, `_GLIBCXX_USE_GETTIMEOFDAY` and
`_GLIBCXX_USE_NANOSLEEP` undefined, so `steady_clock::now()` falls back to `time()`. A config cache cannot fix it:
the results live in shell variables. Other features are affected the same way (for example
`std::filesystem::is_symlink()` always false).

**Expected behavior.** `steady_clock` with the resolution of `clock_gettime(CLOCK_MONOTONIC)` (nanoseconds on
Phoenix); `std::this_thread::sleep_for(20ms)` sleeps about 20 ms.

**Actual behavior.** `steady_clock` advances once a second; a 20 ms `sleep_for` slept 760 ms. SuperTuxKart ran at
exactly 1 frame per second because its frame-time loop waits for a non-zero clock difference.

**Reproduction.**
```cpp
auto a = std::chrono::steady_clock::now(), b = a;
while (b == a) b = std::chrono::steady_clock::now();
printf("%lld ns\n", (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
/* prints about 1000000000 */
```

**Environment.** See the section header; our toolchain built from upstream's GCC 14.2 recipe showed the same
`#undef`s before we moved to GCC 16.

**Fix.** In the `*-phoenix*` stanza of `gcc-14.2.0-05-libstdcpp.patch`, set the results that libphoenix supports (clocks,
`nanosleep`, `sched_yield`, `lstat`/`readlink`, …), and pass `--enable-libstdcxx-time=rt`; or make libphoenix define
`_POSIX_TIMERS`/`_POSIX_MONOTONIC_CLOCK` and let the checks link against a stub `libstdc++`.

**Evidence.** On the Pi with the fix: tick 1 s → 2 µs; `sleep_for(20ms)` 760 ms → 20.0 ms; `is_symlink()` correct.
Full audit: `docs/misc/2026-09-09-toolchain-silent-feature-audit.md`.

### 1.7 Ports (phoenix-rtos-ports)

<a id="r01"></a>

#### R01 — jq: the 1.8.2 recipe installs libjq and oniguruma into the build host's `/usr/local/lib`

- **Issue fields:** Category: BUG · labels `bug`, `ports` · title "ports/jq: make install-binaries writes libjq and libonig to the host's /usr/local"
- **Severity:** build (fails without root; with root it writes into the host)
- **Scope:** the build host (every target)
- **Our fix:** none for upstream's recipe: we build jq 1.8.2 with our own recipe and link the oniguruma port (ports
  `d5e26ae`).
- **Patch:** n/a
- **Upstream status:** open; checked against phoenix-rtos-ports `23101f9` on 2026-11-21. `jq/port.def.sh:31` runs
  `./configure ... --host="${HOST}" --bindir="$PREFIX_PROG"` (no `--prefix`/`--libdir`) and `:37` `make
  install-binaries`. In jq 1.8.2, `install-binaries` is `install-exec` (`Makefile.am:213-214`), which installs
  `lib_LTLIBRARIES = libjq.la` (`:61`) and recurses into `SUBDIRS = vendor/oniguruma` (`:199-202`) when no system
  oniguruma is found (`configure.ac:251`, `:274-276`); the default prefix is `/usr/local`.
- **Tracker:** searched 'jq', 'oniguruma': none found.

**Expected behavior.** The port installs only into the build tree.

**Actual behavior.** `make install-binaries` tries to write `libjq` and `libonig` (and `oniguruma.pc`) under the
host's `/usr/local/lib`: "Permission denied" as a normal user.

**Reproduction.** Build the `jq` port as a non-root user on a host without a system oniguruma.

**Environment.** Linux x86_64 build host, phoenix-rtos-ports `23101f9` recipe (seen on 2026-11-18 when we tried to
adopt it).

**Fix (suggested).** Pass `--prefix`/`--libdir` inside the build tree, or install just the binary (`install -m 755 jq
"$PREFIX_PROG"`). `--with-oniguruma=builtin` alone still installs `libonig`.

### 1.8 Tests (phoenix-rtos-tests)

##### TS01 — libc/stdio: `stdio_fflush` leaves `stdio_file_test` behind, so the next run fails

- **What:** `stdio_fflush.stdio_fflush_input_syncs_offset` (`4c5b3f9`) creates `stdio_file_test` in the current
  directory and nothing removes it: the group's `TEST_TEAR_DOWN` is empty, unlike every other group in the file. On a
  root filesystem that survives a reboot, the next `test-libc-stdio` run finds the file, so
  `stdio_fopenfclose_file`'s `fopen(..., "r")` succeeds and the `errno` check reads the EBADF the preceding
  `file.fclose_stdin_ebadf` test left: "Expected 2 Was 9" (`stdio_file.c:92`).
- **Severity / scope:** a false test failure on every second run; any target whose root keeps files between runs.
- **Our fix:** tests `793e190` (the group's teardown removes the file). Pi: two full runs in a row, 113/0 each, no
  file left (2026-11-21).
- **Upstream status:** open; checked against phoenix-rtos-tests `7d48d34` on 2026-11-21: `libc/stdio/stdio_file.c:1701`
  empty teardown, the test at `:1802`. Tracker: not searched (found the same day).
- **Short form because:** a test-hygiene defect with a one-line fix.

## 2. Mesa

Checked against Mesa `main` `98fc8b8f62a` (2026-11-21); we ship 26.2.0 with the patches in phoenix-rtos-ports
`mesa_drm/patches/` (our fork, `master`). Mesa takes issues and merge requests on
[gitlab.freedesktop.org/mesa/mesa](https://gitlab.freedesktop.org/mesa/mesa); the issue template asks for System
information, Describe the issue, Regression, Log files. **Mesa does not allow AI-written issue text, commit messages
or code comments, nor autonomous tools on its tracker** (see the checklist): every entry below is a set of notes for
the owner to write a report from, not text to paste. Mesa expects a reproduction on Linux with `main`; we have none,
which is why all five are short. The issue tracker was not searched (left to the owner, who has to write each report by hand
anyway).

Patches 0020–0024 were ported unchanged from a Mesa 26.2 tree used by another operating-system port on the same
board, and their measurements were taken there (the patch headers say so), not on Phoenix-RTOS. If the same patches
are reported from that port, file each once.

<a id="m01"></a>

#### M01 — gallium/auxiliary/u_vbuf: a NULL translate object is dereferenced, and the NULL is cached

- **What:** `translate_create()` returns NULL for some format combinations (`translate_generic.c:812-814`) and on
  allocation failure; `translate_cache_find()` stores the NULL in the cache (`translate_cache.c:101-102`) and returns
  it, and `u_vbuf_translate_buffers()` calls `tr->set_buffer()` on it (`u_vbuf.c:474`, `:496`).
- **Severity / scope:** crash; any gallium driver that uses u_vbuf. x86 builds also fall back to the generic path
  (`translate.c:41-48`), so it is not aarch64-only.
- **Our fix:** ports `mesa_drm/patches/0005-u_vbuf-NULL-check-the-translate-object-fix-aarch64-V.patch`. **It is
  incomplete:** the next lookup with the same key compares against the cached NULL in `cso_cache.c:123-124` and
  faults there. An upstream fix must also not insert NULL into the cache. The patch's statement that x86's SSE path
  covers all formats is wrong (see above).
- **Upstream status:** open; checked against Mesa `main` `98fc8b8f62a` on 2026-11-21 at the lines above (all under
  `src/gallium/auxiliary/`). The patch applies.
- **Short form because:** seen once on Phoenix (Quake III, `far=0x3a0`, which matches `tr->set_buffer` with
  `tr == NULL`); the vertex format that produced the NULL was never identified, so we have no reproducer.

<a id="m02"></a>

#### M02 — egl/drm, gbm: a GBM back buffer without a DRI image is dereferenced in `eglMakeCurrent()`

- **What:** `gbm_dri_bo_create()` falls back to `create_dumb()` (a buffer with `bo->image == NULL`) when the screen
  has no dma-buf export or the caller asks for `GBM_BO_USE_WRITE` (`src/gbm/backends/dri/gbm_dri.c:911-912`).
  `get_back_bo()` (`src/egl/drivers/dri2/platform_drm.c:305-306`) only checks `back->bo == NULL`, so
  `buffers->back = bo->image` (`:344-346`) can be NULL, and `src/gallium/frontends/dri/dri_drawable.c:409` reads
  `images->back->texture`.
- **Severity / scope:** crash; inferred to be reachable on Linux too: `gbm_surface_create()` does not reject
  `GBM_BO_USE_WRITE` (`gbm_dri.c:1109-1150`) and `create_dumb()` accepts SCANOUT with XRGB/XBGR8888 (`:850-852`).
- **Our fix:** ports `mesa_drm/patches/0009-egl-drm-treat-a-GBM-back-buffer-without-a-DRI-image-as-failure.patch`
  (fail `get_back_bo()` like any back-buffer allocation failure: `EGL_BAD_ALLOC`, no fault). Applies to `main`.
- **Upstream status:** open; checked against Mesa `main` `98fc8b8f62a` on 2026-11-21 at the lines above.
- **Short form because:** on our side only reachable through a driver without dma-buf export; the Linux trigger is
  inferred from the code and not run.

<a id="m03"></a>

#### M03 — broadcom/compiler: register-allocation passes do work proportional to blocks times temporaries

- **What:** (1) `vir_live_variables_defin_defout_dataflow()` solves a forward problem but visits the blocks in
  reverse (`src/broadcom/compiler/vir_live_variables.c:361`), so it needs about as many passes as there are blocks;
  (2) the live-interval computation clears per-temp state for every block (`:268-269`), tests every temp's bits in
  every block (`:391-413`) and allocates six bitsets per block per call (`:559-587`); (3) copy propagation scans
  every temp for each instruction that writes one (`vir_opt_copy_propagate.c:241`) and clears the MOV table per
  block (`:265`).
- **Severity / scope:** performance (shader compile time); v3d/v3dv on any OS. Upstream added incremental liveness
  after a spill since 26.2 (`af333031d54`, also `7bd40e5b37b`, `c4cb1b20e4d`), so the full recomputation now runs once
  per register-allocation attempt instead of once per spill; the per-pass costs above remain.
- **Our fix:** ports `mesa_drm/patches/0020` (program order), `0021` (no per-block passes over every temp), `0022`
  (list of available MOVs). `0020` and `0022` apply to `main`; `0021` conflicts in `vir_compute_start_end()`.
- **Upstream status:** open (code present, impact on `main` not measured); checked against Mesa `main` `98fc8b8f62a`
  on 2026-11-21 at the lines above.
- **Short form because:** the measurements (a 667-block, 10.5k-temp WGSL shader: 33.5 s → 7.5 s with all three) were
  taken on 26.2 on another port, and must be redone on `main`.

<a id="m04"></a>

#### M04 — v3dv: tiles outside a render area that does not start on a supertile boundary are overwritten

- **What:** the supertile grid ignores the render area's offset (`src/broadcom/vulkan/v3dv_cmd_buffer.c:421-445`);
  the first supertile row/column starts at `render_area.offset / supertile_size`
  (`v3dvx_cmd_buffer.c:781-782`), and when the area is tile-aligned the attachments are not loaded
  (`v3dv_pass.c:675-682`). The tiles of a straddling supertile that lie outside the render area are stored with
  undefined contents. Since `d30a6f81023` ("consider render area when configuring supertiles") the far edge is right;
  the origin is not.
- **Severity / scope:** data corruption (tiled `vkCmdCopyBufferToImage` uploads lose parts of earlier tiles); v3dv
  on V3D 4.2 and 7.x, any OS.
- **Our fix:** ports `mesa_drm/patches/0023-v3dv-align-the-supertile-grid-to-the-render-area.patch` (choose the
  smallest supertile size whose grid is aligned to the render area's offset; fall back to the old sizing). Hunks 1–2
  apply to `main`; hunk 3 needs a rebase.
- **Upstream status:** open; checked against Mesa `main` `98fc8b8f62a` on 2026-11-21 at the lines above; no later
  commit touches the sizing.
- **Short form because:** the measurement (192 tiled 256×256 copies into a 4096×4096 image: 2.48 M wrong RGBA32F
  pixels and 1.48 M RGBA8 before, 0 after) was taken on another port; a dEQP-style reproducer on Linux is needed.

<a id="m05"></a>

#### M05 — v3d: every shader-variant lookup computes a BLAKE3 digest

- **What:** `cache_hash()` feeds the variant key, together with the digest already stored for the shader, through
  BLAKE3 again on every hash-table lookup, that is on every draw that changes state
  (`src/gallium/drivers/v3d/v3d_program.c:1036-1042`); the table only needs a 32-bit hash, and the key already
  contains a digest.
- **Severity / scope:** performance (CPU time per draw); v3d, any OS.
- **Our fix:** ports `mesa_drm/patches/0024-v3d-hash-shader-variant-cache-keys-without-a-BLAKE3-.patch`
  (`_mesa_hash_data()`; equality check unchanged). Applies to `main`.
- **Upstream status:** open; checked against Mesa `main` `98fc8b8f62a` on 2026-11-21 at the lines above.
- **Short form because:** the measurement (5.1 % of a game's CPU time in its menu) was taken on another port.

## 3. libnfs

Checked against libnfs `master` `e6ed2b1` (2026-11-21, 8.1.0-5); we ship 6.0.2 with the patches in
phoenix-rtos-ports `libnfs/patches/`. Issues and pull requests: [github.com/sahlberg/libnfs](https://github.com/sahlberg/libnfs).
The GitHub tracker was not searched (not searched: no automated search of third-party trackers in this pass).

<a id="n01"></a>

#### N01 — nfs_v4: `st_blocks` is counted in 4096-byte units

- **Issue fields:** GitHub issue, title "NFSv4 stat: nfs_blocks uses NFS_BLKSIZE (4096) instead of 512-byte units"
- **Severity:** wrong output (`du`, `ls -s`, `stat` report about 1/8 of the real usage)
- **Scope:** every libnfs user that reads `nfs_blocks` over NFSv4
- **Our fix:** ports `libnfs/patches/02-nfs4-st-blocks-512-units.patch` (written against 6.0.2, line 577; needs
  re-anchoring to `:710`)
- **Upstream status:** open; checked against libnfs `e6ed2b1` on 2026-11-21: `lib/nfs_v4.c:710`
  `st->nfs_blocks = (st->nfs_used + NFS_BLKSIZE -1) / NFS_BLKSIZE;`, while the NFSv3 path uses 512
  (`lib/nfs_v3.c:4094`).

**Description.** `struct nfs_stat_64.nfs_blocks` is filled like `st_blocks` (512-byte units) for NFSv3, but in
4096-byte units for NFSv4, so applications that map it to `st_blocks` under-report space eightfold.

**Expected / actual.** For a 1 MiB file, `nfs_blocks == 2048` (as over NFSv3); over NFSv4 it is 256.

**Reproduction.** Call `nfs_stat64()` on the same 1 MiB file through an NFSv3 and an NFSv4 context and compare
`nfs_blocks` (derived from the code; we saw it through `du`).

**Environment.** libnfs 6.0.2 client on Phoenix-RTOS (aarch64) against a Linux `nfsd` (NFSv4.0); the code is
unchanged in 8.1.0-5.

**Fix.** Divide by 512 as the v3 path does.

**Evidence.** On our NFSv4 root, `stat().st_blocks` came back about 8× low and `du`/`ls -s` under-reported; the
patch fixed it.

## 4. X.org (libX11)

Checked against libX11 `master` `1b16905` (2026-11-19, 1.8.14-4); we ship 1.8.7. Issues and merge requests:
[gitlab.freedesktop.org/xorg/lib/libx11](https://gitlab.freedesktop.org/xorg/lib/libx11). The tracker was not
searched (not searched: no automated search of third-party trackers in this pass).

<a id="x01"></a>

#### X01 — om/generic, XDefaultOMIF: a font-set error path frees the caller's base-name string

- **What:** `create_oc()` stores the caller's base-name pointer (`_XlcSetValues()`, `src/xlibi18n/lcWrap.c:476-477`,
  from `modules/om/generic/omGeneric.c:1581`); when `_XParseBaseFontNameList()` fails (`omGeneric.c:1080-1082`) it
  returns before the private copy is made (`:1165`), and `destroy_oc()` then frees the caller's string
  (`:1427`, `Xfree(oc->core.base_name_list)`). `src/xlibi18n/XDefaultOMIF.c` has the same pattern (`:328`, error
  paths `:367`, `:444`, free at `:525`).
- **Severity / scope:** crash (free of memory the library does not own, e.g. a string literal); every platform.
- **Our fix:** ports `xorg_libs/patches/libX11-1.8.7-phoenix-fontset-basename-ownership-58.patch` (applies to
  `master`).
- **Upstream status:** open; checked against libX11 `1b16905` on 2026-11-21 at the lines above.
- **Short form because:** our trigger on Phoenix was most likely an allocation failure; the likely portable trigger,
  `XCreateFontSet()` with an empty or whitespace-only base name (`src/FSWrap.c:81-87` returns NULL for it), was not
  run.

## Security-relevant entries (held for private reports)

Each of these is security-relevant and is to be reported to the maintainers privately; the details are withheld from
this public document. The classification is from code reading only (see the header). Do not file any of them as a
public issue or a public pull request before the maintainers have said how they want them handled.

| ID | Repository | Status in our fork | Note |
|---|---|---|---|
| S01 | phoenix-rtos-kernel | fixed | the contribution plan's issue 4 |
| S02 | phoenix-rtos-kernel | fixed | the contribution plan's issue 7 |
| S03 | phoenix-rtos-kernel | fixed | |
| S04 | phoenix-rtos-kernel | fixed | |
| S05 | phoenix-rtos-kernel | fixed | |
| S06 | phoenix-rtos-kernel | fixed | |
| S07 | phoenix-rtos-kernel | fixed | |
| S08 | phoenix-rtos-kernel | fixed | |
| S09 | phoenix-rtos-kernel | fixed | |
| S10 | phoenix-rtos-kernel | fixed | |
| S11 | phoenix-rtos-kernel | fixed | |
| S12 | phoenix-rtos-kernel | fixed | |
| S13 | phoenix-rtos-kernel | fixed | |
| S14 | libphoenix | fixed | |
| S15 | phoenix-rtos-posixsrv | fixed | |
| S16 | phoenix-rtos-lwip | fixed | |
| S17 | phoenix-rtos-filesystems | fixed | |
| S18 | phoenix-rtos-filesystems | fixed | |
| S19 | phoenix-rtos-filesystems | fixed | |
| S20 | phoenix-rtos-devices | partly fixed | |
| S21 | phoenix-rtos-usb | fixed | |
| S22 | phoenix-rtos-usb | not fixed | found while checking this list |
| S23 | phoenix-rtos-build | fixed | |

## Already fixed upstream (no report needed)

Upstream has meanwhile fixed these defects itself, frequently with a different change than ours, or has an open pull
request for them; nothing needs to be reported. Each line gives the upstream commit or PR, checked on 2026-11-21
against the revisions in the header. When upstream covers only part of a defect, the rest is an entry above.

**phoenix-rtos-kernel**

1. Freeing a `MAP_CONTIGUOUS` mapping removed a never-inserted object from the object tree and emptied it (our
   `d0fb0ca9`, plan K19), and a page fetched at end of file kept stale contents past the end (half of our
   `f145658f`): **in progress** in upstream PR
   [phoenix-rtos-kernel#872](https://github.com/phoenix-rtos/phoenix-rtos-kernel/pull/872) (open, 2026-11-17, with a
   test in phoenix-rtos-tests PR #550). The other half is [K47](#k47).
2. AF_UNIX socket ids reused at once, so a stale bound pathname reached another live socket (our `69d9a448`): upstream
   `a8300c72` (AF_UNIX rewrite; ids from a rotating counter, `posix/usocket.c:258`).
3. Two further defects in the old `posix/unix.c`: the file was removed by the same rewrite. The new
   `usocket.c`/`uchannel.c` were not re-checked for the same classes of defect.
4. `getsockopt(SO_TYPE)`/`SO_ACCEPTCONN` on AF_UNIX (our `d77b2827`): upstream `d5d9b6b6`.
5. `getsockname()`/`getpeername()` on AF_UNIX returned no address (our `9744388c`, `482f11be`): upstream `566ffeab`
   (2026-09-21; #1672, #1674 closed). Our test `test_unix_sockname` could still go upstream.
6. `PROT_USER` taken from the fault type, so a kernel copy into a fresh copy-on-write user page faulted repeatedly (our
   `043cde80`): upstream `e18948f4` maps with the entry's protection (`vm/map.c:835`).
7. `vm_mapBelongs()` accepted a range that only overlapped a mapping (our `51beb23a`): upstream `52b4de9d`, with
   overflow checks `22d19ead`, `feb7da8a`.

**libphoenix**

8. `semaphoreUp()` lost wake-ups with several waiters (our `e75c4fe`): upstream `e5eb65b`.
9. `select(..., NULL)` never blocked (our `033ee1f`): upstream `03fe6fb`.
10. `std::atomic` in the C++ view of the pthread types (our `94df683`): upstream `6a150a2`.
11. scanf returned `EOF` on a matching failure (our `01f74b0`): upstream `b336093` (#1573).
12. `umask()` not applied (our `c6cec41`): upstream `f677820`, `a18a47d`.
13. A NULL signal handler was branch-called (our `da69de7`): upstream moved dispatch to the kernel and made `SIG_DFL`
    0 (`f4e762f`).
14. atexit index past its arrays (our `e643fa5`, `8dc40bb`): upstream rewrote atexit (`1e3ae62`).
15. `fstatvfs(-1)` and `statvfs("file/")` (our `f09da71`): kernel `b094f63a` and libphoenix `770aceb` (#1632,
    #1723).
16. Default condition-variable clock `CLOCK_MONOTONIC` (our `2ff5fe2`): upstream `4ab9ad9` (`pthread/pthread.c:36`).
17. `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` compared `threadinfo()` with `EOK` (fixed in our merge `b1a37b4`):
    upstream `b2f00c5`.
18. `pthread_getschedparam()`/`pthread_setschedparam()` declared but missing: upstream `97d2dea` (SCHED_RR).
19. `sysconf(_SC_OPEN_MAX)` disagreed with the kernel (our `4c94672`): upstream now returns -1 under
    `FIXME: issue #1610` (`unistd/conf.c:32-41`); known upstream.

**phoenix-rtos-posixsrv**

20. Timed requests (our `6c3ef06`, `90e0d88`, `7ed8949`, `73d7d34`): a request freed while still in the timeout tree,
    a lock inversion and unanswered event-queue timeouts are fixed by upstream `4178dd3`; the tree's order and equal
    deadlines by `a6c1675`; a `VMIN` pty read losing its bytes on timeout by `b089005` (2026-06-16). Upstream has its
    own test (`tests/test_rqtimeout.c`).

**phoenix-rtos-filesystems**

21. ext2 `ext2_block_sync()` wrote a hole-terminated run to the wrong block (our `fd5b7d4`): upstream `aeb7e3c`
    (#1418). Drop ours from the ext2 series.

**Third-party**

22. Mesa, broadcom/compiler: a vector `mov` moved only its first channel (our `mesa_drm/patches/0019`): Mesa
    `ec71bd60285` ("broadcom/compiler: scalarize every phi", MR !46417, 2026-11-18, `Backport-to: *`). Drop 0019
    when moving to a release that has it.
23. libnfs: `nfs_readlink()` reported success with the buffer untouched for a target longer than the buffer (worked
    around in filesystems `fc2f62b`): libnfs `7f4af11` (7.0.0).
24. libnfs: `flags | O_EXCL` instead of `flags & O_EXCL`, so every NFSv4 create was exclusive (our
    `libnfs/patches/03`): libnfs `cd12157` (7.0.0).
25. libnfs: no NFSv4.0 lease renewal (our `libnfs/patches/01`, a feature): libnfs `e0d26a0`, `f83f4a4`.
26. SDL2 KMSDRM: use after free at teardown (our `sdl2_kmsdrm/patches/0010`): SDL `9cc2f248f5`.

## Raspberry Pi 4 / BCM2711-specific items

None. Every Pi 4 defect we fixed is in code that exists only in our fork (the BCM2711 drivers, `usb/xhci/`, GENET,
the PL011/GIC HAL files). One lesson is general and fits a note to upstream rather than a report: our SD driver
programmed a CPU-physical address where the SoC's `dma-ranges` requires a bus address, which corrupts only writes and
was long misread as a silicon quirk; every DMA master on a SoC whose device tree declares `dma-ranges` needs its own
check.

## Checked and not reported

Candidates that are not upstream defects, are documented as intended, are latent with no upstream trigger, or are
enhancements. Checked on 2026-11-21 against the revisions in the header.

**phoenix-rtos-kernel**

- `fcntl()` record locks are a stub that returns success (our implementation `3844d204`): documented as unsupported
  (`phoenix-rtos-doc` `libc/functions/fcntl/fcntl.md:232`; #1235 closed as intended). Our implementation is an
  enhancement for discussion (plan K09).
- `MAP_SHARED` and `MAP_PRIVATE` are both 0 (`include/mman.h:28-29`): known upstream (#1655, #1657).
- `lib_printf()` from the scheduler deadlocking: our own diagnostic code; upstream's asserts call `log_disable()`
  first (`lib/assert.c:27`).
- Boot translation tables built by every CPU: upstream builds them on CPU 0 only (`hal/aarch64/_init.S:213-215`); the
  race came from our own boot rework.
- `ioctl()` requests sign-extended to 64 bits: latent (upstream libphoenix passes an `unsigned long`); only stale or
  third-party binaries trigger it.
- 32 KiB main-thread user stack (`hal/aarch64/arch/cpu.h:31`): a sizing choice.
- `meminfo()` marks "no anonymous map" with `~0U` in a `size_t` (`vm/map.c:1351`, `:1388`, `:1442`), which is not
  `SIZE_MAX` on 64-bit targets: minor (our `19d13cd5`, clean); a one-line PR rather than an issue.
- Enhancements (plan kind c): demand-zero anonymous memory (K15), file page cache (K30), zone free-list hardening
  (K24), tickless idle (K46).

**libphoenix**

- `malloc(0)` returns NULL (legal); printf flushing every 15 characters (performance); POSIX `TZ` support (a feature;
  #1243); `free()` hardening (an enhancement).
- Double `fclose()` writes through NULL (`sys/list.c:46`): undefined behaviour in the caller; our robustness fix
  (`5fa3847`) matches streams by address, which a reused address defeats, so do not send it as is.
- `fopen()` rejects `"rt"`, `"wx"`, `"re"` (`stdio/file.c:73-89`): minor; the `""` over-read we once listed was our
  own regression.
- `basename()` writing into a string literal: POSIX allows `basename()` to modify its argument.
- Three-argument `main()` gets no `envp` (`crt0-common.c:92`): a common extension, not POSIX; a one-line PR if
  upstream wants it.
- `<assert.h>` cannot be re-included with a different `NDEBUG` (`include/assert.h:14-15`): minor; a one-line PR.
- `gettime()`/`settime()` in the global namespace (`include/sys/time.h:34`, `:37`) collide with gnulib: worked around
  in our ports.
- libm `asinh`/`atanh`/`log1p`/`scalbln`/`tgamma` defects, `wcstombs()` truncation, `sinhf()` returning `sin()`, a
  `pthread_join()` double unlock and a `sem_timedwait()` deadline: all in code our fork added (the functions are
  absent upstream or the bug was ours).

**Other Phoenix repositories**

- posixsrv `tmpfile()` dead after `/` is remounted: latent (needs the root to change after posixsrv starts, as our
  netboot does).
- lwip "getifaddrs incomplete": not a defect; our diff removes two unused variables that GCC 16 rejects with
  `-Werror` (`port/sockets.c:1049-1050`); upstream builds with GCC 14.2.
- usb: the missing reset-recovery delay before SET_ADDRESS is latent on EHCI (the hub polls every 100 ms and the
  EHCI driver already waits, phoenix-rtos-devices `usb/ehci/ehci-hub.c:115`, `:127`); in passing, `usb/hub.c:207`/`:216` return -1 if a reset completes on
  the fifth poll (minor, not reproduced). The unbounded re-enumeration depends on the host controller (seen with our
  fork's xHCI). The `sprintf()` of the device link path fits its buffer (`usb/dev.c:574`, `:590`).
  `usb_clearFeatureHalt()` addressed to the device has no upstream caller (see [D03](#d03)).
- plo `syspage_entryAdd()` rounds a segment's start up to `p_align` (`syspage.c:373`): a `PT_LOAD` segment whose
  start is not aligned (allowed by ELF) would be placed above its link address. Latent: no upstream kernel was shown
  to have such a segment (we hit it only with our own kernel change, and avoided it in the kernel).
- devices: a umass node for a disk without a partition table and the USB mouse wheel are features.
- filesystems: writing the superblock after every operation, re-reading bitmaps of full groups, and coalesced reads
  are performance changes (plan F07–F09); our `391da7e` also leaves the on-disk free counts stale until the next
  sync, which a PR must state. `i_blocks` of indirect blocks: upstream never counts them (`ext2/file.c:228` FIXME).
- corelibs libcache write-back of unchanged lines: a performance change (plan C01).
- ports: a build-tree terminfo path in ncurses: upstream has no ncurses recipe.

**Third-party**

- Mesa: patch 0006 (skips the index-unrolling path when `u_vbuf_translate_begin()` fails; root cause unknown, so not
  a report), 0007 (early-Z off: a workaround for our stack), 0016 and 0018 (port glue), and the dropped 0004 (NPOT
  mipmaps: not an upstream defect; our earlier claim was wrong).
- X.org server, glamor: `glamor_destroy_pixmap()` and `glamor_create_gc()` do not chain to the wrapped screen
  procedures (`glamor/glamor.c:265-272`, `glamor/glamor_core.c:286-297`); harmless with the init order of the stock
  modesetting driver, Xwayland and Xephyr (glamor's saved procedures are fb's), exposed only by our former DDX.
- Games and applications: vkQuake/QuakeSpasm ignore `+` commands with shareware data (minor; checked only against
  local mirrors), SuperTuxKart graph-index and font-fallback guards, an SDL2 KMSDRM swap that waits for the flip
  before submitting, an xedit lisp NULL with a Phoenix-only trigger: not re-checked against current upstream; OpenSSL
  armv4 `.rodata` placement: known upstream (openssl#20923).

## Before sending: checklist for the owner

For every project:

- [ ] On the day of sending, fetch upstream again and re-read the lines named under "Upstream status"; the code may
  have moved or been fixed. Look for duplicates by hand (we only matched keywords against titles).
- [ ] Run the reproduction on something upstream can run (`ia32-generic-qemu`, `aarch64a53-zynqmp-qemu`; for Mesa,
  Linux with `main`) and name it in the report; where an entry's scope rests on reading the code, say so.
- [ ] Keep each issue and each PR to a single defect and each commit to a single change; the ext2 PRs depend on each
  other, so follow the order in the plan's section 6.
- [ ] Remove local details before sending: host names, lab addresses, user names, local paths, our internal IDs
  (`TD-xx`, `C1`, `#121`, "build 45") and reasons that only apply to the Pi.
- [ ] Security entries `S01`–`S23`: contact the maintainers privately first (no `SECURITY.md` in the Phoenix-RTOS
  repositories), agree how to report, and keep them out of public issues, PR titles and commit messages until then.

### Phoenix-RTOS (GitHub)

Read on 2026-11-21: `.github/PULL_REQUEST_TEMPLATE.md` (every repository), `phoenix-rtos-doc` `coding/index.md`,
recent issues and commits.

- [ ] Issues only in `phoenix-rtos/phoenix-rtos-project` (disabled elsewhere), with upstream's headings: Category,
  Description, Expected behavior, Actual behavior, POSIX requirement (if applicable), Minimal reproduction code,
  Environment (target and project revision, e.g. "ia32-generic-qemu, phoenix-rtos-project 46bbc34").
- [ ] PRs to the component repository from a fork, with the PR template filled in (description, motivation, type of
  change, how tested, checklist); the matching test as a separate PR to `phoenix-rtos-tests`, linked; reference the
  issue with `Fixes: phoenix-rtos/phoenix-rtos-project#NNNN`.
- [ ] Commit subject `subsystem/file: imperative summary` (lower case, no period; `!` prefix for a breaking change);
  short body. Upstream's `YT:`/`TASK:` trailers are for their internal tracker: leave them out.
- [ ] Code style: `clang-format-diff-18` against each repository's `.clang-format` (upstream's lint CI runs it), tabs,
  MISRA-leaning rules (`U` suffixes, `(void)` on ignored results, explicit comparisons); new files with
  `SPDX-License-Identifier: BSD-3-Clause` and the header layout in `coding/index.md`.
- [ ] Licence: BSD-3-Clause, same as the files touched. No CLA or DCO was found; `Signed-off-by` is rare upstream (7 of
  460 kernel commits since 2025) and not required. Ask the maintainers before the first PR whether they expect an
  agreement.
- [ ] AI policy: none found. Our commits end with an AI `Co-Authored-By:` line; keeping or dropping it is the
  owner's call (the plan, section 8).
- [ ] Duplicates to comment on rather than open: [K17](#k17) → #844, [L30](#l30) → #409, [L38](#l38) → #1279,
  [L49](#l49) `fmod` → #1229, [F11](#f11) → #1443, [B01](#b01) → #1733; [L22](#l22) needs a reply to closed #707;
  [K47](#k47) belongs on PR kernel#872; [L39](#l39) should be coordinated with libphoenix PR #284.

### Mesa (GitLab)

Read on 2026-11-21: `docs/submittingpatches.rst`, `docs/bugs.rst` and `.gitlab/issue_templates/Bug Report.md` at
`main` `98fc8b8f62a`.

- [ ] **AI rules (explicit):** no autonomously acting tools may submit contributions or interact with issues or merge
  requests; code comments, commit messages and GitLab comments have to be written by a person; disclose AI help with
  `Assisted-by: LLM` (or `Generated-by: LLM`), without naming the model and without `Co-authored-by`. Our Mesa patches
  carry `Co-Authored-By: Claude …` trailers and AI-written messages: rewrite every message and comment yourself and
  replace the trailer before opening a merge request. Write each issue yourself from the notes in section 2.
- [ ] Issue: test `main` first; template headings System information (`inxi -GSC -xx`), Describe the issue, Regression,
  Log files (backtrace, apitrace or a small test program). Each entry here first needs a reproduction on Linux (for
  example a Pi 4 running Raspberry Pi OS).
- [ ] Merge request from a personal fork; subject prefixed `u_vbuf:`, `egl/drm:`, `broadcom/compiler:`, `v3dv:`, `v3d:`;
  75-column lines; `Fixes: <sha> ("subject")` for a regression, `Closes: <issue URL>`, `Backport-to:` for stable.
  `Signed-off-by` is not required. Licence MIT.

### libnfs (GitHub)

- [ ] Issue or pull request at github.com/sahlberg/libnfs (the README also names a Google Group mailing list). No
  CONTRIBUTING file, DCO or AI rule found; the maintainer's own commits carry `Signed-off-by`. `lib/` and `include/`
  are LGPL-2.1-or-later. Re-anchor our patch to `master` first.

### X.org libX11 (GitLab)

- [ ] Issue or merge request at gitlab.freedesktop.org/xorg/lib/libx11; patch rules on the X.org wiki
  (SubmittingPatches: `Signed-off-by` with your real name, summary under 78 characters; read through a summary, so
  check the page itself). No AI policy found; recent commits in libX11 and the X server carry `Assisted-by:` trailers.
  Security issues go through `SECURITY.md` (X01 is a plain crash, not a security report). Licence MIT/X11.

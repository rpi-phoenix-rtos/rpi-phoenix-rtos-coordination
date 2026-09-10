# W37 detail snapshot 4 — 2026-09-10 (overnight)

Moved out of `docs/inprogress/WEEK-2026-W37.md` to keep that file short.
Everything here is SETTLED or SUPERSEDED; nothing below needs a decision.
Superseded specifically: the old 6/6 app gate and the old STK baseline are both replaced by
the tables in §2 of the weekly log, which were measured on the shipped image.

## 2b. ✅ Fixed this week

All HW-verified and in the image. One-line each in
[`docs/done/2026-09-10-w37-detail-snapshot-3.md`](../done/2026-09-10-w37-detail-snapshot-3.md).

## 2d. ✅ FIXED: `st_nlink` was stale after `unlink()` over the NFS root — root-caused

`test-libc-misc` → `stat_nlink_size_blk_tim/nlink`: three hard links give `st_nlink == 4`, unlink
one, and `stat()` still said **4**. Intermittent, and present since at least **2026-09-01**.

**Root cause.** nfs-fs nodes carry a positive attribute cache with a **100 ms** TTL. An op that
changes an inode's link count changes it for *every name that inode has*, but `nfs_ops_unlink()`
dropped only the directory and the unlinked name's own node — so a **sibling hard link kept answering
`stat()` from its own cache with the pre-unlink count** until its TTL expired. That is precisely why
it was intermittent: whether the wrong answer shows depends on where the 100 ms window falls. The
asymmetry was already in the code — `nfs_ops_link()` drops the target with the comment
*"st_nlink moved"*; unlink never got the matching sweep.

**Fix** (nfs-fs `4f2d8fd`): new `nfs_node_attrDropByIno()` clears the cache on every node whose
*cached* stat claims that inode. Unlink already had the inode from its pre-unlink stat; link captures
it before dropping. Spliced special files are skipped (their `nfs_ino` is a synthesised node id).

**What made it diagnosable** (tests `007c67c`): the test called `unlink()` three times and checked
**none** of them, so a silent unlink failure and a stale attribute were indistinguishable. Asserting
the return split them in one run — unlink returned 0 with errno 0, which pointed straight at the
cache. The group also leaked its hard links on failure, changing what the next run measured; setup
and tear-down now clean them.

HW: **`test-libc-misc` 208/0 — the whole suite clean**, where that case had been failing since
2026-09-01. nlink group 9/0 twice, `dirent` 38/0, NFS-root boot 0 faults.

## 2e. ✅ Closed a heap-corruption path: readlink byte counts are no longer trusted

`_readlink_abs()` returned `msg.o.err` verbatim — a count reported by whichever filesystem server
owns the oid, never checked against the `msg.o.size` we offered. Nothing downstream survives an
over-report: `_resolve_abspath()` walks **backwards** through its own `PATH_MAX` heap block right
after the readlink (`p -= symlink_len; memmove(p, path, symlink_len)`), so an over-long count writes
over **that block's own malloc chunk header** — which then reports as a "double free" on a header
that still validates, nowhere near its cause. The only guard was an `assert()`, and the build sets
`-DNDEBUG`, so it was compiled out of every shipped image.

Fixed both ends (libphoenix `50eab64`): the clamp, plus `_resolve_abspath` rejecting `>=` budget as a
real runtime check rather than an assert. ⚠ **Not the §2c cause** — that child's path is `/tmp/...`
with no symlink component so this `mtRead` never runs for it, and `nfs-fs`'s reply path is correct
(returns `min(target_len, len)`). It is hardening, and it removes a candidate that would otherwise
muddy the next §2c report. HW: `resolve_path` **16/0** (new boundary test included), `dirent`
**38/0**, `unix-socket` **27/0**, boot 0 faults.

## 3. THE FOUR BUGS YOU REPORTED — all four addressed

Status and evidence per bug:
[`docs/done/2026-09-10-w37-detail-snapshot-3.md`](../done/2026-09-10-w37-detail-snapshot-3.md).

## 4. ALSO DONE THIS WEEK (short)

STK 1 → 5.84 → 9 fps; root CAs; libphoenix stdio partial-write fix; V3D BO leak proven bounded;
libphoenix allocator hardened + a regression test; buffer-sharing steps 0 and 2 proven on HW (D7).

**★ Upstream sweep #10: 13 commits merged including a coordinated API BREAK — verified and pushed.**
Upstream widened the priority space **8 → 64** (a 64-bit ready bitmask replaces the linear scan) with
`setPriority()` migrations across devices/usb/utils/corelibs. 13 repos clean, **3 conflicted and were
resolved by hand**. The kernel conflict landed on the ready-queue guard added the same day; adopting
upstream's bitmask needed one adaptation worth knowing — **dropping a corrupt queue must clear its
bitmask bit**, or `_readyMinPrioIdx()` returns the same index forever and hangs the scheduler on the
exact path the guard exists to survive. Two of **our own** call sites broke at
`-Werror=deprecated-declarations` (the USB `main()` in our fork-only block, and `psh/cpuburn`, whose
worker clamp also had to go **6 → 62**). HW-verified before pushing: 0 kernel faults, AF_UNIX **27/0**,
misc **4/0**, STK unchanged. Manifest `manifests/2026-09-09-upstream-prio64-merge.md`.

## 4b. ✅ Docs refreshed on your ask; reliability figure recomputed

`README.md`, `PHOENIX-RTOS-RPI4-CHANGES.md`, `KNOWN-ISSUES.md` and the hardware matrix are current.
Reliability recomputed honestly with `scripts/reliability-tally.sh --since`: **313/313 reached the prompt**,
5 mid-print stops. Detail:
[`docs/done/2026-09-10-w37-detail-snapshot-3.md`](../done/2026-09-10-w37-detail-snapshot-3.md).

## 4c. STK baseline pinned before the toolchain swap

8 trials of the shipped 0.75 build: **7 rendered, 6 of them exactly `8/9/9`, one `7/7/9`.** That is
the before-number the toolchain swap gets compared against.

⚠ The bench first looked like it had failed — **STK's frame rate is on the SCREEN, not in the UART
log** (`show_fps` HUD), so grepping the log found nothing while the numbers sat in `artifacts/hdmi/`.
New `./scripts/stk-fps-from-hdmi.py <label>` reads them into one contact sheet, and flags a trial
whose frames are all small as MISSING rather than dropping it. T5 was such a trial: it reached the
psh prompt with **0 faults** and the capture then ended early — a harness truncation, not an STK
crash.

## 4d. Artifact timestamps were on two different clocks — fixed

`test-cycle-netboot.sh` stamped names in UTC, the other cycle scripts in local time, so a cycle could be
filed a **day** off and `reliability-tally.sh --since` would silently drop it. All local now.

## 4e. ✅ 6/6 showcase apps re-gated on the promoted toolchain — 0 faults each

One Pi cycle per app (they never exit), verified from the HDMI frames, not the UART log:

| app | last frame | |
|---|---|---|
| X desktop (`startx_gpu action`) | wmaker + xlogo, xbill, xclock, python GoL, xterm | ✅ |
| QuakeSpasm | textured 3D, **38.81 fps** (was 37) | ✅ |
| Quake III | main menu rendered, reached its `]` console | ✅ renders |
| Quake II | in-game, weapon + HUD | ✅ |
| vkQuake | rendering, console overlay down | ✅ |
| SuperTuxKart | main menu rendered | ✅ renders |

**0 faults and the psh prompt in all six.** Q3 and STK stopped at their menus rather than reaching
gameplay: that is my gate script's 90 s window, not an app problem — both need longer to auto-start.

⚠ **Method correction:** I first read X and Q3 as "never rendered" from PNG size. Wrong — size tracks
detail, not whether anything drew. The firmware's red netboot screen (~333 kB) is *larger* than a
working Window Maker desktop (~178 kB), so picking the biggest frame of an X session hands you the
boot screen. For anything that is not dense 3D, look at the **last** frame. Fixed the comment in
`stk-fps-from-hdmi.py` that asserted the opposite.

## 4f. ✅ CPython's silently-lost syscalls fixed and HW-verified

The audit said CPython lost all 7 `dir_fd=` syscalls to a probe-poisoning compat header. Fixed and
proven on hardware:

```
os.supports_dir_fd = access chmod chown link lstat mkdir mknod open readlink
                     rename rmdir stat symlink unlink     (14; the 7 *at ones were absent)
os.fwalk           = present        os.sched_getparam = present
time.get_clock_info('monotonic').resolution = 1e-06   (the true µs, not the shim's fake 1 ns)
```

Three findings while doing it: my new libphoenix `clock_getres` made the port's `static inline`
shim **illegal**, which broke configure's first probe outright (`C compiler cannot create
executables`) — the shim is gone; `clock_getres` then flipped to *no* via the same header-collision
mechanism as the `*at` family, caught live; and CPython has no `HAVE_SCHED_GETPARAM` macro at all —
it gates both get/setparam on `HAVE_SCHED_SETPARAM`.

## 4g. Upstream sync sweep (2026-09-10)

- **All 16 Phoenix siblings: 0 behind `origin/master`.** Nothing to merge.
- **quake3e 0 behind, quakespasm 0 behind** — already current.
- **yquake2: 10 commits merged, HW-verified, pushed.** Only one mattered —
  `gl3: fix GL3_Draw_TileClear for scrap images`, in the renderer we actually run (the port uses the
  **ES3/gl3** refresher, not gl1 as an earlier note claimed). Clean auto-merge, one file touched by
  both sides (`gl3_draw.c`), no conflicts. Regenerated the port patch (it went STALE — the fork is
  the source of truth). Quake II re-verified in-game: **38.85 fps**, HUD intact, 0 faults.
- **vkQuake: 39 behind, deliberately NOT merged.** **6 of our 10 patched files are also changed
  upstream** (`gl_rmisc.c`, `r_alias.c`, `gl_screen.c`, `host_cmd.c`, `glquake.h`, `common.c`) — the
  exact files `docs/misc/2026-09-03-game-fork-upstream-sync-plan.md` calls hard conflicts, since the
  MBOIT-pipeline and MD5-skinning rewrites moved every one of our anchor points. And the 39 commits
  are ray-tracing/acceleration-structure, SSAO/XeGTAO, MoltenVK-on-Apple, Ironwail feature imports
  and CI: **nothing we need and nothing that supersedes a patch of ours.** Per your settled rule
  ("game forks: if ours works, leave it") and with vkQuake rendering clean in tonight's gate, this
  stays put. It is a re-port, not a merge — worth its own scheduled session, not a sweep.


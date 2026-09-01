# RAM-staging: fast game asset loads over netboot (load-time workaround)

**Status: DONE + measured (2026-08-11/12).** The owner's "large RAM-disk + pre-download the assets"
load-time workaround, implemented, validated on hardware across all three Quake games, and productized
into a one-command helper. This doc is the usage reference; the chronological build log is in
`docs/inprogress/autonomous-plan.md` "Last progress".

## Why (the problem, measured)

Game loads over the netboot NFS root are slow because game asset access is **latency-bound**, not
bandwidth-bound. Measured on the Pi 4 (`nfs-read-bench`, id1/pak0.pak):

| access | NFS | tmpfs (`/tmp`, RAM) | ratio |
|---|---|---|---|
| random 4 KiB read | **1.46 ms/read** (a network round-trip each) | **0.07 ms/read** | **~20×** |
| bulk sequential | ~8 MiB/s (100 Mbit cable cap) | ~234 MiB/s | ~29× |

So the ~12.5 MiB/s cable cap is irrelevant to load time — a game doing thousands of scattered asset
reads pays thousands × ~1.46 ms over NFS. Staging the assets into RAM once (a cheap bulk copy) makes
every subsequent read RAM-speed.

## End-to-end results (HW, same Pi + build, only asset source differs)

| game | engine | result from RAM |
|---|---|---|
| Quake 1 | quakespasm-sdl | **3.6× faster** load (main→Host_Init: NFS ~3.84 s → RAM ~1.06 s) |
| Quake 2 | yquake2 | renders full 3D gameplay from RAM (50 MiB baseq2 staged) |
| Quake 3 | quake3e | **5.49× faster** (`CL_InitCGame`: NFS **63.77 s** → RAM **11.61 s**) |

## How to use it: `ram-stage-play`

One command recursively copies a game's asset tree NFS→tmpfs, then execs the game with a basedir
pointing at the RAM copy (psh has no `&&`/`;`, so it must be one command). Source:
`tools/ram-stage/ram-stage-play.c` (standalone static aarch64-phoenix ELF; staged to the netboot
export `bin/`).

```
ram-stage-play <src-dir> <dst-dir> <exec> [exec-args...]
```

Per-game recipes (validated on HW):

- **Quake 1** (quakespasm honors a RAM basedir natively — its `wait_for_gamedata()` probes
  `/ramtmp/quake`, `/tmp/quake` before the NFS dir — so staging alone suffices, or via the helper):
  ```
  ram-stage-play /usr/share/quake/id1 /tmp/quake/id1 \
      /bin/quakespasm-sdl -basedir /tmp/quake
  ```
- **Quake 2** (`yquake2` honors `+set basedir`):
  ```
  ram-stage-play /usr/share/quake2/baseq2 /tmp/baseq2 \
      /usr/bin/yquake2 +set basedir /tmp +set allow_download 0 \
      +set vid_renderer gl1 +set vid_fullscreen 2 +set r_mode -1 +map demo1
  ```
- **Quake 3** (`quake3e` honors `+set fs_basepath`):
  ```
  ram-stage-play /usr/share/quake3/demoq3 /tmp/demoq3 \
      /usr/bin/quake3e +set fs_basepath /tmp +set fs_game demoq3 +map q3dm1
  ```

## Infrastructure

- **`/tmp` is a RAM-backed dummyfs**, enlarged from the 32 MiB default to **256 MiB**
  (`rpi4b board_config.h` → `DUMMYFS_SIZE_MAX`) so a big game's assets fit. Per-instance cap,
  grows on demand — reserves no RAM. Manifest `2026-08-11-dummyfs-ramdisk-256mb`. HW-verified
  `/tmp` holds 255.5 MiB.
- **`nfs-read-bench`** (`tools/nfs-bench/`) modes: `read` (bulk), `mmap` (demand-paging),
  `rand` (scattered small reads = the game access pattern), `mkrand` (self-staged RAM rand),
  `stage` (reliable single-file NFS→tmpfs copy — the shell `cp` stalls on large NFS→tmpfs copies).

## Gotchas

- Stage to **`/tmp`** (tmpfs, re-bound during the NFS-root takeover), NOT `/ramtmp` (a pre-takeover
  mount point, absent on the NFS root).
- The shell `cp` silently stalls on large NFS→tmpfs copies (cp-specific); use `nfs-read-bench stage`
  or `ram-stage-play` (both do their own chunked copy).
- `/tmp` is cleared on reboot — staging is per-boot (that's what `ram-stage-play` automates).

## Possible extensions (not done)

- Boot-time / launcher auto-stage of a configured game (transparent, no manual command).
- A precise Q1/Q2 A/B like Q3's `CL_InitCGame` (yquake2's `Qcommon_Init` never returns, so it needs
  a load-time hook deeper in the engine than quakespasm's `-loadbench`).

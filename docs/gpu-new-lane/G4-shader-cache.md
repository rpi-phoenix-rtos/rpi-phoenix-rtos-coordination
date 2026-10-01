# G4: on-disk shader cache for the GPU stack

Register row: KNOWN-ISSUES **G4**. vkQuake took ~75 s to its first frame on every start, because Mesa recompiled every shader each time. Fix: ports `69344b2` on branch `g4-shader-cache`, a `mesa_drm` recipe change plus patch `0018-util-disk_cache-v3d-v3dv-the-on-disk-shader-cache-on.patch`.

## Why it was off

`-Dshader-cache=disabled` (`mesa_drm/port.def.sh`) had two causes. The first is that `v3d_disk_cache.c` asserted on an ELF build-id, which Mesa can only find through `dl_iterate_phdr`, and libphoenix has none of that ([E7](E7-drm-userspace-build.md)). The second is that turning the cache on exposes three gaps in libphoenix:

- `<ftw.h>` is included but never used;
- `dirfd()` does not exist;
- `_SC_GETPW_R_SIZE_MAX` is not defined.

## Design

**Cache type: Mesa's default multi-file cache.** Each entry is its own file. It is written as `<key>.tmp` under an fcntl lock, then renamed into place. Phoenix's `rename` is link + unlink, so the final name always points at a complete file. Every read re-checks the driver identity and a CRC32.

**P21: the index stays out of shared memory.** The only `MAP_SHARED` in the cache code is the index file, which every cache type maps. Under P21 its writes would never reach the file, so on Phoenix patch 0018 keeps the index in process memory. The cost is that the size counter starts at 0 in each process: the 1 GB limit bounds only what one process adds, and a `has_key()` miss just means a recompile.

**Rejected types.** The database and single-file types map the same index, and they lock through `flock`. libphoenix emulates `flock` with record locks, which are dropped when any descriptor of the file is closed.

**Identity.** The built `MESA_GIT_SHA1` was `""`, so v3dv's identity was the constant `"v3dv 26.2.0"` across every rebuild. That is exactly the G3 stale-blob hazard. The recipe now sets `MESA_GIT_SHA1_OVERRIDE` (Mesa's own mechanism) per ninja run. Its value is a digest of:

- the patched source tree and the port directory;
- the sysroot headers;
- `gcc -v` and the `cc1`/`cc1plus` binaries;
- the variant's cross file.

The digest's inputs are listed in `<build>/build-identity.txt`. The id lands in `<v>/shader-cache-id.txt` and shows as `(git-<id>)` in GL_VERSION and in `strings`. Both drivers keep the cache off if the id is empty. v3d gallium gets the same version-string identity v3dv has.

**Rebuild guard.** The recipe now rebuilds a meson build directory from scratch when its setup options change. Before, `meson setup` was skipped for an existing directory, and an option change would have been ignored silently.

**Location.** `$HOME/.cache/mesa_shader_cache`, which with `HOME=/` from passwd is `/.cache/...`. It persists on the NFS root and on the SD card's ext2.

## Pi check (pre-registered by the implementing agent), build 17, 2026-10-01

`B17ID 0e52cb5e31`. The `v3dv 26.2.0 (git-…)` string is present in `vkquake-drm`. The pristine export started with 0 cache files.

| run | `vkquake-drm: first present` | cache files after | faults |
|---|---|---|---|
| cold (fresh export) | **77 376 ms** (baseline before the change: 73 577 ms) | 117 | 0 |
| warm (second start) | **2 817 ms** | 117 (all hits) | 0 |

That is a **27× faster** start the second time. The HDMI frames of both runs show the same scene, the same start map with torches lit, at 41 and 43 fps. There is no speckle or garbage, which is what G3's stale blobs looked like.

## Open

- **Invalidation not yet run on the Pi.** The check: rebuild Mesa with one byte changed, and expect a new id, a cold start again, and the file count roughly doubling. On the host, the identity digest is reproducible and changes with each listed input.
- **Superseded entries are never evicted**, because the size counter is per process. Each Mesa rebuild adds one set to a persistent export.
- **`sync-netboot-tree.sh` still wipes the old stack's `.mesa-shader-cache`.** The new cache lives in `/.cache/mesa_shader_cache`, so that wipe no longer does anything.

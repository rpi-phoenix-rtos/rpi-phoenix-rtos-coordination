# NFS-as-root default + the /tmp-for-X blocker (2026-06-28)

Task #44/#45 (user directive: kill the `/nfstest` top-level hack, give a real root).

## ✅ NFS-as-root — ACHIEVED + committed + boots
The **default** boot is now `nfsroot`: the NFS volume is the real `/` (with
`/bin /usr /var /etc`), `/dev` is devfs, `/tmp` is meant to be a tmpfs. Boots to
psh with a normal tree; paths are root-relative (`/bin/startx`, `/bin/Xphoenix`,
`/usr/share/fonts`). Commits:
- `rpi4b: make nfsroot the default boot variant` (project `7c4bff1`)
- `nfs: takeover mount at "/"` (filesystems `05f049a`) — after lwip, bind /dev +
  portUnregister/register("/") so NFS becomes `/`. `nfs-fs: registered / (takeover)`.
- `project: NFS-as-root default boot — tmpfs /tmp, devfs /dev` (`9d9110f`)
- `x11: build wmaker + xlaunch with root-relative paths (drop /nfstest)` (`9e2c6a0`)
- `nfs: stable NFSv4 client id` (`4b5acb4`) — fixes #156 rapid-reboot NFS4ERR_EXPIRED.
- Rollback manifest: `manifests/2026-06-28-pre-nfsroot-default-44-45.md`.

## ✅ RESOLVED — X11/wmaker runs on the NFS-as-root default (HW-proven)
`startx wmaker` launches the full X stack on the NFS-root Pi: Xphoenix binds
`/tmp/.X11-unix/X0`, wmaker renders its deep-blue desktop + dock on HDMI
(1920x1080), keyboard (`/dev/kbd0`) and mouse (`/dev/mouse0`) are active. Two
fixes landed (both HW-validated):

1. **Writable RAM `/tmp`** (project `dfe96f6` + filesystems `16e9759`):
   `dummyfs-tmp` now MOUNTS itself at `/ramtmp` (`-m`, its own atDev splice onto
   the dummyfs RAM-root — honored, unlike onto NFS) instead of registering a
   bare `-N tmpfs` name. The NFS takeover resolves `/ramtmp` (a real mount,
   resolves reliably like `/dev`) and re-binds its own `/tmp` node onto that
   fresh writable dummyfs. Combined the `/dev`+`/ramtmp` mkdir into one
   invocation (plo registers one alias per program name — a second `mkdir;`
   line collides and bricks boot). Boot log: `re-bound /tmp (takeover, tmpfs
   port=4)`; `echo>/tmp/probe.txt`+`cat` ok; `mkdir /tmp/.X11-unix` ok.

2. **Bare command names resolve** (utils `52ddbda`): psh now defaults
   `PATH=/bin:/usr/bin:/sbin:/usr/sbin` when unset, so `startx wmaker` works
   directly (the plo-spawned NFS-root console psh inherits no env; previously
   only `/bin/startx` worked). libphoenix's execve already searches PATH — it
   just needs PATH set. Also unblocks Quake-by-name (#46).

Evidence: `artifacts/hdmi/20260628-135911-startx-abs-final.png` (wmaker desktop).

### Original root cause (kept for reference)
A bare `-N tmpfs` named dummyfs port registered but never mounted was not
resolvable by the late takeover `lookup("tmpfs")`, while the identical devfs
resolved — because devfs is *referenced* by `bind;devfs;/dev`. Mounting (`-m`)
gives the same persistent reference. The two earlier failed approaches:

1. **Fresh `dummyfs-tmp` named "tmpfs" port** (committed base): boot launches
   `dummyfs-tmp;-N;tmpfs;-D`; it reaches `dummyfs: initialized` (so it forked,
   `portRegister("tmpfs")` returned ok, and it serves). But the NFS takeover's
   `lookup("tmpfs")` (srv.c:585) **fails** — even with a 2 s retry loop (tested,
   reverted: not a timing race). Meanwhile `lookup("devfs")` (the identical
   `dummyfs;-N;devfs;-D` pattern) **succeeds** and /dev re-binds fine.
   → Result: `nfs-fs: re-bind /tmp: tmpfs port not found` → /tmp stays on NFS →
   `_XSERVTransSocketUNIXCreateListener: failed to bind listener`.

2. **Reuse the live `dummyfs-root` port for /tmp** (Agent A's pivot, reverted):
   splice OUR /tmp node's `mnt` to the old-root port (resolvable via `lookup("/")`).
   /tmp re-binds (`re-bound /tmp (takeover, dummyfs-root port=1)`) and X's socket
   bind no longer errors, BUT `/tmp` is then the OLD ROOT (shows stale `dev`/etc),
   and writes fail: `mkdir /tmp/.X11-unix: EPERM` + `Could not create lock file in
   /tmp/.tX0-lock` → server exits. (dummyfs-root's root isn't a clean writable /tmp.)

### Root-cause hypothesis (for the next session)
`devfs`'s name persists/resolves because it is **referenced** early (`bind;devfs;/dev`
does `lookup("devfs")`+splice at syspage step 5). `tmpfs` is registered (step 6) but
**never referenced** until the takeover (step ~11); the leading theory is that an
unreferenced named port's dcache name is not resolvable by a later `lookup()` even
though the serving process is alive. NEXT STEPS to try:
- Reference "tmpfs" early like devfs: add a `bind`/lookup of tmpfs right after it
  launches, or reorder it adjacent to devfs.
- OR debug `lookup()`/`portRegister` named-port resolution directly (why does an
  alive, registered second dummyfs name not resolve while the first does?).
- OR a cleaner mechanism: have the NFS takeover server itself expose a writable
  in-process scratch dir for /tmp (no second daemon), or mount the RAM /tmp via the
  kernel mount path rather than the in-process `node->mnt` splice.
- The `node->mnt` splice mechanism itself is proven (it works for /dev) — the only
  gap is obtaining a handle to a CLEAN, WRITABLE RAM-fs port for /tmp.

### Side effect to revisit
The x11 launcher + build are now root-relative (`/bin`, `/usr/share`). The legacy
`netboot` (`/nfstest`) variant's X therefore also needs its paths revisited (the
launcher no longer prepends `/nfstest`). nfsroot is the path forward.

## #45 status
Partial: x11 build/launcher de-/nfstest'd (`9e2c6a0`); ~28 `/nfstest` refs remain
(quakespasm/vkquake ports, stress, some scripts/docs). Finish after /tmp-X lands.

## Deployed image note
The currently-flashed netboot image was built with a (now-reverted) 2 s tmpfs
retry; rebuild `--scope core` to match committed source. Behaviour identical
(X still blocked) — the retry was a no-op (registration genuinely fails).

# T3b — exec-from-NFS investigation + verdict

**Status:** Investigated host-only (NO HW this session). **Headline: the "needs mmap"
hypothesis is DISPROVEN. The NFS fs server already implements the full exec/loader
contract; no code change was required (or made) to the NFS read/load path.** The most
likely cause of the observed `execv → ENOENT` is a *stale test*: the exec target
`/nfstest/bin/nfs-smoke` did not exist on the host export when exec was first tried
(the night doc lists `bin/` as **empty**; the binary's mtime is later). A bisecting HW
test (below) will pinpoint whether any genuine load-path bug remains.

---

## 1. The exec/loader mechanism (read, with file:line)

`execv`/`execve` in psh → libphoenix → kernel. The chain and the EXACT mt* ops it needs:

1. **libphoenix `execve`** — `sources/libphoenix/unistd/sys.c:99`.
   - `shebang(file)` (`sys.c:76`): `open` + `read` 2 bytes; ELF magic `7f 45 4c 46`
     is not `#!`, so the shebang branch is skipped for our binary.
   - `resolve_path(file, NULL, 1, 0)` (`sys.c:189`) — path canonicalization.
   - `stat(canonicalPath, &buf)` (`sys.c:196`) then `S_ISREG` gate; only then `exec()`.
   - **Both `resolve_path` and `stat` are SHARED with `cat`/`open`** (`stat` →
     `sys/stat.c:130` calls the *same* `resolve_path(...,1,0)`; `open` →
     `unistd/file.c:391` calls `resolve_path(...,1,1)`). Since `cat /nfstest/etc/hostname`
     and `cat /nfstest/test/*` (depth-2, through the mount) work on HW, these libphoenix
     steps are PROVEN to work on NFS paths. They are **not** the gap.

2. **kernel `exec()` syscall** — `sources/phoenix-rtos-kernel/syscalls.c:201` →
   `syscalls.c:197 proc_fileSpawn(path, argv, envp)`. (The posix `execve` variant,
   `proc_execve` at `proc/process.c:1666/1757`, takes the same path.)

3. **`proc_fileSpawn`** — `proc/process.c:1266`:
   - `proc_lookup(path, NULL, &oid)` (`process.c:1272`) — resolves the path to an oid
     (note: passes `file=NULL`, so `oid` receives the **dev** oid).
   - `vm_objectGet(&object, oid)` (`process.c:1277`) → which calls `proc_size(oid)`
     (`vm/object.c:76`) = **`mtGetAttr` with `i.attr.type = atSize` (=3)**
     (`proc/name.c:686`).
   - `proc_spawn(object, ..., object->size, ...)` → `process_load` (MMU build:
     `process.c:686`) does `vm_mmap(kmap, ..., o, base, MAP_NONE)` over the file object
     and walks the ELF headers — **the page faults are served on demand**, not by a
     filesystem mmap.

4. **The demand-page op = `object_fetch`** — `vm/object.c:174`:
   ```
   proc_open(oid, 0);                         // mtOpen
   proc_read(oid, offs, page_buf, SIZE_PAGE); // mtRead at page-aligned offs
   proc_close(oid, 0);                        // mtClose
   ```
   (`proc_open` `name.c:447`, `proc_read` `name.c:613`, `proc_close` `name.c:472`.)
   This repeats per 4 KB page of the binary. **There is no `mmap`/`mtDevCtl(MMAP)`/
   `mtMap` / flat-physaddr request anywhere in the loader.** The kernel never asks the
   fs server for a contiguous in-memory mapping; it reads pages.

### Exec contract, complete:
`mtLookup` → `mtGetAttr(atSize)` → repeated {`mtOpen`, `mtRead(off, 4096)`, `mtClose`}.
That's it. (Same path `#120` ext2-from-SD exec used — ext2/storage answers `mtRead`
page reads; it does NOT implement an mmap op either.)

## 2. What the NFS server implements vs the contract

`sources/phoenix-rtos-filesystems/nfs/`:

| Contract op | NFS handler | Status |
|---|---|---|
| `mtLookup` | `nfs_ops_lookup` (nfs_ops.c:73), dispatch srv.c:279 | OK — `len` (chars-consumed) accounting matches dummyfs (count leading slashes + component length per step); proven by the mount-crossing trace + working `cat` at depth 2. |
| `mtGetAttr(atSize)` | `nfs_ops_getattr` `case atSize` (nfs_ops.c:351), dispatch srv.c:264 | OK — returns `st.nfs_size`; `atSize==3` matches `file.h:21`. |
| `mtOpen` | `nfs_ops_open` (nfs_ops.c:167) | OK — opens fh on files. |
| `mtRead` | `nfs_ops_read` (nfs_ops.c:220) | OK — `nfs_pread(fh, buf, len, offs)`; opens-on-demand if no cached fh; short read at EOF (last partial page) returns the byte count, which the loader accepts (`object_fetch` treats any `>=0` as success). |
| `mtClose` | `nfs_ops_close` (nfs_ops.c:200) | OK. |

**Conclusion: every op the loader needs is implemented and, by inspection + the working
`cat`, correct.** Implementing an mmap/`mtDevCtl` handler would be **dead code** — the
loader would never call it. (The task's strong hypothesis was wrong; the source settles
it.)

## 3. Why the ENOENT — leading cause

The observed failure was `execv("/nfstest/bin/nfs-smoke") → ENOENT` ("psh: ... not
found", pshapp.c:1369/1408). Given §1–§2, ENOENT must come from `proc_lookup`
(`process.c:1272`) / `proc_size` returning `-ENOENT`, **or** from libphoenix `stat`
returning ENOENT (`sys.c:196`) — i.e. **the file was not found**, not a load-mechanism
failure. (A real load-path defect would surface as `-ENOEXEC`/`-EIO`/a fault, not
ENOENT.)

The parsimonious explanation, consistent with all evidence:
- `docs/inprogress/2026-06-07-nfs-night-progress.md:14` lists the host export as having
  an **empty `bin/`** at the time the file inventory was written.
- The exec target `/srv/phoenix-rpi4-nfs/bin/nfs-smoke` has **mtime 01:52**; the T3b
  read/write HW test (and the exec attempt) is timestamped "~01:5x" — **ambiguous /
  likely after the file inventory but possibly before/at the binary copy**.
- The binary now present is **byte-identical** (`cmp` clean, 228608 bytes) to the
  syspage-loaded `prog.stripped/nfs-smoke` the kernel already runs at boot — so it is a
  valid, loadable Phoenix aarch64 ELF. If the load path runs at all on a path that
  resolves, *this exact binary* will load.

So the leading cause is **the exec target did not exist (empty bin/) when exec was first
tried**, producing a genuine ENOENT that was misread as "loader can't load from NFS".

Two branches remain to disambiguate **on HW** (host-only cannot):
- **(A) Stale test** — bin/ was empty at exec time. The bisecting test below will now
  PASS at all three steps. This is the expected outcome.
- **(B) A real lookup/read defect specific to the `bin/` subtree** that the depth-2
  `cat` happened not to hit. Considered unlikely (the lookup code is component-generic
  and the `bin/nfs-smoke` descent was traced to consume exactly `len`), but the test
  isolates it: step 1/2 would fail in that case, NOT step 3.

If, and only if, step 3 fails while 1 and 2 pass, that is the **first genuine evidence**
of a load-path bug (capture: ENOENT-at-lookup vs `-ENOEXEC`/fault-during-load), and the
next session has a precise starting point — but it would still not be `mmap`.

## 4. What was implemented

**No code change to `sources/phoenix-rtos-filesystems/nfs/`.** A speculative mmap
handler or "robustness hardening" would be dead code touching the proven-working
read/write path — explicitly avoided. The deliverable is this analysis + the bisecting
HW test. The build was re-run to confirm the server still bundles (see §6).

## 5. EXACT HW verification (orchestrator) — bisecting

Prereqs: host NFS export up at `10.42.0.1:/srv/phoenix-rpi4-nfs` (fsid=0), and the exec
target present + executable:
```sh
ls -l /srv/phoenix-rpi4-nfs/bin/nfs-smoke   # must exist, mode 0755, 228608 bytes
```
(Already staged this session; `cmp` against `prog.stripped/nfs-smoke` is clean.)

Boot the netboot image; wait for UART:
```
nfs-fs: mounted 10.42.0.1:/ via v4
nfs-fs: mounted at /nfstest
(psh)%
```
Then from psh, run the three steps **in order** and record where it first fails.
(NOTE: psh has **no `|` pipe parsing** — do not pipe; use plain commands.)

1. **readdir / visibility:**  `ls /nfstest/bin`
   → expect: `nfs-smoke` listed.
2. **lookup + read on THIS file:**  `stat /nfstest/bin/nfs-smoke`
   → expect: a regular file, size **228608**. (Proves `mtLookup` + `mtGetAttr` resolve
   this exact path — the same ops the loader uses pre-load. Use `stat`, NOT `cat`/`head`/
   `xxd`: the binary is binary and psh can't pipe.)
3. **exec it:**  `/nfstest/bin/nfs-smoke`
   (argv, from `sources/phoenix-rtos-utils/nfs-smoke/nfs-smoke.c:159-166`:
   `nfs-smoke [server-ip] [export-path] [file-to-read]`, defaults
   `10.42.0.1 / /etc/hostname`. Optional explicit form: `/nfstest/bin/nfs-smoke
   10.42.0.1 / /etc/nfs-smoke.txt`.)

   **What step 3 actually proves:** that the kernel **loaded + launched the ELF from
   /nfstest at all** (the lookup → mtGetAttr(atSize) → page-fault mtRead loader path).
   nfs-smoke is a *standalone libnfs client* — it does its **own** DHCP-wait + NFS mount
   over the NIC and is independent of the `/nfstest` fs server, so its NFS round-trip is
   incidental to T3b (it's the T1 probe). The make-or-break signal is simply that it
   *runs* instead of `psh: ... not found`.

   → expect stdout (verbatim format strings from nfs-smoke.c:166/172/205/225/259):
   ```
   nfs-smoke: start (server=10.42.0.1 export=/ file=/etc/hostname)
   nfs-smoke: interface bound, ip=10.42.0.x
   nfs-smoke: mounted 10.42.0.1:/ via NFSv4 in NNN ms
   nfs-smoke: READ ok 17 bytes in NNN ms: "phoenix-rpi4-nfs"
   nfs-smoke: WRITE ok (21 bytes, readback MATCH)
   nfs-smoke: DONE (overall PASS if READ ok + WRITE ok above)
   ```
   (READ byte count/content depends on the `file-to-read` arg; `/etc/hostname` →
   17 bytes `"phoenix-rpi4-nfs"` per the T1 HW capture. The exec verdict is the
   **`nfs-smoke: start ...` line appearing at all** + a clean exit, no `not found`,
   no Data Abort.)

Interpretation:
- **All 3 pass** → exec-from-NFS WORKS; the earlier ENOENT was the empty-bin stale test
  (branch A confirmed). Unblocks T3 (NFS-root) and running ported programs from NFS.
- **Step 1 or 2 fails** → never a loader problem; it's lookup/readdir on the `bin/`
  subtree (branch B) — debug in `nfs_ops_lookup`/`nfs_ops_readdir`, NOT the loader.
- **Only step 3 fails** → first real load-path evidence; capture the exact errno/fault
  (ENOENT vs ENOEXEC vs Data Abort + far/elr) for the next session.

## 6. Build status

```
./scripts/rebuild-rpi4b-fast.sh --scope core --variant netboot   # clean
prog.stripped/nfs            283472 bytes (links libnfs.a)
loader.disk: "app ram0 -x nfs;/nfstest;10.42.0.1;/;v4 ddr ddr"   present
loader.disk: "app ram0 -x mkdir;/dev;/nfstest ddr ddr"           present
loader.disk: nfs-fs: text strings                                present
exec target: /srv/phoenix-rpi4-nfs/bin/nfs-smoke == prog.stripped/nfs-smoke (cmp clean)
```

## 7. Confidence

- **High** that the loader uses read-based demand paging, not mmap (object.c:174–207 is
  unambiguous; corroborated by ext2/#120 using the same path).
- **High** that the NFS server implements every op the loader needs and that no NFS code
  change fixes a problem that mmap was wrongly blamed for.
- **Moderate-high** that the observed ENOENT was the empty-bin stale test (branch A).
  Only HW disambiguates; the bisecting test resolves it in one boot.

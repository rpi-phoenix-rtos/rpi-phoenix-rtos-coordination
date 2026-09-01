# NFS-root exec `-EIO` (`err=-5`) — root-cause analysis, synthetic test, candidate fix (2026-07-26)

Read-only research task. No build, no Pi cycle, no source edits were performed.
All findings are from code + prior-doc reading. The orchestrator validates on HW.

## Symptom

On the RPi4 **nfsroot** boot, exec of the ~17 MB `/usr/bin/rpi4-quake` from the
NFS root fails intermittently (~1/10 boots, boot 6 of the 2026-07-26 campaign):

```
proc: exec '/usr/bin/rpi4-quake' failed (err=-5)      (-5 = -EIO)
```

Key facts (from `docs/inprogress/2026-07-26-nfs-boot-and-quake-10boot-campaign.md`):
- Fails **fast** (no multi-second stall).
- Logs **zero** nfs-fs errors ⇒ it does **not** traverse the 25×-with-backoff
  retry loop in `nfs_ops.c`.
- Occurred right after the psh prompt; re-running `rpi4-quake` succeeds (transient).

---

## 1. Where `-EIO` is born in the exec path (exact citations)

### 1a. The `-5` message is printed only *after* lookup + object creation succeed

The string `proc: exec '%s' failed (err=%d)` is emitted at exactly one site:

- `sources/phoenix-rtos-kernel/proc/process.c:1254`, inside
  **`process_exec()`** (`process.c:1175`).

`process_exec()` is the common tail of *both* exec entry points, and both first
resolve the path to an oid and build the backing object, only *then* calling
`process_load()`:

- psh / posix exec → `proc_execve()` (`process.c:1813`):
  - `proc_lookup(path, NULL, &oid)` — `process.c:1848`
  - `vm_objectGet(&object, oid)` — `process.c:1856`
  - → `process_execve()` (`process.c:1757`) → `process_exec()` (`process.c:1806`)
- fork/spawn → `proc_fileSpawn()` (`process.c:1360`):
  - `proc_lookup(...)` `:1366`, `vm_objectGet(...)` `:1371`
  - → `proc_spawn()` → `proc_spawnThread()` → `process_exec()` (`process.c:1302`)

`process_exec()` calls `process_load(..., spawn->object, ...)` at `process.c:1224`;
on `err != EOK` it prints line 1254 and exits the child.

**Hard constraint this imposes:** for the `-5` message to print, `proc_lookup`
**and** `vm_objectGet` must have already **succeeded**. If either failed, the
error returns to the caller (`proc_execve` returns at `:1849/:1857`;
`proc_fileSpawn` at `:1368/:1373`) and psh/libc prints its own message — the
`process.c:1254` string is never reached. So at the moment of failure the path
**was resolvable** ("/" was already the NFS root and reachable) and the file's
size was obtained. This single fact is what refutes H1 (see §2).

### 1b. The failing read path during ELF load

`process_load()` (64-bit, `process.c:749`) maps the ELF lazily into the kernel
map and forces in only the metadata pages the parser dereferences:

- `vm_mmap(...MAP_NONE, o, base...)` with `process->lazy = 1` — `process.c:777-779`
- `vm_mapForce(kmap, ehdr, PROT_READ)` (page 0 / e_ident) — `process.c:791`
- `process_forceElf64Headers(ehdr, size)` — `process.c:793` → `process.c:719`,
  which calls `process_forceRange()` (`process.c:689`) for the phdr table
  (`:728`), shdr table (`:733`) and the shstrtab (`:740/:742`).

`process_forceRange()` faults each page via `vm_mapForce()` (`process.c:703`) and
**propagates the real fault code** on failure (`process.c:704-708`, "Propagate
the real fault (e.g. -EIO ...)").

The force chain into the backing store:

- `vm_mapForce()` (`vm/map.c:704`) → `_map_force()` (`vm/map.c:721`)
- `_map_force()` (`vm/map.c:733`): `err = vm_objectPage(...)` (`vm/map.c:760`),
  then `if (err != EOK) return err;` (`vm/map.c:766-767`) — **errno propagated
  unchanged** (this is the post-2026-07-12 behaviour, see §1d).
- `vm_objectPage()` (`vm/object.c:299`): for an uncached page it drops the locks
  and calls `object_fetchCluster()` (`vm/object.c:346`); on failure it now
  **surfaces the real backing-store error**:
  `return (fetchRc < 0) ? fetchRc : -ENOMEM;` (`vm/object.c:397-398`).

### 1c. THE BIRTH SITE — `object_fetchCluster()` fabricates `-EIO` on `proc_open` failure

`object_fetchCluster()` (`vm/object.c:192`) fetches a demand-page cluster with one
open + one bulk read + one close:

```c
if (proc_open(oid, 0) < 0) {          /* vm/object.c:230 */
    vm_kfree(buf);
    return -EIO;                       /* vm/object.c:232  <-- birth of -5 */
}
...
r = proc_read(oid, ...);               /* vm/object.c:241 */
if (r < 0) { err = r; break; }         /* vm/object.c:242-244 */
...
(void)proc_close(oid, 0);              /* vm/object.c:254 (return ignored) */
```

Two independent, decisive observations:

1. **`vm/object.c:232` hard-codes `-EIO` and discards `proc_open`'s real return
   value.** Whatever `proc_open` actually returned (`-EINVAL`, `-ENOENT`, an
   nfs errno, …) is *masked* as `-EIO`. This mask is the headline finding: from
   the console log alone you cannot recover the true errno.
2. **This open is NOT retried.** `proc_read` at `:241` routes to the nfs-fs
   `mtRead` handler which retries transients 25× with backoff
   (`nfs_ops.c:361-371`), and even opens-on-demand-with-retry if it has no fh
   (`nfs_ops.c:334-353`). The `proc_open` at `:230` has **no** such wrapper — a
   single blip aborts the whole exec, fast, and (because the failure can be at
   the message-transport layer) with no nfs-fs-side log.

`proc_open` itself (`proc/name.c:447`) sends `mtOpen` via `proc_send`
(`name.c:462`) and returns either the transport error or `msg->o.err`. `proc_send`
(`proc/msg.c:351`) returns **`-EINVAL`** (not `-EIO`) when the port is gone
(`msg.c:366-368`, `proc_portGet == NULL`) or closed (`msg.c:384-385`,
`p->closed`). The nfs-fs `mtOpen` handler `nfs_ops_open` (`nfs_ops.c:216`) can
*fast*-fail before its own retry loops (e.g. `nfs_node_find == NULL → -ENOENT`
`:219-221`, or a non-transient `nfs_refreshStat` error `:238-241`); its retry
loops only cover the O_RDONLY-fallback open and the stat.

So the exec `-EIO` is manufactured at **`vm/object.c:232`** by converting a failed
`proc_open` into `-EIO`, which then propagates unchanged (§1b) to `process.c:1254`.

### 1d. Why this is the *current* failure and not the July-12 one

`docs/inprogress/2026-07-12-nfs-large-exec-enomem-rootcause.md` fixed an earlier
`-ENOMEM` (`vm_objectPage` used to swallow the fetch error and let `_map_force`
invent `-ENOMEM`); the fix made `vm_objectPage` propagate `fetchRc`
(`vm/object.c:397-398`). That work identified the underlying flake as a transient
**`nfs_pread`** (read) error. Since then, the 25× backoff loops were added to
`nfs_ops_read` / `nfs_ops_open` / `nfs_ops_lookup`, which cover the *read* path —
but a covered read fails **slowly** (seconds of backoff) and logs on the nfs side.
Boot 6 failed **fast and silent**, so it is **not** the July-12 read flake. The
only exec-path site that yields a fast, un-retried, un-logged `-EIO` is
`vm/object.c:232`.

---

## 2. Verdict: H2, and H1 is refuted

**H1 (takeover-window race) is refuted** — two independent prongs, both from
evidence in hand:

1. **Definitional:** the `-5` string prints only after `proc_lookup` +
   `vm_objectGet` succeed (§1a). A hit during the pre-takeover window fails at
   `proc_lookup` with `-ENOENT` — the sparse dummyfs RAM root has no
   `/usr/bin/rpi4-quake` — which returns to psh and never reaches
   `process.c:1254`. The srv.c `#156` notes confirm this window produces ENOENT,
   not EIO (`filesystems/nfs/srv.c:375-384`: "`ls /usr/bin` ENOENTs at the prompt
   BEFORE `registered / (takeover)`"). The observed error is *by construction*
   post-lookup-success ⇒ "/" was already the NFS root.
2. **Empirical:** the 2026-07-26 campaign confirmed the takeover 10/10
   (`registered / (takeover)` printed on every boot, boot 6 included), and the
   orchestrator psh-launches quake **after** confirming takeover — nfsroot never
   auto-launches it (`user.plo.yaml:220-244`, quake gated out of nfsroot). So the
   exec did not race the handoff. The global object cache is also cold at first
   exec (all boot `app -x` launches are syspage programs loaded from loader.disk
   via `VM_OBJ_PHYSMEM`, never touching "/"), so there is no stale-port cached
   object to inherit either.

**H2 (steady-state exec-read transient on a non-retrying path) is the cause**,
made precise: a transient failure of the per-cluster **`proc_open`** in
`object_fetchCluster` during ELF-header force-in, converted to a fabricated,
un-retried `-EIO` at `vm/object.c:232`. Because that line masks `proc_open`'s real
return, code-reading **cannot** further decide whether the true errno was
`-EINVAL` (transport: port momentarily closed/gone) or an `nfs_ops_open` fast
errno (libnfs reconnect / reserved-port reflood / `NFS4ERR_EXPIRED`, per the
July-12 candidate list). Resolving *that* is exactly what the unmask fix (§4) plus
the synthetic test (§3) are for. The asymmetry is the defect: the exec's **read**
is retried, its **open** is not.

---

## 3. Synthetic test (discriminates H1 vs H2, reproduces at high frequency)

### Blind spot to avoid (false negative)

Re-execing the **same** binary in a loop will NOT re-exercise the cold
open/fetch path after the first success: `vm_objectPage` serves cached pages
(`vm/object.c:325-329`) and nfs-fs lazy-close keeps the fh cached
(`nfs_ops.c:281-300`, reused at `:230-234`). A same-binary loop tests the page
cache, not the cold `object_fetchCluster` open that boot 6 hit (boot 6 was the
*first, cold* exec of that file). The object is only re-fetched if the prior
process fully exits and drops the object refcount to 0 — and even then the pages
persist in the cached `vm_object` until it is `vm_objectPut` to 0 refs.

**⇒ Each iteration must exec a DISTINCT file** so every iteration is a cold node
(new nfs `nextId`), a real `nfs_open`, and a real cluster fetch.

### 3a. Steady-state variant (primary; targets H2)

Run long after `registered / (takeover)` so the takeover window is irrelevant; if
failures still appear, H2 is confirmed independently of any handoff.

Preparation on the host export (`/srv/phoenix-rpi4-nfs`), before the run — make N
distinct ~17 MB copies so each exec is cold. `rpi4-quake` runs a continuous render
loop, so prefer a large binary that **exits by itself**; `/usr/bin/vkquake`
(~21 MB) still needs a display. The most reliable "large + exits fast" target is a
copy that is exec'd and then immediately killed, OR any large binary invoked with
an argument that makes it exit. Simplest robust recipe: copy the binary and exec
the copies; kill each quickly from a second psh reader if it does not self-exit.

Host (orchestrator, on the NFS server — not on the Pi):
```
for i in $(seq 1 40); do cp /srv/phoenix-rpi4-nfs/usr/bin/rpi4-quake \
    /srv/phoenix-rpi4-nfs/tmp/qk$i; done
```

psh on the Pi (one command per line — psh has NO `;`, `|`, `>`):
```
/tmp/qk1 ddr ddr
/tmp/qk2 ddr ddr
/tmp/qk3 ddr ddr
...
/tmp/qk40 ddr ddr
```
(Drive these via `scripts/test-cycle-psh-interact.sh`. Because quake does not
self-exit, either (a) send each line, wait for either the engine-init banner
[=exec OK] or `failed (err=-5)` [=exec FAIL], then reboot/kill between iterations,
or (b) build the loop around a large binary that DOES exit — see 3c.)

Each exec of a distinct `/tmp/qkN` forces a cold `object_fetchCluster` open+fetch.
The exec either prints the engine-init banner (PASS) or
`proc: exec '/tmp/qkN' failed (err=-5)` (FAIL). This reproduces the cold-open path
at up to 40×/boot instead of 1×/boot.

### 3b. Handoff variant (secondary; the H1 cross-check)

To *attempt* to reproduce H1 (and confirm it does NOT fire), exec the binary as
early as possible relative to the takeover:

- Orchestrator adds a boot-time launch of the exec immediately after the `nfs ...
  takeover` line in `user.plo.yaml` (nfsroot block, currently
  `user.plo.yaml:129`), i.e. a psh script or a small launcher that execs
  `/usr/bin/rpi4-quake` the instant psh starts — BEFORE the orchestrator's usual
  "wait for `registered / (takeover)`" gate.
- Or: rapid-reboot loop (< ~90 s apart, inside the NFSv4 lease window) driving the
  exec at the prompt without waiting for the takeover marker.

**Predicted outcome per hypothesis (this is the discriminator):**
- If failures in 3b are `err=-5` **EIO** that appear only in the tight
  post-takeover cold-open window and 3a *also* fails deep in steady state → **H2**
  (open-path transient; handoff timing irrelevant).
- If 3b instead fails with **`-ENOENT`** (or psh "no such file") when fired before
  `registered / (takeover)` → that is the known `#156` pre-takeover window, a
  *different* bug from the `-5` EIO, and confirms H1 is not the EIO cause.

### 3c. Pass/fail signal to grep from the UART log

Use `./scripts/uart-summary.sh <label>` then grep the captured log:

- **FAIL (the bug):** `exec '.*' failed (err=-5)` — count occurrences.
- **PASS:** the quake engine-init banner line for each launch (exec proceeded).
- **Distinguish H2 vs #156:** `failed (err=-2)` or psh "No such file" = ENOENT =
  pre-takeover `#156` window, NOT this bug.
- **Confirm the retry loop did NOT fire (fast/silent):** absence of any
  `nfs-fs:` error line and absence of a multi-second gap before the failure.

Fail-rate target: with 40 distinct cold execs/boot the ~1/10-boot rate should
surface within a few boots if it is a per-cold-open transient (H2).

---

## 4. One minimal CORRECT candidate fix (H2)

**Location:** `sources/phoenix-rtos-kernel/vm/object.c`, `object_fetchCluster()`,
the `proc_open` at `:230-233`. (Description only — no edit performed.)

Two paired changes, both load-bearing:

### 4a. Unmask (necessary, diagnostic, behaviour-neutral on success)

Stop fabricating `-EIO`; propagate `proc_open`'s real return, mirroring the
already-accepted 2026-07-12 precedent for `vm_objectPage` (`vm/object.c:397-398`):

```c
int openRc = proc_open(oid, 0);
if (openRc < 0) {
    vm_kfree(buf);
    return openRc;          /* was: return -EIO; */
}
```

This alone changes nothing on success and never fires on the SD path (SD opens do
not fail), but it makes the true errno reach `process.c:1254` so the orchestrator
can read it off the console — closing the mask that currently prevents deciding
`-EINVAL` vs an nfs errno.

### 4b. Cure — make the exec's OPEN as reliable as its READ already is

The defect is the **asymmetry**: `proc_read` here routes to a handler that retries
transients (`nfs_ops_read` `:361-371`, and its on-demand open `:334-353`), while
this single `proc_open` aborts the entire exec on one blip. The correct, targeted
fix is a **single bounded, backed-off re-drive of this one uncovered `proc_open`**,
matching the proven `nfs_ops` semantics — NOT a blanket bump of existing retry
counts:

```c
int openRc = -EIO;
for (int tries = 0; tries < RETRY; tries++) {
    openRc = proc_open(oid, 0);
    if (openRc == 0) {
        break;
    }
    /* bounded backoff, same shape as nfs_ops.c: 10,20,40,... ms, capped */
    ...
}
if (openRc < 0) {
    vm_kfree(buf);
    return openRc;      /* propagate the REAL error, never fabricate -EIO */
}
```

**Why this is correct and deterministic (not "more retries"):**
- It targets the *one* operation on the exec load path that lacks the resilience
  its sibling read already has. The read is retried; the open was not. Closing
  that specific gap is a correctness fix, not a knob-turn.
- Under the errno reading every piece of evidence supports (fast, silent,
  post-takeover, transient, read-already-retried), a single transient
  open-blip no longer aborts a 17 MB exec — the same guarantee the read path
  already provides.
- It is **behaviour-neutral on the SD deliverable** (SD `proc_open` never fails,
  so the loop runs once and returns immediately — the `8834eaf3` SD large-exec
  speedup is untouched; read-ahead stays at 16, `vm/object.c:182`).
- It preserves error *quality* (propagates the true code), so a genuine
  non-transient error still fails promptly and namefully.

**Note on scope:** if 4a's unmask reveals the true errno is consistently
`-EINVAL` (transport-level `proc_send` failure — a port momentarily
closed/gone, `msg.c:366-368/384-385`), that would point at a port-lifecycle event
rather than an nfs transient, and the fix should instead address that port event
directly (root-cause, not re-drive). The unmask (4a) is what lets the orchestrator
make that call from one HW run. If the errno is an nfs transient, 4b is the cure.
4a is a prerequisite for both.

### Not needed for this bug: takeover gating

H1 being refuted (§2), a takeover-atomicity / psh-gating change is **not** the fix
for the `-5` EIO. (Gating psh on `registered / (takeover)` remains worthwhile
hygiene for the separate `#156` pre-takeover ENOENT window — `srv.c:375-384` — but
that is a distinct issue and out of scope here.)

---

## 5. How the orchestrator should validate on HW

1. **Baseline reproduce (pre-fix):** run §3a (40 distinct cold execs/boot) across
   several boots on the current image; confirm `exec '.*' failed (err=-5)` recurs
   at ~the campaign rate and that each failure is fast + carries no `nfs-fs:`
   error line (confirms the non-retrying birth site).
2. **Apply 4a (unmask) only, rebuild `--scope core`** (core kernel change — see the
   stale-core hazard in `CLAUDE.md`; verify with
   `strings ...loader.disk` for the changed symbol or a distinctive string).
   Re-run §3a. Read the now-unmasked errno printed at `process.c:1254`
   (`err=-22` = `-EINVAL` transport; an nfs errno otherwise). This decides
   transport-event vs nfs-transient.
3. **Apply 4b (bounded re-drive) per the step-2 verdict, rebuild `--scope core`.**
   Re-run §3a over enough cold execs to exceed the pre-fix failure count with
   zero `err=-5`; confirm the SD variant still boots and quake(SD) still starts
   fast (no regression to the `8834eaf3` behaviour).
4. **Regression guard:** one clean nfsroot boot to `registered / (takeover)` +
   psh, 0 faults, and a normal `rpi4-quake` launch, to confirm the change is inert
   on the success path.

---

## Citation index

- Error print: `proc/process.c:1254` (in `process_exec`, `:1175`).
- Exec entry (psh): `proc/process.c:1813` `proc_execve` — lookup `:1848`,
  objectGet `:1856`; `process_execve` `:1757` → `process_exec` `:1806`.
- Exec entry (spawn): `proc/process.c:1360` `proc_fileSpawn` — lookup `:1366`,
  objectGet `:1371`.
- ELF load / force: `process_load` `:749`, `vm_mapForce` `:791`,
  `process_forceElf64Headers` `:719`, `process_forceRange` `:689`
  (propagates real fault `:704-708`).
- Force → backing store: `vm/map.c:704` `vm_mapForce` → `:733` `_map_force`
  (`vm_objectPage` `:760`, propagate `:766-767`).
- `vm/object.c`: `vm_objectPage` `:299` (cached page `:325-329`, propagate fetchRc
  `:397-398`); `object_fetchCluster` `:192` (**birth site** `:230-232`, read `:241`,
  close `:254`); read-ahead `:182`.
- Transport: `proc/name.c` `proc_open` `:447`, `proc_read` `:613`, `proc_size`
  `:675`, `proc_portLookup` `:224`; `proc/msg.c` `proc_send` `:351`
  (`-EINVAL` on bad/closed port `:366-368`, `:384-385`).
- nfs-fs handlers: `filesystems/nfs/nfs_ops.c` `nfs_ops_open` `:216`
  (fast `-ENOENT` `:219-221`, lazy fh reuse `:230-234`, retry O_RDONLY `:251-257`),
  `nfs_ops_read` `:306` (on-demand open retry `:334-353`, read retry `:361-371`);
  node table `filesystems/nfs/nfs_node.c` (`nfs_node_find` `:76`, ids monotonic
  `:120`, only removed on unlink/destroy `:132`).
- Takeover: `filesystems/nfs/srv.c` `nfs_runTakeover` `:498` (`#156` window notes
  `:375-384`, `registered / (takeover)` `:690`); boot order
  `_projects/aarch64a72-generic-rpi4b/user.plo.yaml` (nfsroot block `:103-161`,
  nfs takeover launch `:129`, quake gated out of nfsroot `:220-244`).
- Prior docs: `docs/inprogress/2026-07-26-nfs-boot-and-quake-10boot-campaign.md`
  (boot 6), `docs/inprogress/2026-07-12-nfs-large-exec-enomem-rootcause.md`
  (ENOMEM→propagate precedent).

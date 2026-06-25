# #156 — NFS "first-read transient ENOENT" — root cause + verdict (2026-06-25)

**Status: root-caused; no in-scope non-regressing fix; mitigated by the
wait-for-`registered /` orchestrator protocol below. NOT closed — the real fix is a
plo boot-order gate (deferred, out of scope). The ENOENT still reproduces on any
boot where a client races the takeover, so the wait-for-takeover protocol must be
honored.**


**Verdict: NOT a code bug in the NFS server or libnfs. It is the takeover WINDOW —
a boot-order race in `user.plo.yaml`, which is out of scope for this task. No clean,
non-regressing in-scope fix exists, so no functional code was changed.** The one
code change shipped is a comment correction in `srv.c` (the prior comment guessed
"mount-settle timing"; this characterizes the proven cause). Behavior is unchanged.

## The symptom

On the `nfsroot` variant (NFS owns "/" via takeover), the FIRST access to a fresh
directory/file transiently fails `ENOENT` ("no such file or directory" / "not
found"), while later accesses to the same paths succeed. Re-confirmed examples:
`ls /usr/bin` → "can't access", and the first `/usr/bin/lua` of a boot → "not found".

## Root cause — the takeover window (mechanism "a"), PROVEN

`user.plo.yaml` (nfsroot block) launches the NFS takeover server and `psh` (plus the
device drivers) as **siblings** — plo spawns each `app -x <prog>` and moves on; it
does **not** block on the takeover server registering "/". So psh reaches its prompt
and the first client commands run while "/" is still the **sparse dummyfs RAM root**
(it has no `/usr/bin`, no `/root/luatest.lua`, etc.). Those commands ENOENT against
the RAM root. Once the takeover server finishes (DHCP lease → `nfs_mount` →
`portRegister("/")`), "/" becomes the NFS export and every subsequent access works.

### Log proof (`artifacts/rpi4b-uart/rpi4b-uart-20260610-070721-lua-nfsroot.log`)

```
398  ls /usr/bin
399  ls: can't access /usr/bin: no such file or directory   <-- ENOENT at the prompt
400  (psh)% lwip: genet@fd580000: link up: 100 Mbps full-duplex
403  nfs-fs: takeover: interface bound, ip=10.42.0.12          <-- DHCP lease only now
405  nfs-fs: mounted 10.42.0.1:/ via v4
406  nfs-fs: re-bound /dev (takeover, devfs port=3)
408  nfs-fs: takeover via portRegister rc=0 (unregister rc=0)
409  nfs-fs: registered / (takeover)                          <-- "/" is NOW the NFS export
411  /usr/bin/lua /root/luatest.lua
413  ver Lua 5.3                                               <-- succeeds first-try
428  INLINE_OK                                                 <-- succeeds first-try
```

The lone userspace ENOENT fires at line 399, **10 lines before** `registered /
(takeover)` at line 409. The same pattern holds in the other two nfsroot logs:

- `...ssl-curl-nfsroot.log`: ENOENT at line 399, `registered / (takeover)` at 409.
- `...ssl-slow-nfsroot.log`: ENOENT at line 398, `registered / (takeover)` at 408.

**No ENOENT ever occurs AFTER `registered / (takeover)` in any nfsroot log.** The
"lua not found" boot is simply one where `lua` happened to be the first windowed
command; the mechanism is identical.

## Why it is NOT the dircache (refutes the original #156 premise)

The task premise (stale libnfs dircache) is stale. Two independent disproofs:

1. The dircache is **already disabled** at HEAD — `nfs_set_dircache(nfs, 0)` in
   `nfs_makeContext` (srv.c, committed `1628928`). The ENOENT still reproduces with
   the cache off, which `1628928`'s own message records.
2. The lookup path does not touch the dircache. `nfs_ops_lookup` (nfs_ops.c:73)
   resolves each path component with a fresh `nfs_lstat64` RPC; the libnfs dircache
   flag is read only by `nfs_closedir` (cache-vs-free of a readdir result), not by
   the lookup/open/pread paths. There is **no first-miss-then-hit cache** in the
   lookup path, so a cold-cache "(b)" mechanism cannot occur.

So task angles 1–2 (fix the ERANGE so the dircache can be turned off) are moot —
the cache is already off and reads are fine (the earlier ERANGE was reclassified an
unrelated flake in `1628928`). Angle 3 (post-mount dircache invalidate / warm-up
readdir) is a no-op: with the cache off there is nothing to invalidate, and during
the window clients never reach the NFS context at all — they hit the RAM root.

## Why there is no clean, non-regressing in-scope fix

- The NFS server **cannot win the race.** Takeover is gated on the DHCP lease
  (`wait_for_dhcp_lease`), which only completes after lwip brings the link up
  (lua-nfsroot.log: link up at 400, lease at 403). psh prompts well before that.
  No latency tweak inside `srv.c`/libnfs registers "/" before psh accepts input.
- **Early-register "/" before mount/DHCP** (so lookups route to the NFS server and
  block until the mount is ready) would regress the no-brick RAM-root degrade-on-
  failure path (`eed921c`) and the in-process `/dev` re-bind ordering
  (`nfs_runTakeover`). That is a real regression — rejected under this task's
  "ship only a fix that does NOT regress" rule.
- **Gating clients on takeover-complete** is the correct fix, but it is a
  `user.plo.yaml` boot-order change (launch psh/drivers only after a takeover-done
  signal). `plo`/boot config is **explicitly out of scope** here.

Per the task's fallback clause, no functional code was changed.

## Orchestrator validation protocol (the actionable deliverable)

The transient ENOENT is fully avoided by not racing the takeover:

1. Boot the nfsroot variant. **Wait for the UART line `nfs-fs: registered /
   (takeover)` before sending ANY psh command.** (Arming a `Monitor`/grep on that
   exact string is the clean way; do not send on the bare `(psh)%` prompt.)
2. First-access-to-a-fresh-dir test (previously ENOENT'd): `ls /usr/bin` — now
   lists the directory first-try; `/usr/bin/lua -e print("INLINE_OK")` prints
   `INLINE_OK` first-try.
3. No-ERANGE read regression test: `cat /etc/hostname` (or any small file) returns
   the content with no `ERANGE`/`Result too large`; a larger read e.g.
   `cat /usr/bin/lua | wc -c` (or `dd if=/usr/bin/<bin> of=/dev/null`) completes
   without ERANGE — confirming the dircache-off state did not regress reads.

If step 1 is honored, steps 2–3 pass first-try with the code exactly as-is.

## Future proper fix (out of scope, for the backlog)

Make `user.plo.yaml`'s nfsroot block gate psh + the device drivers on takeover
completion — e.g. the takeover server creates a `/dev/<ready>` node (or writes a
sentinel) after `portRegister("/")`, and a small `waitfor` shim precedes the psh
launch. Alternatively, plo could spawn-and-wait on the takeover program reaching a
ready barrier. Both are boot-order changes, not NFS-server changes.

## Files
- `sources/phoenix-rtos-filesystems/nfs/srv.c` — comment in `nfs_makeContext`
  updated to the proven cause (no behavior change).
- `sources/phoenix-rtos-filesystems/nfs/nfs_ops.c:73` `nfs_ops_lookup` — the
  per-component `nfs_lstat64` lookup path (unchanged; cited as proof).
- Evidence: `artifacts/rpi4b-uart/rpi4b-uart-20260610-070721-lua-nfsroot.log`,
  `...-ssl-curl-nfsroot.log`, `...-ssl-slow-nfsroot.log`.

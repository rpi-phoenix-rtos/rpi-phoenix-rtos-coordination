# #156 residual (c): "first read after psh ENOENTs, 2nd succeeds" — REFRAMED

**Date:** 2026-06-09. **Status:** root-caused by deduction; resolved as document + safe
cleanup (Option C). The proper UX fix is an **attended** boot-sequencing change (below).

## Original framing (WRONG)
The residual was filed as an "attr-cache / settle race" in the NFS server, suspected to
interact with the lazy-close fh cache (f14838c).

## Actual root cause: a boot-ordering race (not the NFS server)
On the design-A nfsroot boot the psh prompt becomes interactive **several seconds before
the NFS takeover finishes**. Proven from a real psh-interact log
(`artifacts/rpi4b-uart/rpi4b-uart-20260608-224856-psh-interact.log`):

- line 386 — `(psh)%` live and interactive
- line 404 — `nfs-fs: takeover: interface bound` (the `nfs` daemon only now clears its
  `wait_for_dhcp_lease` ≤30 s wait)
- line 408 — `nfs-fs: mounted 10.42.0.1:/ via v4`
- line 412 — `nfs-fs: registered / (takeover)` — the `portUnregister("/")`+`portRegister`
  root swap (srv.c) lands here

plo spawns `nfs` and `psh` **concurrently** (they are independent syspage programs); psh
does not need NFS to print its prompt, so it wins the race. Any command typed before line
412 resolves against the **dummyfs RAM root**, which is sparse — the syspage list only does
`mkdir /dev`, so there is no `/etc`. Hence `cat /etc/hostname` → ENOENT on the RAM root;
the user retries a second later, "/" is now the NFS export, the file exists → success.

This is the ordinary "prompt appeared before services finished coming up" effect, not a
cache bug. **Lazy-close (f14838c) is exonerated:** on a first-ever access the idle LRU is
empty so its fast path cannot fire, and it never touched `nfs_ops_lookup`.

### Why automation never reproduces it
A scripted cycle usually fires its command *after* line 412 (takeover done) and sees
success. The captured logs that "didn't show it" used `/tmp/...` and `/bin/...` — `/tmp`
exists in the RAM root and `/bin` resolves post-takeover, so neither hit the window with an
NFS-only path. To see the transient you must issue an **NFS-only path** command (e.g.
`cat /etc/hostname`) in the first ~2–3 s after the prompt.

### The one discriminator to keep in mind
If an ENOENT is ever observed landing **strictly after** `registered / (takeover)`, there
is a second, genuine NFS transient and this reframing is incomplete. Absent that, ordering
fully explains it.

## What was done (Option C — always safe)
`nfs_ops.c` `nfs_ops_lookup` masked **any** `nfs_lstat64` failure as bare `-ENOENT`,
discarding the real errno. Changed it to `return nfs_err(rc)` so a transient RPC error
(EIO/ESTALE/ETIMEDOUT) is reported as itself; a genuine missing entry still maps to
`-ENOENT` (libnfs returns `-ENOENT` for `NFS*ERR_NOENT`, and `nfs_err` passes it through).
This is a diagnosability fix, independent of the ordering race; it does **not** make the
first op succeed.

## The proper UX fix — ATTENDED follow-up (NOT done; would regress (e) if done naively)
"Prompt only when root is ready" requires releasing psh on a single **root-settled** event
that fires on BOTH paths:
- success: NFS owns "/", or
- degrade: the takeover failed and the RAM root was kept (#156 (e)).

Gating psh on a takeover-**success** signal would brick the (e) RAM-root fallback (the
signal never fires on the degrade path → psh never starts). The correct shape is to have
the `nfs` takeover process **spawn psh as its final action on each path** (success *and*
degrade) and remove psh from the nfsroot plo block. That is a deliberate boot-sequencing
change touching the fragile plo ordering (cf. the dup-program brick) and must be done
attended with HW validation — deferred.

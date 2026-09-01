# NFS large-exec `err=-12` — root cause + fix (2026-07-12)

## Symptom

Launching a large binary from an NFS root (e.g. `/usr/bin/rpi4-quake`, 17.7 MB)
sometimes failed with:

```
proc: exec '/usr/bin/rpi4-quake' failed (err=-12)
```

`-12` is `-ENOMEM`. The user reported this as a regression: quake (a big binary)
had run from NFS "many, many times" historically.

## What it was NOT (measured, not assumed)

Instrumenting **every** `-ENOMEM` return in the exec / object / map path and
running quake over NFS repeatedly showed none of them firing, which forced a
correction of several plausible-but-wrong theories:

- **Not kmap VA exhaustion / a "whole-ELF-mapped-into-kmap" loader defect.**
  Measured: `VADDR_KERNEL = 0xffffffffc0000000`, kmap `end = VADDR_MAX`, so the
  kernel map has **~1 GB** of VA. Mapping a 17 MB ELF into 1 GB is trivial. The
  loader rework that was scoped ("don't map the whole ELF") would have fixed
  nothing.
- **Not the read-ahead *count*.** `OBJECT_READAHEAD_PAGES` (16, from `8834eaf3`)
  is a red herring. `=1` appeared to "fix" it only because it changed timing;
  `=16` is required — it is the SD large-exec fix (`8834eaf3`: quake main()
  68 s → 5.5 s). Shipping `=1` would **regress the SD image** (the actual
  deliverable) back to the ~68 s "fails-to-start" symptom.
- **Not the `o->pages` allocation, the segment mmaps, the stack mmap, TLS setup,
  or `page_map`.** All instrumented; all silent.

## Actual root cause

An **intermittent NFS READ failure** during demand-paging of the executable:

1. `object_fetchCluster()` issues `proc_read()` RPCs to the nfs-fs server. On a
   transient NFS-client hiccup (libnfs reconnect / reserved-port reflood /
   `NFS4ERR_EXPIRED`) a read returns a negative error.
2. `vm_objectPage()` **swallowed** that error: `if (object_fetchCluster(...) < 0)
   got = 0;` — it discarded the real code and left `*page = NULL`, then returned
   `EOK`.
3. `_map_force()` saw `p == NULL` with `err == EOK` and returned a **generic
   `-ENOMEM`** (the `else if (p == NULL) return -ENOMEM;` arm).

So a transient **read** error was relabelled as an **out-of-memory** error, which
sent the investigation chasing phantom allocation failures. The flake is
timing-dependent: quake-alone often succeeds (confirmed: 40 fps, textured
GLQuake on HDMI over NFS), while an `ls /usr/bin` immediately before quake made
the next read more likely to trip — but even that is ~50/50, not deterministic.

## Fix (this session)

`vm/object.c` `vm_objectPage()` now **propagates the real backing-store error**
instead of inventing `-ENOMEM`:

```c
if (*page == NULL) {
    return (fetchRc < 0) ? fetchRc : -ENOMEM;
}
```

Now a failed NFS read surfaces as its true code (e.g. `-EIO`) at the exec site,
so it can never again masquerade as a loader/OOM bug. Read-ahead stays at `16`.

This is an error-*quality* fix, upstreamable and behaviour-neutral on success
(SD reads never fail, so the SD deliverable is unaffected).

## Residual (separate, pre-existing, attended)

The underlying **intermittent NFS-client read flake** is unchanged and is a
known, multi-session-unresolved issue in the libnfs / nfs-fs layer (see the NFS
large-write / poll-stall notes). It only affects the **NFS-root** boot path used
as a test harness; it does **not** affect the SD image, where quake (24 MB) and
the X11 stack are proven. Making NFS-root large-exec bulletproof is a dedicated
NFS-client-robustness task (retry/reconnect handling), not part of the RPi4
bring-up deliverable.

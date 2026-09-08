# X desktop soak + a per-session GPU-memory residue (2026-09-08)

Motivation: the glamor `DestroyPixmap` chain fix (bug #4) changed behaviour on **every** pixmap
destroy — `damageDestroyPixmap` now runs where it previously never did — so three short trials
could not rule out a slow leak or a double-free. Also, nothing had ever tested the desktop for
demo-length durations.

## Result 1 — the fix holds over a long session

`xsoak2`: `startx_gpu --quit-after 150 deskapps` — the desktop ran **150 s** (1.7× any previous
run), then `--quit-after fired → SIGTERM → session ended → shutting down X`. **0 faults.**

`xleak`: **two full X lifecycles in one boot**, both started all five clients and both exited
cleanly (`session ended` ×2). **0 faults.** So the desktop is restartable — worth knowing for a
live presentation.

## Result 2 — each X session leaves ~15.7 MB behind

`/bin/mem` sampled at three points in one boot (`xleak`):

| point | KB used | map entries |
|---|---|---|
| after boot | 91040 | 193 |
| after 1st X lifecycle | 151320 | 363 |
| after 2nd X lifecycle | 167044 | 448 |

* 1st session: **+60280 KB / +170 entries** — largely one-time (V3D daemon start, GPU buffers,
  X server, shader cache).
* 2nd session: **+15724 KB / +85 entries** — this is the steady-state per-session residue.

Reproduced consistently: an independent run (`xmemdelta`, 60 s session) gave 91044 → 151340 KB
and 196 → 356 entries, matching the first-session figure.

## Cause: the V3D daemon never reaps a dead client's BOs

`sources/phoenix-rtos-devices/gpu/rpi4-v3d/`:

* `rpi4-v3d.c` `v3d_srv_gemClose()` → `v3d_gpu_closeBo(req->handle)` is the **only** path that
  frees a BO, and `v3d_gpu.c:730` shows that free is real (`va_free` + `munmap`).
* `v3d_srv_handleMsg()` dispatches purely on request type. The server stores **no client
  identity** with a BO — no pid, no oid — and has **no disconnect handler**.

So any BO a client does not explicitly `gemClose` stays allocated in the daemon for the
daemon's lifetime. An X server killed with SIGTERM does not walk its GPU objects on the way
out, which fits the observed residue exactly.

**This is pre-existing and independent of the glamor fix** — the ownership model has no reaping
at all, in any code path. That is why no attribution build was run: the mechanism is visible in
the source.

## Impact and recommendation

~15.7 MB per desktop restart on a 4 GB board: restarting ten times costs ~157 MB. **Not a demo
risk** — a presentation starts the desktop once or twice, and 0 faults across every run.

**Do not fix this before the demo.** Adding per-client BO ownership + reaping to the GPU
daemon touches the allocator and the VA page tables (`va_alloc`/`va_free`, `GPUVA_PT_PAGES`),
which is exactly the code whose bugs previously produced render wedges and VA exhaustion. It is
a good post-demo item: tag each BO with its owning client, and free that client's BOs when its
port closes.

## Harness lesson (cost two Pi cycles)

`test-cycle-psh-interact.sh --idle-secs N` is an **idle-detection** timeout — N seconds with no
UART output ends the command. A silent workload (a desktop soak prints nothing after startup)
looks idle, so `--idle-secs 14` ended a 300 s soak after ~14 s. For a soak of duration D:
set `--idle-secs > D` **and** `--max-cmd-secs` just past D, and remember the harness then waits
out that budget for *every* command in the list — so a long soak plus several short probes
overruns the 10-minute Bash cap. Put the soak last, or keep D small enough that
`(D + slack) × n_commands` fits.

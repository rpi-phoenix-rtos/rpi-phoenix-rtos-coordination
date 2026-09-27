# G6 — cross-process GPU synchronisation (dma-buf implicit sync)

Gap **G6** of the [new-lane plan](PLAN.md) ([M3](M3-libdrm-phoenix.md) §4 G6 row,
[M4](M4-xorg-modesetting.md) DRI3/Present, [M5](M5-vulkan.md) G15/G4a, [M6](M6-wayland.md) §8, §15, §16).

**Status (2026-09-27): implemented and host-tested, pending Pi cycle [`g6-sync`](#cycle-g6-sync-bash-timeout-600000).**
Before G6, implicit sync worked only inside one process: a buffer rendered by one process and
then sampled, scanned out or read by another had no fence the consumer could see. M6 §16 put it
as "Weston's direct scan-out of a client buffer has no cross-process fence: tearing expected
until `BO_LAST_FENCE` or G6". G6 closes the **implicit** half (the one Linux dma-buf gives every
program for free). Explicit fence **descriptors** across processes are split off as **G6b** (§5).

Evidence tags as in M6: **[read]** source read at the cited place, **[host]** shown by the host
harness, **[inferred]** reasoning, not yet verified on the Pi.

---

## 1. What was missing, precisely

| Consumer in process B of a buffer rendered by process A | Before G6 | Why |
|---|---|---|
| B's GPU job samples it (Weston's GL renderer compositing a client buffer) | ordered only by luck of the scheduler: SERIAL mode runs the globally oldest *ready* job, so it holds while A's job is not blocked on an in-syncobj; PIPELINE mode round-robins clients per queue and can run B's job first | the server resolves only explicit in-syncobjs (`add_dep`, `v3da_jobs.c`); nothing looked at the BO [read] |
| B flips it on a plane (Weston direct scan-out, G7) | **ungated** | libdrm-phoenix's G13 flip fence comes from the process's *own* submit mirror (`implicit_pending`, `drm_phoenix_v3d.c`), empty for a foreign buffer [read] |
| B maps it and reads with the CPU after `WAIT_BO` | correct for the importer (an imported BO's `WAIT_BO` goes to the server, which knows every client's jobs); **wrong for the exporter** when another process wrote it (its `WAIT_BO` used only its own mirror) | `ioc_wait_bo` fast path [read] |
| B asks the dma-buf for its fences (`DMA_BUF_IOCTL_EXPORT_SYNC_FILE`; Mesa's WSI probe) | `ENOTTY` | `drm_phoenix_ioctl` refused every ioctl type other than `'d'` [read] |

What already existed and makes the fix small: a G4 `/v3dbuf` import **shares the exporter's BO**
(one handle, one GPU VA, one last-use record `v3da_bo_t.last[]`), and a fence
`(slot, queue, gen, seqno)` is global: every client maps the same fence page, and
`rpi4-kms -G` checks any fence by slot and generation (`kms_fence_signaled`, `kms_main.c:400`),
not only the committing client's [read]. So the server already *had* the reservation object;
G6 makes the scheduler honour it and lets other processes read and extend it.

## 2. Design

The render server's per-BO last-use record is this stack's dma-buf reservation object. Every
use is treated as a write, as Linux v3d does (it adds each job's fence to every BO of the job as
`DMA_RESV_USAGE_WRITE`).

**Server (`rpi4-v3d-async`, protocol 4):**

1. **Implicit dependencies** (`v3da_bo_implicit_deps`, called by `v3da_submit` before
   `v3da_bo_mark_use` overwrites the record). A submit that names a BO on which **another
   client** has a pending use gets that use's fence as a dependency of its first job (one per
   `{slot, queue, gen}`, the newest). In both scheduling modes. The submitter's own earlier jobs
   are left alone (its per-queue FIFO orders them; Mesa chains its own queues with syncobjs), so
   a single-process program's scheduling does not change. Cost: six fence-page loads per named
   BO. Overflow of the job's 16 dependency slots is counted and logged (`g6 implicit DROPPED`).
   Consequence used below: the newest use recorded in `last[]` implies every older use of
   another client (it waited for them), by induction.
2. **`V3DA_OP_BO_LAST_FENCE` (23)** — the BO's *pending* last-use fences, newest (by global
   submission order, the new `last_gseq[]`) first, at most 3 per answer
   (`V3DA_BO_FENCES_MORE` if more). Named by the caller's handle or **by buffer name**
   (`/v3dbuf/<id>`, or every import of a `/kmsbuf/<id>` name), so a process that holds only the
   dma-buf descriptor can ask — as anyone holding a dma-buf can on Linux.
3. **`V3DA_OP_BO_ATTACH_FENCE` (24)** — `DMA_BUF_IOCTL_IMPORT_SYNC_FILE`. A zero-length
   **join job** on the caller's CPU queue whose dependencies are the attached fence and the BO's
   pending uses; it is recorded as the BO's last use (queue CPU). Its fence therefore implies
   both, and every existing mechanism — implicit dependencies, `BO_WAIT`, `BO_LAST_FENCE`, a
   gated flip — waits for the attached fence with **no new per-BO state**. `-EBUSY` (more than
   16 dependencies): the library waits for the fence on the CPU instead, which is equivalent
   (a signalled fence adds nothing to wait for).

**Library (libdrm-phoenix):**

4. **`DMA_BUF_IOCTL_EXPORT_SYNC_FILE` / `IMPORT_SYNC_FILE`** on a `/v3dbuf` or `/kmsbuf`
   descriptor, through `drmIoctl` (Mesa's WSI path) and the `--wrap=ioctl` interposer (raw
   `ioctl()`): export = `BO_LAST_FENCE` by name → an emulated sync file (M3 §2.8: a `dup()` of a
   render descriptor + the fence set); import = `BO_ATTACH_FENCE` per pending fence of the
   (in-process) sync file, and the join fence is folded into the process's own mirror of that
   buffer (G13). Any live render connection of the process is used (fences are global, the
   buffer is named by its name). No render connection, or a server before G6: `ENOTTY`, exactly
   as before (Mesa's WSI then falls back as today).
5. **Flip gate for foreign buffers.** `implicit_attach` (atomic and legacy page flip) now tells
   the two kinds of framebuffer apart by the memref's port: the display server's own dumb
   buffers (`port == kms buf_port`) keep the zero-IPC G13 mirror (kmscube, STK, SDL, Weston's
   own GBM output — unchanged); a **G7 import of a render BO** is asked of the render server
   (`BO_LAST_FENCE`, one IPC per flip of such a buffer) and the newest pending fence becomes the
   flip's `IN_FENCE`, which `rpi4-kms -G` gates exactly as for G13. Older fences of *the same
   client on another queue* are waited for on the CPU first (the newest does not imply them);
   other clients' older fences are implied (item 1). No change in rpi4-kms.
6. **`WAIT_BO` of an own, exported BO** now goes to the server (another process may have
   written it); private BOs keep the fast path.

What G6 does **not** add: new descriptors, a namespace, poll support, kms protocol, Mesa
patches. Old binaries keep working and simply get no cross-process gating (§3).

## 3. Protocol and compatibility

`v3da_proto.h`: `V3DA_PROTO_VERSION` 4, `V3DA_PROTO_BO_SYNC` 4; `V3DA_PROTO_BASE` stays 2 and the
server accepts HELLO 2..4. The fence page keeps `version = 2` (layout unchanged). Opcode 23 was
reserved as `V3DA_OP_BO_LAST_FENCE_EXT` in `drm_phoenix_ext.h`, which now points at the proto
header (`#error` if built against an older one).

```
v3da_bo_sync_req_t (40 B):  ns (0 = by handle | V3DA_IMPORT_NS_V3DBUF | _KMSBUF), port, id,
                            handle, flags (0), fence (ATTACH only)
v3da_bo_fences_resp_t (56 B): count, flags (V3DA_BO_FENCES_MORE), f[3] newest first
BO_ATTACH_FENCE reply: v3da_fence_resp_t = the join fence (seqno 0: the fence had signalled)
errors: -ENOENT unknown / withdrawn name, -EINVAL bad ns/port/flags or a fence never handed out,
        -EBUSY (attach: > 16 dependencies)
```

| binary | against the G6 server | against a proto-3 (G4/G7) server |
|---|---|---|
| everything staged today (proto 2: `rpi4-kms-gate`/`-g7 -G`, `v3dasync-ping`, games; proto 3: `drmprobe-g4/-g7`, `weston-g7`, the G4 `weston-simple-egl`) | **unchanged** (HELLO 2 accepted; no G6 op is sent). They do get item 1 for free: their submits now wait for other clients' pending use of a shared BO | — |
| G6 libdrm-phoenix (`build-out-g6`: `drmprobe-g6`, `weston-g6`, `weston-simple-egl-g6`) | HELLO 4; dma-buf ioctls and the foreign flip gate work | HELLO 4 → `EPROTO` → HELLO 2 (reply 3): the dma-buf ioctls answer `ENOTTY`, foreign flips go ungated — the pre-G6 behaviour (host control `g6-negative`, §6) |
| rpi4-kms | unchanged (not rebuilt; the cycle uses `rpi4-kms-g7`) | — |

## 4. Lifetime and ordering

- **Fences outlive their producer correctly.** A dead client's queued jobs are discarded and its
  slot's `completed[]` is set past them (`v3da_jobs_client_gone`), a reassigned slot's fences read
  as signalled (generation), so a consumer can never wait on a dead producer forever.
- **Join jobs** belong to the importing client (its CPU-queue FIFO); they do not pin BOs (they
  touch no memory) and complete at the next event-thread tick after their dependencies. They are
  not counted as NOPs in `DBG_STATS` (the staged `v3dasync-ping` grades `nops_done`).
- **Sync files stay process-local** (a `dup()` of a render descriptor + a table entry), exactly
  as since M3. EXPORT_SYNC_FILE snapshots the fences at call time (Linux semantics).
- **A `/kmsbuf` name imported by several render clients** is one BO *per client* (`v3da_bo_import`);
  `BO_LAST_FENCE`/`ATTACH` by that name cover all of them, but implicit dependencies between
  those separate BOs are **not** added (SERIAL mode orders them by submission, as before). No G6
  target uses this (Wayland buffers are `/v3dbuf`; X DRI3 client pixmaps are copied, M4).
- **Residual:** a dependency list that is full drops implicit dependencies (logged). Two
  writers of one queue in PIPELINE mode whose second submit found the first already signalled
  need nothing; one that found it pending waits for it — the case the old `last[q]` overwrite got
  wrong is exactly what item 1 fixes.

## 5. Covered / not covered

| Covered by G6 | Not covered (G6b or other gaps) |
|---|---|
| Weston's GL renderer sampling a client's `/v3dbuf` buffer waits for the client's render job (server implicit dependency; any Weston binary) | sync-file and syncobj **descriptors across processes**: `linux-explicit-synchronization`, `wp_linux_drm_syncobj`, DRI3 1.4 / Present explicit sync, `vkGetSemaphoreFdKHR` to another process, opaque `SYNCOBJ_HANDLE_TO_FD` → **G6b** (`/v3dsync/<id>` namespace, proto 5; `drm_phoenix_ext.h` 55/56) |
| Weston direct scan-out: a flip of a client buffer waits for the client's GPU work (`weston-g6`) | `poll()` on an emulated sync file (G15) |
| Xorg Present flips of a foreign `/v3dbuf` pixmap, once an Xorg is linked with the G6 library (same code path) | kms out-fence `OUT_FENCE_PTR` (G8) |
| `DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE` on `/v3dbuf` and `/kmsbuf` descriptors; Mesa's WSI probe `wsi_drm_check_dma_buf_sync_file_import_export` succeeds | read vs write fences (every use is a write, as Linux v3d) |
| CPU reads of a shared BO after `WAIT_BO`, by importer and exporter | implicit dependencies between different render clients' imports of one `/kmsbuf` name (§4) |
| — | binaries linked before G6 (no foreign flip gate, dma-buf ioctls `ENOTTY`) |

**Consequence for Vulkan (M5).** With the probe passing, a v3dv program **relinked** with the G6
library switches WSI from driver implicit sync to `dma_buf_semaphore`: `IMPORT_SYNC_FILE` of the
render semaphore into each swapchain image on present, `EXPORT_SYNC_FILE` on acquire
(`wsi_common.c:2871`, `2227`) [read]. On `VK_KHR_display` the images are `/kmsbuf` dumb buffers
imported by the same process, which G6 serves (export by name; the join fence is folded into the
G13 mirror, so the flip still carries it). Not staged in `g6-sync`; the staged m5b `vkcube` is
unaffected. A `vkcube-drm-g6` arm graded against m5c (60.15 fps) is the follow-up (§9).

## 6. Tests

`drmprobe` (`build-out-g6`), four new keys. The Pi build runs the two-process versions (fork +
`socketpair` + `SCM_RIGHTS`); the host build replaces "the other process" with the fake
server's **foreign job** (below).

| key | what it checks |
|---|---|
| `dmabuf_sync_probe` | Mesa's WSI probe verbatim: a 4 KiB render BO, its dma-buf, `EXPORT_SYNC_FILE` (idle: 0 fences), `IMPORT_SYNC_FILE` of that sync file |
| `dmabuf_sync_import` | this process's pending job chain (256 dependent 64×64 clears) → its syncobj as a sync file → `IMPORT_SYNC_FILE` into a shared buffer the chain never touches → `EXPORT_SYNC_FILE` of that buffer holds a pending fence (`pending_after_import=1`); waiting for it means the chain is done (`chain_done=1`, a poll with no wait) |
| `dmabuf_sync_read` | the producer (Pi: a forked child with its own render node; host: the foreign job) renders a 64×64 target — a chain of `-g` (256) dependent clears, colour X, the last one Y — and hands the dma-buf over **at submit time**. The consumer maps it and reads at once (`early_stale=1`: sentinel or X), then `EXPORT_SYNC_FILE` (`pending_at_export=1`), imports the sync file into a syncobj and waits (gallium's `fence_finish` way), reads again: every word Y (`bad_words=0`) |
| `dmabuf_sync_flip` | the producer's second chain names a 1920×1080 LINEAR buffer (colour bands); the consumer imports it on card0 (G7), ADDFB2, and page-flips it **with no in-fence**, as Weston flips a client buffer on a plane: pending at the commit, done by the flip event (`done_at_flip=1`), `flip_us` ≈ the rest of the chain. Host: the foreign job fills the whole buffer, so `bad_words=0` also checks the pixels at the event. ADDFB2 `EINVAL` (a buffer above 1 GiB) grades `gap=1` |

**Host harness** (`DRMPHX_OUT=…/build-out-g6 hosttest/run.sh`). The fake render server
(`mock/fake.c`) now keeps per-BO last-use fences and answers `BO_LAST_FENCE`/`BO_ATTACH_FENCE`
as `v3da_bo.c` does; `drmprobe_host_foreign_job()` is a pending job of a client the library does
not know (its own fence-page slot), recorded as the BO's last use, which fills the BO with a
colour when it completes. As every fake job, it completes only when something waits (a server
wait or a fence-gated commit) — so between hand-over and a wait the buffer is really stale.
Results [host]:

    HOSTTEST libdrm-phoenix checks=134 fails=0 verdict=PASS
    DRMPROBE dmabuf_sync_probe export_errno=0 import_errno=0 idle_fences=0 ok=1
    DRMPROBE dmabuf_sync_import setup=0 import_errno=0 reexport_errno=0 pending_after_import=1 nfences=1 wait=0 chain_done=1 jobs=256 ok=1
    DRMPROBE dmabuf_sync_read producer=foreign export_errno=0 pending_at_export=1 nfences=1 wait=0 early_stale=1 bad_words=0 done_at_read=1 ok=1
    DRMPROBE dmabuf_sync_flip producer=foreign addfb=0 pending_at_commit=1 flipped=1 flip_us=2513 done_at_flip=1 bad_words=0 flipped_back=1 ok=1
    HOSTE2E legacy verdict=PASS / HOSTE2E dri verdict=PASS (only the fake-GPU pixel checks failed, as expected)
    HOSTE2E g4-negative / g7-negative / g7-high verdict=PASS (expected sets updated: the G6 keys need G4, the flip needs G7)

**Fail-then-pass (the negative control, `FAKE_V3DA_PROTO=3`: the G4/G7 server without G6).**
The same probe, the same library; the library falls back to proto 3:

    DRMPROBE dmabuf_sync_probe export_errno=25 import_errno=-1 idle_fences=99 ok=0
    DRMPROBE dmabuf_sync_read producer=foreign export_errno=25 pending_at_export=0 nfences=0 wait=-1 early_stale=1 bad_words=4096 done_at_read=0 ok=0
    DRMPROBE dmabuf_sync_flip producer=foreign addfb=0 pending_at_commit=0 flipped=1 flip_us=15 done_at_flip=0 bad_words=2073600 flipped_back=1 ok=0
    DRMPROBE RESULT pass=39 fail=8 gap=0 failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,dmabuf_sync_probe,dmabuf_sync_import,dmabuf_sync_read,dmabuf_sync_flip, …
    HOSTE2E g6-negative verdict=PASS (the G6 tests fail against a proto-3 server: stale read, ungated flip; the rest as before)

Without G6 the consumer reads all 4096 words stale, and the flip completes in 15 µs with the
producer's job still pending and every one of the 2 073 600 on-screen words stale; with G6 the
read waits and sees the producer's colour, the flip is deferred until the job is done (the fake
kms counts it, `deferred_flips=2`) and shows the producer's pixels. Every other key passes in both
runs (G4 export included: `g4-regressed` check).

**What the host cannot show.** The server's code — implicit dependencies, `BO_LAST_FENCE` over
the real `last[]`, the CPU-queue join job, the event-thread completion — needs the Phoenix kernel
and the GPU: it is proven only on the Pi (`g6-sync` rows 4–7, 10–12). The fake models the
protocol and the fence semantics, not the scheduler. `-Werror` builds: server, library, drmprobe,
Weston (same 9 pre-existing libdrm warnings as the G7 build); `rpi4-kms` still builds against the
new header (not staged).

## 7. Artifacts (built 2026-09-27; sha256, first 16 hex)

| file | sha256 | notes |
|---|---|---|
| `tools/gpu-lane/v3d-async/out-g6/rpi4-v3d-async` | `dc88c71a94b883d8` | server, proto 4; `strings … \| grep -c 'V3DA srv g6'` = 5 |
| `tools/gpu-lane/v3d-async/out-g6/v3dasync-ping` | `bb3a7c7801e7089c` | not staged (the staged proto-2 ping is the compatibility check) |
| `tools/gpu-lane/libdrm-phoenix/build-out-g6/drmprobe` | `2b9cb41737aa29e8` | `strings -a … \| grep -c dmabuf_sync_` = 9 |
| `tools/gpu-lane/libdrm-phoenix/build-out-g6/prefix/lib/libdrm.a` | `dd0d877625c23bcb` | the snapshot the Weston programs link (`weston-drm/build-out-g6/libdrm-snapshot.txt`) |
| `tools/gpu-lane/weston-drm/build-out-g6/weston-stripped` | `27c9a5ccb374a5ea` | `build.sh --no-mesa --libdrm-prefix libdrm-phoenix/build-out-g6/prefix --out <abs>/build-out-g6`; Mesa `build-out-wayland` unchanged; unstripped `weston` `4fa4c5187f484cec` for `addr2line` |
| `tools/gpu-lane/weston-drm/build-out-g6/weston-simple-egl-stripped` | `de051a4ae2b4a2ed` | the client with the G6 library (its exported back buffers' `WAIT_BO` now asks the server); unstripped `b9068ce2148cbb06` |
| `tools/gpu-lane/weston-drm/pi/weston-m6a.sh` | `b5dc486c84c2d116` | new `EGL_CLIENT` knob (default `/bin/weston-simple-egl`, backward compatible); staged under a new name |

Frozen copies under the staged names: `/home/houp/.claude/jobs/c8f1289c/tmp/g6-frozen/` (same sha).

## 8. Staging (coordinator)

New names only; nothing staged before is replaced. Needs §15/§16 of M6 in place (`rpi4-kms-g7`,
`shmsrv`, `/etc/xdg/weston/weston-drm.ini`).

```
G=/home/houp/phoenix-rpi/tools/gpu-lane
EXPORT=/srv/phoenix-rpi4-nfs-gcc16
sudo -n install -m 755 "$G/v3d-async/out-g6/rpi4-v3d-async"                   "$EXPORT/bin/rpi4-v3d-async-g6"
sudo -n install -m 755 "$G/libdrm-phoenix/build-out-g6/drmprobe"               "$EXPORT/bin/drmprobe-g6"
sudo -n install -m 755 "$G/weston-drm/build-out-g6/weston-stripped"            "$EXPORT/bin/weston-g6"
sudo -n install -m 755 "$G/weston-drm/build-out-g6/weston-simple-egl-stripped" "$EXPORT/bin/weston-simple-egl-g6"
sudo -n install -m 755 "$G/weston-drm/pi/weston-m6a.sh"                        "$EXPORT/bin/weston-m6a-g6.sh"
cmp "$G/v3d-async/out-g6/rpi4-v3d-async" "$EXPORT/bin/rpi4-v3d-async-g6"
cmp "$G/libdrm-phoenix/build-out-g6/drmprobe" "$EXPORT/bin/drmprobe-g6"
cmp "$G/weston-drm/build-out-g6/weston-stripped" "$EXPORT/bin/weston-g6"
cmp "$G/weston-drm/build-out-g6/weston-simple-egl-stripped" "$EXPORT/bin/weston-simple-egl-g6"
cmp "$G/weston-drm/pi/weston-m6a.sh" "$EXPORT/bin/weston-m6a-g6.sh"
cmp /home/houp/.claude/jobs/c8f1289c/tmp/g7-frozen/rpi4-kms-g7 "$EXPORT/bin/rpi4-kms-g7"   # unchanged since m6h
```

Preconditions as M6 §9: netboot image ≥ build 11 (the E1 kernel), no GPU app, no X, no old-lane
`rpi4-v3d`.

### Cycle `g6-sync` (Bash `timeout: 600000`)

**Question:** does the render server keep one reservation object per shared BO across processes —
a consumer process that waits on a dma-buf's exported fences reads the producer's finished frame,
a page flip of another process's buffer is held until that process's GPU work is done, Mesa's
WSI probe passes — and does Weston then composite and directly scan out a GPU client with those
waits in place (no tearing on direct scan-out)?

```
./scripts/test-cycle-psh-interact.sh --label g6-sync --idle-secs 45 --max-cmd-secs 150 \
    --hdmi-dense-on 'DRMPROBE kms_flip start|WESTONDRM client start' -- \
    "/bin/rpi4-v3d-async-g6 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96" \
    "/bin/shmsrv -v" \
    "/bin/drmprobe-g6 -n 30" \
    "export WESTON=/bin/weston-g6" \
    "export EGL_CLIENT=/bin/weston-simple-egl-g6" \
    "/bin/bash /bin/weston-m6a-g6.sh gl egl noinput" \
    "/bin/shmsrv -s" \
    "/bin/kmstest-poll stats" \
    "/bin/v3dasync-ping stats"
```

Only the render server, the probe, Weston and the client move against `m6h-g7`
(`artifacts/rpi4b-uart/rpi4b-uart-20260927-144453-m6h-g7.log`: drmprobe `pass=44`, Weston direct
scan-out of the client, 45 fps, `flips=907 deferred=285`). Grade:

```
grep -a -E '^(DRMPROBE|V3DA srv (ready|bufns|export withdrawn|g6)|KMS (srv (ready|flipstat)|v3d|import|scanout|fb FAIL)|WESTONDRM|MESA|KMSTEST|V3DAPING|SHMSRV stats) |frames in|caught signal|Failed to' \
    artifacts/rpi4b-uart/rpi4b-uart-*-g6-sync.log
./scripts/uart-summary.sh g6-sync
```

Allow ~1.3 % UART line corruption (re-read, don't count); EL0 dumps print twice. `<c>` = a render
client id, `<s>` = a fence-page slot (= client id − 1), `<h>` = a render handle / `/v3dbuf` id.

**Predictions:**

| # | Line / observation | Predicted | If instead… |
|---|---|---|---|
| 1 | `V3DA srv bufns … registered=1 (G4)`, `V3DA srv ready … proto=2..4 bufns=1` | once | `proto=2..3`: the G4 server was started (staging, `cmp`) — stop |
| 2 | `KMS v3d connect=1 …` (rpi4-kms-g7, proto 2, HELLOs the proto-4 server); `KMS srv ready … proto=1..2 import=v3dbuf` | as m6h | `connect=0 why=hello`: the HELLO range check — compatibility blocker |
| 3 | drmprobe rows up to `prime_export_xproc … ok=1` exactly as m6h (44 keys) | unchanged | a regression outside G6: compare with the m6h log |
| 4 | `DRMPROBE dmabuf_sync_probe export_errno=0 import_errno=0 idle_fences=0 ok=1` | Mesa's WSI probe passes | `export_errno=25`: the library fell back to proto 3 (row 1) or the probe's `drmIoctl` never reached the dma-buf branch (a pre-G6 drmprobe: `strings`) |
| 5 | `DRMPROBE dmabuf_sync_import setup=0 import_errno=0 reexport_errno=0 pending_after_import=1 nfences=1 wait=0 chain_done=1 jobs=256 ok=1`; server `V3DA srv g6 attach client=<c> ns=2 id=<h> handle=0x0 bos=1 fence=<s>/1/<n> deps=1 join=<j> n=1` | the join job on hardware | `pending_after_import=0`: the chain had finished before the import (note, not a fail; the join then answered seqno 0 and no `attach` line appears); `chain_done=0` with `wait=0`: the join job completed before its dependency — **blocker** (read `job_ready` on the CPU queue); a hang here: the join job never completed (event-thread tick) — **blocker** |
| 6 | `DRMPROBE dmabuf_sync_read producer=child jobs=256 chain_us=<T> export_errno=0 pending_at_export=1 nfences=1 wait=0 early_stale=1 bad_words=0 done_at_read=1 ok=1`, T ≈ 20–150 ms; server `V3DA srv g6 last_fence client=<parent c> ns=2 id=<h> handle=0x0 pending=1 newest=<child s>/1/<n> more=0` | **a second process reads the producer's finished frame after waiting on the dma-buf's fences** | `pending_at_export=0 early_stale=0`: the chain was done before the hand-over — the race was not provoked (not a fail; re-run with `-g 2048`); **`bad_words>0` with `wait=0`: the exported fence did not cover the producer's job — blocker**; `export_errno=2` (+ no `last_fence` line): the name was not resolvable (`bo_by_export`) |
| 7 | `DRMPROBE dmabuf_sync_flip producer=child jobs=256 chain_us=<T2> addfb=0 pending_at_commit=1 flipped=1 flip_us=<F> done_at_flip=1 flipped_back=1 report=1 ok=1`, F ≥ 17 ms and of the order of T2; `KMS import … scanout=1`, `KMS scanout import … (first flip)`; HDMI (dense snapshots): a banded gradient for ~1 s (not m6h's 8 bands) | **a flip of another process's buffer is held until its GPU work is done** | `done_at_flip=0`: the flip was not gated — a `V3DA srv g6 last_fence` line for this id shows the library asked (then rpi4-kms: started without `-G`? `KMS srv ready … v3d=0`); no such line: the library took the G13 path (port check in `implicit_attach`); `addfb=-22` + `KMS fb FAIL … why=above_1g`: graded `gap=1` |
| 8 | `KMS srv flipstat client=<drmprobe's> … deferred=D …` with D ≥ 1 (m6h: `deferred=0`) | the G6 flip was deferred by the gate | D = 0 with row 7 passing: the chain was done before the commit (`pending_at_commit=0`) |
| 9 | `DRMPROBE RESULT pass=48 fail=0 gap=0 failed=- … verdict=PASS` (m6h's 44 + 4); `pass=47 … gap=1` with row 7's `above_1g` | as listed | any `failed=` key: its row |
| 10 | `WESTONDRM start renderer=gl client=egl weston=/bin/weston-g6 … egl_client=/bin/weston-simple-egl-g6`; Weston up as m6h; `WESTONDRM client start: /bin/weston-simple-egl-g6` | the exports reached the script | `weston=/bin/weston` or `egl_client=/bin/weston-simple-egl`: psh's `export` did not reach bash — then pre-G6 binaries ran; grade rows 11–13 as m6h |
| 11 | server `V3DA srv g6 implicit client=<weston's c> queue=0 deps=1 newest=<client s>/1/<n> n=1..4` while Weston composites with GL (before direct scan-out takes over) | **Weston's composition waits for the client's render** (server implicit dependency) | none: every client job had finished before Weston's submit (SERIAL + the client's flush before commit) — not a fail; `g6 implicit DROPPED`: a dependency list full — note |
| 12 | direct scan-out as m6h (`KMS scanout import fb=<f> … (first flip)` for client buffers), plus `V3DA srv g6 last_fence client=<weston's render c> ns=2 …` (the server prints the first 16 answers with a pending fence; drmprobe uses about 4) and at Weston's/the client's exit `V3DA srv g6 stats client=<c> queries=Q pending=P …` with Q ≈ the direct-scan-out flips | Weston's library asks for the client buffer's fence before each such flip | `queries=0`: weston-g6 not running (row 10) or every flip was a GL composite (no scan-out: then as m6h row 10) |
| 13 | Weston's `KMS srv flipstat … flips=N deferred=D` with D/N above m6h's 285/907 | direct-scan-out flips now wait for the client's render when it is pending | D/N unchanged and row 12's `pending=0`: the client's frames were always done at commit (no gate needed) — note |
| 14 | `N frames in 5 seconds: X fps` with X ≥ 40 (m6h: 45); HDMI: the rotating triangle full screen, **no torn or partial triangles** | the gate costs no frame rate; tearing (m6h's expected risk) gone | X < 30: the gate serialises client and scan-out (a flip waits a full client frame) — read `flip_us`-style timings from `KMS srv flipstat` `q2a_us_avg` |
| 15 | exit: `KMS import released …` per import, `V3DA srv export withdrawn … live=0`, `weston exited rc=0`; `SHMSRV stats live=0`, `KMSTEST stats … bos=0 exports=0`, `V3DAPING stats … bos_live=0 parked=0 … verdict=PASS` (the staged proto-2 ping HELLOs the proto-4 server; join jobs are not counted as NOPs) | no leaks, compatibility | `V3DAPING … nops_done` mismatch: join jobs counted as NOPs (server stats) |
| 16 | fault dumps | 0 kernel, 0 EL0 | EL0 in the server: `aarch64-phoenix-addr2line -f -e tools/gpu-lane/v3d-async/out-g6/rpi4-v3d-async <pc>`; in drmprobe: the unstripped `build-out-g6/drmprobe`; in Weston: `weston-drm/build-out-g6/weston` |

**Decides:** rows 4–9 PASS = G6 closed on hardware (the server's reservation object, the join
job, the dma-buf ioctls, the foreign flip gate). Rows 11–14 PASS = Weston's composition and direct
scan-out of a GPU client are synchronised across processes; M6 §8's "tearing expected" row is
closed. Then: `vkcube-drm-g6` (§9) and, for explicit sync, G6b.

## 9. Risks only the Pi can show

- **Server paths never run on hardware:** the CPU-queue join job (kick when ready, completion on
  the next tick, `hw_submitted`/`hw_completed` accounting on the CPU queue), implicit
  dependencies in a real schedule, `last_gseq` ordering.
- **The race may not be provoked:** 256 chained 64×64 clears may finish before the parent's
  export (row 6 `pending_at_export=0`). The test stays correct, but proves less; `-g` widens it.
- **Weston per-flip IPC:** one `BO_LAST_FENCE` per direct-scan-out flip (~30 µs, E5); a CPU wait
  only for the same client's work on another queue (normally none).
- **Implicit dependencies change Weston's schedule:** its composite job now waits for a pending
  client job in PIPELINE mode too (in SERIAL it effectively did already). A client that never
  finishes a job would now stall Weston's composition as it stalls Linux's (the watchdog still
  ends a wedged job).
- **Vulkan WSI path switch** for relinked v3dv programs (§5): `vkcube-drm-g6` arm needed before
  any Vulkan binary is relinked with this library — grade against m5c 60.15 fps, expect
  `DRMPHX dmabuf … name=DMA_BUF_IOCTL_EXPORT_SYNC_FILE rc=0` / `IMPORT … rc=0` trace lines per frame.

## 10. Files

`tools/gpu-lane/v3d-async/{v3da_proto.h, v3da.h, v3da_bo.c, v3da_jobs.c, v3da_main.c}` (server:
implicit dependencies, `BO_LAST_FENCE`, `BO_ATTACH_FENCE`, counters line),
`tools/gpu-lane/libdrm-phoenix/{src/xf86drm_phoenix.c, src/drm_phoenix_v3d.c, src/drm_phoenix_kms.c,
src/drm_phoenix_priv.h, src/drm_phoenix_wrap_ioctl.c, include/drm_phoenix_ext.h, drmprobe/drmprobe.c,
hosttest/{run.sh, e2e_main.c, mock/fake.c}}`, `tools/gpu-lane/weston-drm/pi/weston-m6a.sh`
(`EGL_CLIENT`). Server log lines: `V3DA srv g6 last_fence …` (the first 16 with a pending fence), `g6 attach …`,
`g6 implicit …` (the first 4 each), `g6 implicit DROPPED …`, `g6 stats …` (when a client closes after any G6
activity). Library trace (with `DRMPHX_TRACE`): `DRMPHX dmabuf fd=… path=… name=… rc=… fences=…`.

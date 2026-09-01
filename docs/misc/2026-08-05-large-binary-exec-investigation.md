# Large-binary NFS-exec reliability — mechanism + fix plan (F1)

Investigation 2026-08-05 (non-Pi kernel code analysis + `readelf` of the binaries).

## Symptom
Large userspace binaries intermittently FAIL to exec over the netboot NFS root — the process
prints NOTHING (not even its banner), ~50% of boots. `yquake2` (47 MB) flaky; `sdl2-gltest`
(18 MB) reliable. Failure is EARLY (before program output) = exec/process-load path.

## Mechanism (confirmed)
Phoenix commits **every PT_LOAD segment, the BSS, and the full PT_GNU_STACK eagerly, page-by-
page, at exec** (MMU targets: `process->lazy=0`, proc/process.c:222). `readelf` shows the sole
structural difference:

| binary | text | **BSS (MemSiz−FileSiz)** | PT_GNU_STACK | eager anon commit |
|---|---|---|---|---|
| sdl2-gltest (reliable) | 17.4 MB | **0.23 MB** | 32 MB | ~32.2 MB |
| yquake2 (flaky) | 18.3 MB | **26.5 MB** | 32 MB | ~58.5 MB |

Both link the SAME 32 MB stack (so stack is NOT the differentiator — the earlier assumption was
wrong). The only difference is the **26.5 MB eager BSS commit**: `process_load64` (process.c:594)
does an anon `vm_mmap` (:674) then `hal_memset(vaddr+filesz, 0, whole bss)` (:678); `_vm_mmap`
with lazy=0 force-commits every page (vm/map.c:625-630) → ~14,600 pages + one `anon_t` each
(vm/amap.c) under `map->lock` during exec.

**It is NOT a returned -ENOMEM** (that prints `proc: exec ... failed (err=)` at process.c:1254
via the direct-MMIO kernel console, hal/aarch64/generic/console.c:32-58 — verified reachable) and
**NOT a userspace fault** (would dump at vm/map.c:828). The failing logs stop DEAD at the shell
command-echo with neither print → a **silent HANG** in the eager load path. Managed RAM is 3.81 GB
(boot log), so 58 MB is trivial — NOT total exhaustion.

Most-likely trigger (needs Pi confirmation): the long eager-commit window (~14.6k pages under
map->lock, 4-core SMP + concurrent per-page NFS text reads on the known-flaky netboot NFS)
intermittently stalls. yquake2's far larger window raises the hit rate.

**status.md correction:** the "`-ENOMEM at process_load:704` whole-file map" residual is STALE —
current process_load forces only ELF HEADERS (process_forceElf64Headers, process.c:719-746,793).

## Fix / mitigation plan (ranked)
1. **[cheap, low-risk, DO NOW] Cut yquake2's linked stack 32 MB → ~4 MB.** Both binaries eagerly
   commit 32 MB; gltest proves ~32 MB is reliable. yquake2 stack 4 MB → total eager commit
   ~30.5 MB, below gltest's reliable ~32 MB. If the trigger scales with eager-commit work, moves
   yquake2 into the reliable regime. Quake2 stack use is a few MB. Set via `-Wl,-z,stack-size=`.
   No kernel change. **Applied 2026-08-05** (build-yquake2-phoenix.py 32MB→4MB); efficacy needs a
   multi-boot Pi check on healthy netboot. (Reducing BSS is impractical — engine static arrays.)
2. **[kernel, medium-risk, proper fix] Lazily demand-page exec-time BSS/stack.** Set lazy=1 around
   the process_load64 anon maps + the stack vm_mmap (mirror the header-map toggle process.c:777-780)
   AND remove the full-BSS hal_memset (:678) — amap_page (:299) zero-fills fresh anon on fault, so
   keep only the sub-page tail zero. Makes exec near-O(1) in BSS/stack size → fixes large-binary
   exec generally. Risk: validates the anon SMP demand-fault path for large regions; core-exec →
   `--scope core` + full boot-verify (defer until netboot healthy).
3. **[kernel, lower-risk] Drop the redundant full-BSS hal_memset** (process_load64:678) even keeping
   eager commit — eager amap_page already returns zeroed pages; zero only the sub-page tail. Shrinks
   the in-kernel memset window under map->lock.
4. Alloc-retry: NOT indicated (silent hang, no printed -ENOMEM).

## Pi instrumentation for a definitive diagnosis (future attended turn)
Bracket prints (kernel lib_printf → UART) in process_load (process.c:749): on entry, after
process_forceElf64Headers, after process_load64, after the stack vm_mmap — with vm_pageGetFree()
+ kmalloc allocsz each stage → localizes hang to text-read vs BSS-commit vs stack-commit vs crt0.
Add a page-progress counter in the _vm_mmap eager loop (map.c:625) to see a stall inside _map_force
(anon vs file/NFS page via vm_objectPage→proc_read). Also timestamp NFS proc_read per-page during
exec (netboot NFS is independently flaky — MEMORY.md poll-stall/reconnect).

Refs: proc/process.c:222,594-683,749-834,1254; vm/map.c:573-633,733-789,828; vm/amap.c:228-318;
vm/page.c:41; hal/aarch64/generic/console.c:32-58.

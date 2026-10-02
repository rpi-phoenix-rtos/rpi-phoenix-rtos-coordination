# Demand-zero anonymous memory on MMU builds (2026-10-02)

**Branches:** kernel `vm-anon-demand-zero` (`d2284b32`), tests `vm-anon-demand-zero` (`fffa332`). Both branch from `master` and are unmerged. Worktrees are under `/home/houp/.claude/jobs/c8f1289c/tmp/wt-lazy/{kernel,tests}`.

**Status:** syntax-check is CLEAN for every touched file (a full compile under the real flags, `-Werror`, to `/dev/null`). Afterwards, the `.buildroot/` copies were restored to the `sources/` master versions. No image has been built and no Pi cycle has been run.

## Why

On MMU builds, `_vm_mmap()` used to call `_map_force()` on every page of a new mapping unless `process->lazy` was set, and `process->lazy` is 0 on every MMU process. So anonymous memory was never demand-paged. WebKit and mimalloc reserve 64 MiB arenas and a 32 MiB JIT pool, and every page of them was allocated and zeroed at mmap time. The `f9-anonsz` log shows the cost: `jsc` takes 215 MB resident on LLInt and 386 MB with the JIT, almost all of it arena that is never touched.

## Policy

A mapping is **demand-zero** when all of the following hold (`vm/map.c:623` `_map_isDemandZero()`, called at `:699`):
- it has no backing object (`o == NULL`), so files, `MAP_PHYSMEM` and `MAP_CONTIGUOUS` are excluded (the last two become objects in `syscalls_sys_mmap`);
- it is not in the kernel map;
- it was made in process context, and that process is not `anonEager`;
- it is neither `MAP_UNCACHED` nor `MAP_DEVICE`.

Everything else stays **eager**, exactly as before.

`process->lazy` is left alone. It also switches `vm_mapCopy` (`remap_readonly`) and `vm_mprotect` into a full lazy copy-on-write mode that has never run on MMU, and `process_load` uses it as a scoped hack for the ELF-header mapping in the kernel map.

How a fault on a demand-zero page is served:
- `map_pageFault` → `vm_mapForce` → `_map_force` → `amap_page`. On a NULL slot it allocates a page, zeroes it through a kernel alias (`vm/amap.c:306`) and stores the anon.
- There is no shared zero page. A read fault therefore allocates too, and meminfo counts that page.
- **New:** an anonymous page is entered with the entry's full `e->prot`, whatever access faulted it (`vm/map.c:941`). A read followed by a write takes one fault, and a page that `vm_mapPopulate()` made resident can never fault again on a write. A page still shared with another process is copied first by `amap_page` (refs > 1 and a write).

## Correctness fix the policy needed: a read fault in a copy-on-write anon entry

`vm/map.c:950`. Before this change, `_map_force` gave a `MAP_NEEDSCOPY` entry its own amap only on a **write**. A **read** fault on a NULL slot of an amap still shared after fork made `amap_page` zero-fill a page into the *shared* amap, with refs 1. A later write by the other process copies the amap pointers, finds refs == 1 and does not copy, so it writes straight into the first process's page.

Eager memory never had NULL slots, so this could not happen before. It now can, so an anonymous `NEEDSCOPY` entry gets its own amap on any access (UVM does the same). For writable entries, upgrading the fault to the full `e->prot` already covers this. The new rule covers read-only and `PROT_NONE` entries. Tested by `fork_first_touch_readonly`.

`MAP_NEEDSCOPY` itself is still cleared **only by a write**. After a read, the entry may map a page it shares read-only, for example the child's eager copy of a read-only resident page. `vm_mprotect()` relies on the flag being set to copy such a page before making it writable. Clearing the flag on a read-time `amap_create` broke `test-mprotect pages_in_parent_copied`: the child's later write landed in the parent's page. That was caught in review and fixed before the commit.

## Audit

Verdicts used below:
- **OK**: unaffected, or already handles absent pages.
- **FIXED**: changed on this branch.
- **EAGER**: deliberately kept eager.
- **NOTE**: behaviour change worth knowing about.

### Kernel paths that touch user memory or user physical addresses

| Site | What happens with a page that was never touched | Verdict |
|---|---|---|
| `proc/msg.c:203/233/243` `msg_map()` | `pmap_resolve()` on the sender's page returned 0. **Physical page 0 was then mapped into the server** (`:233`), and the partial pages were copied through a kernel view of PA 0 (`:203/:243`). That is every `read()`/devctl into a fresh `malloc` buffer. | FIXED: `vm_mapPopulate(srcmap, data, size)` at `:168`, in receiver context with no lock held. A populate failure (ENOMEM) refuses the message. |
| `vm/map.c` `vm_mapBelongs()` (`:1683`) | The checked buffer may be written while a lock is held that the fault path also needs: `vm_mapinfo()` (`:1716`) writes `emap[]` holding the target's `map->lock`, which is a **self-deadlock** when the target is self; `vm_pageinfo()` (`vm/page.c:378`) holds `pages_info.lock`, which `vm_pageAlloc` needs; `proc_threadsInfo()` (`proc/threads.c:3362`) holds `threads_common.lock`. | FIXED: one place for all 96 callers. Every buffer a syscall validates is populated, which matches what eager mmap left behind. The result is ignored: if populating fails, the later access takes the same fault and reports it. |
| `syscalls.c:1364` `va2pa()` | Returned 0 plus the offset for a lazy page. | FIXED: populates the one page first. An address in no mapping still gives 0 (contract pinned by `tests/libc/misc/va2pa.c`). |
| `proc/threads.c:826` `threads_canaryInit()` | Writes the user-stack canary **under `threads_common.spinlock`**. A fault there cannot be served, and the box wedges. | FIXED: `proc_threadCreate` populates `[stack, stack+stacksz)` first (`:846`). The main thread's stack is populated in `process_load` (`proc/process.c:877`). |
| `proc/threads.c:2123` `hal_cpuPushSignal()`, frame below SP | Writes the signal frame under the scheduler spinlock. The frame routinely falls in a page below SP that was never touched. | FIXED by keeping stacks populated (as above). `_threads_signalFrameFits` already confines the frame to the recorded stack or altstack; its comment wrongly claimed stacks were demand-paged and is corrected. |
| `proc/threads.c:726` scheduler canary check | Reads `ustack` under the spinlock. | OK: that page is populated and was written by `canaryInit`. |
| `sigaltstack` (`proc/threads.c` ~2337) | The frame lands on the altstack under the spinlock. | FIXED: `vm_mapBelongs()` per page, which now populates. |
| `proc/userintr.c:78` user interrupt handler | `ui->f` runs **at IRQ level** in the driver's address space and may touch any `.bss`, heap or ring page. A fault there enters `map_pageFault` with the interrupted process's map and takes mutexes. | EAGER: `userintr_setHandler` sets `process->anonEager` and populates the whole map before the handler is registered (`:118`). Every later anon mmap of that process is eager. The flag is set before populating, and `_vm_mmap` reads it under the map lock, so no mapping slips through. Not inherited by fork (interrupt handlers are not copied, `proc/resource.c` `proc_resourcesCopy`). |
| `proc/process.c` `process_tlsInit()` (`:2201`) | The kernel `memcpy`s tdata into a fresh anon mapping. A fault there could be served, but the whole mapping is used anyway. | FIXED: populated. |
| `proc/process.c:608/717` ELF `.bss` past the file page | Previously eager. The comment at `:615` explained it was not demand-paged. | NOTE: now demand-zero, which is the intended win. The kernel only `memset`s the partial file-backed page (`:625/:725`). Comment updated. |
| `proc/process.c:830` ELF header in the kernel map | Uses the `lazy` toggle; kernel map. | OK, unchanged. |
| `vm/map.c:1556` `vm_mapCopy()` (fork) | The eager child loop called `_map_force` on every page and so **materialised every untouched page** (64 MiB arena → 64 MiB per fork). | FIXED (`:1604`): skip anon pages the parent has no frame for. Resident pages are copied as before, because the parent's PTEs stay writable. A non-resident page that does have data (`PROT_NONE`) stays in the shared amap, counted by `getanons`, and is copied on first access via `NEEDSCOPY`. |
| `vm/map.c:1255` `vm_mprotect()` `needscopyNonLazy` | Forced every page of a COW entry with the new protection, allocating untouched ones. | FIXED (`:1391`): force only resident anon pages; object entries are unchanged. Absent pages fault later under the new `e->prot`. The resident-RO-shared case from commit `84b66c29` still forces a copy. |
| `vm/map.c` `vm_mprotect()`, non-COW | `pmap_resolve` gives 0, so the page is skipped. | OK: the later fault uses the new `e->prot`. |
| `vm/map.c:476` `_vm_munmap()` of untouched pages | `amap_putanons` on NULL slots or a NULL amap is a no-op; `pmap_remove` of a range with no PTEs is fine. | OK. Tested by `munmap_untouched`. |
| `vm/map.c` `_map_map` amap reuse and `amap_clear` (MAP_FIXED into a hole) | The slots of a reused amap are cleared. | OK. Tested by re-mapping the hole in `munmap_untouched`. |
| entries with `amap == NULL` that live long (merge, split, `aoffs` after a front munmap) | `amap_create(NULL, &aoffs, size)` resets `aoffs`; every merge and split condition already handles NULL. | OK. |
| `vm/map.c` `map_entryAnonSize()` (`:1699`) | An untouched anon entry has no amap and reported `(size_t)-1`, which WebKit's pressure handler would read as huge. | FIXED: 0 for anonymous entries; -1 is kept only for object-backed ones. |
| meminfo free/anonsz accounting | `anonsz` counts amap slots, so only resident pages; `page.free` counts allocated frames. | OK. |
| `proc/userintr.c:148` (riscv) `pmap_resolve` of handler code | Code pages are object-backed, so eager. | OK. |
| `vm/object.c` `vm_objectExport` / `vm_mapObjectRange` (shm, `memExport`) | Only `MAP_CONTIGUOUS` objects can be exported (`e->object != NULL`). | OK (EAGER). |
| futex (`proc/futex.c`) | Reads the user word under a mutex; the user word is validated, so populated. | OK. |
| EL1 user-copy faults outside locks (posix AF_UNIX, pipes, `msg_ipack`/`opack`) | Served by `map_pageFault` (the PROT_USER fix). The buffers are validated, so populated anyway. | OK. The `map_usercopyFaultCount` storm canary should stay quiet. |
| `GETFROMSTACK(ustack)` syscall args, `hal_cpuSigreturn` | Read from the user stack near SP, which is resident. | OK. |

### Userspace consumers

From a separate read-only audit of every `va2pa`/mailbox site:
- **No Pi 4 driver is at risk.** Every DMA buffer or mailbox page is `MAP_CONTIGUOUS` and/or `MAP_UNCACHED`, so it stays eager. The sites are xhci/usb `mem.c`, bcm-genet `dmammap`, rpi4-audio, rpi4-vcmbox, rpi4-hci, the pcie server, kms_bo, v3da bo/hw/sched, bcm2711-emmc, rpivid, and the tools/ probes. Imports through `MAP_SHARED` exports touch each page and check contiguity.
- The only multi-page-from-one-PA users (genet rx/tx pools, audio ring, emmc dmaBuffer, the v3d page table, rpivid, the kms pool) are all CONTIGUOUS.
- Every Pi driver that registers an interrupt is now fully eager through `anonEager` anyway.

Remaining notes:
- **va2pa failure value.** On aarch64 a failed `va2pa` returns `0 + offset`, never -1. So the `== -1`/`(addr_t)-1` checks in `kms_bo.c:79,506`, `v3da_bo.c:877`, `v3da_lowmem.h:166`, `rpi4-vcmbox.c:369` and `pcie.c:247` are dead code. This predates the change; follow up by comparing the page-aligned value against 0.
- **dummyfs** (`phoenix-rtos-filesystems/dummyfs/memory.c:117`) mmaps anonymous chunks and counts them against `DUMMYFS_SIZE_MAX`. Its "preallocate to avoid ENOMEM" (`dummyfs.c:1059`) no longer actually reserves frames. dummyfs registers no interrupt, so its memory is demand-zero, and out-of-memory moves from ENOSPC to a SIGSEGV at touch time. This is low risk with 4 GB. Fix if needed: `MAP_CONTIGUOUS` for its one-page chunks.
- **Overcommit** (NOTE, design consequence): `mmap()` of anonymous memory no longer fails up front when memory is short. A touch that finds no free page fails in `vm_mapForce` with -ENOMEM and kills with SIGSEGV (`SEGV_ACCERR`). This is Linux-like overcommit without an OOM killer; a commit limit can be added later if needed.
- Thread stacks (libphoenix `pthread.c:366`, plain anon mmap) are **populated in full by the kernel** at `beginthreadex`, the same cost as today. A v2 could populate only the top and canary pages once signal delivery can defer a frame that would fault.

### Other architectures

The change applies to every MMU target (ia32, armv7a, riscv64, sparcv8leon). They share `map_pageFault`, but their kernel-mode user-copy fault handling has only been exercised on aarch64. It is not tested elsewhere.

## Tests (`phoenix-rtos-tests` `mem/test_mmap_demand_zero.c`, binary `test-mmap-demand-zero`, in `mem/test.yaml`, NOMMU targets excluded)

**Fail on the eager kernel:**
- `reserve_costs_no_memory`: 64 MiB mmap, free drop < 8 MiB, entry anonsz ≤ its size − 64 MiB;
- `touched_pages_counted_and_zero`: anonsz == touched pages, contents zero;
- `munmap_untouched`: anonsz after munmapping the untouched middle; re-map with MAP_FIXED gives zero pages; a big partly-touched mapping gives its memory back;
- `mprotect_untouched`: RO read, RW write, PROT_NONE and back; anonsz == 3 pages;
- `fork_partly_touched`: the child has exactly the 8 written pages and sees zero in the rest; writes stay private;
- `va2pa_untouched_page`: va2pa populates only that page, and the PA is stable after a write;
- `bss_is_demand_zero`: 8 MiB `.bss`, anonsz < 4 MiB.

**Guards that also pass on the eager kernel:**
- `fork_first_touch_writable` and `fork_first_touch_readonly`: the amap is still shared after fork; first touch in either process, then a write by the other;
- `file_io_through_untouched_buffer`: read at a page-crossing offset into a fresh 1 MiB mmap; write from untouched memory reads back as zeros;
- `meminfo_into_untouched_buffer`: the deadlock canary;
- `signal_on_untouched_thread_stack` and `signal_on_untouched_altstack`: a wedge canary.

## Pi gate (coordinator)

1. Build with **`--scope core`** (a kernel change). The change adds no new strings, so check the kernel ELF for the new symbol instead of using `strings`: `aarch64-phoenix-nm <kernel elf> | grep vm_mapPopulate`. Also confirm the kernel ELF hash differs from build 30's.
2. Build the tests repo and run `test-mmap-demand-zero` (expect 13/13, or 12 + 1 ignored if altstack is unsupported). Also run the existing `test-mprotect` (fork/COW/anonsz split), `test_mmap_new`, `test-libc-unix-socket`, the libc `va2pa` group, and the pthread/signal libc groups.
3. Smoke, because driver and IPC paths are at risk:
   - boot to `(psh)%` over netboot and over SD;
   - NFS root read/write (exec large binaries: quake, python3, `jsc`);
   - lwip/genet up and DHCP;
   - USB keyboard and mouse (xhci);
   - WiFi DHCP lease (#91);
   - audio `/dev/audio0` playback;
   - GPU: kmscube or GLES demo, X desktop (`startx_gpu`), vkQuake and STK frame;
   - HEVC decode (rpivid);
   - the browser: WebKit WebProcess loading a page.
4. Memory win to record: run `jsc` LLInt and JIT micro benchmarks plus one WebProcess. Compare RSS (`sum(anonsz)` from meminfo / `ps`) and free pages against build 30. Expected: `jsc` drops from 215/386 MB to roughly what it actually touches, likely tens of MB rather than hundreds, because 64 MiB arenas and the 32 MiB JIT pool stop counting. Each WebProcess saves the same, and fork() of a large-arena process stops materialising the arenas. Driver processes (anonEager) are unchanged.
5. Watch the UART for `vm: N kernel user-copy page faults` (storm canary), `msg:` refusals and EL1 Data Aborts.

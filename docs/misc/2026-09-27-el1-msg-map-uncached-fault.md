# EL1 alignment-fault storm in `msg_map` (cycle `mig-q3`, 2026-09-27)

**Status:** root cause found and fixed in the kernel, not yet on the Pi (§8).
The storm and the later refusals were a **kernel lookup bug**: `vm_mapFlags`
gave an unaligned payload in ordinary memory the flags of the mapping *above*
it. The payload was the render server's stdout buffer, which is ordinary RAM.
§3's conclusion that the sender "wrote from inside an MMIO page" is wrong (§8.4).

| | |
|---|---|
| Log | `artifacts/rpi4b-uart/rpi4b-uart-20260927-130015-mig-q3.log` |
| Kernel fix | `phoenix-rtos-kernel` branch `fix/msg-map-uncached-align`, `794bf591` (on `publish`) |
| Test | `phoenix-rtos-tests` branch `fix/msg-map-uncached-align`, `b52301c`: `test-msg-devmem` (**unsafe on old kernels**, §5) |
| Kernel the log ran | `fb8b66ee` (the image's manifest lines, same as `mig-q2`) |
| Root-cause fix | `phoenix-rtos-kernel` master `3da3fb38` (`vm_mapFlags`, §8), on `publish` |
| Test for it | `phoenix-rtos-tests` master `295cc14`: group `msg_devnext` of `test-msg-devmem` |
| Sender-naming log | `artifacts/rpi4b-uart/rpi4b-uart-20260927-163009-mig-q3-fix.log` (build 17, kernel `794bf591`) |

## 1. What the log shows

While `quake3-drm` (the new-GPU-lane clone of quake3e) was loading its UI
(`4 arenas parsed`, `6 bots parsed`), the console filled with the same EL1 dump
until the 300 s capture ended:

```
Exception #37: Data Abort (EL1)
 x0=000000000000e049  x1=ffffffffc4cbf049  x2=000000000000003b ...
x19=0000000000005000 x20=ffffffffc4cbf000 x21=0000000000000000 x22=ffffffffc41ff8c0 x23=000000000000e000
x24=ffffffffc41d4cc0 x25=0000000000000039 x26=ffffffffc432bb60 x27=0000000000004000
x28=0000000000000049  ... lr=ffffffffc001bee0 ... pc=ffffffffc000a970 esr=0000000096000021 far=ffffffffc4cbf049
in thread 8, process "pl011-tty" (PID: 4)
```

Every one of the ~4200 dumps has identical registers: one access, retried.

- `pc` = `hal_memcpy` (`hal/aarch64/_memcpy.S:123`, the first `ldp` of the
  33..128-byte path). `lr` = `msg_map` (`proc/msg.c:134`), i.e. the call at
  line 132, the **head shadow-page copy**
  `hal_memcpy(w + boffs, vaddr + boffs, min(size, SIZE_PAGE - boffs))`.
- `esr` 0x96000021: EC 0x25 (data abort, same EL), DFSC **0x21 = alignment
  fault**. `far` = `x1` = the source, 8-byte loads at offset 0x49.
- Registers, read against the disassembly of this kernel's `msg_map`:
  `x28` = `boffs` = 0x49, `x19` = `CEIL(data)` = 0x5000, `x27` =
  `FLOOR(data + size)` = 0x4000, `x21` = `n` = 0, `x2` = 59, so the sender's
  payload is **59 bytes at user VA 0x4049**. `x23` = `w` = 0xe000 (the window in
  pl011-tty), `x20` = the kernel view of the sender's page, `x24` = pl011-tty's
  map, `x22` = the sender's map: a heap pointer, not the kernel map
  (`main_common` is in `.bss` at `ffffffffc002c000`), so the sender is a user
  process.
- **`x25` = `attr` = 0x39** = `PGHD_PRESENT | PGHD_NOT_CACHED | PGHD_USER |
  PGHD_DEV`. `dir == 0` contributes `PRESENT | READ` (0x20) and `to != NULL`
  `USER` (0x08); the remaining 0x11 is `vm_flagsToAttr(flags)`. The sender's
  map entry for that page therefore has **`MAP_UNCACHED | MAP_DEVICE`**.

## 2. Why it faults, and why it never stops

### Memory types on aarch64 (`hal/aarch64`)

`MAIR_EL1` = 0x444FF (`_init.S`): index 0 Normal WB, index 1 Normal
non-cacheable, index 2 Device-nGnRE, index 3 Device-nGnRnE. `_pmap_writeTtl3`:

| mmap flags | PGHD | memory type |
|---|---|---|
| none | – | Normal write-back (idx 0) |
| `MAP_UNCACHED` | `NOT_CACHED` | **Normal non-cacheable** (idx 1) |
| `MAP_DEVICE` (needs `MAP_PHYSMEM`) | `DEV` | Device-nGnRE (idx 2) |
| `MAP_DEVICE \| MAP_UNCACHED` | both | **Device-nGnRnE** (idx 3) |

`SCTLR_EL1.A` is 0, so unaligned accesses to Normal memory, cacheable or not,
are fine. An unaligned access to **Device** memory takes an alignment fault
whatever `SCTLR.A` says. So the starting hypothesis — an anonymous
`MAP_UNCACHED` buffer — cannot produce DFSC 0x21. It takes a Device mapping,
and `attr` 0x39 says it was one: the `MAP_DEVICE | MAP_UNCACHED |
MAP_PHYSMEM` combination drivers use to map MMIO.

`msg_map` builds its kernel view of the sender's partial page with the flags of
the sender's mapping (`vm_mmap(kmap, …, VM_OBJ_PHYSMEM, bpa, flags)`), which is
right for Normal-NC (no mismatched aliases) but hands `hal_memcpy` — which
relies on unaligned accesses — a Device mapping.

### Why it repeats

`map_pageFault` (`vm/map.c`) never looks at the fault status code:

1. kernel PC and a kernel-map `far`, so it dumps up front
   (`process_dumpException`). The log shows each dump twice, the second copy
   followed by `in thread 8 …`. Inferred, not checked: the second copy is that
   function's `posix_write(2)`, which reaches the UART here because the faulting
   process is the console server itself;
2. `map` = the kernel map; `vm_mapForce(kmap, page, PROT_READ)` finds the entry
   of `msg_map`'s own kernel view, re-forces the same, present PTE and
   **succeeds**;
3. the handler returns, `eret` re-executes the `ldp`, same fault.

There is no exception-fixup table for kernel copies, so nothing turns the fault
into an error return. Even the "unresolved" branch would not help an EL1
fault: it posts `SIGSEGV` to the process (pl011-tty) and returns to the same
instruction. The same holds at EL0: an unaligned user access to Device memory
is "resolved" the same way and retried for ever, silently.

### The same pattern elsewhere in `proc/msg.c`

- the **tail** page copy in `msg_map` (`hal_memcpy(w + …, vaddr, eoffs)`), and
  the copy-backs of both shadow pages in `proc_respond`;
- **packed** payloads: `msg_ipack` copies a small request payload (a `write()`
  of at most 40 bytes, 64 − `sizeof(io)`) straight from the sender's buffer, and
  `proc_sendEx` copies a packed response (a `read()` of at most 64 bytes) back
  into it — in the **sender's** context, so there the sender's thread loops;
- the pages between head and tail are not copied but mapped into the receiver
  with the same memory type, so the receiver's own unaligned accesses would
  loop at EL0.

## 3. The sender: a process with an MMIO mapping, not quake3e-drm

> **Corrected by §8.** The sender is the render server, but page 0x4000 is its
> stdout buffer, ordinary memory. Its GPU registers are mapped in the page
> directly above it, and `vm_mapFlags` returned that mapping's flags.

User VA 0x4049 is in the low mmap area: programs link at 0x400000 and
`mmap(NULL, …)` takes the lowest free gap from 0x1000, so page 0x4000 is an
early (or hole-reusing) `mmap(NULL, …, MAP_DEVICE | MAP_UNCACHED |
MAP_PHYSMEM | MAP_ANONYMOUS)`.

**quake3e-drm has no such mapping.** All its `mmap` calls go through
`__wrap_mmap` (`call-sites.txt` of the build); disassembling the 22 call sites
in `build-out/quake3-drm/quake3e-drm` gives constant flags 0x40
(`MAP_ANONYMOUS`: libc heaps, stdio, pthread stacks, the QVM JIT), 0 (Mesa
`v3d/vc4_bo_map`, `dri/kms_sw_displaytarget_map`, `gbm_dri_bo_create` — all
resolved by `drmPhoenixMmap`), and `drmphx_map_memref`'s
`{0, MAP_UNCACHED} | MAP_FIXED? | MAP_PHYSMEM | MAP_ANONYMOUS`. None has
`MAP_DEVICE` (0x4). The other candidates named in the incident brief are
harmless too: `__wrap_mmap` passes anonymous and `fd < 0` maps through
unchanged; GPU buffer maps are `MAP_UNCACHED` = Normal-NC; the SDL "dma buffer"
is quake3e's own `calloc` (`SNDDMA_Init`); quake3e's `Sys_Print` writes from a
stack array.

The processes that do map `MAP_DEVICE | MAP_UNCACHED` include both GPU-lane
servers — **rpi4-kms** (`kms_vblank.c:408/411`, SMI and HVS registers) and
**rpi4-v3d-async** (`v3da_hw.c:77`, `map_dev`) — and the MMIO drivers. So one
of them wrote 59 bytes from inside an MMIO page, i.e. from a pointer into MMIO
or a stale pointer whose VA a later MMIO `mmap` reused. The dump cannot say
which process: the sender is only in `kmsg->src`, not in the registers. The
fixed kernel names it (§4), which is what the pre-registered cycle (§6) is for.

So **no quake3-drm or libdrm-phoenix change** follows from this incident, and
none was made. Why quake3-drm and not quake2-drm triggers it is open until the
sender is named.

## 4. The kernel fix (`794bf591`)

On a HAL that declares `PGHD_DEV_ALIGNED_ONLY` (aarch64, in `arch/pmap.h`)
device memory is refused as a message payload:

- `msg_map` refuses a payload whose first or last page is in a `MAP_DEVICE`
  mapping: `proc_recv` fails with `-ENOMEM` as on any mapping failure (every
  server loop checked — pl011-tty, dummyfs, posixsrv — does `continue`), and the
  sender's call fails with `-EINVAL`. It prints one line, at most 8 times per
  boot:
  `msg: refused a payload in device memory (59 bytes at 0x4049) from <path> (PID n)`.
- `msg_ipack` and `msg_opack` leave such a payload unpacked, so `msg_map`
  refuses it; the packed copies in the sender's context never touch it. Only
  user buffers are looked up, so the kernel's own small sends take no extra
  kernel-map lock (the up-front fault dump sends from the kernel stack).
  New for the packed path: `msg_opack` takes the sender's map lock from the
  receiver (as `msg_map` already does for unpacked payloads). A sender holding
  its own map lock while sending a small user-space read buffer would now
  deadlock. No such path was found: the vm paths that send while holding a map
  lock (`object_fetch`) use kernel buffers.
- The tail page's kernel view now takes the memory type of the mapping the tail
  is in (it used the first page's), and the kernel views get only the
  memory-type flags of the source mapping, no longer e.g. `MAP_FIXED`.

Refusing rather than copying byte by byte: reads of MMIO can have side effects,
and the uncopied middle pages would reach the receiver as Device memory anyway.
Linux fails the same copies with `EFAULT`. Other HALs are unchanged. armv7-a has
the same Device alignment rule and very likely the same bug, but it was not
changed here: its `hal_memcpy` was not checked and it cannot be tested here.

Cost on aarch64: one or two `vm_mapFlags` lookups (map lock + rb-tree search)
more per message that has a partial last page or a packed user payload.

Checked: `proc/msg.c` compiled `-fsyntax-only` with this kernel's own flags
(`-Werror`, `-mstrict-align`, from the build's `make -n`) against the worktree,
both with and without `PGHD_DEV_ALIGNED_ONLY`. Not built into an image and not
run on the Pi yet.

### Not changed: the EL1 fault loop itself

Stopping `map_pageFault` from retrying an unresolvable EL1 fault is **not
low-risk**, so it is documented, not done. There is no fixup table to turn the
copy into `-EFAULT`. Ending the faulting thread (`THREAD_END_NOW`) mid-kernel
would leak whatever it holds. Here that is a `kmsg` in `msg_received` on the
sender's kernel stack, so the sender would wait uninterruptibly for ever (the
FIXME in `proc_sendEx`). A panic halts the Pi 4 (`hal_cpuReboot` is a halt
loop). The storm at least left the other cores running. Two narrower
improvements for later:

1. have the aarch64 HAL report whether a fault status is resolvable by mapping
   (translation, access-flag and permission faults are; alignment and external
   aborts are not), and send the rest to the "unresolved" path. That gives EL0
   alignment faults a `SIGSEGV` instead of a silent busy loop; EL1 still needs
   (2);
2. an exception-fixup table for the kernel's user copies, as Linux has, so a
   bad copy becomes `-EFAULT`.

## 5. The test (`b52301c`)

`test-msg-devmem` (phoenix-rtos-tests `mem/`, built, **not in `test.yaml`**)
writes and reads `/tmp/test-msg-devmem.dat` (a server-backed tmpfs) with the
buffer at unaligned offsets that hit each copy path: head (0x49, 59 bytes),
head + tail (page end − 55, 200 bytes), tail only (page-aligned, 100 bytes),
packed (offset 1, 20 bytes), and reads of 100 and 20 bytes.

- group `msg_uncached`: a `MAP_UNCACHED` buffer. The data must arrive intact.
  **Safe on any kernel.**
- group `msg_devmem`: the physical pages of a `MAP_UNCACHED | MAP_CONTIGUOUS`
  buffer (`va2pa`), mapped again `MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM` the
  way drivers map MMIO. Each call must fail with `EINVAL` and leave the file
  and memory unchanged. The Device alias is never touched from user space.
  **On a kernel without the fix every case loops a thread at EL1** (the file
  server's for head/tail, the test's own for the packed ones). Reboot after it.

```
test-msg-devmem -g msg_uncached    # any kernel
test-msg-devmem                    # fixed kernel only: 13 tests, 0 failures
```

## 6. Pre-registered Pi cycle

Build the kernel branch into a `--scope core` image and confirm it ships:
`strings loader.disk | grep 'refused a payload in device memory'`. Then:

0. **Full boot + the showcase gate** on that image. Page-aligned device
   payloads that happened to work before (a zero-copy MMIO window) are now
   refused on aarch64, so any `msg: refused a payload in device memory` line
   during an ordinary boot or gate run means an in-tree driver or server relied
   on that. PASS = no such line, gate unchanged.

1. **`mig-q3-fix`**: the same cycle as `mig-q3` (quake3-drm, same staging).
   PASS = 0 `Exception #37: Data Abort (EL1)` lines, and quake3-drm gets past
   `6 bots parsed`. Record the `msg: refused a payload in device memory …` line:
   it names the process that wrote from device memory, the input for the
   user-space half. No such line and no fault = not reproduced (it is not known
   to be deterministic); re-run once before calling it either way.
2. **`msgdevmem`**: run `test-msg-devmem` at the psh prompt (netboot, `/tmp`
   is tmpfs). PASS = `13 Tests 0 Failures`, 0 EL1 dumps, and up to 8 kernel
   `refused` lines naming `test-msg-devmem`.
3. Optional negative control, on the **old** kernel only if a board reboot is
   acceptable: `test-msg-devmem -g msg_devmem -n write_head` should reproduce
   the storm, with dummyfs looping instead of pl011-tty.

## 7. Proposed KNOWN-ISSUES row

(Historical. The register row is P10, and it has been updated for §8.)

Not added to the register (proposal only):

> | C? | **A 59-byte `write()` from an MMIO page loops the kernel.** `msg_map` copied the partial page through a Device-type kernel view; the unaligned `ldp` alignment-faults, `map_pageFault` "resolves" it by re-forcing the same PTE, and pl011-tty's thread retried it for 300 s (`mig-q3`, ~4200 EL1 dumps, `attr`=0x39 ⇒ sender mapping `MAP_DEVICE\|MAP_UNCACHED` at VA 0x4049). The sender has an MMIO mapping, so it is a server or driver, not quake3e-drm. | **Kernel half fixed on a branch, not on the Pi yet** (kernel `fix/msg-map-uncached-align` `794bf591`: device payloads refused with `EINVAL` plus a line naming the sender; test `test-msg-devmem`, tests `b52301c`, unsafe on old kernels). Sender not named yet: pre-registered cycles `mig-q3-fix` + `msgdevmem`. The EL1 retry loop for unresolvable faults stays (no fixup table). [details](misc/2026-09-27-el1-msg-map-uncached-fault.md) |

## 8. Sender named: the render server

Cycle `mig-q3-fix` (build 17, kernel `794bf591`, same staging as `mig-q3`) named
the sender and ended the storm (0 EL1 dumps, quake3-drm plays q3dm1). The
kernel printed three lines, all at about 83 s, while quake3-drm was loading:

```
msg: refused a payload in device memory (99 bytes at 0000000000004021) from /bin/rpi4-v3d-async-m3p2 (PID 26)
msg: refused a payload in device memory (130 bytes at 0000000000004002) from /bin/rpi4-v3d-async-m3p2 (PID 26)
msg: refused a payload in device memory (161 bytes at 0000000000004021) from /bin/rpi4-v3d-async-m3p2 (PID 26)
```

The binary is `out-m3p2/rpi4-v3d-async` of `tools/gpu-lane/v3d-async`
(sha256 `ea13136832989089434d2d77e571adc8d592f39fbc16bbca13d25e18bcb69b89`,
built 2026-09-27 00:50, identical to `/srv/phoenix-rpi4-nfs-gcc16/bin/rpi4-v3d-async-m3p2`).
**The server is not at fault and was not changed or rebuilt.** Page 0x4000 is
its stdout buffer, which is ordinary memory. The kernel looked up the wrong mapping.

### 8.1 The payloads are the rest of a stdout line after a short write

- Every payload ends at the same place: 0x21 + 99 = 0x02 + 130 = 132, and in
  `mig-q3` 0x49 + 59 = 132. 132 bytes is the length, newline included, of a
  `V3DA srv import handle=… ns=kmsbuf … live=N` line (all four in the log are 132).
  161 + 0x21 = 194 = 132 + 62, and 62 is the length of the line the server prints
  right after an import, `V3DA srv import released handle=0x2002 id=2 pages=2026 live=2`.
- The console shows what was sent before each refusal. Log line 359 has, among
  quake3-drm's output: `V3DA srv import handle=0x2033 cli` (33 = 0x21 bytes),
  then `V3` (2 = 0x02 bytes), then `V3DA srv import handle=0x2033 cli` again
  (0x21). Those are the three refusals in order. The complete lines follow at
  368–369: the `import handle=0x2033 … live=3` line and then the `released` line.
  The kernel lines come earlier in the log (265–269) because the kernel writes
  to the UART directly, while the server's text was still queued in pl011-tty
  behind quake3-drm's console output.

How libphoenix gets there:

1. `_file_init` allocates the stdout buffer with `buffAlloc(BUFSIZ)`, i.e.
   `mmap(NULL, 4096, MAP_ANONYMOUS)`: a page-aligned page of ordinary memory, here
   at 0x4000. The server's `setvbuf(stdout, NULL, _IOLBF, 0)` keeps that buffer
   (equal-size reuse).
2. `printf` goes through `putchar`. At the newline, the line-buffered path calls
   `write_buffer` → `full_write(stream, stream->buffer, 132)`: `write(1, 0x4000, 132)`.
3. pl011-tty's output buffer is nearly full while quake3-drm prints its loading
   log, so it accepts only part of the line (33 bytes, or 2, or 73 in `mig-q3`).
   `full_write` continues at `ptr += err`: `write(1, 0x4021, 99)`. That payload
   starts at an unaligned address.
4. The kernel refused it (§8.2), `full_write` returned −1 and `write_buffer`
   kept the whole line with `F_ERROR` set. The next `putchar` found the old
   newline and flushed the line again from 0x4000. The prefix already sent was
   sent a second time: that is the duplicated fragment. Once pl011-tty took a
   whole flush in one write, the line went out and the next one was added
   (hence 194 = both lines).

Every write that started at 0x4000 went through the same device check in
`msg_map` and was **not** refused. Same page, same mapping; only the unaligned
starts were refused. If page 0x4000 were device memory, every server line would
have been refused (`132 bytes at 0x4000`). None was.

### 8.2 Why the kernel thought it was device memory: `vm_mapFlags`

```c
t.vaddr = vaddr;          /* the payload address, unaligned */
t.size = SIZE_PAGE;
e = lib_treeof(map_entry_t, linkage, lib_rbFind(&map->tree, &t.linkage));
```

`map_cmp` treats two entries as equal when they **overlap**, and `lib_rbFindEx`
returns the first node it meets that compares equal. A probe at 0x4021 covers
[0x4021, 0x5021). It overlaps the stdout-buffer entry (0x4000) **and** any entry
that starts at 0x5000. Which of the two comes back depends on the shape of the
tree. The server maps its MMIO with `map_dev` (`MAP_DEVICE | MAP_UNCACHED |
MAP_PHYSMEM`) right after start-up: `v3d_power_on` maps and unmaps the PM and
ASB pages, then `v3da_hw_init` maps the V3D register window. The first free VA
then is the page right after the stdout buffer. The page above 0x4000 is
therefore one of those MMIO mappings. Probably the V3D register window
(`hw->hub`), though the log cannot tell which one, and it does not matter
for the fix.

The same lookup explains the original storm (§1). Before `794bf591`, `msg_map`
built its kernel view of the head page with `vm_mapFlags(srcmap, data)` at
`data` = 0x4049. It got the MMIO neighbour's `MAP_DEVICE | MAP_UNCACHED`, so it
mapped a page of **RAM** as Device-nGnRnE and hit the alignment fault. `attr` 0x39
in the dump was the neighbour's memory type, not the payload's. §3 read it as
the payload's, which was wrong.

`vm_mapFlags` has three callers, all in `proc/msg.c`: `msg_srcFlags` for the
first and last page of `msg_map`, and `msg_payloadIsDevice` for the packed
paths. The first-page and packed calls pass the raw, unaligned payload pointer.
The last-page call passes `FLOOR(data + size)` and was always right. The other
lookups in `vm/map.c` receive page addresses: the fault handler floors the
address before `vm_mapForce` (`map.c:908`), `vm_objectExport` rejects unaligned
ranges before `vm_mapObjectRange`, `vm_mprotect` rejects them too, and
`vm_lockVerify` runs on the fault path.

**Consequence, not checked here:** before `794bf591` the kernel views took *all*
of the neighbour's flags. A cacheable payload whose upper neighbour is a
`MAP_UNCACHED` mapping (GPU BOs, contiguous DMA buffers — common in the GPU
lanes) got a **Normal non-cacheable** kernel view of a cacheable page. The head
copy then reads RAM without the dirty cache lines, and the `read()` copy-back
writes around the cache. That silently corrupts message payloads, and nothing
faults. The reverse (an uncached payload under a cacheable neighbour) creates a
cacheable alias of an NC page. This is a candidate mechanism for intermittent
payload corruption. It is not a claim: nothing here shows it happened.

### 8.3 The fix: `3da3fb38` (kernel master, on `publish`)

`vm_mapFlags` now probes the page that holds `vaddr`:
`t.vaddr = (void *)((ptr_t)vaddr & ~(SIZE_PAGE - 1U))`. Map entries are
page-aligned, so a page-sized probe from the page start overlaps exactly the
entry that holds `vaddr`. The same change fixes `msg_payloadIsDevice`. `vm/map.c`
compiled with the kernel's real flags (`scripts/syntax-check.sh`, `-Werror`,
clean). Not built into an image yet, not run on the Pi.

Shipped-check (no new string): in the built kernel ELF,
`aarch64-phoenix-objdump -d --disassemble=vm_mapFlags .buildroot/_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf`
must contain the immediate `#0xfffffffffffff000` (an `and`; the registers depend
on the compiler). Build 17 has no such `and`: it stores the address unmodified
(`stp x19, x0, [sp, #104]` with `x19 = x1`).

`794bf591` stays. Real device payloads are still refused, and the §2 retry loop
for unresolvable EL1 faults is still there. With `3da3fb38` they apply only to
payloads that really are in device memory.

**No user-space change.** None was needed and none was made. Copying log lines
to the stack first, or remapping anything in the server, would only have hidden
a kernel bug that hits **any** process with an MMIO mapping just above a page
it sends from.

### 8.4 Other new-lane servers

The problem was never specific to the render server. Any process that has a
`MAP_DEVICE` mapping directly above a page it sends from was exposed, whenever
the payload started at an unaligned offset in that page. A short console write
is the common way to get one.

- **rpi4-kms** (`kms_vblank.c:408/411`, SMI and HVS registers) prints to stdout
  the same way: exposed in exactly the same way, fixed by the same kernel change.
  `kmsprobe` too.
- **shmsrv** (`weston-drm/shmsrv`) and the labwc/x11 compat layers have no
  `MAP_DEVICE` mapping: not exposed.
- Every in-tree MMIO driver is exposed in principle. The build 17 showcase had 0
  refusals. That only shows the conditions (an MMIO page right above the sending
  page, and a short write from an unaligned address) did not happen there.

### 8.5 The test: `295cc14` (tests master, on `publish`)

New group **`msg_devnext`** in `test-msg-devmem`. It maps two ordinary pages and
replaces the second one, `MAP_FIXED`, with a `MAP_DEVICE | MAP_UNCACHED |
MAP_PHYSMEM` alias of a contiguous page. Then it does `write()` and `read()` in
the first page at 0x21 (99 and 100 bytes: the head copy) and at 0x1 (20 bytes:
the packed paths). The data must arrive intact. The read helpers now clear only
the payload they check, so no test touches the device page from user space. On
`794bf591` without `3da3fb38` these cases *may* fail with `EINVAL`, depending on
the tree shape, so the test does not show whether the fix shipped. The objdump
check above does. On a kernel older than `794bf591` they may loop a thread at
EL1. Now 17 tests: 7 + 6 + 4. Compiled with the real flags (clean), not built.

### 8.6 Pre-registered Pi cycle `v3da-devlog`

Needs a `--scope core` image with kernel `3da3fb38` and tests `295cc14`. Confirm
`vm_mapFlags` with the objdump check (§8.3) before the cycle. The server binary
is the unchanged `rpi4-v3d-async-m3p2` (`ea131368…`), staged already.

```
./scripts/test-cycle-psh-interact.sh --label v3da-devlog --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'quake3-drm: new GPU lane' -- \
    "/bin/test-msg-devmem" \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/usr/bin/quake3-drm +map q3dm1"
```

| Line / observation | PASS | If instead… |
|---|---|---|
| `test-msg-devmem` | `17 Tests 0 Failures`; up to 8 kernel `refused` lines naming **test-msg-devmem** (group `msg_devmem` only) | a `msg_devnext` failure with `EINVAL` = the image lacks `3da3fb38` (check the objdump) |
| `msg: refused a payload in device memory … from /bin/rpi4-v3d-async-m3p2` | **0** (`grep -a -c 'refused a payload.*v3d-async'`) | any = the fix did not ship, or a second cause: record offset + size, compare with §8.1 |
| `V3DA srv import handle=… live=N` lines | each complete (132 bytes), and **no** duplicated fragment such as `V3DA srv import handle=0x2033 cliV3` | a fragment without a refusal = a different short-write bug in libphoenix stdio |
| `V3DA srv qstat` every 5 s, quake3-drm plays q3dm1 | as `mig-q3-fix` (59.8 fps vsync) | — |
| `Data Abort (EL1)` | 0 | addr2line first |

The trigger is a short console write during quake3-drm's loading output. It
happened in both `mig-q3` and `mig-q3-fix`, but it is timing-dependent: a clean
run is strong evidence only together with `msg_devnext` passing.

# lwip `_route_find` crash in c1prov2 C6/C7: the collector thread's 512-byte stack

**Date:** 2026-09-28. **Status:** mechanism established statically (high confidence); fix on
`phoenix-rtos-lwip` branch `fix/lwip-route-race` (`356ae98`), not yet run on the Pi.
**Pre-registered Pi check:** §6.

## 1. Summary

The crash is not a route-table race and not a netif use-after-free. It is a **stack overflow in
lwip's own collector thread** (`thread_waittid_thr`, `port/threads.c`), which runs on a
**512-byte static array** `global.collector_stack`. The linker places `rt_table`
directly below that struct. When the collector frees an exited thread's stack, it goes through
`free → _malloc_chunkJoin → _malloc_chunkRemove → lib_rbRemove → rb_transplant`. That path needs
**exactly 512 bytes** with stock libphoenix, and **528 bytes** with the c1prov libphoenix, where
`_malloc_chunkRemove`'s frame grows from 128 to 144 bytes. With 528 bytes,
`rb_transplant`'s `stp x30, x19, [sp, #-32]!` stores its return address `0x42a38c` into
`rt_table.entries`, and the caller's x19 into `rt_table.lock`. The next IPv4 packet (a DNS query
to 8.8.8.8) walks code as a route list.

The kernel half of the instrument is not involved; the libphoenix half is. Stock has **zero bytes
of margin** on the same path, so this is a real lwip-port bug that any 16-byte frame growth in
libc exposes. It is **not C1**: C1 is a 32-bit `0x8000000x` store at page+4, and this is an
8-byte LR spill by the victim process's own thread.

## 2. Decoding the dump

Both boots (`rpi4b-uart-20260928-071256-c1prov2C6.log`, `…071547-c1prov2C7.log`) dump identical
registers, which fits one binary and a deterministic path.

⚠ **Forensic trap:** `.buildroot/_build/…/prog/lwip` on disk was rebuilt at 07:23. That build is
stock, and it does **not** match the image that crashed. The booted, stripped lwip was cut out of
the 06:31 `loader.disk`, which contains `c1prov = NO ANSWER` (the instrumented libc). It is
kept at `artifacts/forensics/2026-09-28-c1prov2-lwip-stripped.elf` (sha256 `ab8464e6…`). Its lwip
code sits at the same addresses as the stock build. Its libc code is shifted by +0x180 to +0x190,
depending on the function, and its .data/.bss by −0xa70, so booted `rt_table` = `0x438a70` and booted `global` (threads.c) =
`0x438a80`. To cut ELFs out of an image, use `tools/elf-stack-depth/extract-loader-elf.py`.

Booted disassembly of `route_find` (`port/route.c:235`, `_route_find` inlined):

```
402638 adrp x19, 0x438000 ; 40263c add x20, x19, #0xa70    x20 = &rt_table
402644 ldr  w0, [x20, #8]  ; 402648 bl mutexLock            rt_table.lock
40264c ldr  x3, [x19, #2672]                                 x3 = rt_table.entries   <-- 0x42a38c
402654 ldr  w4, [x21]                                        w4 = dest = 0x08080808 (8.8.8.8)
402660 ldr  x0, [x0]            (e = e->next)
402664 cmp  x3, x0
40266c ldr  w1, [x0, #24]       (e->genmask)                 FAULT, far = x0 + 0x18
```

- **The bad pointer is the list head `rt_table.entries` itself.** It is not `e->netif`, and not
  a `next` read from a heap entry. `x3 = 0x42a38c` is a **text** address, so the first iteration
  reads code as an `rt_entry_t`:
  - `dst = [0x42a39c] = 0x9a930013` = x2;
  - `genmask = [0x42a3a4] = 0xf94002c0`, so x1 = `0x08080808 & 0xf94002c0 = 0x08000000`;
  - the match fails, and `next = [0x42a38c]`.
- **`0x54fffec17100071f` is two AArch64 instructions read as a little-endian pointer:**
  `0x7100071f` = `cmp w24, #1` and `0x54fffec1` = `b.ne`, at `0x42a38c`/`0x42a390`. It is not
  ASCII, an lwIP packet or a pbuf payload.
- **`0x42a38c` is a return address:** the instruction after `bl rb_transplant` at `0x42a388` in
  `lib_rbRemove` (stock `0x42a1fc`, `sys/rb.c:309`). `rb_transplant` begins with
  `stp x30, x19, [sp, #-32]!`.
- **x1 = `0x08000000` is not C1's value.** It is `8.8.8.8 & <instruction word>`, while C1 writes
  `0x8000000x`. The location is not page+4 of a heap page either: it is VA `0x438a70`, in `.bss`.
- **Other registers:**
  - x4 = `0x08080808` is the destination.
  - x21/x24 = `0x438cc0` = `&dns_servers[0]`, x23 = `dns_table`, x27/x28 point into a
    `dns_table` name.
  - x6/x8 = `0x01000100` is the DNS question's type A / class IN (`00 01 00 01`) read little-endian,
    x7 = `0x100` is the RD flag, and x10 = `0x67` is `'g'` (end of "pool.ntp.org"). These are
    leftovers from `dns_send`.
- **x15..x18 = `0x0f0f…`, `0x1010…`, `0x1111…`, `0x1212…`** come from the kernel's initial user
  context. `hal_cpuCreateContext` (`hal/aarch64/cpu.c`) sets `ctx->x[i] = 0x0101010101010101 * i`,
  so these registers have not been written since the thread started.
- **Stack words:** the dump prints them before `Exception #36`. Addr2line against the booted
  binary, using the offsets above, gives this chain:
  - `0x40264c` route_find;
  - `*0x416ec0` ip4_route (`ip4.c:210`, the `LWIP_HOOK_IP4_ROUTE`);
  - `0x41232c` udp_sendto;
  - `0x408934` dns_send;
  - `0x4094fc` dns_enqueue / dns_gethostbyname_addrtype;
  - `0x41a738` lwip_netconn_do_gethostbyname.

  The remaining words are `.bss` pointers (`dns_servers`, `dns_table`, `dns_pcbs`), sizes and
  `0x01000100`.
- **Thread 21** is the `socketsrv_thread` (`port/sockets.c:1167`). It serves ntpclient's
  `getaddrinfo` (`sockets.c:1239`). With `LWIP_TCPIP_CORE_LOCKING=1`, `tcpip_send_msg_wait_sem`
  runs `lwip_netconn_do_gethostbyname` directly in the calling thread. Its stack, `sp=0x11bd0`, is
  malloc'd (`SOCKTHREAD_STACKSZ`). Thread 21 is the **victim**. The corrupter is the collector,
  a different thread that had already returned.

## 3. The writer: arithmetic that closes exactly

`thread_waittid_thr` (booted `0x403f40`) runs on `global.collector_stack[512]`. `global` is at
`0x438a80` (`add x20, x20, #0xa80`), so the initial SP is `0x438c80` (the kernel rounds it down to
16). Prologue frame sizes in the booted binary, checked against the disassembly:

| frame | booted | stock |
|---|---|---|
| thread_waittid_thr | 144 | 144 |
| free | 64 | 64 |
| _malloc_chunkJoin | 48 | 48 |
| **_malloc_chunkRemove** | **144** (`0x426a80: stp x30, x19, [sp, #-144]!`) | **128** (`0x426900: … #-128`) |
| lib_rbRemove | 96 | 96 |
| rb_transplant | 32 | 32 |
| **total** | **528** | **512** |

`0x438c80 − 528 = 0x438a70 = &rt_table.entries` gets x30 = `0x42a38c`, and `+8 = rt_table.lock`
gets x19. This is the only path to `rb_transplant` with a 528-byte total. The value found and
the address it was found at agree to the byte.

Why nothing caught it:
- The kernel's user-stack canary is 16 bytes at `collector_stack[0]`. It is checked only under
  `STACK_CANARY || !NDEBUG` (`proc/threads.c:717`), and both are off.
- The overflowing frame skips over the canary anyway: `rb_transplant` writes only `[sp]` and
  `[sp+8]`, because malloc's trees have no augment callback.

Why only C6 and C7 (2 of 8): the path needs an lwip thread to exit, and then a `free()` that
coalesces with a neighbour sitting in a large (rb-tree) bin. That depends on heap state. The
corruption is also invisible until the next IPv4 send. Whether the ntpclient failure is a cause
or only a consequence (lwip died) cannot be separated from these logs.

**Static check (host, ~0.2 s):** `tools/elf-stack-depth/` holds the checker and two helpers.
- `elf-stack-depth.py` computes the worst static call-path depth of a thread entry from the
  linked ELF.
- `check-lwip-thread-stacks.sh` runs it on every lwip thread that has a static stack.
- `extract-loader-elf.py` cuts program ELFs out of a `loader.disk`.

```
$ tools/elf-stack-depth/elf-stack-depth.py artifacts/forensics/2026-09-28-c1prov2-lwip-stripped.elf \
    --names-from <stock prog/lwip> --root thread_waittid_thr:512 --path-to rb_transplant
FAIL: thread_waittid_thr stack=512 worst=704 margin=-192
  deepest path to rb_transplant: 528 bytes, margin -16: thread_waittid_thr(144) -> free(64) ->
    _malloc_chunkJoin(48) -> 0x426a80(144) -> lib_rbRemove(96) -> rb_transplant(32)
$ tools/elf-stack-depth/check-lwip-thread-stacks.sh <stock prog/lwip>        # rc=1
FAIL: thread_waittid_thr stack=512 worst=544 margin=-32
  deepest path to rb_transplant: 512 bytes, margin 0
OK: ephy_linkThread stack=2048 worst=1776 margin=272
OK: genet_linkPollThread 8192/3120, genet_irqThread 16384/2432, wifi_rxThread 16384/2352, wifi_joinThread 8192/2160
$ tools/elf-stack-depth/check-lwip-thread-stacks.sh <stock prog/lwip> <fix worktree>   # rc=0
OK: thread_waittid_thr stack=4096 worst=544 margin=3552
```

How to read these numbers:
- **Stock already fails by 32 bytes** on the worst path, `_malloc_chunkRemove → malloc_debugHex
  → debug`. That path is the corrupt-chunk report, so it runs only when the allocator is already
  reporting a problem. The normal path has margin 0.
- The depths are static bounds. `blr` calls are not followed, and a worst path through `%e`
  formatting is rarely taken.
- `ephy_linkThread` (272 bytes spare) is flagged, not fixed.
- The last run measures the stock binary against the fixed size, because this session could not
  link an image. `threads.c`'s frames do not change with the fix.

## 4. What was ruled out (task items 2 and 3)

- **Unlocked route-list access:**
  - every walker holds `rt_table.lock` (`route_add/del/find/get_gw` in `route.c`, the
    `/dev/route` read in `devs.c:136`);
  - `route_add`/`route_del` are reached only through the `SIOCADDRT`/`SIOCDELRT` ioctl
    (`sockets.c:658`);
  - no boot path adds a route, so `entries` is normally NULL and `_route_find` returns at once.
  The lock was clobbered too, but only as a consequence: `mutexLock` on a bogus handle fails and
  `route_find` does not check the result.
- **netif freed under a route:** there is no `netif_remove` anywhere in the port or its drivers,
  so netifs live for the whole process.
- **The kernel instrument (`0edb27d8..cdc7201c`):** it has no path that hands out a live page or
  returns the wrong one.
  - `_page_provNote` writes only its own static arrays, with indexes bounded to the band.
  - `nextKind` is set and cleared under `pages_info.lock`, the same lock every `_page_alloc`
    caller holds.
  - `vm_pageAllocProv` is `vm_pageAlloc` plus that one field.
  - `meminfo` changes behaviour only when `page.mapsz` and `entry.mapsz` both carry the magic.

  The lwip ELF is determined by libphoenix and lwip alone, so the kernel cannot change its frame
  sizes. Six other boots of the same image ran without an lwip fault, which fits a heap-state
  trigger, not a VM bug.
- **The libphoenix instrument (`a41d8d5..36c43d7`)** is the trigger. Its code is never *executed*
  in lwip: the calls are gated on `C1_HEAP_TRACE_ALL` or a corrupt heap. It still changes code
  generation, and `_malloc_chunkRemove`'s frame grows by 16 bytes.

**Archive:** `rpi4b-uart-20260531-125824-netboot-verify-cooldown.log` has the same shape: lwip,
`x0 = 0x42c338`, a code address, and far = two instruction words. That is the same class of
fault, **unverified**, because the binary for that build is gone. The other historical lwip
faults do not share the signature.

**For the C1 series:** c1prov2 C6 and C7 are **void** for the C1 question, because STK never ran.
Any further instrumented series should carry the lwip fix, or it loses about a quarter of its
trials this way.

## 5. The fix

`phoenix-rtos-lwip` `fix/lwip-route-race` `356ae98`, pushed to `publish`:
`port/threads.c` changes `collector_stack` to `WAITTID_THREAD_STACKSZ`:
- 4096 bytes on `__aarch64__`, `aligned(16)`;
- 512 bytes on other targets, which keep their size (fork policy: per-arch, other arches
  unchanged);
- the size can be overridden per target.

The branch name was given before the analysis. The defect is the collector stack, not a race.
Validation:
- `syntax-check.sh` passes against a private root: its `sources/phoenix-rtos-lwip` is a symlink
  to the worktree, and its `.buildroot/phoenix-rtos-lwip` is a private copy, so the live
  buildroot was never touched;
- a negative control (an injected undeclared identifier) fails the check as it should;
- no image was built.

## 6. 📋 PRE-REGISTERED, 2026-09-28 07:50, before any data: `lwipstk`

**Question:** does the lwip fault follow the **libphoenix** instrument (the stack-depth mechanism
above) rather than the kernel instrument, and does the fix remove it?

**Arms.** All are netboot and all-cold as in `c1prov`, with `C1_HEAP_TRACE_ALL=1 stk …`. N = 8 per
arm, labels `lwipstk{A,B,C}1..8`.

| arm | kernel | libphoenix | lwip | build knobs | prediction |
|---|---|---|---|---|---|
| A | stock | `c1/page-provenance` | master (unfixed) | `LIBC_DIAG='-DC1_PAGE_PROVENANCE'` | lwip faults at about the c1prov2 rate (2/8), with the **exact** signature |
| B | `c1/page-provenance` | stock | master (unfixed) | `KERNEL_DIAG='-DC1_PAGE_PROVENANCE'` | 0 lwip faults |
| C | `c1/page-provenance` | `c1/page-provenance` | `fix/lwip-route-race` | both knobs | 0 lwip faults (C1 grading as `c1prov`) |

**Gates, before any trial of the arm:**
- Kernel instrument: `strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep -c 'C1PROV d='` must be ≥ 1 in B and C, and 0 in A.
- libc instrument: `strings …/loader.disk | grep -c 'c1prov = NO ANSWER'` must be ≥ 1 in A and C, and 0 in B.
- Arm A only, the same lwip binary as c1prov2:
  `tools/elf-stack-depth/extract-loader-elf.py …/loader.disk /tmp/lwipA --needle genet`. The ELF
  marked `has:genet` must have sha256 prefix `ab8464e6c2bae8c7` (the ELF in `artifacts/forensics/`).
  If it differs, the exact-signature reading is off and only the count reading applies.
- Arm C only: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm -S .buildroot/_build/aarch64a72-generic-rpi4b/prog/lwip | grep ' b global$'`
  must show size `0000000000001020` (the unfixed build shows `…0220`), **and**
  `tools/elf-stack-depth/check-lwip-thread-stacks.sh` must exit 0.
- After every core commit, rebuild with `--scope core` (the stale-core hazard).

**Per-log grading.** Strip ANSI first: `sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' LOG > /tmp/L`.
- Fault count: `grep -a -c 'in thread [0-9]*, process "lwip"' /tmp/L`. This line prints once per
  fault; the register dump prints twice.
- Exact signature: `grep -a -c 'far=54fffec171000737' /tmp/L`. That is 2 per fault, and it is
  valid for arm A only.
- Void trial: the log never reaches `nfs-fs: registered / (takeover)`.

**Readings, fixed now.** P(0 of 8 | p = 0.25) ≈ 0.10.
- **A ≥ 1 with the exact far** → confirmed: the libc frame growth plus the 512-byte collector
  stack is the mechanism.
- **A = 0/8** → "not reproduced, n = 8", never "refuted". Add 8 more A trials before any reading.
- **B ≥ 1 lwip fault whose far is *not* two instruction words, or whose PC is in a malloc
  report path** → that is the stock −32 path (§3), not the kernel.
- **Any other B ≥ 1 lwip fault** → the kernel instrument is implicated. That would contradict §4, and the
  page-provenance kernel is then suspect for C1 too. Read the fault's far first.
- **C ≥ 1 lwip fault** → the fix is insufficient or the mechanism is wrong. Run addr2line on the
  new PC against that build's own `prog/lwip`, archived before the next build.
- **C = 0/8 and A ≥ 1** → merge `fix/lwip-route-race` to lwip master.

**Optional sharper arm, no rate involved:** build the kernel with `-DSTACK_CANARY` on a stock
libphoenix. The stock path's deepest store lands exactly on `collector_stack[0..15]`, so the first
collector free on that path should trigger `user stack corrupted`, pid = lwip. ⚠ That is an
assert, so it halts the box.

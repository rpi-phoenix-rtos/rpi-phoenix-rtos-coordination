# Kernel heap free-list corruption — work order

Caught once on hardware, fully characterised, **not fixed**. Recording it while
the evidence is fresh, because the class (kernel heap corruption) is far more
serious than the trigger that exposed it.

## What was seen

`test-libc-misc` on an NFS root, immediately after the symlink group
(`symloop_max`, `//link1`) and during `stat_nlink_size_blk_tim`:

```
Exception #37: Data Abort (EL1)
 x0=74735f747365742f   far=74735f747365742f   pc=ffffffffc000df2c  lr=ffffffffc000e170
 x7=00316b6e696c2f2f
```

Resolved against the kernel ELF:

- `pc` → `_vm_zalloc`, `vm/zone.c:98`
- `lr` → `_kmalloc_alloc`, `vm/kmalloc.c:64`

Line 98 is the free-list walk:

```c
block = zone->first;
zone->first = *((void **)(zone->first));   /* faults here */
```

`x0` and `far` are `0x74735f747365742f` = ASCII **`/test_st`**, and `x7` is
`//link1`. So `zone->first` pointed at a block whose first eight bytes are a
**path string**, and dereferencing it faulted.

## What that means

The zone allocator keeps its next-pointer *inside* the free block. A free block
therefore holds a path string exactly when either:

1. something wrote into the block after it was freed (use-after-free), or
2. a block was put on the free list without its link being written — which is
   what a **double free**, or a `vm_kfree()` of an interior/mis-computed
   pointer, produces.

Either way the corruption is committed long before the crash: the allocator only
falls over at the *next* allocation that happens to reach that block. The
reported stack is therefore the victim, not the culprit, and instrumenting
`_vm_zalloc` will not find it.

## Where to look

Path strings of exactly this shape are copied into kernel memory by the posix
layer. `posix.c` has ~38 `vm_kmalloc`/`vm_kfree` sites; the per-fd canonical
path (`f->path`, allocated in `posix_open`, freed in the refcounted close) is the
one that holds strings like these, though inspection did not find a fault in it:
the refcount is taken before the read and the free happens once at `refs == 0`.

Worth checking first:

- every `vm_kfree` in `posix.c` that could run twice on one pointer, or on a
  pointer that is not the start of its allocation;
- the error paths in `posix_open` — one of them frees `f` without freeing
  `f->path` (a leak, not this bug, but it shows the paths are not symmetric);
- anything freeing a buffer that a message send still references.

## How to reproduce — CORRECTED

The first sighting followed removing the redundant `nfs_chmod` from
`nfs_ops_create`, and I wrongly attributed it to that. **It reproduces in the
shipped configuration too** (chmod restored, poisoning off), with a
byte-identical signature: same `pc`, same `far` = `/test_st`, same `x7` =
`//link1`, same `x16/x17` = `aaaaaaaa`, and the same preceding tests
(`symloop_max`, `fifo_type`, `sock_type` pass, `chr_type` ignored,
`stat_nlink_size_blk_tim/nlink` passes, then the fault during `tim`).

So: `/bin/test-libc-misc` on an NFS root, repeatedly. Roughly 2 runs in 5.

**What changed to make it appear:** every sighting is from a run *after* NFS FIFO
support landed. Before that, `mkfifo()` failed early with `-1`, so the kernel's
mkfifo path stopped at `posix_create` and never completed. Now `fifo_type`
passes, which means `posix_mkfifo`'s full sequence — `proc_create` of the pipe in
posixsrv, `proc_link` into posixsrv's namespace, then `posix_create` of the
otDev node — runs to completion for the first time. That newly reachable kernel
path is the prime suspect, not the filesystem change that reached it.

Note also that the corrupting content is a *full path* beginning with `/`, and
`posix_create` is precisely the function that `lib_strdup`s the full path into a
kernel block (`name`) and frees it at the end. Its own alloc/free is balanced;
the question is whether anything else holds or re-frees that block, or an
interior pointer into it (`lib_splitname` hands out `basename`/`dirname` pointing
*into* `name`).

## Tools now in place

Both halves of the earlier suggestion are implemented:

- `_vm_zfree` rejects a **misaligned** free (kernel `b516c8a4`). Does not fix
  this bug — it is a write-after-free, not a misaligned free — but closes the
  neighbouring hole.
- **Free-block poisoning**, off by default, build with `-DVM_ZONE_POISON=1`
  (kernel `3e913c24`). Stamps freed blocks with 0xa5, records the freeing site,
  and verifies on the next allocation, printing block/freer/first-bad-offset and
  the first 16 bytes as hex+ASCII.

With poisoning on, the corruption did **not** reproduce in two runs. The memset
per free changes allocation timing, so it may be masking the race; the next
attempt should run many more iterations, and consider poisoning without the
memset (link + freer word only) so timing is barely perturbed.

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

## How to reproduce

It appeared only after removing the redundant `nfs_chmod` from `nfs_ops_create`
(commit message in phoenix-rtos-filesystems explains why that chmod exists). That
change removes one RPC per file create, so it shifts allocation timing — it does
not itself write anything. Restoring the chmod made the crash go away and both
suites pass, which is the state currently shipped.

To reproduce: drop that chmod again, rebuild `--scope core`, and run
`/bin/test-libc-misc` repeatedly. Expect it to be intermittent.

## Suggested first move

Before hunting, make the allocator fail loudly instead of silently: validate in
`vm_kfree`/`_vm_zfree` that the pointer lies inside the zone and is
block-aligned, and poison freed blocks. Silent corruption that surfaces an
unbounded time later is what makes this expensive; a clean assert at the bad
free would name the culprit directly.

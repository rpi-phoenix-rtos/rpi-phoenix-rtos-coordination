# exportprobe — two-process check of the kernel memory export (E1)

Exercises `memExport()` / `memUnexport()` (kernel + libphoenix prototype, see
`docs/gpu-new-lane/E1-vm-object-export.md`). One binary forks before any buffer exists: the child
exports three 1 MiB `MAP_CONTIGUOUS` buffers (A cached, B uncached, C cached) under its own port
and serves `/exportprobe/<id>`; the parent opens, maps and checks them. Every check prints
`EXPORTPROBE <exp|imp> <key>=<0|1>`; the last line is `EXPORTPROBE RESULT failures=<n> verdict=PASS|FAIL`.

## Build (only after a `--scope core` build that contains the kernel + libphoenix change)

The two new functions exist only in the tree sysroot, and only once the core build has
regenerated libphoenix's syscall stubs from the new `<phoenix/syscalls.h>`. The toolchain's
bundled `libphoenix.a` does not have them (the link fails loudly — that is the TD-21 trap, safe
here because the change only appends).

```
S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
# must print a "T memExport" line; if not, the sysroot predates the core build -- or the build
# reused a cached syscalls.o: touch sources/libphoenix/arch/aarch64/syscalls.S, rebuild --scope core
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm $S/lib/libphoenix.a | grep -w -e memExport -e memUnexport
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 \
    --sysroot=$S/ -B$S/lib/ -o tools/gpu-lane/exportprobe/exportprobe \
    tools/gpu-lane/exportprobe/exportprobe.c
sudo cp tools/gpu-lane/exportprobe/exportprobe /srv/phoenix-rpi4-nfs-gcc16/bin/
# on the Pi:  exportprobe
```

Before the core build the file can only be compile-checked, against the source header:
`... -I sources/libphoenix/include -c -o /tmp/exportprobe.o ...` (links fail on the two symbols).

## Expected on the E1 kernel

All keys `=1`, `verdict=PASS`, three `free_delta_*` lines (`a` and `c` about 1048576, `b` about 0),
and exactly one kernel line when the exporter exits with C still exported:

```
vm: port <n> released with 1 memory export(s) still published, withdrawn
```

On a kernel without the change the new syscall numbers are past the table and the kernel answers
`-EINVAL` to both — which would make every "refused with EINVAL" check pass for the wrong reason.
So the exporter first asks `memUnexport` for an oid with nothing exported (only the real facility
answers `-ENOENT`), prints `kernel_has_export=0` and stops if that fails; the run is then `FAIL`.

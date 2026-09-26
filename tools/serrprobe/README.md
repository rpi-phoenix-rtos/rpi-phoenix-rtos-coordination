# serrprobe — provoke a known asynchronous SError

Reads a physical address that is known to raise an external abort on the Pi 4. The default,
`0xfd506000` (BCM2711 PCIe core), raises `esr=0xbf000002` once per read — the same code Linux
reports on this board. Use it to prove that SError is unmasked and handled: since kernel
`df9da09d` the fault kills only the reading process, with a dump naming it.

```
S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 \
    --sysroot=$S/ -B$S/lib/ -o tools/serrprobe/serrprobe tools/serrprobe/serrprobe.c
sudo cp tools/serrprobe/serrprobe /srv/phoenix-rpi4-nfs-gcc16/bin/
# on the Pi:  serrprobe [pa_hex] [reads]
```

Expected on the current kernel: one `Exception #47: SError exception` dump (printed twice, as
every EL0 dump is) with `process "/bin/serrprobe"`, no `serrprobe: read` line, and the system
carries on. Record: `docs/inprogress/2026-09-26-p2-serror-rediagnosis.md`.

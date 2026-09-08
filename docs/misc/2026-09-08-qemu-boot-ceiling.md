# How far QEMU boots this port, and why it stops there

2026-09-08. Answering "can QEMU be a kernel/userspace test loop for the Pi 4
port, so iteration stops needing the Pi and the UART lock?" **Not today.** The
ceiling is plo; the kernel never starts. Recorded so nobody re-attempts it
expecting a fast loop.

## What actually happens

`./scripts/qemu-debug.sh` (`-M raspi4b -cpu cortex-a72 -smp 4 -kernel plo.elf`)
produces a 13-line, 380-byte UART log, every time:

```
hal: console_init done … hal: init complete
plo: firmware DTB rejected
Phoenix-RTOS loader v. 1.21 rev: 58b79dfb0
cmd: Executing pre-init script
alias: Setting relative base address to 0x0000000000200000
hal: smp release-2 → kernel entry

A3
```

So plo runs fully — console, memory, timer, video, EL3 entry, pre-init script,
SMP release — and hands off. Then nothing. `--gdb` shows why:

| core | state | pc |
|---|---|---|
| 0 | **running** | `0x200` (no symbol) |
| 1–3 | halted | `0x2001f0: b 0x2001e8` |

Cores 1–3 are parked in the secondary spin loop, which is correct. Core 0 is at
`0x200` — an *exception vector offset* with `VBAR` still unset, i.e. it faulted
immediately after the kernel-entry jump and vectored into unmapped memory. The
`A3` is two cores' single-character markers interleaving (plo's `exc_tag`, which
prints one char and `b .`, `_init.S:624`).

## Root cause: no firmware DTB

`plo/hal/aarch64/generic/hal.c:224-235` takes the DTB address from
`hal_firmwareDtb` (falling back to the armstub's), requires the `0xd00dfeed`
magic there, and otherwise prints `firmware DTB rejected` and leaves
`hs->firmwareDtb = 0`. `_init.S:271-272` shows where that value comes from:

```asm
ldr x2, =hal_firmwareDtb
str x19, [x2]
```

x19 derives from x0 at entry — the standard "firmware passes the DTB pointer in
x0" convention. On real hardware VideoCore (or the armstub) satisfies it. Under
QEMU nothing does, so the kernel is handed no device tree and dies in early hal
init before it can install its own vector table.

**Tested and it does NOT help:** adding `-dtb bcm2711-rpi-4-b.dtb` (the DTB out
of the flashable image's own FAT partition). Still `firmware DTB rejected` —
QEMU's `raspi4b` does not hand a `-dtb` pointer to a bare-ELF `-kernel` entry in
x0. The option is kept as `QEMU_DTB` in the harness because it is the natural
hook for whoever fixes this, but on its own it changes nothing.

## Fix paths, if it is ever worth doing

1. Load the DTB with `-device loader,file=…,addr=<pa>` (the harness already uses
   that mechanism for `loader.disk`) and have plo look at that fixed PA when x19
   is 0. Cheap, but it is a plo change on the demo boot path, so it needs the
   full re-gate.
2. Synthesise a minimal DTB in plo when the firmware supplies none. More
   invasive, more generally useful.

Neither is a quick win, and the project already has a working hardware loop, so
this stays parked.

## What QEMU IS good for here

plo-level checks, which is exactly what `scripts/qemu-boot-sdimage.sh` uses it
for: it takes a flashable image's own `loader.disk` through plo to kernel entry
and confirms the syspage is the SD variant. That is real value on an artifact
that otherwise gets no boot testing at all, and it is all the QEMU ceiling
allows.

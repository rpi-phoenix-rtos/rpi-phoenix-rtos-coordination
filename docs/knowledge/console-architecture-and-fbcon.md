# Console paths on Pi 4 — what reaches UART vs HDMI/fbcon

Status: 2026-05-18 (Pass 4 noise cleanup + speed-fix bundle verification).

## TL;DR

There are **two independent console paths** on Pi 4 and they have very different
properties. Confusing them was the source of long-standing "why doesn't this
print show up on HDMI?" puzzles.

| Path | Source | Sink | Cost | fbcon? |
|------|--------|------|------|--------|
| **A — Direct PL011 MMIO** | `hal_consolePrint`, `pl011_writeRaw`, `debug()` syscall, plo `lib_printf` | UART register, busy-wait, ~87 µs/byte at 115200 baud | Synchronous, blocks caller per byte | **No** |
| **B — klog → libtty → drain thread** | kernel `lib_printf` (via `log_write`), userspace `printf`/`write(stdout)` | klog ring → `pl011_klogClbk` → `libtty_write` → `pl011_thr` drain → UART **and** `pl011_fbcon_write` | Asynchronous, klog ring buffers writes, drain thread paces them | **Yes** |

If you want a kernel message to appear on the HDMI framebuffer, you must use a
Path B print (i.e. `lib_printf` in the kernel, or any userspace `printf` on a
process whose stdout is wired to `/dev/console`). `hal_consolePrint` and
`debug()` only ever reach the serial UART.

## Why this matters for boot-speed work

Two related effects to keep straight:

1. **fbcon mirror coverage is determined by which path you choose.** Stripping
   a `hal_consolePrint(ATTR_USER, …)` line removes a UART-only diagnostic; it
   does **not** silence anything that was visible on HDMI in the first place.
   Conversely, a `lib_printf` line strip removes content from both UART **and**
   HDMI.
2. **Per-byte cost differs by an order of magnitude.** Path A's
   `_hal_consoleEarlyPrint` (aarch64 generic console) does
   `while (FR & TXFF) {}` then writes `DR` — about 87 µs per byte at 115200 8N1.
   A typical 60-character debug line therefore blocks the caller for ~5 ms.
   Path B's `log_write` is a `memcpy` into a ring with a wake flag — well under
   a microsecond per byte. The drain thread eats the 87 µs/byte cost on a
   dedicated context, off the hot path.

The combination of these two effects is what caused the
"fast-to-`fbcon: ok`-then-mysteriously-slow-to-prompt" observation: kernel and
plo were emitting Path A traces on every scheduler entry, thread create,
syscall, and so on, each one a synchronous UART blast that delayed everything
behind it. fbcon was quiet because those same prints never went through klog.

## Mechanism in code

### Path A — `hal_consolePrint` for aarch64

```c
/* hal/aarch64/generic/console.c */
void hal_consolePrint(int attr, const char *s)
{
    if (attr == ATTR_BOLD)      _hal_consoleEarlyPrint(CONSOLE_BOLD);
    else if (attr != ATTR_USER) _hal_consoleEarlyPrint(CONSOLE_CYAN);
    _hal_consoleEarlyPrint(s);
    _hal_consoleEarlyPrint(CONSOLE_NORMAL);
}

static void _hal_consoleEarlyPutch(char c)
{
    volatile u32 *uart   = (volatile u32 *)0xffffffffffe00000ull;
    volatile u32 *uartfr = (volatile u32 *)0xffffffffffe00018ull;
    while ((*uartfr & (1U << 5)) != 0U) { }   /* TXFF busy-wait */
    *uart = (u32)(u8)c;
}
```

That early MMIO alias is set up in `_init.S` so it works the moment the MMU is
on. It bypasses any kernel ring buffer.

The userspace `debug(const char *s)` library wrapper is just a syscall that
calls `hal_consolePrint(ATTR_USER, s)` from kernel context (see
`kernel/syscalls.c:syscalls_debug`). It is therefore Path A as well, and
synchronously blocks the user thread for the duration of the UART write.

### Path B — `lib_printf` → klog → libtty → pl011\_thr → fbcon

```c
/* lib/printf.c */
void lib_putch(char s)             { char c[2] = { s, 0 }; (void)log_write(c, 1); }

/* log/log.c */
size_t log_write(const char *data, size_t len)
{
    if (log_common.enabled) {
        /* memcpy into ring, set updated flag, wake klog readers */
    } else {
        /* enabled == 0 only before _log_init: fall through to hal_consolePutch */
    }
}
```

```c
/* phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c */
static void pl011_klogClbk(const char *data, size_t size)
{
    libtty_write(&pl011_common.uart.tty, data, size, 0);
}

/* In main(): */
libklog_init(pl011_klogClbk);     /* registers as the klog ring consumer */
```

`libtty_write` pushes into the tty TX queue (4 KiB on Pi 4). `pl011_thr` drains
the queue with a batch loop that writes to the PL011 data register **and**
mirrors the same bytes to the HDMI framebuffer via `pl011_fbcon_write` (after
the TD-12 batch cleanup, a single mutex/lock per N popped bytes rather than per
byte).

## Practical implications

- **Bringup-era synchronous markers** (the alphabet-soup single-character UART
  puts in `kernel/main.c`, `_init.S`, plo, etc., and the `hal_consolePrint`
  status-step prints I added during cache/SMP/USB work) are all Path A. They
  never reached HDMI. They are also the *most* expensive print form: each
  syscall is a synchronous busy-wait. After the cache/SMP/USB bring-up was
  settled these became pure overhead and we have stripped most of them.

- **Upstream kernel `lib_printf` traces** (`pmap:`, `vm:`, banner via
  `hal_consolePrint(ATTR_BOLD,…)` plus the subsequent `lib_printf` "hal: %s"
  features) are Path B once `_log_init` has flipped `log_common.enabled = 1`.
  These appear on HDMI. Be conservative removing them — they're the
  upstream-style "what's the kernel doing" signal, not Pi-4-added noise.

- **The "fbcon mirror has hidden buffered data" hypothesis is essentially a
  non-issue today.** fbcon mirror runs on the same drain that empties the
  libtty TX queue to UART; once a byte is past `libtty_popchar` it has been
  written to both the PL011 `DR` register and the framebuffer in the same
  iteration. There is no second queue gating the framebuffer specifically.
  What did happen historically was the *libtty TX queue* (and the klog ring
  upstream of it) filling faster than `pl011_thr` could drain, in which case
  the klog ring drops oldest data (see `log_write` overflow handling). The
  symptom looks like "some lines never show on HDMI" but it's really "klog
  dropped the lines before libtty saw them." The TD-12 round-1 fixes (usb
  daemon `setvbuf`, batched fbcon mirror) cut the producer/consumer rate
  mismatch hard enough that this is no longer observable in normal boots.

## Diagnostic discipline going forward

When adding a new diagnostic print on the Pi 4 port:

- If the message is purely for **early boot before klog/libtty exist** (plo,
  kernel `_hal_init` pre-`_log_init`), Path A is the only option. Keep it
  short, gate it on `#ifndef NDEBUG`, and remove it once the bug it was
  chasing is closed.
- If the message is **kernel runtime status** (post-`_log_init`), use
  `lib_printf`. It is faster (klog memcpy) and shows up on HDMI without extra
  plumbing.
- If the message is **userspace progress** that the user might want to see on
  HDMI, use a normal `printf`/`fprintf(stderr, …)` and let it route through
  libtty.

When deciding whether to remove an existing print:

- `git blame` it. If the line was added by an RPi4 bringup commit (Witold Bołt
  / AI-agent commits with TD-* markers, "rpi4" tags, cache/SMP/USB work),
  treat it as fair game.
- If the line predates RPi4 work (original Phoenix-RTOS authors / upstream
  commits), keep it unless there's a concrete reason. Stripping upstream
  prints risks regressing diagnostics that other Phoenix-RTOS platforms rely
  on.

## Related TDs

- TD-12 (boot-speed mitigations) — see
  [TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](../inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
- TD-14 (pl011-tty / psh slow-IPC workarounds) — same file
- TD-15 (VC4 framebuffer + fbcon plumbing) — same file

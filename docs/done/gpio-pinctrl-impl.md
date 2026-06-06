# Implementation Plan — Phoenix-RTOS BCM2711 GPIO + pinctrl

Cross-references:
- Research: [docs/knowledge/gpio-pinctrl.md](../knowledge/gpio-pinctrl.md)
- Non-Linux survey: [docs/knowledge/gpio-pinctrl-non-linux.md](../knowledge/gpio-pinctrl-non-linux.md)
- Existing reference driver: `sources/phoenix-rtos-devices/gpio/zynq7000-gpio/`
- Project board config: `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`

## 1. Goal and tier ladder

Deliver a Phoenix-RTOS userspace driver for the BCM2711 GPIO + pinctrl
block, exposing it through the Zynq-7000 message-port pattern extended
with two BCM2711-specific devctls (pull, alt-function). The block lives
at `0xFE200000` in low-peripheral mode (matches `PL011_TTY_BASE
0xfe201000u` already in `board_config.h`).

The work splits into five tiers, each independently testable:

- **Tier 0 — MMIO toggle, no API.** A throwaway test harness (in
  `phoenix-rtos-kernel` HAL or a tiny standalone server). Maps one page
  at `0xFE200000`, writes `GPFSEL2[5:3] = 001` to set GPIO 21 as output,
  then alternates `GPSET0[21]` / `GPCLR0[21]` in a 1 Hz loop. Acceptance:
  multimeter on header pin 40 reads ~3.3 V / 0 V at 1 Hz; an LED with a
  330 ohm series resistor between pin 40 and pin 39 (GND) blinks. No
  driver process, no devctl ABI, no hand-off.
- **Tier 1 — fsel API.** `gpio_set_func(pin, alt)` plus refactored
  read/write/dir following Zynq-7000's shape. ALT0..5 tables for the
  26 user-pinned BCM lines on the header. Acceptance: a userspace test
  binary toggles pin 21 via the message port; redirecting UART through
  ALT0 still produces the same boot console, proving alt routing works.
- **Tier 2 — Pull configuration via GPIO_PUP_PDN_CNTRL.** Read-modify-
  write the new register at offset `0xE4..0xF0`. Two bits per pin,
  encoding `00=off, 01=up, 10=down`. Strictly BCM2711 only — the legacy
  `GPPUD`/`GPPUDCLK` strobe path is dead silicon. Acceptance: GPIO 21
  configured as input with pull-up reads 1 with the pin floating;
  pull-down reads 0; a button to GND drops the pin in either case.
- **Tier 3 — GPIO IRQ.** Wire GIC SPI 113 (bank 0), 114 (bank 1), 115,
  116 (combined and async) through the existing Phoenix IRQ subsystem
  (the GIC distributor already serves the timer; the same registration
  path applies). Per-pin trigger via GPRENn/GPFENn/GPHENn/GPLENn plus
  W1C ack on GPEDSn. Async edge variants (GPAREN/GPAFEN) deferred.
  Acceptance: a button on GPIO 21 to GND with internal pull-up
  generates a single interrupt per press; a userspace test prints a
  press count.
- **Tier 4 — libgpiod-style ABI.** A `/dev/gpiochip0` chardev shim
  emulating `GPIO_GET_LINE_INFO_IOCTL` and `GPIO_GET_LINEHANDLE_IOCTL`,
  enabling stock `gpioget`/`gpioset`/`gpiomon` to run. Optional; gates
  on Phoenix's chardev infrastructure. Acceptance: a statically built
  `gpioget` from libgpiod 1.6 reads pin 21 without a Phoenix-specific
  patch.

Tiers 0..3 are the ~2-3 dev-week target. Tier 4 is a stretch goal to
revisit once Tiers 0..3 ship and we have a real consumer (button input
or status LED) exercising the ABI in anger.

## 2. Phoenix conventions audit

The canonical reference is Zynq-7000 (`gpio.h`, `gpio.c`, `gpiosrv.c`,
`zynq7000-gpio-msg.h`, `libzynq7000-gpio-msg.c`). Its devctl set:

```
gpio_devctl_read_pin / write_pin
gpio_devctl_read_port / write_port
gpio_devctl_read_dir  / write_dir
```

Payload union `gpio_devctl_t { type, val, mask }` carries everything.
The `oid_t` per device file selects the bank (`/dev/gpio0/port`,
`/dev/gpio0/dir`, `/dev/gpio1/port`, `/dev/gpio1/dir`) so the existing
ABI is bank-implicit and pin-bitfield based. We extend the enum with
two BCM2711-specific entries:

```
gpio_devctl_set_pull,         /* input: pin index, pull mode */
gpio_devctl_set_alt_function, /* input: pin index, alt function */
```

Numbering preserves all existing values so the Zynq-7000 client library
continues to compile against the shared header without touching call
sites that use only the original six. The `val` field in the request
encodes the pin index (low byte) plus the new mode (next byte); a
helper `gpiomsg_setPull(oid_t *bank, unsigned pin, gpio_pull_t pull)`
wraps the bit-packing. This keeps the union from growing.

The driver process model also matches Zynq: one `phoenix-rtos-devices`
binary, `portCreate` once, fork a worker per (bank, kind) device file,
respond to `mtDevCtl` messages. Use the Zynq Makefile as a starting
template; only the `NAME` and `OBJS` change.

## 3. File-level breakdown

New driver tree under `sources/phoenix-rtos-devices/gpio/`:

```
rpi4-bcm2711-gpio/
  Makefile
  gpio-bcm2711.h           — register definitions, constants, struct decls
  gpio-bcm2711.c           — register-level core: init, read/write, fsel,
                             pull, IRQ enable/ack
  pinctrl.c                — alt-function decode helpers and validation
  pinctrl-tables.c         — header-pin → ALT0..5 lookup tables, copied
                             verbatim from BCM2711 datasheet table 5-7
  gpiosrv.c                — message-port dispatcher, ports and threads
  libbcm2711-gpio-msg.c    — userspace wrapper library (mirrors
                             libzynq7000-gpio-msg.c structure)
  bcm2711-gpio-msg.h       — public devctl ABI (extends Zynq enum)
```

Header copy split: `bcm2711-gpio-msg.h` is the public surface; everything
else is private. The message header re-uses the Zynq union shape, just
adds the two enum values and a typedef pair (`gpio_pull_t`,
`gpio_alt_t`).

Coordination repo:
- New manifest entry `manifests/2026-XX-XX-bcm2711-gpio-tier0.md` per
  tier validation.

DTS-equivalent pin assignments. Phoenix-RTOS Pi 4 currently has **no
DTB integration** (confirmed in `roadmap-first-boot.md` and the absence
of any `.dts` under `phoenix-rtos-project/_projects/aarch64a72-generic-
rpi4b/`). Default pinmux is therefore whatever the VPU firmware
programs at boot via `config.txt` and the `dt-blob.bin` it loads. Our
driver respects the firmware's defaults (UART, SD, USB pins already
muxed) and only re-muxes pins on explicit `set_alt_function` calls.

A small static table in `pinctrl-tables.c` records the firmware's
assumed default for each header pin so a `gpio_devctl_reset` (future
extension) can restore them without poking the mailbox.

## 4. Public ABI

`bcm2711-gpio-msg.h` extends Zynq's enum:

```
enum {
    gpio_devctl_read_pin = 0,        /* unchanged */
    gpio_devctl_write_pin,
    gpio_devctl_read_port,
    gpio_devctl_write_port,
    gpio_devctl_read_dir,
    gpio_devctl_write_dir,
    gpio_devctl_set_pull,            /* NEW: pin + pull mode */
    gpio_devctl_set_alt_function,    /* NEW: pin + alt function */
};

typedef enum { GPIO_PULL_OFF = 0, GPIO_PULL_UP = 1,
               GPIO_PULL_DOWN = 2 } gpio_pull_t;

typedef enum { GPIO_FUNC_INPUT = 0, GPIO_FUNC_OUTPUT = 1,
               GPIO_FUNC_ALT0 = 4, GPIO_FUNC_ALT1 = 5,
               GPIO_FUNC_ALT2 = 6, GPIO_FUNC_ALT3 = 7,
               GPIO_FUNC_ALT4 = 3, GPIO_FUNC_ALT5 = 2 } gpio_alt_t;
```

The `gpio_alt_t` integer values match the BCM2711 GPFSEL bit encoding,
so the driver writes the value straight into the field without a
remap. The deliberate ordering matches the kernel DT binding values too
(see the `brcm,function` table in research §3) — that's a free win
when somebody later does want to hand the same numbers across.

Compatibility commitment: the first six enum values keep their current
numbering forever, so any code linked against `zynq7000-gpio-msg.h` can
recompile against `bcm2711-gpio-msg.h` and just work.

## 5. Key functions and data structures

Private (`gpio-bcm2711.h`):

```
typedef struct {
    volatile uint32_t *base;         /* mmap'd 0xfe200000, page-sized */
    handle_t lock;                   /* mutex for RMW (GPFSEL, PUP_PDN) */
    handle_t irqLock[GPIO_BANKS];    /* per-bank IRQ ack serialisation */
    handle_t irqCond[GPIO_BANKS];
} gpio_ctx_t;
```

Core APIs (all return `<0` on error, follow Zynq sign convention):

- `int gpio_init(void)` — open `/dev/mem`-equivalent (`mmap` of the
  physical GPIO page through Phoenix's `physmem` interface), zero
  state, build the pinctrl tables, return success.
- `int gpio_set_func(unsigned pin, gpio_alt_t alt)` — RMW
  `GPFSEL[pin / 10]` writing `alt` into bits `[(pin % 10) * 3 +: 3]`.
  Validates `pin < 54` and `alt <= 7`. Holds `lock` across read /
  modify / write.
- `int gpio_read_pin(unsigned pin, uint32_t *val)` — read
  `GPLEV[pin / 32]` bit `pin % 32`.
- `int gpio_write_pin(unsigned pin, uint32_t val)` — write
  `GPSET[pin / 32]` or `GPCLR[pin / 32]` bit `pin % 32` (atomic; no
  lock needed; the dual-register design is the point).
- `int gpio_set_pull(unsigned pin, gpio_pull_t pull)` — RMW
  `GPIO_PUP_PDN_CNTRL_REG[pin / 16]` writing the 2-bit code into bits
  `[(pin % 16) * 2 +: 2]`. Strict BCM2711-only path; no GPPUD fallback.
- `int gpio_set_irq(unsigned pin, gpio_edge_t edge)` — set or clear bits
  in GPRENn/GPFENn/GPHENn/GPLENn per the requested edge / level. Edge
  enum mirrors the Linux gpio bindings (`rising`, `falling`, `both`,
  `high`, `low`, `none`).
- `void gpio_irq_handler(unsigned bank)` — wired through Phoenix's IRQ
  subsystem to GIC SPI 113 + bank index. Reads `GPEDSn`, walks the
  set bits, dispatches to per-pin callbacks, then W1C the bits back to
  `GPEDSn`. ACK ordering matters: clear before re-enable to avoid the
  classic spurious-rearm.

Server-side (`gpiosrv.c`) follows the Zynq pattern: one thread per
device file consuming a `msgRecv` loop, switch on `gpio_devctl_t.i.type`,
dispatch to the right `gpio_*` core call, build `gpio_devctl_t.o.val`,
`msgRespond`.

## 6. License-clean reference

Circle's `gpiopin.cpp` / `gpiopin.h` is the cleanest reference but is
GPL v3 (`docs/knowledge/gpio-pinctrl-non-linux.md` §4 and
`docs/knowledge/circle-reference-review.md`). Phoenix-RTOS is BSD-3 — direct
copy is forbidden.

Process: read Circle to confirm register sequences and edge cases, then
type fresh C against the BCM2711 ARM Peripherals datasheet. No diff-
based porting, no line-by-line translation. Each source file gets a
short header comment citing the datasheet sections the implementation
follows (e.g. `/* See BCM2711 ARM Peripherals datasheet §5, table 5-2
GPFSEL register encoding */`). Commit messages name Circle as
"correctness cross-check, not source." The kernel HAL already
established this pattern; the same review checklist applies.

For double-checking register sequences without GPL exposure, use
rpi4-osdev's tutorial code (CC-BY-SA, examples in plain MMIO) and the
FreeBSD / OpenBSD trees (BSD license; safe to read freely).

## 7. Phased delivery

Each phase ends with a manifest commit recording all sibling SHAs (run
`scripts/snapshot-integration-state.sh`).

**Phase 1 — Tier 0 blink.** Throwaway hack inside the existing kernel
init path (or a tiny standalone test). Maps `0xFE200000`, sets GPIO 21
as output via `GPFSEL2`, toggles `GPSET0[21]` / `GPCLR0[21]` from a 1 Hz
timer hook. **Hardware test:** external red LED with 330 ohm resistor
between header pin 40 (GPIO 21) and pin 39 (GND). LED should blink at
1 Hz. Multimeter on the same pin in DC mode reads ~1.65 V average. Tag
the kernel branch `gpio-tier0-blink` and rip the hack out for Phase 2.

**Phase 2 — driver skeleton, Tier 1.** New `rpi4-bcm2711-gpio/` driver
process. Implements the original six Zynq devctls plus
`gpio_devctl_set_alt_function`. **Hardware test:** Phoenix userspace
binary blinks the same LED via `gpiomsg_writePin`. Then re-mux pins
14/15 to ALT0 and verify console traffic resumes (proves alt routing
works without breaking existing UART use).

**Phase 3 — Tier 2 pull config.** Add `gpio_devctl_set_pull`. **Hardware
test:** GPIO 21 input with pull-up + floating reads `1`. Pull-down +
floating reads `0`. Push-button between GPIO 21 and GND drops the pin
to `0` regardless of pull setting; release returns it to the pulled
level. Validates the `GPIO_PUP_PDN_CNTRL` write actually took effect
(legacy `GPPUD` would silently no-op on Pi 4 — that is the trap).

**Phase 4 — Tier 3 IRQ.** Register GIC SPI 113..116 handlers, wire to
the per-pin callback table. **Hardware test:** push-button on GPIO 21,
internal pull-up + falling edge. Userspace registers an IRQ callback;
each press increments a counter printed to console. Verify no events
from other GPIO bank-0 pins (mask discipline). Bench-test 1000 presses
without spurious wake.

**Phase 5 — Tier 4 libgpiod (optional).** A chardev shim. Acceptance:
unmodified `gpioget gpiochip0 21` returns the pin level.

## 8. Test strategy

Every tier has a bench-rig test plus a regression hook in the netboot
test harness (`docs/knowledge/netboot-test-cycle.md`).

Bench setup: a 40-pin breakout cable + breadboard + LED (330 ohm
series resistor) on GPIO 21 (pin 40 / GND on pin 39). For Tier 3, swap
the LED for a tact switch between GPIO 21 and GND with the internal
pull-up enabled. A multimeter (DMM) suffices for Tiers 0..2; Tier 3
optionally wants a logic analyser to capture the press → IRQ latency.

Automated regression. Each tier ships a `phoenix-rtos-tests` test under
`tests/devices/gpio-bcm2711/` that runs on the netboot loop:
- T0/T1: write pin 21 high, read pin 21, expect `1`. Write low, expect
  `0`. (Requires a physical loopback wire from pin 21 to a second pin
  configured as input — pin 11 / GPIO 17 is convenient.)
- T2: pull-up on input pin 17, read, expect `1`. Pull-down, expect `0`.
  (Loopback wire removed for this test.)
- T3: drive pin 21 from a second pin configured as output (loopback
  again); falling edge on 21 should fire the registered callback within
  a 100 ms timeout. Pulse 100 times and require exactly 100 callbacks.

Manual tests live in `docs/knowledge/pi4-first-hardware-trial.md` with photos of
the bench rig.

## 9. Inter-dependencies

- **Depends on**: kernel IRQ subsystem reaching steady state (it
  already serves the ARM generic timer through the GIC, so SPI
  registration is mechanical for Tier 3); EL1 boot (already in place);
  `physmem`-style page mapping for userspace driver processes (used by
  the existing PL011 server, so proven). TD-04 cache coherence work
  (the kernel has reached `_hal_init` per `docs/inprogress/status.md`, so the
  userspace path the driver lives in is reachable).
- **Conflicts with**: peripherals that need alt-function header pins —
  UART (GPIO 14/15 ALT0), I2C-1 (GPIO 2/3 ALT0), SPI-0 (GPIO 7..11
  ALT0). The current default is whatever the firmware muxed at boot;
  once Phoenix's GPIO driver is in charge, future SPI/I2C drivers must
  call `gpio_set_alt_function` before touching their controller. We
  must not auto-mux on driver start — that would tear down the firmware
  console UART.
- **Foundational for**: every later peripheral driver. SPI controllers,
  I2C controllers, button input, status LEDs, HAT-EEPROM probing.
  Tier 4 also unblocks running upstream Linux GPIO tooling against
  Phoenix.

## 10. Effort estimate

~2-3 dev-weeks for Tiers 0..3, broken down:

- Phase 1 Tier 0 blink: 0.5 day. Throwaway hack, pure MMIO.
- Phase 2 Tier 1 driver: 4-5 days. Most of the time is the message-
  port boilerplate (Makefile, gpiosrv, libwrapper) — the register code
  is small.
- Phase 3 Tier 2 pull: 1 day. One register, two helpers, one test.
- Phase 4 Tier 3 IRQ: 4-6 days. The risk is in GIC SPI registration on
  a freshly-booting Pi 4 — the first SPI other than the timer. Budget
  extra debug time.
- Documentation, manifest, code review, upstreamability polish: 2-3
  days spread across phases.

Tier 4 (libgpiod): a separate 1-2 week effort once the chardev
infrastructure is mature; not on the critical path.

## 11. Open questions

- **ABI direction.** Stick with the Zynq devctl shape, or pivot to a
  libgpiod-compatible chardev as the primary public API and let Zynq-
  style devctl be a compatibility wrapper? The Zynq pattern is faster
  to ship and matches existing Phoenix code; libgpiod is the long-
  term ergonomic win and lets users run unmodified upstream tooling.
  Recommendation: ship Zynq-style for Tiers 0..3, decide on libgpiod
  after we have one real consumer (button input or status LED).
- **Default pinmux ownership.** Today the VPU firmware's `dt-blob.bin`
  programs pins. Once Phoenix has a GPIO driver, who owns the boot
  pinmux? Options: (a) keep firmware-driven defaults forever (status
  quo); (b) Phoenix re-asserts an explicit pinmux table at driver
  start; (c) `board_config.h` declares pin assignments and the driver
  consumes them. (a) is least work, (c) is the upstream Linux pattern.
  Recommendation: (a) until we have a DTB story; revisit when DT
  integration lands.
- **Bank topology.** The SoC has 54 pins across two banks of 32. Header-
  exposed pins fit in bank 0. Do we expose `/dev/gpio1/*` at all if
  bank 1 only routes to internal peripherals (SD card, Ethernet PHY,
  LED expander)? Recommendation: expose both for completeness; users
  who shoot themselves in the foot by reconfiguring the SD pins
  deserve what they get.
- **GIC routing on reset.** Research §8 flags that we have not yet
  confirmed whether SPI 113..116 are routed to a CPU on reset or need
  explicit `GICD_ITARGETSRn` programming as the timer did. This
  becomes urgent at the start of Phase 4 — schedule a half-day spike
  to verify on hardware before designing the IRQ registration path.
- **Pi 5 forward-compat.** BCM2712 changes layout significantly.
  Recommendation: ship a flat BCM2711-only driver now; revisit
  abstraction when Pi 5 work begins.

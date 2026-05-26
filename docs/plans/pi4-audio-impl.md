# Implementation plan — Pi 4 audio output (BCM2711 PWM / PCM / HDMI / USB)

Status: forward plan. Greenfield — Phoenix-RTOS has **no audio subsystem today**
(see §3). This document sets the output-path decision, register-level bring-up
for the recommended path, the device-interface design, and the tier ladder.
It commits no code; work lands in `phoenix-rtos-devices` (a new `audio/` tree)
with observability scaffolding first in `phoenix-rtos-lwip/port/diag-udp.c`,
matching the WiFi/Eth bring-up idiom.

Primary references (cited inline): bare-metal PWM audio tutorial
[rpi4os.com part9-sound](https://www.rpi4os.com/part9-sound/) and the
[rpi4-osdev part9 source](https://github.com/sypstraw/rpi4-osdev/tree/master/part9-sound);
[Circle audio devices](https://circle-rpi.readthedocs.io/en/51.0/devices/audio-devices.html)
and the [Circle PWM sound source](https://github.com/rsta2/circle);
[BCM2711 ARM Peripherals datasheet](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf)
(clock manager §5.4, DMA DREQ §4.2.1.3, alt-function §5.3);
[eLinux BCM2835 registers](https://elinux.org/BCM2835_registers) and the
mikem [bcm2835 PWM library](http://www.airspayce.com/mikem/bcm2835/group__pwm.html).

## 1. Goal and tier ladder

End goal: a Phoenix-RTOS Pi 4 image that plays PCM audio out of the on-board
3.5 mm headphone jack, exposed to user space as a streaming character device
(`/dev/audio` / `/dev/dsp`-style), with a small `aplay`-equivalent that plays a
WAV file. The jack is driven by the dedicated PWM1 engine, exactly as the
[rpi4os.com part9-sound](https://www.rpi4os.com/part9-sound/) bare-metal
tutorial does. I2S, HDMI, and USB are out of scope for first light but are
analysed in §4 and accommodated by the device abstraction in §6.

Each tier is independently testable on the bench rig and rollback-able via a
`manifests/*.md` snapshot per the rollback discipline in
[`AGENTS.md`](../../AGENTS.md).

- **Tier 0 (MUST) — Scout.** A `diag-udp` sub-command maps the PWM1 + clock-
  manager + GPIO blocks and reads back their state, confirming MMIO is
  reachable and the firmware has not left the audio clock in a hostile state.
  Exit: UDP reply prints `CM_PWMCTL`, `CM_PWMDIV`, `PWM1_CTL`, and `GPFSEL4`
  for GPIO 40/41.
- **Tier 1 (MUST) — Clock + GPIO + PWM bring-up.** Program the PWM clock
  (oscillator → divider → 27 MHz class), mux GPIO 40/41 to ALT0, and enable
  PWM1 channel 1+2 in M/S + FIFO mode. Exit: scout reports `BUSY=1` on the
  clock, `PWEN1=PWEN2=1`, and the FIFO drains a hand-fed constant.
- **Tier 2 (MUST) — Tone on the jack (PIO).** Write a software-generated
  square/sine wave into the PWM FIFO from a CPU loop, polling `STA.FULL`.
  Exit: an audible 440 Hz tone on headphones plugged into the jack;
  oscilloscope/ear confirmation, scout reports FIFO never underran.
- **Tier 3 (MUST) — DMA-driven streaming.** Pace the FIFO from a legacy-DMA
  channel using DREQ 1 (PWM1), double-buffered, IRQ on block complete. Exit:
  a continuous tone with no CPU spin and no audible gaps for 60 s.
- **Tier 4 (MUST) — `/dev/audio` + WAV playback.** A `pwmsnd` server registers
  a streaming char device; an `aplay`-equivalent reads a 16-bit/44.1 kHz WAV,
  downconverts, and writes to the device. Exit: a recognisable WAV plays end-
  to-end from a Phoenix shell command.
- **Tier 5 (STRETCH) — Mixing / format conversion / volume.** Server-side
  s16→PWM resampling-free format conversion, software volume, and a 2-client
  mixer. Exit: two simultaneous tones mix without distortion.
- **Tier 6 (STRETCH) — I2S/PCM path for external DACs.** Bring up the PCM/I2S
  block (§4.2) behind the same device interface, selectable at server launch.
  Exit: a HiFiBerry/UDA1334-class I2S DAC plays the same WAV.

Tiers 0–2 are "the PWM engine makes sound"; Tier 3–4 are "Phoenix has an audio
device"; Tier 5–6 are production hardening.

## 2. Output-path survey and decision

Pi 4 / BCM2711 has four candidate output paths. Circle abstracts all four as
subclasses of `CSoundBaseDevice`
([Circle audio devices](https://circle-rpi.readthedocs.io/en/51.0/devices/audio-devices.html)):
`CPWMSoundDevice`, `CI2SSoundBaseDevice`, `CHDMISoundBaseDevice`,
`CUSBSoundBaseDevice`. The tradeoffs for *this* project:

| Path | HW on Pi 4 | Phoenix prerequisites | Complexity | Quality | Verdict |
|---|---|---|---|---|---|
| **PWM → 3.5 mm jack** | PWM1 on GPIO40/41, wired to the jack's analog circuit | none beyond MMIO + DMA (both already exercised by GENET) | low | 11-bit-ish, PDM/sigma-delta, audible noise floor | **FIRST** |
| **PCM/I2S → ext DAC/HAT** | PCM block on GPIO18-21 | GPIO header DAC + the PCM register block; same DMA model | medium | up to 24-bit, clean | Tier 6 |
| **HDMI audio** | VideoCore-owned; IEC958/S-PDIF framing into HDMI0 | a VC4 mailbox audio interface **or** direct HVS/HDMI programming (the GPU plan calls this "high risk, undocumented") | high | clean, but display-coupled | defer |
| **USB audio** | UAC over xHCI | a working USB host stack | n/a now | clean | **blocked** |

Decisions and rationale:

- **Recommend PWM first.** It is the smallest path that produces sound: the
  whole pipeline (clock manager → PWM1 → DMA → FIFO) lives in peripherals the
  ARM owns directly, with no VideoCore handshake. The
  [rpi4os.com part9](https://www.rpi4os.com/part9-sound/) tutorial proves the
  exact register sequence works bare-metal on a Pi 4, and Phoenix already has
  every primitive it needs (MMIO `mmap`, `MAP_CONTIGUOUS`/`MAP_UNCACHED`,
  `va2pa`, `interrupt()` — all used by the GENET driver,
  [`bcm-genet.c`](../../sources/phoenix-rtos-lwip/drivers/bcm-genet.c)).
- **USB audio is out** until the USB host is unblocked. The xHCI bring-up is
  *statistically* failing today (cap-probe poisoning, PCIe-bridge degradation
  across resets — see [`docs/status.md`](../status.md) "USB status:
  statistical"). USB Audio Class would be the *highest-quality* path and is
  worth revisiting once USB is reliable, but it cannot be a first target.
- **HDMI audio is deferred.** On Pi 4 the HDMI block was rewritten and is
  firmware-owned; the GPU plan ([`gpu-vc6-impl.md`](gpu-vc6-impl.md), Phase 5)
  flags the HDMI clock complex (PLLH + BVB + HSM) as undocumented and
  high-risk. HDMI audio would either ride a VC4 mailbox audio interface (the
  approach the Linux `bcm2835` staging driver + VCHIQ uses) or require direct
  HDMI0 register programming. Both depend on display work landing first.
- **I2S/PCM is the natural Tier 6 follow-on.** It reuses the same DMA + device
  interface, swaps the peripheral block, and is the only path to clean audio
  without USB. It needs an external DAC at the bench. Good "production audio"
  story but not needed for first sound.

Pi-4-specific caveat worth recording up front: the analog audio quality is
limited because **the Pi 4 has no dedicated audio DAC/codec** — the jack is fed
straight from PWM through an RC/buffer filter. A Raspberry Pi engineer confirms
the analog path is "exactly the same as the Pi 3 (except it now has its own
dedicated PWM)"
([RPi forum t=243555](https://forums.raspberrypi.com/viewtopic.php?t=243555)).
So PWM audio will be noticeably noisier than I2S/USB; that is acceptable for a
first-light "it makes sound" milestone.

## 3. Phoenix conventions audit

**Phoenix has no audio subsystem.** A sweep of `sources/` found no ALSA, no
`/dev/audio` or `/dev/dsp`, no `audiosrv`/`soundsrv`, and no `audio/` or
`sound/` driver tree under `phoenix-rtos-devices/`. The closest existing code:

- **i.MX SAI driver** — [`adc/ad7779/imx6ull/sai.c`](../../sources/phoenix-rtos-devices/adc/ad7779/imx6ull/sai.c)
  and [`adc/ad7779/imxrt/sai.c`](../../sources/phoenix-rtos-devices/adc/ad7779/imxrt/sai.c).
  These drive the i.MX Synchronous Audio Interface (the I²S-class block) — but
  only in **RX** mode, to clock an ADC. They are the only "audio peripheral"
  register code in the tree and a useful structural reference for the Tier 6
  PCM/I2S work (frame-sync / bit-clock / data-line register layout, DMA-request
  enable bit). They are **not** a playback path and expose no audio device.
- **PWM char drivers** — [`gpio/zynq-pwm/pwm.c`](../../sources/phoenix-rtos-devices/gpio/zynq-pwm/pwm.c)
  and [`gpio/rcpwm/rcpwm.c`](../../sources/phoenix-rtos-devices/gpio/rcpwm/rcpwm.c).
  These are **control** PWM (servo/LED duty-cycle), not audio FIFO streaming,
  but `zynq-pwm.c` is the canonical "char-device server" idiom this plan
  mirrors: `mmap` the peripheral, `portCreate`, `create_dev("/dev/pwmN")`,
  a `msgRecv`/`msgRespond` loop handling `mtOpen`/`mtRead`/`mtWrite`/`mtDevCtl`.

The audio driver is therefore **greenfield**, modelled on the Phoenix
"library + server" idiom:

- A server binary (`pwmsnd`) `mmap`s the PWM1 + clock-manager + GPIO + DMA MMIO
  windows, owns them exclusively, `portCreate`s a port, and `create_dev`s a
  device node.
- It exports a **streaming char-device** contract over `msg_t`: `mtWrite`
  appends PCM frames to a ring that the DMA engine drains; `mtRead` is unused
  (output-only) or returns playback position; `mtGetAttr`/`mtSetAttr` carry
  sample rate / format / volume; `mtDevCtl` carries an ioctl-shaped control
  channel for start/stop/drain/flush.
- It runs an IRQ thread (the GENET pattern:
  [`bcm-genet.c`](../../sources/phoenix-rtos-lwip/drivers/bcm-genet.c)
  registers `interrupt()` with a cond, masks the level-2 source in the handler,
  and signals a worker thread). The audio IRQ thread refills the inactive DMA
  buffer on each block-complete.

### Device-interface design (greenfield, modelled on UART/diag char devices)

```c
/* /dev/audio0 — output-only streaming PCM device. */

/* mtWrite: payload is interleaved PCM frames in the currently-set format.
 * Blocks (or returns short) when the server ring is full; the DMA engine
 * is the consumer. Returns bytes accepted. */

/* mtGetAttr / mtSetAttr (or a typed mtDevCtl): */
typedef struct {
    uint32_t rate;       /* sample rate Hz (e.g. 44100) */
    uint16_t channels;   /* 1 (mono dup) or 2 (stereo) */
    uint16_t format;     /* AUDIO_FMT_U8 | _S16LE (server converts to PWM) */
    uint8_t  volume;     /* 0..255 software gain */
} audio_params_t;

/* mtDevCtl ops: */
enum { audio_start, audio_stop, audio_drain, audio_flush, audio_getpos };
```

The server internally converts the client format (s16le is the WAV-friendly
default) into the **PWM hardware format** (unsigned, scaled to the PWM range —
Circle's internal PWM format is `SoundFormatUnsigned32`,
[Circle audio devices](https://circle-rpi.readthedocs.io/en/51.0/devices/audio-devices.html)).
Keeping the *public* interface format-agnostic means the same `/dev/audio0`
node and the same `aplay` client work when the Tier 6 I2S backend is selected
at server launch — only the backend's hardware format differs.

This is deliberately a thin, Phoenix-native interface, **not** an ALSA port:
ALSA's `snd_pcm` ioctl surface is large and POSIX-foreign; a Phoenix char
device with a typed `mtDevCtl` matches how `zynq-pwm`, the tty layer, and the
diag tooling already work.

## 4. Output paths — register-level detail

### 4.1 PWM path (recommended) — register-level bring-up

**Addressing model (critical).** BCM2711 documents peripherals at *legacy
master* addresses `0x7Enn_nnnn`; the ARM in Low-Peripheral mode (Pi 4 default)
sees them at `0x0_FEnn_nnnn` (datasheet §1.2.4). So every `0x7E…` in the
tutorials becomes `0xFE…` for ARM MMIO. **But the DMA engines must be
programmed with the legacy `0x7E…` addresses** — the VideoCore translates them
(datasheet §1.2.4: "Software accessing peripherals using the DMA engines must
use 32-bit legacy master addresses"). This is exactly the "0x7E for DMA"
quirk the [rpi4os.com part9](https://www.rpi4os.com/part9-sound/) tutorial
flags. Phoenix already lives in this world: `board_config.h` defines the
mailbox at `0xfe00b880`, GENET maps `0xfe…`, and the diag SDIO code uses
`va2pa()` to get DMA-visible physical addresses.

**Base addresses (ARM / Low-Peripheral):**

| Block | Legacy (DMA-visible) | ARM MMIO (`mmap`) | Note |
|---|---|---|---|
| GPIO | `0x7E200000` | `0xFE200000` | already used by diag `'g'`, `BCM2711_GPIO_BASE` |
| Clock manager (CPRMAN) | `0x7E101000` | `0xFE101000` | datasheet §5.4.2, base `0x7e101000` |
| PWM0 | `0x7E20C000` | `0xFE20C000` | not the audio engine on Pi 4 |
| **PWM1** | `0x7E20C800` | **`0xFE20C800`** | the audio engine; PWM0 + 0x800, same 4 kB page. Confirmed by RPi engineer ([forum t=295059](https://forums.raspberrypi.com/viewtopic.php?t=295059)) |
| Legacy DMA | `0x7E007000` | `0xFE007000` | channel N at +0x100·N; channel 1 used |
| PCM/I2S | `0x7E203000` | `0xFE203000` | Tier 6 only |

**GPIO muxing.** GPIO 40 = `PWM1_0`, GPIO 41 = `PWM1_1`, both via **ALT0**.
They are internally wired to the Pi 4B analog audio circuit / 4-pole jack
(datasheet §5.3 alt-function table; confirmed by Ultibo/pigpio docs and
[forum t=295059](https://forums.raspberrypi.com/viewtopic.php?t=295059)).
The function-select code already exists in diag-udp.c
([`diag_gpioSetFsel`](../../sources/phoenix-rtos-lwip/port/diag-udp.c), the WiFi
GPFSEL helper): GPIO 40/41 live in `GPFSEL4` (`GPIO_GPFSEL0 + 4*4`), 3 bits per
pin; ALT0 encodes as **4** (the datasheet's `0b100` — note diag-udp's comment
maps fn-select value 4 = ALT0). Set `fsel(40)=4`, `fsel(41)=4`.

> Caveat to flag in code with a `TODO(TD-xx)`: GPIO 40/41 are **not** broken
> out on CM4 (they carry eMMC control there,
> [forum t=295059](https://forums.raspberrypi.com/viewtopic.php?t=295059)).
> This plan targets the Pi 4 *Model B* jack only.

**Clock manager (PWM clock).** The PWM clock uses the same CTL/DIV layout as the
General-Purpose clocks documented in datasheet §5.4.2 (`CM_GP0CTL`…). The PWM
clock pair is `CM_PWMCTL`/`CM_PWMDIV` at CPRMAN offsets **`0xA0`/`0xA4`**
(legacy `0x7E1010A0`/`0x7E1010A4`,
[forum t=92365](https://forums.raspberrypi.com/viewtopic.php?t=92365)).
Register layout (datasheet §5.4.2 Tables 99/100):

- **CTL** bits: `PASSWD` = `0x5A` in bits 31:24 (every write); `SRC` bits 3:0
  (`1`=oscillator, `4`=PLLA, `5`=PLLC, `6`=PLLD, `7`=HDMI-aux); `ENAB` bit 4;
  `KILL` bit 5; `BUSY` bit 7 (RO); `MASH` bits 10:9 (`0`=integer divide).
- **DIV** bits: `PASSWD` 31:24; `DIVI` integer 23:12; `DIVF` fractional 11:0.

Bring-up sequence (must not change SRC/DIV while `BUSY=1`, datasheet warns of
lock-ups):

1. Stop the clock: `CM_PWMCTL = 0x5A000000 | KILL` (or clear `ENAB` and spin on
   `BUSY==0`).
2. Set source + divisor. The [rpi4os.com part9](https://www.rpi4os.com/part9-sound/)
   recipe uses the **54 MHz oscillator** (Pi 4's crystal is 54 MHz, vs 19.2 MHz
   on Pi ≤3) with `DIVI=2` → 27 MHz, then PWM `RNG=612` → 27e6/612 ≈ **44.1 kHz**:
   `CM_PWMDIV = 0x5A000000 | (2 << 12)`; `CM_PWMCTL = 0x5A000000 | SRC_OSC(1)`.
   (Circle instead sources a PLL and uses `CLOCK_RATE = 125 MHz` for Pi 4+ with
   `RNG = round(CLOCK_RATE / sample_rate)`,
   [Circle PWM source](https://github.com/rsta2/circle); either works — the
   oscillator route is simpler and is the recommended starting point.)
3. Start: `CM_PWMCTL = 0x5A000000 | SRC_OSC(1) | ENAB(0x10)`; spin until
   `BUSY==1`.

**PWM1 register map** (offsets from `0xFE20C800`; same layout as PWM0, datasheet
PWM chapter, mikem [bcm2835 PWM](http://www.airspayce.com/mikem/bcm2835/group__pwm.html)):

| Off | Reg | Use |
|---|---|---|
| `0x00` | `CTL` | enable/mode/FIFO bits |
| `0x04` | `STA` | status: `FULL1` bit0, `EMPT1` bit1, `BERR`, `GAPO*`, `RERR1` etc. |
| `0x08` | `DMAC` | DMA enable + PANIC/DREQ thresholds |
| `0x10` | `RNG1` | channel-1 range (= 612 for 44.1 kHz) |
| `0x14` | `DAT1` | channel-1 data (PIO, when not using FIFO) |
| `0x18` | `FIF1` | shared FIFO input (both channels in FIFO mode) |
| `0x20` | `RNG2` | channel-2 range |
| `0x24` | `DAT2` | channel-2 data |

`CTL` bits (per-channel; ch1 low byte, ch2 bits 8-15):
`PWEN1` bit0 (enable), `MODE1` bit1 (0=PWM/serial), `RPTL1` bit2 (repeat last
on FIFO empty), `SBIT1` bit3, `POLA1` bit4, `USEF1` bit5 (**use FIFO**),
`CLRF1` bit6 (clear FIFO, write-1), `MSEN1` bit7 (**M/S mode** — set for audio,
otherwise the default PDM/"balanced" mode is used,
[eLinux BCM2835](https://elinux.org/BCM2835_registers)); `PWEN2` bit8,
`USEF2` bit13, `MSEN2` bit15.

For audio: stereo = both channels fed from the **shared FIFO** (`USEF1=USEF2=1`,
`MSEN1=MSEN2=1`), `CLRF1` once at init, then `PWEN1=PWEN2=1`. Interleave L,R
samples into `FIF1`.

**DMA path (Tier 3).** Pace `FIF1` with a legacy-DMA channel. Per datasheet
§4.2.1.3 the DREQ map is: **DREQ 1 = DSI0/PWM1**, DREQ 2 = PCM TX, DREQ 3 =
PCM RX, DREQ 5 = PWM0. So a PWM1 audio DMA uses **DREQ 1** (rpi4-osdev part9
uses channel 1 + `DMA_PERMAP_1` for exactly this). DMA control block (datasheet
§4.2.1.1):

- `TI` (transfer info): `DEST_DREQ` (pace writes by DREQ), `PERMAP = 1` (PWM1),
  `SRC_INC` set, `DEST_INC` clear (FIFO is a fixed address), optionally
  `WAIT_RESP`.
- `SOURCE_AD` = legacy/bus address of the sample buffer (`va2pa()` of a
  `MAP_CONTIGUOUS` allocation, then mapped into the DMA address window; GENET
  uses `dmammap` for exactly this — page-aligned, uncached, low-32-bit phys).
- `DEST_AD` = **legacy** PWM1 FIF1 = `0x7E20C818`.
- `TXFR_LEN` = bytes; `NEXTCONBK` = next CB for double-buffering (ring of two
  CBs ping-ponging), or `0` for one-shot.
- Enable the channel in the DMA `ENABLE` register (bit N), set `CONBLK_AD`,
  then `CS.ACTIVE`.
- `PWM_DMAC = ENAB(bit31) | (PANIC<<8) | DREQ_THRESHOLD` — Circle sets
  `ENAB | (7<<PANIC) | (7<<DREQ)` ([Circle PWM source](https://github.com/rsta2/circle)).

Sample packing: each FIFO word is 32-bit. 8-bit samples are zero-padded /
left-justified into the 32-bit FIFO width (rpi4-osdev part9); for the
recommended s16→PWM conversion, scale the signed 16-bit sample to `0..RNG` and
write one word per channel sample.

**Memory-ordering note (datasheet §1.3).** BCM2711's AXI can return reads out
of order when switching peripherals. Place a DSB write-barrier before the first
write to a peripheral and a DSB read-barrier after the last read; the diag and
GENET code already do `__asm__ volatile("dsb sy")` around MMIO — follow that.

### 4.2 PCM/I2S path (Tier 6) — for external DACs

PCM/I2S block at `0xFE203000` (legacy `0x7E203000`). Register map (datasheet
PCM chapter): `CS_A` 0x00, `FIFO_A` 0x04, `MODE_A` 0x08, `RXC_A` 0x0C,
`TXC_A` 0x10, `DREQ_A` 0x14, `INTEN_A` 0x18, `INTSTC_A` 0x1C, `GRAY` 0x20.
GPIO 18=`PCM_CLK`, 19=`PCM_FS`, 20=`PCM_DIN`, 21=`PCM_DOUT` (ALT0). DMA uses
DREQ 2 (PCM TX). The PCM clock is `CM_PCMCTL`/`CM_PCMDIV` at CPRMAN offsets
`0x98`/`0x9C` ([forum t=92365](https://forums.raspberrypi.com/viewtopic.php?t=92365)),
same CTL/DIV layout as the PWM clock — sourced from PLLD for accurate audio
rates. The i.MX SAI driver
([`adc/ad7779/imx6ull/sai.c`](../../sources/phoenix-rtos-devices/adc/ad7779/imx6ull/sai.c))
is the structural reference for frame-sync / bit-clock / data programming,
though the register names differ. Slots into the same `pwmsnd` device interface
behind a `--backend i2s` flag.

### 4.3 HDMI audio (deferred)

VideoCore-owned on Pi 4. Two routes, both gated on display work:
(a) a VC4 mailbox audio interface (the Linux `bcm2835` staging driver routes
PCM through VCHIQ to the firmware) — Phoenix has a working mailbox property
client already (the diag `diag_mboxProp1in1out`/`diag_mboxGetClockRate` helpers,
[`diag-udp.c`](../../sources/phoenix-rtos-lwip/port/diag-udp.c) §"WiFi Tier 1a"),
so the mechanism exists but the audio-specific VCHIQ service does not; or
(b) direct HDMI0 register programming — flagged high-risk in
[`gpu-vc6-impl.md`](gpu-vc6-impl.md) (PLLH/BVB/HSM clock complex undocumented).
Defer until the GPU/HDMI track is further along; revisit (a) first.

### 4.4 USB audio (blocked)

USB Audio Class over xHCI would be the highest-quality path and the only one
needing no Pi-specific DAC knowledge. **Blocked**: the USB host is statistically
failing on Pi 4 today (cap-probe poisoning, PCIe-bridge degradation across
resets — [`docs/status.md`](../status.md), [`usb-xhci-impl.md`](usb-xhci-impl.md)).
Cross-reference and revisit once USB host is reliable; UAC then layers on the
USB stack with the same `/dev/audio` device contract.

## 5. File-level breakdown

```
sources/phoenix-rtos-devices/audio/                 # NEW greenfield tree
    Makefile                       # obj-y += pwmsnd/ (and i2ssnd/ at Tier 6)
    common/
        audio-dev.h                # public msg structs: audio_params_t,
                                   # mtDevCtl ops (audio_start/stop/...)
        pcm-format.c/.h            # s16le/u8 -> hardware-format conversion
    pwmsnd/
        Makefile                   # builds pwmsnd server
        pwmsnd.c                   # main: mmap blocks, portCreate,
                                   # create_dev("/dev/audio0"), msg loop
        bcm2711-pwm.c/.h           # PWM1 register map + CTL/DMAC/RNG/FIF1
        bcm2711-cprman.c/.h        # CM_PWMCTL/DIV clock bring-up (passwd 0x5A)
        bcm2711-dma.c/.h           # legacy DMA ch1 control-block + IRQ thread
        bcm2711-gpio.c             # GPIO40/41 ALT0 mux (reuses GPFSEL idiom)
    i2ssnd/                        # Tier 6
        i2ssnd.c, bcm2711-pcm.c/.h

sources/phoenix-rtos-utils/aplay/  (or .../psh applet)   # NEW
    aplay.c                        # read WAV, set params, stream to /dev/audio0

sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/
    board_config.h                 # add PWM1_BASE 0xfe20c800, CPRMAN_BASE
                                   # 0xfe101000, DMA_BASE 0xfe007000,
                                   # PWM1_DMA_CHANNEL 1, PWM1_DMA_IRQ
    user.plo.yaml                  # add pwmsnd server entry (Tier 4)
    config.txt                     # see §8: audio_pwm_mode / dtparam=audio
```

Scout code (Tiers 0–2) begins in
[`diag-udp.c`](../../sources/phoenix-rtos-lwip/port/diag-udp.c) as new single-
character sub-commands (see §7), then migrates into `audio/pwmsnd/` once the
register sequence is firm — exactly how the WiFi SDIO code began in diag-udp
(see [`wifi-bcm43455-impl.md`](wifi-bcm43455-impl.md) §6 P0–P2 note).

## 6. Phased delivery

| Phase | Scope | Success criterion | diag-UDP / UART signature | Est. |
|---|---|---|---|---|
| **P0** | Scout: `mmap` PWM1 `0xfe20c800`, CPRMAN `0xfe101000`, GPIO; read back CTL/DIV/STA/GPFSEL4. New diag sub-command. | All four registers read non-garbage; firmware clock state known | diag `'a'` → `CM_PWMCTL=… CM_PWMDIV=… PWM1_CTL=… GPFSEL4=…` | 1 wk |
| **P1** | Program PWM clock (osc/2 → 27 MHz), mux GPIO40/41 ALT0, enable PWM1 ch1+2 M/S+FIFO, hand-feed constants | clock `BUSY=1`, `PWEN1=PWEN2=1`, FIFO `FULL` toggles as fed | diag `'a'` shows `BUSY=1 PWEN=1 STA=…` | 1 wk |
| **P2** | PIO tone: CPU loop writes 440 Hz square/sine into FIF1, polling `STA.FULL` | **audible tone** on jack; no FIFO underrun reported | ear/scope; diag `'a'` underrun counter = 0 | 1 wk |
| **P3** | DMA streaming: legacy DMA ch1, DREQ 1, double-buffered, block-complete IRQ thread | continuous tone, no CPU spin, no gaps 60 s | UART `pwmsnd: dma blk N`; diag underrun = 0 | 2 wk |
| **P4** | `pwmsnd` server: `/dev/audio0`, `audio_params_t`, `mtWrite` ring → DMA; `aplay` WAV client | recognisable WAV plays from shell end-to-end | UART `pwmsnd: /dev/audio0 ready`; `aplay foo.wav` audible | 2 wk |
| **P5** | Format conversion (s16→PWM), software volume, 2-client mixer | two tones mix without clipping | shell plays two streams; no distortion | 2 wk |
| **P6** | PCM/I2S backend behind same device, `--backend i2s` | external I2S DAC plays the WAV | UART `i2ssnd: ready`; DAC audible | 3 wk |

Total to Tier 4 (first usable audio device): ~7 weeks focused. Each phase ends
with a `manifests/*.md` snapshot via
[`scripts/snapshot-integration-state.sh`](../../scripts/snapshot-integration-state.sh).

## 7. Test strategy

The project's hardware-experiment idiom is the **diag-udp UDP responder on
:9999** ([`diag-udp.c`](../../sources/phoenix-rtos-lwip/port/diag-udp.c)): each
inbound datagram's first byte selects a `diag_format_*` handler that prints
register/state text back over UDP (existing commands: `'g'` GPIO dump, `'c'`
clocks, `'w'`/`'i'`/`'f'` WiFi/SDIO, `'x'` xHCI). Bring-up here adds an audio
scout command in the `diag_udp_recv` switch (around
[line 5707](../../sources/phoenix-rtos-lwip/port/diag-udp.c)):

```c
else if (query == 'a') {           /* audio: PWM1 + CPRMAN + GPIO dump */
    len = diag_format_audio(body, DIAG_REPLY_MAX);
}
```

`diag_format_audio()` follows the existing `diag_format_gpio`/`diag_format_clocks`
shape: `mmap(MAP_DEVICE|MAP_UNCACHED|MAP_PHYSMEM)` the PWM1/CPRMAN/GPIO pages,
read the registers, `snprintf` them into the reply. For Tiers 1–2 the same
command can *also* drive the clock/PWM enable and start a tone (a `'A'` variant,
mirroring how `'R'`/`'X'` are active xHCI bring-up commands), so the whole
PWM pipeline is exercised from a UDP poke before any server exists.

Per test, by tier:

- **P0–P1 (registers).** UDP `'a'` reply parsed for expected bits. Capture via
  [`scripts/capture-rpi4b-uart.sh`](../../scripts/capture-rpi4b-uart.sh) for the
  UART side; the UDP reply is the primary signal. Use `--capture-secs 180+` so
  lwip is up before poking (see [`AGENTS.md`](../../AGENTS.md) test-cycle notes).
- **P2 (tone).** Headphones/scope on the 3.5 mm jack. Pass: audible 440 Hz, and
  the scout's FIFO-underrun counter stays 0 across a 10 s tone. This is the
  first *physical* signal — no automation, ear/scope confirmation, recorded in
  the manifest note.
- **P3 (DMA).** 60 s continuous tone with the CPU otherwise idle (confirm via
  the diag `'t'` thread/cpu stats that no CPU is spinning on the FIFO). Pass:
  no audible gaps, underrun = 0.
- **P4 (WAV).** A known 16-bit/44.1 kHz WAV (ship a short test clip in the
  image or TFTP it). Pass: the clip is recognisable end-to-end via `aplay`.
- **P5–P6.** Two simultaneous tones (mixer); I2S DAC plays the same WAV.

Regression: each phase snapshots via
[`scripts/snapshot-integration-state.sh`](../../scripts/snapshot-integration-state.sh),
restorable with `scripts/restore-integration-state.sh <manifest>`. The UART
summariser ([`scripts/uart-summary.sh`](../../scripts/uart-summary.sh)) should
grow recognition of a `pwmsnd: /dev/audio0 ready` stage line.

## 8. Inter-dependencies

Depends on (in order):

1. MMU on, EL drop to EL1 — already landed.
2. lwIP up (for the diag-udp scout channel) — already in tree; the GENET
   ethernet path is the bring-up shakedown for diag-udp itself.
3. DMA + cache coherency on BCM2711 — already exercised by GENET
   ([`bcm-genet.c`](../../sources/phoenix-rtos-lwip/drivers/bcm-genet.c):
   `dmammap`, `va2pa`, `MAP_CONTIGUOUS`/`MAP_UNCACHED`, `interrupt()` + cond +
   IRQ thread). Reuse that pattern; track any new shortcut under a `TD-xx` in
   [`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](../TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md).
4. GPIO function-select — the GPFSEL helpers already exist in diag-udp; a
   standalone GPIO/pinctrl driver ([`gpio-pinctrl-impl.md`](gpio-pinctrl-impl.md))
   would supersede the inline mux but is not a hard prerequisite.
5. Legacy DMA controller access from user space — confirm the `MAP_DEVICE`
   mapping of `0xfe007000` and that the chosen channel (1) is not claimed by
   firmware or another driver (the SD/SDIO path uses DMA; check ownership).

**config.txt interaction (open).** The firmware's own audio driver
(`dtparam=audio=on`, `audio_pwm_mode=2`,
[forum t=243555](https://forums.raspberrypi.com/viewtopic.php?t=243555)) may
claim the PWM/clock for the jack. For a bare-metal Phoenix driver we likely want
`dtparam=audio=off` so the firmware leaves PWM1 and the audio clock alone — but
the firmware must still have routed GPIO40/41 to the analog circuit. Determine
empirically at P0 (the scout's clock/GPFSEL readback tells us what the firmware
left behind). Mirror the [`config.txt`](../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt)
discipline the BT plan uses for `dtoverlay=miniuart-bt`.

Conflicts:

- **PWM0 control users.** A future servo/LED PWM driver would use PWM0
  (`0xfe20c000`); the audio engine is PWM1 (`0xfe20c800`). They share a 4 kB
  page and the **same PWM clock domain** — note from
  [search](https://forums.raspberrypi.com/viewtopic.php?t=243555): both PWMs can
  be fed the same clock, so reprogramming the PWM clock for one can disturb the
  other. If both are ever active, arbitrate the clock.
- **DMA channel allocation.** Channel 1 must not collide with SD/SDIO or any
  other DMA consumer. Decide a project-wide DMA-channel allocation map.

Independent of: USB (until USB audio), Bluetooth, WiFi, the GPU framebuffer
(unless HDMI audio is later attempted, which shares the mailbox/HDMI block).

## 9. Effort estimate

Honest range: **~2 developer-months** to take Tier 0 → Tier 4 (a working
`/dev/audio0` + WAV playback) against the current codebase, because the hard
parts (MMU, DMA, cache coherency, IRQ threads, lwIP scout channel) are already
solved and reusable from GENET. Breakdown:

- ~2 weeks PWM1 + clock + GPIO register bring-up to a PIO tone (P0–P2). The
  register sequence is well-documented by rpi4-osdev and Circle; risk is in the
  Pi-4 clock specifics and whatever state the firmware leaves the audio clock.
- ~2 weeks DMA streaming (P3). The GENET DMA pattern transfers directly; the new
  work is the double-buffer CB ring and the FIFO-pace DREQ wiring.
- ~2 weeks the `pwmsnd` server + `aplay` + format conversion (P4).
- ~2 weeks slack for the analog-quality reality (noise/crackle tuning, M/S vs
  PDM, range/divider sweet spot) and the config.txt ownership question.

Tier 5 (mixer) adds ~2 weeks; Tier 6 (I2S) adds ~3 weeks plus an external DAC.

## 10. Open questions / risks

- **Does the firmware leave PWM1 usable?** With `dtparam=audio=on` the firmware
  audio driver may own the PWM1 clock/engine. Need to determine (P0 scout) the
  right `config.txt` so the firmware routes GPIO40/41 to the jack circuit but
  does **not** keep the PWM engine. ([forum t=243555](https://forums.raspberrypi.com/viewtopic.php?t=243555))
- **Pi 4 analog routing reality.** A Pi engineer says the analog path is "the
  same as Pi 3" with a dedicated PWM
  ([forum t=243555](https://forums.raspberrypi.com/viewtopic.php?t=243555)), and
  GPIO40/41 PWM1 reaches the 4-pole jack
  ([forum t=295059](https://forums.raspberrypi.com/viewtopic.php?t=295059)) — but
  this has not been verified on *our* board; P2 (audible tone) is the proof.
  Low but nonzero risk that a board revision or firmware config breaks the path.
- **Oscillator vs PLL clock source.** rpi4-osdev uses the 54 MHz oscillator
  (`SRC=1`, `DIVI=2`); Circle sources a PLL at 125 MHz for Pi 4. The oscillator
  route is simpler and recommended first; if jitter/quality is poor, switch to a
  PLL source (`SRC=6` PLLD) with MASH noise-shaping (datasheet §5.4, "MASH …
  push fractional-divider jitter out of the audio band").
- **DMA channel ownership.** Channel 1 may be contended; the legacy DMA
  controller at `0xfe007000` is shared with SD/SDIO. Need a project DMA-channel
  allocation map and a check that the firmware/other drivers don't own ch1.
- **DMA address translation.** The DMA engine needs **legacy `0x7E…`**
  peripheral addresses and bus addresses for the sample buffer; confirm Phoenix's
  `va2pa`/`dmammap` produce DMA-visible addresses in the window the DMA4/legacy
  engine expects (GENET works, so the precedent is good, but PWM FIFO dest is a
  fixed `0x7E20C818` not a RAM buffer — verify the engine accepts a peripheral
  dest with `DEST_DREQ`).
- **8-bit vs higher resolution.** rpi4-osdev uses 8-bit unsigned; real audio
  wants ≥11-bit (RNG=612 gives ~9.3 bits, RNG=2048 at a higher clock gives 11).
  The range/clock trade-off (higher range = more bits but lower max sample rate
  for a fixed PWM clock) needs a deliberate pick at P2.
- **Memory ordering.** BCM2711 AXI out-of-order reads (datasheet §1.3) — ensure
  DSB barriers around PWM/DMA MMIO and in the IRQ handler, per the GENET/diag
  precedent.
- **config.txt `audio_pwm_mode`.** Value `2` selects the firmware's newer,
  lower-crackle driver — irrelevant once Phoenix owns the engine, but a
  reference for the M/S-mode / dithering quality target.

---

This plan supersedes nothing currently in tree. It becomes the active step in
[`tracking/current-step.md`](../../tracking/current-step.md) and is referenced
from [`docs/status.md`](../status.md) when audio bring-up is scheduled.

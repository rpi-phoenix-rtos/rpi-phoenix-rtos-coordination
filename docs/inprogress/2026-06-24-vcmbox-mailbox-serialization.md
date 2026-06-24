# VideoCore mailbox serialization: the rpi4-vcmbox server

Date: 2026-06-24
Status: server + libvcmbox + thermal conversion landed (build-verified, not yet HW-validated)
Component: `sources/phoenix-rtos-devices/misc/rpi4-vcmbox/`

## The problem

The BCM2711 exposes exactly one VideoCore property mailbox: a single hardware
FIFO at `0xfe00b880`, channel 8, with **no hardware arbitration**. It is the only
path to several things multiple Phoenix processes need at boot:

| Client | File | Tag / use |
|---|---|---|
| rpi4-thermal | `sensors/rpi4-thermal/rpi4-thermal.c` | GET_TEMPERATURE 0x030006, GET_MAX_TEMP 0x03000a, GET_THROTTLED 0x030046 |
| bcm-genet | `phoenix-rtos-lwip/drivers/bcm-genet.c` | GET_BOARD_MAC 0x10003 (returns **2 words**) |
| usb (VL805 bring-up) | `phoenix-rtos-devices/usb/xhci/bcm2711-pcie.c` | NOTIFY_XHCI_RESET / firmware reload |
| bcm2711-sdio | `phoenix-rtos-devices/storage/bcm2711-emmc/bcm2711-sdio.c` | clock/power query |
| v3d power-on | `tools/v3d-driver-port/v3d_phoenix_power.c` | SET_DOMAIN_STATE / power |
| diag-udp | `phoenix-rtos-lwip/port/diag-udp.c` (if present) | assorted diag tags |

Until now each had its **own copy** of the mailbox protocol and drove the FIFO
directly with no cross-process lock. The read loop pops FIFO entries and discards
any whose channel-packed address word does not match its own request — so when
two clients overlap, one client's read loop **consumes and discards another's
response**, destroying it. The victim sees a transient mailbox failure.

## The three intermittency-consistent failure modes

All three look like the same "transient mailbox failure" at the call site, but
have different root causes. The server's failure path now distinguishes them:

1. **Cross-process race (#1).** Another client's response was popped and
   discarded (or ours was, by them). Server diagnostic: it consumed a
   non-matching FIFO entry — logs the foreign channel-packed address word.
2. **Buffer above VC-addressable range (#2).** The per-call property buffer was
   `mmap(MAP_CONTIGUOUS|ANONYMOUS)` + raw `va2pa`, and on a 4/8 GB boot it could
   land at a physical address the VideoCore cannot reach, so firmware never
   replies. Server diagnostic: the FIFO stayed EMPTY the whole spin; the startup
   banner also logs the (single, early-allocated) bounce-buffer PA so a high
   address is visible.
3. **Firmware matched but returned non-OK (#3).** The request reached firmware
   and our response surfaced, but `msg[1] != RESP_OK`. Server diagnostic: logs
   the firmware response code.

## The server design

`misc/rpi4-vcmbox/` builds two artifacts (one Makefile, `static-lib.mk` +
`binary.mk`):

- **`rpi4-vcmbox`** — the server daemon. Registers `/dev/vcmbox` (`create_dev`,
  via the `devfs` named port, so it needs neither `/` nor the `/dev` bind) and
  runs a single-threaded msg-loop.
  - mmaps the mailbox MMIO **once** at startup (`MAP_DEVICE|MAP_UNCACHED`).
  - Allocates **one** reusable uncached-contiguous bounce buffer **once**, early,
    so it lands low / VC-addressable (addresses #2). Reused for every call;
    uncached, so no clean/invalidate is needed around the round-trip.
  - The msg-loop handles one `mtDevCtl` "property call" at a time. Because a
    Phoenix server processes one message at a time, **all calls are naturally
    serialized** — no two clients ever drive the FIFO at once (kills #1).
  - **Internal retry**: up to 8 attempts with a 2 ms sleep between, on any failed
    transaction (handles residual leftover-FIFO traffic from any pre-server
    client that ran before the server registered).
  - On ultimate failure, logs the one-line discriminating diagnostic above.
- **`libvcmbox`** (`libvcmbox.c` / `.h`) — the client veneer. `vcmbox_call(tag,
  valBufSize, in, nIn, out, nOut)` (general, multi-word) and the convenience
  wrapper `vcmbox_prop(tag, arg_in, *out)` (1-in/1-out). It retry-looks-up
  `/dev/vcmbox` (blocks until the server registers) and sends one `mtDevCtl` msg.

The mailbox protocol code now lives **only** in the server.

## Wire format (the contract for converting the other clients)

A property call is an `mtDevCtl` message; request and response live **entirely in
`msg.i.raw` / `msg.o.raw`** (no `msg.i.data` buffer). This keeps the path usable
in low-level driver processes (genet inside lwip, the usb daemon, sdio) that run
before posixsrv/`/` and cannot rely on libc `open()`+`ioctl()`. The structs are
in `libvcmbox.h`:

```c
#define VCMBOX_MAX_WORDS 12u   /* fits in the 64-byte raw region */

typedef struct {              /* in msg.i.raw */
    uint32_t tag;             /* VideoCore property tag */
    uint32_t valBufSize;      /* firmware value-buffer size in bytes (e.g. 8) */
    uint32_t nIn;             /* number of valid words in in[] */
    uint32_t in[VCMBOX_MAX_WORDS];
} vcmbox_req_t;

typedef struct {              /* in msg.o.raw */
    int err;                  /* 0 on success, negative errno on failure */
    uint32_t nOut;            /* number of valid words in out[] */
    uint32_t out[VCMBOX_MAX_WORDS];
} vcmbox_resp_t;
```

The server builds the firmware message as
`[bufsize, REQUEST(0), tag, valBufSize, reqresp(0), in0..in(valBufSize/4-1), END(0)]`
— i.e. `valWords = valBufSize/4` value slots, the input words copied in, the rest
zeroed for firmware to fill — and copies `valWords` back into `out[]`.

- **1-in/1-out** (thermal temp/max/throttle, V3D power, most clients):
  `vcmbox_prop(tag, arg_in, &out)` → `valBufSize=8`, `nIn=1`, `nOut=1`.
- **2-out** (genet `GET_BOARD_MAC` 0x10003): use `vcmbox_call(0x10003, 8, NULL,
  0, macWords, 2)` — `valBufSize=8`, **two** response words (MAC lo/hi). This is
  exactly why the struct is multi-word from day one; `vcmbox_prop` would only
  return the first word.

`o.err` at the msg layer is the IPC status (`EOK` = the message was handled); the
**property-call** status is `vcmbox_resp_t.err` inside `o.raw`. `vcmbox_call`
returns the resp `err` (so callers get `0` / `-EIO` directly).

## Boot ordering

`rpi4-vcmbox` must launch **before any mailbox client**, and only needs the
`devfs` named port (for `create_dev`) — not `/`, the `/dev` bind, or posixsrv.

- **netboot / sd**: launched right after `pl011-tty`, gated `!= nfsroot`. This is
  before the **sd ext2-root `bcm2711-emmc -r`** (which runs before
  mkdir/bind/posixsrv), before thermal, usb, lwip, v3d — i.e. before every
  mailbox user.
- **nfsroot**: launched in the pre-takeover block, after `bind devfs /dev` and
  before posixsrv/lwip (lwip = a future genet MAC-read client). `/dev/vcmbox` is
  created in the `devfs` process, which the NFS takeover re-binds onto the new
  root, so the node survives the takeover with **no relaunch** (the server
  process is never killed).

Two `-x rpi4-vcmbox` occurrences under **mutually-exclusive `if:` gates** → exactly
one rendered launch per variant → no alias collision (same dual-placement pattern
`rpi4-thermal` already uses). Verified: the netboot loader.disk renders exactly
one `app ram0 -x rpi4-vcmbox` line and one `alias -r rpi4-vcmbox`.

## Rollout plan — converting the remaining clients (validated waves)

Each wave: convert one client to `libvcmbox`, build `--scope core` for the
relevant variant, then HW-validate one boot before the next. The thermal
conversion in this change is the proof; the rest follow this template. After
each, remove that client's now-dead local mailbox protocol code + register
`#define`s (keep its property-tag constants).

### Wave 1 — bcm-genet MAC read (low risk)
- File: `phoenix-rtos-lwip/drivers/bcm-genet.c`, the `rpi4_mboxProp*` /
  GET_BOARD_MAC path (~lines 241–320).
- **Multi-out**: `vcmbox_call(0x10003, 8, NULL, 0, mac, 2)` (2 words). The driver
  already has a fallback MAC path if the read fails, so the failure mode is
  benign.
- Build: lwip needs to link `libvcmbox` — the lib lives in phoenix-rtos-devices,
  so either expose it cross-repo (the `libusbxhci`/`USB_HCD_LIBS` precedent in
  `build-core-aarch64a72-generic.sh`) or, simpler, link the small `.o` /
  duplicate the header include path. **Risk**: cross-repo lib linkage — resolve
  the include/link path before converting; this is the main blocker for the
  lwip-resident and usb clients.
- Rollback: revert the one file; the local protocol code is git-tracked.

### Wave 2 — v3d power-on (low risk, isolated process)
- File: `tools/v3d-driver-port/v3d_phoenix_power.c`.
- 1-in/1-out (`SET_DOMAIN_STATE` style). Isolated harness process; a regression
  kills only the V3D demo, not the boot. Same cross-repo link consideration
  (tools build).
- Rollback: revert the one file.

### Wave 3 — bcm2711-sdio (medium risk, on the storage path)
- File: `phoenix-rtos-devices/storage/bcm2711-emmc/bcm2711-sdio.c`.
- Same repo as the lib → clean `DEP_LIBS := libvcmbox` link. **Risk**: storage is
  on the sd-boot critical path and the sd variant launches `bcm2711-emmc -r`
  *very* early (right after vcmbox). Confirm vcmbox is up before the emmc `-r`
  mount (it is, per boot ordering above). Validate on the **sd** variant.
- **Pre-bind lookup hazard**: in the sd variant `bcm2711-emmc -r` runs *before*
  `mkdir /dev` + `bind devfs /dev` — the same pre-bind window where the emmc root
  mount had to fall back to resolving `devfs/<name>` instead of `/dev/<name>`.
  `libvcmbox` currently does an **unbounded** `lookup("/dev/vcmbox")`, which would
  block forever in that window. Before Wave 3, give the client a `devfs`-port
  resolve (resolve `vcmbox` against the `devfs` named port, falling back from the
  literal `/dev/vcmbox` path) the same way `bcm2711-emmc` resolves its root device
  — otherwise an early sdio client hangs the boot.
- Rollback: revert the one file.

### Wave 4 — usb VL805 bring-up (highest risk)
- File: `phoenix-rtos-devices/usb/xhci/bcm2711-pcie.c` (`bcm2711NotifyXhciReset`
  and any other mailbox calls in PCIe/VL805 bring-up).
- **Risk**: USB enumeration reliability is historically fragile (#129/#142); the
  mailbox call here triggers the VL805 firmware reload. Any timing change is a
  regression risk. Convert last, and bench it (multi-trial enum-rate, not a
  single boot) before declaring it good. The usb daemon is built with
  `USB_HOSTDRV_LIBS`/`USB_HCD_LIBS` machinery — work out the `libvcmbox` link
  through that path.
- Rollback: revert the one file; keep the fallback so a mailbox failure degrades
  the same way it does today.

### diag-udp
- File: `phoenix-rtos-lwip/port/diag-udp.c` if still present (it was largely
  removed in the 2026-06-06 upstream-readiness cleanup). Convert opportunistically
  if it still drives the mailbox; otherwise drop it from the list.

## Cross-cutting risk

The biggest unknown is **cross-repo lib linkage**: `libvcmbox` lives in
phoenix-rtos-devices, but genet/diag-udp (lwip) and the v3d harness (tools) are
other repos. Same-repo clients (thermal ✓, sdio, usb) link it cleanly via
`DEP_LIBS`. Before Wave 1, settle how lwip/tools pull in the lib + header (mirror
`libusbxhci`'s `USB_HCD_LIBS` cross-repo pattern in
`build-core-aarch64a72-generic.sh`, or build the lib into a location both repos'
include/lib search paths see). This is documented here so the orchestrator does
not rediscover it mid-wave.

## Validation status

- Build: `./scripts/rebuild-rpi4b-fast.sh --scope core --variant netboot` → clean.
- Bundle: `strings loader.disk | grep vcmbox` shows the binary, `/dev/vcmbox`,
  the three failure diagnostics, and exactly one `app ram0 -x rpi4-vcmbox`.
- Thermal: old `mailbox temperature read failed` string is gone; new banner
  present. Conversion is the first proof of the server.
- **Not yet HW-validated** (this session is build-only; the UART is reserved for
  the orchestrator). First HW boot should show:
  `rpi4-vcmbox: mailbox @ 0xfe00b880, bounce buf_pa=0x... ; registered /dev/vcmbox`
  followed by the thermal banner with real T/max/throttle (proving thermal read
  through the server), and zero `FAILED` diagnostics.

---
name: rpi4-run
description: >-
  Boot the current Phoenix-RTOS image on the real Raspberry Pi 4 and run
  commands / capture results. Use whenever you need to run something on the Pi:
  execute a binary at the psh prompt (e.g. a test), capture a boot log, or grab
  the screen — over SD boot or NFS/netboot root. This is the stable, repeatable
  Pi test-run recipe; follow it instead of re-reading scripts/*.sh each time.
---

# Running things on the Raspberry Pi 4

One physical Pi 4, driven over UART (`/dev/ttyUSB0`) with switched power and an
optional HDMI capture card. **Only one Pi cycle at a time** — the UART is
exclusive; a second concurrent cycle gets an empty log. Builds are parallelizable;
the boot/UART step is not.

Host facts (this machine): `/dev/nvme0n1` is the system SSD — never touch it.

⛔ **Address removable storage by `/dev/disk/by-id/`, never by `sdX`.**
This file used to say "`/dev/sda` is *always* the SD card (safe to `dd`, no identity
check)". That was **true when it was written**, and for a good reason: the card reader
was the only removable device ever attached, so `sda` could not be anything else. It is
not a rule about the host, it is a consequence of that one-device assumption — and the
assumption ended on **2026-09-20**, when a USB stick joined the bench (SanDisk Ultra,
serial `A2004263F6063324`, currently `/dev/sda` *because the reader is unplugged*).

With **both** a reader and a stick attached, `sda`/`sdb` are handed out in **enumeration
order**, which is not stable across boots or replugs. So the node name carries no
identity at all, and a `dd` aimed at a remembered name can land on the wrong device.
Use the stable path instead — it contains the model and serial:

```
ls -l /dev/disk/by-id/usb-*          # stable; pick the one you mean
lsblk -o NAME,SIZE,TRAN,MODEL,SERIAL # confirm size + serial before writing
```

ⓘ Normally this does not come up: the SD card lives permanently in the Pi and is written
*by Phoenix itself* (`rpi4-flash-sd`), so there is usually no reader on the host at all.

Toolchain + buildroot are under the repo (`.toolchain/`, `.buildroot/`).

✅ **SD boot WORKS and is verified (2026-09-16/17).** A card written with a Phoenix
2-part image boots the Pi: plo → kernel → fbcon → pcie → xhci → psh, `mmcblk0p2`
ext2 root, 0 faults, and the games run off it. Drive it with
`scripts/test-sd-boot.sh`, or grade the whole showcase on the card with
`scripts/run-showcase-gate.sh --sd-boot`.

✅ **With a BOOTABLE card you get BOTH lanes, selected by dnsmasq — leave the card
in.** Measured 2026-09-17: card in + dnsmasq **up** → the Pi **netboots** (syspage
`'nfs;/;10.42.0.1;/;v4;takeover'`, `nfs-fs: start`, psh, 0 faults); card in +
dnsmasq **down** → it **SD-boots** (`'bcm2711-emmc;-r;/dev/mmcblk0p2:ext2'`). The
EEPROM's network-first `BOOT_ORDER=0xf12` does exactly what it says, so switching
lanes costs one `netboot-server-{up,down}.sh` and a power cycle — no card swap.
ⓘ `bcm2711-emmc` appears in the netboot syspage too, as a plain driver; grade the
lane by the **root** argument or `nfs-fs: start`, not by that string.
* So: `netboot-server-down.sh` → SD boot; `netboot-server-up.sh` → netboot.
* With a **BLANK** card in, the Pi **does not boot at all** — measured over 4
  power-ons: 0 DHCP, 0 UART, black HDMI. It does not fall back to the network. So
  you cannot netboot Linux to flash a blank card in the slot; write the card on the
  host first (it appears as a USB reader — **check the device, never assume
  `/dev/sda`**) or have it written elsewhere.
* Card **out** ⇒ netboot as usual.

## Pick the scenario

| Goal | Boot mode | Card | Recipe |
|---|---|---|---|
| Run a binary/command at psh + capture its output | SD (self-contained) | in Pi | A |
| Same, over the network | netboot / nfsroot | in Pi (**written**) | B |
| Just capture a boot (no interaction) | either | — | C |
| Build a fresh image + get it onto the card | — | in host | D (then A) |

`sd` mounts a local ext2 root; `nfsroot` mounts the NFS export as `/`; `netboot`
uses a RAM root.

⛔ **A BLANK card in the slot stops this Pi booting AT ALL.** Measured 2026-09-16
with a blank microSD inserted: four power-ons produced **zero DHCP requests, zero
UART bytes and a black HDMI frame**, where the same bench had netbooted 10/10 an
hour earlier with the slot empty. It does not fall back to network; it simply does
not come up. ✅ **A WRITTEN card does not do this** — it netboots normally when
dnsmasq is up (measured 2026-09-17, see the top of this file), which is why the
bench now keeps a written card in permanently and Phoenix flashes it in place
(`rpi4-flash-sd`). The rule is therefore: **leave a written card in; never leave a
blank one in.**

⚠ **Retraction:** an earlier edit of this file claimed the opposite — that the
EEPROM's `BOOT_ORDER=0xf12` (network first, SD second, written 2026-05-21) meant a
card could not block netboot. The config is real and documented
(`docs/misc/2026-09-02-pi-firmware-pin-revisit.md:489`), but the hardware disagrees,
and hardware wins. Whatever the bootloader does with a non-bootable card, it is not
an orderly fall-through. Untested: a card holding a **bootable** image may well
behave differently (that is the SD-boot path itself) — only the blank-card case is
measured.

## A — Run commands over SD boot (most common)

Card is already in the Pi. `test-cycle-psh-interact.sh` power-cycles, waits for the
`(psh)%` prompt, sends each command, captures per-command UART output, then powers
off. Skip the netboot server for SD.

```
./scripts/test-cycle-psh-interact.sh --skip-server-up --label <label> -- \
  "<command 1>" "<command 2>" ...
```

- Each `"<command>"` is one psh line (quote the whole thing incl. its args).
- Defaults: `--wait-secs 150` (max wait for prompt), `--idle-secs 20` (capture
  window after each command), `--max-cmd-secs 120`. Raise `--idle-secs` for a
  long-running command; raise `--wait-secs` only if boot is unusually slow.
- Bash tool `timeout`: set ≥ `(wait_secs + n_cmds*(idle+3) + 120) * 1000` ms.
  For ~4 quick commands, `480000` (8 min) is safe.
- UART log: `artifacts/rpi4b-uart/rpi4b-uart-<ts>-<label>.log`. HDMI snapshots (if
  `/dev/video4` present): `artifacts/hdmi/` (periodic `-tick.png` + a `-final.png`).
  Older snapshots are archived by month: `artifacts/hdmi/YYYY-MM/` —
  `scripts/archive-hdmi-snapshots.py` files away everything not dated today (safe to run any
  time; the graders search both the in-tray and the month folders).

### Grading anything VISUAL: use a readiness marker, or the run may score as a failure

HDMI snapshots run on a fixed cadence from power-on, so a slow-starting program
(any GPU game; vkQuake especially, since its 67 shader modules push first frame
out) can have **every** frame land during loading. The visual grader then has
nothing at the reference viewpoint and the run looks like a failure of whatever it
was testing. Two knobs fix that (added 2026-09-04):

```
./scripts/test-cycle-psh-interact.sh --label vkq --idle-secs 175 --max-cmd-secs 200 \
  --ready-line 'vkquake-drm flipstat' --ready-extra-secs 90 --hdmi-dense-on 'vkquake-drm flipstat' \
  -- "/usr/bin/vkquake"
```

- `--ready-line ERE` — until it matches, `--max-cmd-secs` is only the deadline for
  **reaching** readiness; once matched, capture is guaranteed `--ready-extra-secs`
  longer. A run that never matches says so explicitly, so you can tell "never
  started" from "started and failed".
- `--hdmi-dense-on ERE` — snapshot every 5 s (not 15 s) once the marker appears.
  Measured effect on vkQuake: 20 frames / 13 at the reference viewpoint, vs
  13–16 / 6–9 before.

`test-cycle-bench.sh` takes the same four flags, so a pass-RATE bench can use them:

```
./scripts/test-cycle-bench.sh 4 vkq --idle-secs 175 --max-cmd-secs 200 \
  --ready-line 'vkquake-drm flipstat' --ready-extra-secs 90 -- "/usr/bin/vkquake"
```

Games launch through their launchers, never the engine ELF: `/usr/bin/quakespasm`,
`/usr/bin/quake2`, `/usr/bin/quake3 +map q3dm1`, `/usr/bin/vkquake` (it already adds
`+map start`), `/bin/stk`; `game-res <game> WxH` for a scaled mode. The launchers set
the video mode, the data path and the map. Hand-rolling the engine command line
(`/usr/bin/*-drm`) is how a run ends up at the wrong size or double-loading the map —
both look exactly like render regressions. The proven gate commands are the entries
of `scripts/run-showcase-gate.sh`. Every game prints `<name> flipstat N frames in
T ms = X fps (total M)` lines once it presents frames — the readiness marker to use.
Desktop cycles: `/bin/bash /bin/xfce-session` (with `export HOLD=N` first) and
`/bin/bash /bin/startx action` (`export HOLD=200`); both end on their own when HOLD
is over.

Example (this is how the libc suite is run):
```
./scripts/test-cycle-psh-interact.sh --skip-server-up --label libc -- \
  "/bin/test-libc-string -v -g string_memmem" \
  "/bin/test-libc-misc -v -g langinfo"
```

## B — Run commands over netboot / nfsroot

Leave the (written) card in — dnsmasq up selects netboot. Do NOT pass
`--skip-server-up` (dnsmasq/TFTP must be up):

```
./scripts/netboot-server-up.sh                    # if not already running
./scripts/test-cycle-psh-interact.sh --label <label> --inter-cmd-secs 8 -- "<cmd>" ...
```

Raise `--inter-cmd-secs` (e.g. 8) so a post-takeover command lands after the NFS
root has mounted.

## C — Capture a boot only (no commands)

```
./scripts/test-cycle-netboot.sh --sd-boot --capture-secs 180   # SD boot (dnsmasq down)
./scripts/test-cycle-netboot.sh --capture-secs 240             # netboot (dnsmasq up)
```

`--capture-secs` ≥ 180 to see user-space/lwip; set the Bash `timeout` to
`(capture_secs + 80) * 1000` ms. Then analyze the boot stages:

```
./scripts/uart-summary.sh <label|path>     # stage health table (psh prompt, lwip, faults)
./scripts/uart-list.sh                      # recent UART logs
```

## D — Build a fresh image + flash the SD card

Build the variant you need (add `--with-tests` to include `/bin/test-libc-*`,
`--with-ports` for busybox etc., `--with-showcase` for the launchers' helper programs — every GPU program is a port):

```
./scripts/rebuild-rpi4b-fast.sh --variant sd --with-tests     # -> artifacts/rpi4b/rpi4b-sd-2part.img
```

Flash to the card — ⛔ **not possible on this bench** (no card, no reader; see the
top of this file). Kept for when a card is available; `/dev/sda` was the reader's
node historically, so re-confirm the device before trusting it:

```
lsblk -o NAME,SIZE,TRAN,MODEL,SERIAL        # IDENTIFY the card; do not assume /dev/sda
DEV=/dev/disk/by-id/<the-card-you-just-identified>
udisksctl unmount -b "${DEV}1" 2>/dev/null || true
sudo dd if=artifacts/rpi4b/rpi4b-sd-2part.img of="$DEV" bs=4M conv=fsync status=progress
sync
```

Optional integrity check (cheap, catches a bad write): read back N=`ceil(size/4M)`
blocks and compare SHA to the source image. Then tell the user to move the card to
the Pi (or, if you flashed it, it's ready — proceed to recipe A).

Notes:
- `rpi4b-sd-2part.img` is the real bootable card (FAT boot + ext2 root).
  `rpi4b-sd.img` is FAT-boot-only (no rootfs) — don't flash that for SD boot.
- `--scope`: `auto` (default) is fine after a normal edit; use `--scope core`
  after a committed kernel/devices/usb/lwip/libphoenix change (stale-core hazard),
  `full-clean` from cold.

## ⚠ Give the Pi a real power-off settle between cycles

Back-to-back cycles with no gap can corrupt the next boot into a **runaway kernel
print loop**. Measured 2026-09-17 after firing three SD cycles in a `for` loop with
no pause: a 60 MB UART log, **2 057 279 lines before the psh prompt**, only *3
unique lines* among the first 20 000 —

```
vm: [36864.][32768x][770048.]
map: enter
```

A healthy boot has **exactly one** `map: enter` and a ~7 KB log, so that count is
the cheap discriminator. ⓘ It is NOT the "stale buffer while the Pi is off" host
artefact — these are real kernel prints, and the boot still reached psh with 0
faults, just after two million lines.

`test-cycle-*.sh` always powers off on exit, but the *next* power-on can still be
too soon. A `pi_power_off.sh` + `sleep 45` before the next cycle cleared it
immediately. Prefer the bench/gate scripts, which pace themselves, over hand-rolled
`for` loops.

## Long runs: detach with `setsid`, or the tool timeout kills them

A gate (~45 min) or a multi-trial bench (~20 min) outlives any single Bash tool
call. `nohup … &` is **not** enough on its own — when the tool call that spawned
it is torn down, the child goes with it. Measured 2026-09-17: a 6-trial bench died
after trial 1, twice, with `rc=143` (SIGTERM) and no summary line, which reads
exactly like a script bug.

```
setsid nohup ./scripts/run-showcase-gate.sh --label mygate > gate.log 2>&1 < /dev/null & disown
```

Then poll the log from later calls (`grep -aE '^--- \[' gate.log`) or wait on a
condition. ⚠ A truncated run is easy to misread as a failure of the thing under
test: check for the script's own final summary before concluding anything, and
`rc=143` means killed, not failed.

## ⛔ First check every cycle: did it produce data at all?

`check-capture-complete.py` already prints the one line that matters, and it is easy to scroll
past because the cycle still ends with `power off` and `done`:

```
CAPTURE: could not find the last command in the log — the cycle may not have
reached the psh prompt at all
```

**That is a VOID RUN.** The command was never echoed, so nothing ran — whatever else the log
contains says nothing about your change. It has appeared **12 times in one session**, so it is a
regular transient, not an exotic failure. Re-run it; do not interpret it, and do not "read around"
it by grepping for a number that happens to be present from the boot banner.

Two companions, both of which have produced confident wrong readings here:

- **`dd` with no `records out` line aborted**, whatever the output file's size looks like. Check
  for the summary before trusting an image.
- **Assert the work happened.** A loop over a missing directory reports 0 s; a `grep -c` on a file
  the cycle never wrote reports 0 matches, which can be the result you were hoping for.

⚠ When a cycle is void, say so and re-run. A void run reported as a result is worse than no run,
because it becomes a number somebody else quotes later.

## Reading results

```
log=$(ls -t artifacts/rpi4b-uart/rpi4b-uart-*-<label>.log | head -1)
grep -aE 'TEST\(|Tests .*Failures|^OK$|^FAIL|Exception|Data Abort|Fatal' "$log"
```

Use `grep -a` (logs have binary bytes). For Unity tests, look for the
`N Tests M Failures` summary + `OK`/`FAIL`; a failing assertion prints its
`file:line` + expected/actual. If a stage is missing (no psh prompt, no lwip),
the capture was too short — raise `--wait-secs`/`--capture-secs`, don't blame the
harness. "Slow boot"/"truncated" is usually a crash: grep the whole log for
`Exception`/`Data Abort` and check the last few HDMI frames (the final one is
often black from power-off).

### On a Data Abort, ADDR2LINE THE PC FIRST

The dump prints `pc`, and the unstripped binary from the build that made the
image is right there:

```
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-addr2line -f -C \
    -e .buildroot/_build/aarch64a72-generic-rpi4b/prog/<process> 0x<pc>
```

One command, and it names the function and line. Do this **before** forming any
theory from the register values. A fault was mis-attributed for a whole session
to a recent change — same session, same stressed code path, and an `x19` that
looked like a plausible byte count for it — when `addr2line` would have pointed
at an entirely different function immediately.

Two companions:

- **Grade the fault by the faulting PROCESS**, printed as
  `in thread N, process "name" (PID: n)`. A test that dies because the driver
  serving its root filesystem died is a bystander, not the culprit.
- **Read the registers as an equation.** Divide a suspicious offset by the
  `sizeof` of the array being indexed and see whether the quotient appears in
  another register. Two registers agreeing on one number is proof; one register
  that "looks about right" is not.
- A **static inline** resolves to its own line. If `addr2line` lands on pure
  arithmetic that cannot fault, the faulting access is the caller's — that is
  the answer, not a wrong one.

## Recording the screen (for a demo / published video)

The cycles grab periodic PNGs (`artifacts/hdmi/*-tick.png`) — right for grading, useless as a
demo. For continuous video use `./scripts/record-hdmi.sh` (output:
`artifacts/hdmi-video/<ts>-<label>.mp4`, H.264 + faststart so it plays anywhere).

The capture card is a **single-opener** V4L2 device, so the recorder and a cycle's snapshotter
cannot both hold it. Disable the snapshots for that cycle and run the recorder alongside — the
recorder only reads the grabber, so it neither touches the UART nor takes the Pi lock:

```
RPI4B_HDMI_INTERVAL=0 ./scripts/test-cycle-psh-interact.sh --label demo \
    --wait-secs 150 --idle-secs 240 --max-cmd-secs 300 -- "export HOLD=200" "/bin/bash /bin/startx action"
# in parallel:
./scripts/record-hdmi.sh --label demo --secs 240
```

The script fails early with that instruction if the device is already held.

## Multi-trial benches: always pass a command

`./scripts/test-cycle-bench.sh N <label> --capture-secs S` with **no** `-- <cmd>` (the
boot-only path) stops after trial 1. Give it a command instead — the psh path is reliable and
captures the same boot evidence:

```
./scripts/test-cycle-bench.sh 3 mylabel --idle-secs 18 --max-cmd-secs 40 -- "/bin/mem"
```

Notes checked on 2026-09-08, so you don't re-derive them: `rc=143` from
`test-cycle-netboot.sh --capture-secs S` is **normal** — it is what the capture watchdog killing
the serial tool produces, and the capture is complete and usable. It also powers the Pi off
correctly (plug queried immediately after: OFF). Why the bench stops anyway is not root-caused.
To check the plug yourself: `/home/houp/meross-plug/plug.py status`.

## Gotchas

- One Pi cycle at a time (exclusive UART). Wait for a cycle to finish before the next.
- SD boot is fast (~30–60 s to psh); netboot adds DHCP/TFTP.
- The cycle powers the Pi OFF on exit (EXIT trap). A re-run with the card in needs
  no re-flash — just run recipe A again (power-cycle only).
- If you change these scripts to make a scenario easier, update this skill too.

---

# ⛔ THE PI LOCK — read this before launching any cycle

"One cycle at a time" above is the single most expensive rule in this project to
get wrong, and the failure is not graceful: a second concurrent cycle makes
`psh-interact.py` die with

```
serial.serialutil.SerialException: device reports readiness to read but returned
no data (device disconnected or multiple access on port?)
```

and it kills **both** runs — the new one gets an empty log AND the one that was
already measuring is destroyed. Three collisions in one night, ~20 min each.

## The rule: do NOT write a waiter. Let the harness serialize you.

```
# CORRECT
Bash(command="./scripts/test-cycle-psh-interact.sh --label foo … ", run_in_background=true)
# then STOP. Wait for the task-notification. That notification IS the lock.
```

A cycle takes 4–10 min and will usually be backgrounded by the tool timeout
anyway. When the notification arrives, the UART is free.

⛔ **A COMPLETE-LOOKING LOG IS NOT COMPLETION.** This is the subtlest version and
it has caused a collision on its own: the UART log can contain every `TAG-` line
including `TAG-DONE` while the cycle is still in its idle window and its EXIT
trap has not yet powered the Pi off. Launching then means the OLD cycle's trap
cuts power to the NEW cycle mid-boot — you get a ~700-byte stub ending at the
plo banner, and the new run's `psh-interact.py` then waits out its full
`--wait-secs` for a prompt that can never come.

Read the log to learn the RESULT; wait for the NOTIFICATION to learn the port is
free. They are different events and the gap between them is minutes.

⛔ **THE SAME RULE COVERS BUILDS.** "Never build while a cycle runs" is not a
separate rule — it is this one. A `rebuild-*.sh` ends in the image stage and
overwrites the TFTP `loader.disk`, so starting a build after reading a
complete-looking log destroys the still-running cycle exactly as a second cycle
would. Reading a result and immediately kicking off a build is the most natural
thing to do and it has broken a run here. **Wait for the notification before a
cycle OR a build.**

ⓘ Watch for a long cycle: N commands at `--idle-secs S` can run for N×S seconds.
A four-command run at `--idle-secs 300` holds the port for up to 20 minutes while
its log has looked finished for most of them.

## The rule that actually prevents this: LAUNCH LAST

Six collisions in one session, and every one had the same shape: read a log
mid-turn, then **keep acting** — start a build, launch the next cycle. Knowing
the rule did not stop it, because the momentum of "I have the answer, now do the
next thing" is what causes it.

**So make it structural: a cycle launch is the LAST action of the turn.** After
`Bash(run_in_background=true)` on a Pi cycle, do nothing further that touches the
bench — no build, no second cycle — and let the turn end. Non-Pi work (docs,
reading, host analysis, merges on branches) is fine and encouraged; a build is
NOT, because the image stage overwrites `loader.disk`.

If a result arrives and you want to act on it, check first whether that cycle's
notification has actually been delivered. If it has not, the port is still held,
however finished the log looks.

**Every clever alternative is subtly broken.** These have all been tried here:

| attempt | why it fails |
|---|---|
| `until ! pgrep -f "psh-interact"; do sleep 5; done` | the waiter's own argv contains the pattern ⇒ waits forever |
| same, with the bracket trick `"[p]sh-interact"` | **still self-matches** when the launch command is in the same argv, because `test-cycle-psh-interact.sh` contains `psh-interact` |
| `until grep -q "TAG-DONE\|exited with" <task>.output` | if you later `TaskStop` that task the marker is never written ⇒ no reachable exit condition |
| a check-and-launch one-liner | the launch half poisons the check half (same argv) |

Two of these orphaned a `sleep` loop for ~4 h each, on top of 11 from earlier
sessions. See memory `feedback_waiter_loop_selfmatch`.

## If you genuinely must check the lock

Put it in its **own** Bash call, separate from the launch, and test the **device**
— a device cannot self-match:

```
fuser -v /dev/ttyUSB0
pgrep -af "picocom|psh-interact.py" | grep -v "bash -c"     # empty = free
```

## Recovering from a collision

1. `TaskStop` the stale task (find it with `TaskList`, or `pgrep -af test-cycle`).
2. Confirm the device is free by the method above.
3. Re-run. **Discard both logs** — the interrupted one is truncated at an arbitrary
   point, and a truncated log reads exactly like a hang or a crash.

---

# Reading results honestly

Hard-won on 2026-09-21, when ten storage bugs were found and several early
conclusions had to be retracted.

- **Never grade by rc.** `grep -c` exits 1 when the count is 0 — which is often
  the result you wanted. Grade by individually tagged `TAG-` lines.
- **Assert the work happened before trusting a timing or a checksum.** A loop over
  a missing directory reports `0 s`; a content check on a path that does not exist
  greps clean. Print the count/size first and treat a wrong one as *void*, not as
  a pass.
- **A partial log is not a result.** Do not read a still-running cycle's log and
  act on it — wait for the notification. Doing so is what caused two of the three
  collisions.
- **"Slow" or "truncated" is usually a crash.** Grep the whole log for
  `Exception|Data Abort|Fatal`, and check the last HDMI frames in `artifacts/hdmi/`.
- Some Pi-side tools crash in ways that mimic data errors: `sha256sum` dies with
  `Data Abort (EL0) far=0x30` (the `libc-uninit-main` NULL-`FILE*` bug) and prints
  nothing, which reads exactly like corruption. Prefer `cmp`.
- **Rates come from coreutils `/usr/bin/dd`'s own report**, never from harness
  wall-clock, and never from `/bin/dd` (busybox — it cannot self-report).
- Counters that a driver prints at unmount (e.g. umass write amplification) are
  **cumulative from boot**. Measure in a boot that does only the traffic you care
  about, or the ratio is diluted to meaninglessness.

For storage and filesystem work specifically, use the **`rpi4-storage-test`**
skill; for taking a code change through build and gating, **`rpi4-core-change`**.

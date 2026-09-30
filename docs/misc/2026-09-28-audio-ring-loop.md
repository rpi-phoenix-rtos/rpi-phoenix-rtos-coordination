# rpi4-audio: the ring replays its last lap after the writer stops (2026-09-28)

**Status:** the mechanism is confirmed from the code. The fix is on `phoenix-rtos-devices`
branch `fix/audio-ring-silence` (pushed to `publish`, not merged), and the host test passes.
Nobody has listened to it and no Pi cycle has run. The Pi check below was pre-registered
before any data.

## 1. The mechanism (confirmed from the code)

References are to `audio/rpi4-audio/rpi4-audio.c` on `master` at `679ee59`.

- **Ring layout.** `RING_WORDS 16384` (l.176). One duty word is one FIFO word, and the FIFO
  alternates channel 1 and channel 2, so the ring holds 8192 stereo frames, about 0.186 s at
  44 117 Hz.
- **Control block.** The block chains to itself: `cb->nextconbk = DRAM_BUS(cb_pa)`, with
  `txfr_len = RING_BYTES` (l.736-741). It is DREQ-paced by PWM1 and never stopped. The only
  writes to `DMA_CS` are the arm, the stall re-arm and the arm trials.
- **Close.** `case mtOpen: case mtClose: msg.o.err = EOK;` (l.591-594). Close does nothing to
  the ring or to the engine.
- **Underrun.** The write loop only waits while the ring is *full*:
  `((write_idx - readIdx + RING) % RING) >= RING - 1` (l.411). Nothing notices when the read
  cursor passes `write_idx`. Nothing ever writes to a word after it has played, so the engine
  plays the whole ring again on every lap. A writer that comes back after an underrun writes
  *behind* the cursor, so it is heard up to a lap late.
- **Every boot.** The self-test (l.864-942) writes 8960 words of a 440 Hz tone and then goes into
  the message loop. By the same mechanism, the jack then pulses about 0.10 s of tone and
  0.08 s of silence until the first program writes. The code comment calls this "a brief blip",
  but the code makes it continuous.

**Silence value.** The duty is `(s + 32768) * 612 / 65536`, so PCM 0 gives 306, which is
`PWM_RANGE / 2`. This is offset binary: silence is mid-scale, not 0. The ring is pre-filled
with this value (l.714), so it is also the level the jack idles at from boot.

## 2. The fix (option (a), without stopping the DMA)

The fix is branch `fix/audio-ring-silence`, head `cd9839a`. `cad6535` adds the ring
bookkeeping header, `84ab300` changes the driver, and `cd9839a` makes close service the ring
before it drops the opener count.

- **Keep the engine running.** Option (b) was rejected. Re-arming is where this driver's ~7 %
  parked-channel stall lives (the comments at l.244-248 and l.667-681, and KNOWN-ISSUES
  q2-sdl-openaudio-hang). Stopping on close and restarting on the next write would re-roll that
  risk at every program launch. An engine looping mid-scale is already the boot idle state, and
  mid-scale is the click-free level. `DUTY_SILENCE` is now defined, and a `_Static_assert` pins
  it to the conversion of PCM 0.
- **`rpi4-audio-ring.h`** holds pure functions with only `<stdint.h>`. They keep one invariant:
  every word outside the pending region `[rd, wr)` holds silence. Each service reads the cursor
  and overwrites the words the engine played with silence. The writer only writes into that
  already-silent free region. After the ring drains, the writer resumes 256 words ahead of the
  cursor, on the same word parity, which is the same channel. Those 256 lead words count as
  pending. They are also ~2.9 ms of audible start latency on the first write after a drain. The
  old driver had none there, but it resumed at a stale position, up to a whole lap (~186 ms) late.
- **Laps.** The cursor index alone cannot tell a short move from a move plus whole laps, so the
  elapsed time is checked too. A cursor that has not moved at all is a parked engine, not a lap.
  This keeps the 10 s stall detector working.
- **Sweeper thread.** It runs at priority 3 and wakes every 20 ms while anything is pending,
  which is 9 times inside one lap. When nothing is pending it sleeps on a condition variable at
  no cost. One mutex serializes the sweeper with the writer, the stall re-arm, `ARMTRIALS` and
  close.
- **Arming.** Each arm fills the ring with silence first. Before this, a re-arm after a stall
  restarted on stale audio.
- **Stats.** `GETSTATE` gains `dma_cs`, `ring_pending`, `ring_nonsilent` (counted on each call),
  `ring_drains`, `ring_underruns`, `ring_laps` and `ring_silenced`. This changes the size of the
  `_IOR`, but nothing in the tree calls `GETSTATE`, and no reader tool is staged.

**Tagged lines.** Each kind is rate-limited to one line per second. Skipped lines are counted in
the next one.

```
rpi4-audio: close: openers=N, W words (~M ms) still to play, then silence (K lines skipped)
rpi4-audio: underrun: silence-filled W words (~M ms), dma=running cs=0x........, ring nonsilent=0/16384, stream=open|closed, lapped=0|1 (drains=D, K lines skipped)
```

`nonsilent` is counted from the ring itself when the line prints, and `dma=` is decoded from
`DMA_CS` bit 0. So the line proves its own claim: the engine is still running, and it is looping
pure silence.

**Validation.**

- `syntax-check.sh` passes: a full compile to `/dev/null` under the real flags. It ran unmodified
  (checked with `cmp`) from a private root. That root's `sources/phoenix-rtos-devices` was a
  symlink to the worktree, and its `.buildroot/phoenix-rtos-devices` was a private copy. The live
  buildroot was not touched (checked with `cmp` after the run).
- The negative control failed as it should: an injected unused variable gave
  `error: unused variable 'negctl_unused' [-Werror=unused-variable]`.
- The host test is `tools/rpi4-audio-ring-hosttest`
  (`make run DEVICES=<devices tree>`). It compiles the driver's own header and models an
  engine that never stops, the writer and the sweeper. It checks every played word: silence, or
  the next token in order, never twice, and on its channel.
  - **Canary:** the same checker run on a model of the old driver must see the replay. It
    reported 50 456 replayed tokens.
  - **Cases:** stop at playback rate, big buffers, a writer faster than playback, 12-gap
    stutter, a starved sweeper (a lap between services) and a parked engine.
  - **It found a real bug on its first run.** At first the resync lead was not counted as
    pending, so a drain was declared 256 words early. That left 256 data words that replayed on
    every lap. It was fixed before the commit.
- No image was built.

## 3. 📋 PRE-REGISTERED, 2026-09-28, before any data: `audioring`

**Question:** does the new driver leave the ring silent with the DMA still running after a
player exits, and after the boot self-test?

**Build.** Merge `fix/audio-ring-silence` into the devices `master`, then run a netboot build
with **`--scope core`**. An `auto` build reuses the stale object. Check the image with
`strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk | grep 'silence-filled'`.

```
./scripts/test-cycle-psh-interact.sh --label audioring --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 120 -- \
    "/bin/bash /bin/video-play /usr/share/m10/m10-h264-720p30-aac.mp4 -t 8" \
    "export FFPLAY_AUTOKEYS=4:quit" \
    "/bin/bash /bin/video-play /usr/share/m10/m10-h264-720p30-aac.mp4"
```

Arm 1 plays 8 s and exits at the end of the clip. Arm 2 quits after 4 s with a full ring.

| # | Line / observation | Predicted (new driver) | If instead… |
|---|---|---|---|
| 1 | Boot, right after `rpi4-audio: self-test fed 8960 samples` | exactly one `underrun: silence-filled W words` with W in 8 000-11 500 (~90-130 ms), `dma=running`, `cs` bit 0 set, `nonsilent=0/16384`, `stream=closed`, `lapped=0`, `drains=1` | no line: the sweeper did not start (look for `sweeper thread failed`) or the image is stale (the `strings` check); `nonsilent` > 0: the invariant is broken, so this is a bug; `dma=stopped`: the engine died, so check `DMA_CS` |
| 2 | Arm 1, while playing | no `underrun:` line, or a few with `stream=open` around start-up | `stream=open` lines all through playback: the SDL feeder really underruns (a finding about the feeder, not about this fix) |
| 3 | Arm 1, exit (`VIDEO-PLAY done rc=0`) | `close: openers=0, W words … still to play`, then within ~0.2 s `underrun: … nonsilent=0/16384, dma=running, stream=closed`. If the tail had already drained before the close, the order flips: `underrun: … stream=open` first, then `close: … 0 words` (also a pass) | no `close:` line: the fd close did not send `mtClose` (still harmless, because the sweeper does not depend on it), so there should still be an `underrun:` line; `openers=0` on every line, even while playing: `mtOpen` is not delivered, which only affects the label |
| 4 | Arm 2, `key=quit` at 4 s | the same pair as in row 3, with `close: … still to play` near a full ring (≈ 16 000 words, ~180 ms) | a small W: SDL drained before closing (fine; record it) |
| 5 | No `write STALLED`, no `self-test ABORTED`, no new `DMA NOT STREAMING` | the fix does not touch arming, apart from the silence pre-fill | any of these: compare with the pre-change rate before blaming the fix |

**What the bug looks like on the old driver.** The old driver has **neither tagged line**: no
`close:` line and no `underrun:` line, after the self-test or after either arm. Its `GETSTATE`
has no ring fields, and no tool reads it anyway. So the old ring's contents cannot be read with
what is staged. The positive evidence on the old driver is the sound, which needs someone to
listen:

- from boot, a continuous 440 Hz pulse, about 0.10 s on and 0.08 s off;
- after each arm, the clip's last ~0.19 s of tone repeating until the next player starts.

With the new driver, the boot gives one ~0.1 s blip and then silence, and each arm ends in
silence.

If an A/B without a listener is ever needed, a userspace peek tool could do it. It would
`MAP_PHYSMEM` DMA channel 5 (`0xfe007500`), follow `CONBLK_AD` to the control block and its
`source_ad` to the ring, and count the words that are not 306. This is the same pattern as
`tools/pwm-dma-probe`. The tool is not built. On the old driver, after arm 1, it would predict
thousands of non-306 words, and 0 on the new driver.

> **2026-09-30:** the `underrun: silence-filled … stream=closed` line quoted above as proof of the ring fix is no longer printed while no stream is open (devices `f35c820`). The GETSTATE counters still show the drain.

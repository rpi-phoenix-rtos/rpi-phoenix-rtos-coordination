# Sustained-load thermal behaviour (2026-09-08)

The stability evidence for the demo build was a large sample of **short** runs —
132 boots, every game gated for ~2 minutes. A live presentation may run a game
for tens of minutes, and BCM2711 throttling would show up on stage as the frame
rate quietly degrading rather than as a crash. Nothing had checked that.

## Method

`tools/thermal-soak/thermal-soak.c` (staged as `bin/thermal-soak`) forks, execs
the program under test in the child, and polls `/dev/thermal` and
`/dev/throttled` in the parent, printing one line per interval. That indirection
is needed because psh runs commands sequentially and a game never exits, so
temperature cannot be sampled from a second psh command.

Formats come from `devices/sensors/rpi4-thermal/rpi4-thermal.c`
(`thermal_format`): `/dev/thermal` is milli-Celsius as `%u`, `/dev/throttled` is
the VideoCore `GET_THROTTLED` bitmask as `0x%08x`. The tool decodes the bitmask
in words rather than logging bare hex, because a hex value in a log is exactly
the kind of thing that gets misread as "fine" months later.

```
thermal-soak 20 /usr/bin/quakespasm +map start
```

QuakeSpasm on purpose: it renders continuously. vkQuake would have played
`demo2` (the hand-staged `id1/phoenix-demo.cfg` on the export takes precedence)
and dropped to the console when the demo ended, removing the load mid-soak.

## Result: no throttling, plateau ~54 °C

| t (s) | 0 | 20 | 60 | 120 | 180 | 240 | 300 | 380 |
|---|---|---|---|---|---|---|---|---|
| T (°C) | 35.0 | 39.9 | 44.3 | 48.7 | 51.6 | 54.0 | 53.0 | 55.0 |

**`throttle=0x00000000` on every one of the 19 samples** — no under-voltage, no
ARM capping, no active throttle, and none of the sticky "has occurred since boot"
bits. 0 faults; QuakeSpasm rendered throughout.

The curve rises for ~4 minutes then flattens: from t=240 s onward it oscillates
in a 53.0–55.0 °C band rather than continuing to climb. The Pi 4 begins soft ARM
capping at 60 °C and hard-throttles at 80-85 °C, so there is roughly 5 °C of
headroom to the *first* intervention and ~25 °C to real throttling.

## Honest limits

- **6.3 minutes, not 30.** The plateau is well established (~2.5 minutes of flat
  samples) but this is not a half-hour soak; the harness cut the child at
  `max-cmd-secs`, which is why the tool's own `DONE` summary line is absent.
- **One board, one ambient temperature**, open-air on a bench. A Pi in a case, or
  a warm room, starts higher and has less headroom.
- QuakeSpasm's load is representative of the games but is not the heaviest thing
  the port can do; vkQuake presents more frames per second.

To extend: raise `--idle-secs`/`--max-cmd-secs` (the tool keeps sampling for the
whole life of the child) and split across two Bash calls if the run exceeds the
10-minute cap.

# Errors, warnings and notices in the UART logs — catalogue (2026-09-30)

**Owner request:** go over the recent UART logs, scan for errors and warnings of every kind (not only
`ERROR`/`WARNING`: GLib `Domain-CRITICAL`, `Gdk-Message`, `[ERROR]` tags, `XKB-NNN`, Quake's `Unknown
command`, …), fix the easy ones, record the hard ones.

**Method:** 16 logs on the current image: the 7 showcase-gate logs of 16:08–16:49, both SD-boot logs,
gate2A, and 6 recording cycle logs. That is ~5 600 distinct lines, normalised (numbers, pids,
addresses) and listed **per program**, then classified. Scripts are in `tools/logscan/` (`all.py`
lists every distinct line per emitter).

## Real defects the noise was hiding

- **X has no keyboard.** `PHXHID dev=/dev/kbd0 … open=Device or resource busy` in 3 of 3 X runs:
  the input driver gives up after 1 s, before the console releases `/dev/kbd0`.
- **SD boot: `/tmp` is not RAM.** `dummyfs mount failed` in 46 of 46 SD boots: `dummyfs-tmp -m /tmp`
  runs before the SD root is registered.
- **`gai_strerror()` never returns a message.** libphoenix's table generator does not match
  `#define EAI_X -N`, so every code prints `Unknown error N`.
- **`mprotect()` rejects a non-page-multiple length** (POSIX rounds it). Quake 3 then runs its JIT
  from an RWX mapping.
- **lwip returns an IPv4 socket for `AF_INET6`** (built without IPv6). The next call fails with EIO
  instead of `EAFNOSUPPORT`.

## Easy fixes (dispatched 2026-09-30 18:3x, one build)

| # | Repo | Fix | Message it removes |
|---|---|---|---|
| 1 | ports `xorg_server_drm/glue/src/phxhid.c` | retry busy devices from the timer | `PHXHID … Device or resource busy` (X keyboard) |
| 2 | ports `xfce_wayland/files/pi/xfce-desktop.sh`, games autostart | TERM xfconfd (by bus pid) before dbus; TERM foot before labwc | `xfconfd-WARNING … Name org.xfce.Xfconf lost`; foot `Broken pipe` / `Hangup` |
| 3 | ports `xfce_wayland` patches | liblauncher file-monitor failure `g_critical`→`g_debug`; thunar ServiceUnknown thumbnailer → `g_debug` | `liblauncher-CRITICAL **: Failed to start file monitor`; `thunar-WARNING … Thumbnailer1` |
| 4 | ports `labwc_desktop` foot patch | skip `F_ADD_SEALS` on Phoenix | `foot: err: shm.c … failed to seal` |
| 5 | ports `sdl2_kmsdrm` | skip the Wayland probe with no runtime dir; stop forcing SDL debug logging; relative mouse mode on Phoenix HID | `XDG_RUNTIME_DIR is invalid`; `DEBUG: KMSDRM_…`; `SDL_SetRelativeMouseMode failed` |
| 6 | ports `supertuxkart` | quiet missing config attributes; `XDG_DATA_HOME`/`XDG_CACHE_HOME` in the launcher | `User Config: Unknown value …` ×63; `Falling back to use '.'` |
| 7 | ports `dillo` | `--sysconfdir=/etc`; ship `/etc/dillo/*` with core-font names | a build path in the binary; `font … not found` |
| 8 | ports `mesa_drm` (optional) | silence `os_same_file_description` on Phoenix | `couldn't determine if two DRM fds …` |
| 9 | project rootfs-overlay + coord `stage-game-data.sh` | no `;` in Quake `.cfg` comments; Quake 3 `net_enabled 1` | `Unknown command "the"`; IPv6 socket spam |
| 10 | project `user.plo.yaml` + utils psh `mkdir -p` | `mkdir;-p;/dev;/mnt` | `mkdir: failed to create /dev … File exists` |
| 11 | libphoenix `string/` | a `gaierr.desc` table and a generator that accepts negative codes; tests | `Unknown error 6` from `gai_strerror` |
| 12 | kernel `vm/map.c` | `mprotect` rounds the length up to pages; a test | quake3 `mprotect(RX) failed` |
| 13 | lwip `port/sockets.c` | `EAFNOSUPPORT` for `AF_INET6` without IPv6 | vkQuake `UDP6_OpenSocket: Input/output error` |
| 14 | filesystems `dummyfs` | retry the `-m` mount point until `/` exists | SD `dummyfs mount failed` |
| 15 | devices `rpi4-v3d-async`, `rpi4-kms` | per-buffer trace behind `-v`, qstat off by default, `read_dump` removed | ~1 500 `V3DA srv import/export/qstat…` lines |
| 16 | devices `rpi4-audio.c` | the drain line only with a stream open | `underrun: silence-filled … stream=closed` |

## Hard (to the register)

- **GLib file monitoring:** no inotify/kqueue backend and no poll fallback for directories.
- **USB enumeration fails** on ~1.6 % of boots (xHCI Address Device cc 36, then 19).
- **Empty hostname / resolver:** `gethostbyname(gethostname())` fails.
- **FreeType without PNG:** no colour emoji (error 7).
- **WindowMaker cannot find `swback.png`** although it is on the path.

## Harmless (kept)

ntpclient offline on SD bench runs; Xorg "old probe method"; STK `kartDirt` shader fallback and no
IPv6; Quake 3/2/Spasm diagnostics for the demo data and a fresh profile; `loc_english.txt`;
vkQuake Steam/cdrip; dbus activation notices; driver status lines that only matched keywords.

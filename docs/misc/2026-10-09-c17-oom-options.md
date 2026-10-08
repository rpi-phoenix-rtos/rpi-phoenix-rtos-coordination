# C17: running out of memory — what happens, and the options

Date: 2026-10-09. Issue: `docs/KNOWN-ISSUES.md` row C17. Evidence log:
`artifacts/rpi4b-uart/20261008-220332-live-test.log` (owner's live test, YouTube in WebKitGTK under
XFCE). Status of this note: analysis plus two branches, nothing merged, nothing run on the Pi.

Kernel paths are relative to `sources/phoenix-rtos-kernel/` (master at the time of writing).
**[V]** = read in the code, **[I]** = inferred.

## 1. What the log shows

- About 5 minutes of YouTube, then the page allocator ran dry (lines 316–324):
  - The first failures were large contiguous requests: `no free block of 9452 KB (free 8400 KB,
    largest free block 4096 KB, file cache 0 KB)` and 11632 KB.
  - Next, 1024 KB, which is shmsrv's minimum pool (`SHMSRV FAIL alloc … errno=12`).
  - Last, one 4 KB page.
- Then `vm: SIGSEGV caught by pid 282 (/usr/bin/webkit-browser) … address 0x52ad6000`, followed by
  a full dump at the same pc. `esr=0x92000047` is a translation fault on a write. This was a
  demand-zero page that could not be allocated, not a wild pointer.
- A second EL0 data abort followed in another process (pid not printed).
- After that, no program could start and windows could not be closed.
- There are no MEMMON lines: the panel plugin is newer than this log. There are no `exit waits for
  tid` lines, the P8 marker (`proc/msg.c:464-514`).
- The browser's stderr went to the session log, not the UART, so there are no `WPEB-MEMPRESSURE`
  lines either.

## 2. How the system behaves when memory runs out

### Allocation
- **[V]** `vm_pageAlloc` (`vm/page.c:218-244`) never blocks. It tries the buddy allocator. If that
  fails, it evicts one cached file object (`vm_objectReclaim`, `vm/object.c:658-692`) and retries,
  and it returns NULL once nothing is left to evict.
- **[V]** The only watermark is the file cache's: below 1/16 of RAM, every allocation also evicts
  (`page.c:240`, `object.c:92-94`). The cache is counted as free by `meminfo()` (`page.c:566-569`).
- **[V]** Requests are rounded up to a power of two, and the whole block is accounted
  (`page.c:118-121`, `177-180`; `object.c:1030-1035`). So:
  - a 1261-page (4.9 MB) import pins 8 MB;
  - the failing 9452 KB request needed a 16 MB block;
  - fragmentation matters, and `meminfo()` does not report the largest free block.
- **[V]** Anonymous memory is **demand-zero**:
  - `_map_isDemandZero` (`vm/map.c:631-635`) is true for plain anonymous mappings, and
    `_vm_mmap` returns without allocating (`map.c:704-706`).
  - There is no reservation or accounting at `mmap()` time. `mmap()` fails with ENOMEM only for
    contiguous memory or exhausted address space or map entries (`syscalls.c:95-98`, `128-131`).
  - Thread stacks are the exception: they are made resident at creation (`proc/process.c:881-888`,
    `proc/threads.c:1045-1050`).
  - ⚠ WebKit patch 0005 (mimalloc) says "anonymous mmap is resident at once". That is wrong for
    plain anonymous memory. See section 3.
- **[V]** A user fault that cannot get a page posts SIGSEGV with `SEGV_ACCERR` (`map.c:1205-1222`).
  The code gives ENOMEM no `si_code` of its own, which misleads anyone reading the dump.
  - With the default action, the whole process dies (`threads.c:1959-1974`).
  - With a handler installed, the handler runs and the instruction faults again.
  - With SIGSEGV blocked, the thread refaults forever (upstream FIXME, `map.c:1211-1216`).
  - So under OOM, the next process to touch a new page dies: here the browser, but it could as
    well have been labwc or the panel.

### Kernel allocations and IPC
- **[V]** These kernel allocations mostly fail with an error rather than hang:
  - kmalloc, thread creation, `vm_mapCreate`, vfork;
  - `msgRecv` needing pages in the server (`proc/msg.c:183`, `220`, `261`). It returns -ENOMEM to
    the server, and the sender gets -EINVAL (`msg.c:863-873`, `724-725`).
- **[V]** shmsrv just continues its loop (`phoenix-rtos-devices/misc/shmsrv/shmsrv.c:501-504`).
- **[I]** The other servers' reaction to `msgRecv` errors was not checked. A server that exits or
  spins there would explain "nothing starts".

### Teardown (why the system may stay wedged after the browser died)
- **[V]** Ghost threads are freed by one kernel reaper loop (`main.c:123-125` →
  `threads.c:1385-1398`). The process is destroyed when its last thread is gone.
- **[V]** `process_destroy` frees the address space **first**: `vm_mapDestroy`
  (`process.c:97`), anonymous pages via `amap_putanon`.
- **[V]** Only then does it run `posix_died` (`process.c:110`) → `posix_sweepFds` →
  `posix_fileDeref`, which ignores the result (`(void)` at `posix/posix.c:717`) → `proc_close`.
- **[V]** `proc_close` (`proc/name.c:473-488`):
  - It `vm_kmalloc`s the close message and returns -ENOMEM if it can't. Under OOM the server never
    hears that the client closed.
  - Otherwise it sends the message with `proc_sendUninterruptible`, which never abandons the
    request (`msg.c:559-560`).
- **[V]** Consequences:
  1. A server that never answers one close stalls the single reaper. From then on, no exiting
     process anywhere is freed: no address spaces, kernel stacks or `process_t`. **[I]** That fits
     "no program could start".
  2. A dropped close leaves the server's per-client state alive:
     - shmsrv's `opens` never reaches 0, so its pools stay (`shmsrv.c:313-328`);
     - V3D's `client_close` never runs (`gpu/rpi4-v3d-async/v3da_main.c:130-142`);
     - KMS's `kms_bo_client_gone` never runs (`video/rpi4-kms/kms_main.c:973`).
     Exported buffers are freed only after they are unexported and their last mapping is gone
     (`object.c:1068-1071`).
- **[V]** The V3D server never returns freed BO memory to the kernel, by design (C1 safety):
  - Freed BOs go into a pool of up to 1024 blocks (`v3da_bo.c:30-35`, `block_put` at `175-193`).
  - A block is reused only for the same page count and cache type (`v3da_lowmem.h:97`).
  - Adaptive video produces frames of several sizes (769 and 1261 pages in this log), so the pool
    keeps growing.
  - **[I]** A large part of what the browser "used" may be GPU and video buffers that stay held
    after it exits.

**Bottom line:**
- A killed process's own anonymous pages do come back, because the map is freed before the closes.
- That only holds while the reaper is not already stuck.
- GPU buffers do not come back (the V3D pool).
- shm pools do not come back if their close was dropped.

## 3. WebKit's side (implemented: ports branch `c17-webkit-pressure`)

- **Before:**
  - On Phoenix, WebKit's system memory monitor (`UIProcess/linux/MemoryPressureMonitor.cpp`,
    which reads `/proc/meminfo`) was compiled out by `OS(LINUX)`, so no browser process was ever
    told the system was low.
  - The only trigger was each web process's own footprint against a 3 GB limit: conservative at
    1013 MB, strict at 1536 MB (patch 0024's log).
  - WebKit's kill threshold for an active web process on 64-bit is **7 GB + 1 GB per tab**
    (`WTF/wtf/MemoryPressureHandler.cpp:141-150`), so it can never fire on a 4 GB Pi.
- **Patch 0025** (shared WPE/GTK) widens the monitor, the `DidReceiveMemoryPressureEvent` IPC
  message and the UI process's handler to `OS(PHOENIX)`:
  - free memory comes from `meminfo()`;
  - warning below 20 % free, critical below 10 % or below 300 MB;
  - each event also empties the UI process's back/forward and web process caches and shuts
    prewarmed processes down;
  - it logs `WPEB-MEMPRESSURE pid= system level= free_mb= total_mb=`.
- **webkit-browser** gets `--memory-limit/--memory-kill/--memory-poll-secs`, as `wpe-browser`
  already has.
- **Limit:** mimalloc's decommit is a no-op on Phoenix (patch 0005). Released caches are reused by
  the process, not returned to the system. The events stop growth; they do not shrink a process.
  - **Follow-up (not done):** since anonymous memory is demand-zero and `MAP_FIXED` unmaps then
    remaps in one call (`map.c:664-668`), mimalloc could decommit with
    `mmap(p, n, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)`.
  - The risk is map-entry growth: every decommit splits an entry, and entries are a finite kernel
    pool (`meminfo` `entry.total/free`). Measure the entry count before enabling it.

## 4. Options

| | What | Cost | Catches | Main risk |
|---|---|---|---|---|
| (a) | **Kernel OOM killer**: when a user allocation would go below a reserve, pick the process with the most anonymous memory and kill it. | High. There is no per-process accounting (`process_t`, `proc/process.h:53-113`), so picking means walking every map from inside the allocator, where map locks are held. The kill must be asynchronous, and teardown must stop needing memory first (`name.c:476`). | Everything | Deadlocks in the allocator path. It still depends on the reaper being free. |
| (b) | **Reserve + clean failure**: keep N MB (e.g. RAM/32) that only kernel allocations may use. User demand-zero faults fail first, with a clear "out of memory" kill instead of a misleading SIGSEGV. Preallocate or retry the close message, and do not let an uninterruptible close stall the reaper. | Medium. `vm/page.c` already separates kernel and user pageblocks (`page.c:28-49`) and has one watermark (`page.c:240`). Plus `name.c`/`posix.c` and the reaper. | Keeps the kernel and servers alive and makes teardown work under pressure. | Does not choose the victim: whoever faults next dies. |
| (c) | **Per-process limits**: `RLIMIT_AS`/`RLIMIT_DATA` (libphoenix stubs, `posix/stubs.c:126-143`), plus WebKit's own limit (`--memory-kill`, available now). | Medium for the kernel: per-process anonymous counters kept at fault and unmap time, checked in `_map_force` and `mmap`. Zero for WebKit's limit. | One runaway program, if configured. | GPU, shm and contiguous buffers are attributed to no process. Limits need per-program tuning. |
| (d) | **Userspace low-memory daemon**: poll `meminfo()`, report, and kill the largest process below a reserve. | **Low, no kernel change.** Implemented on utils branch `c17-lowmem-daemon` (`lowmemd`). | Whatever grows in anonymous memory, if the 1 s poll sees it coming. | A spike faster than the poll. Kills are useless while the reaper is stuck. Blind to GPU/shm pools and to fragmentation. |

## 5. Recommendation

**Do (d) now, together with WebKit's 0025, then (b) as the kernel follow-up. (a) only after (b),
if at all.**

- **Why (d) first:**
  - It is cheap, opt-in (it is started by nothing, and kills only with `-k`), and measurable.
  - The kernel frees a killed process's anonymous pages before the steps that can stall
    (`process.c:97` before `110`).
  - It is also the per-process sampler that the reproduction below needs. `webkit-browser` cannot
    report its per-role memory; `wpe-browser --rss-secs` can.
- **Thresholds are layered** so that the gentler mechanisms act first:
  1. WebKit trims at 20 % / 10 %-or-300 MB free (0025).
  2. Web processes over a limit end themselves (`--memory-kill`, if the owner sets a default, e.g.
     limit = RAM, kill = 0.6).
  3. lowmemd kills only below 200 MB (`-k 200 -m 256`).
- **Why (b) next:** (d) and (a) both depend on teardown working under pressure. Today a dropped or
  unanswered `mtClose` can stall the reaper or leak server state, and a fault anywhere can kill a
  bystander. (b) fixes the substrate; (a) would merely be (d) moved into the kernel.
- **Server side, cheap, independent of kernel policy:** trim the V3D BO pool when free memory is
  low or on a timer. Bound it by bytes, not by 1024 blocks, and keep the C1 safety argument for the
  blocks it does keep. This is the most likely reason memory did not come back.

### lowmemd as implemented
`phoenix-rtos-utils` branch `c17-lowmem-daemon`, `lowmemd/lowmemd.c`, about 490 lines, BSD.
Installed as `/bin/lowmemd` on aarch64a72-generic; not started by anything.

```
lowmemd [-i ms] [-r s] [-w MB] [-c MB] [-k MB] [-m MB] [-d s] [-x name]... [-t N] [-l path]
```

- **Polling:** every 1 s it reads `meminfo()`. The free count includes the file cache.
- **Reporting:** on each level change (warning 20 %, critical 10 %/300 MB) and every `-r` s it logs:
  ```
  LOWMEMD t= <level> free_mb= total_mb= top=pid:name:MB,...
  ```
  Processes are ranked by the anonymous memory of their map entries, as the panel and WebKit count.
- **Killing:** with `-k MB`, when less than MB is free, it SIGKILLs the largest process. The victim
  must have at least `-m` MB (default 128). It never kills itself, pid 1, or a `-x` name. It then
  waits `-d` s (default 10) and logs `after-kill pid= gone= free_mb= was_mb=`. A process that is
  not gone, or memory that did not come back, points at a stuck teardown.
- **Its own memory:** all buffers (2048 threads, 4096 map entries) are static and touched at start,
  and it logs with `write()`.
- **To enable it in the desktop** (owner decision), add to `xfce-desktop.sh` or the session script:
  ```
  /bin/lowmemd -k 200 -m 256 -x labwc -x xfce4-panel -l /dev/console &
  ```
- **Risks:**
  - Killing the wrong process: use the `-x` list. Servers are far below `-m` anyway.
  - A spike faster than 1 s.
  - Nothing returns if the reaper is already stuck. The after-kill line shows it, and only (b)
    fixes it.

## 6. Reproduction plan (owner)

Goal: find which browser process grows while a heavy video site plays for 10 minutes, how fast it
grows, and whether memory returns after the browser exits. Run it for WebKitGTK and for WPE.

**Prerequisites:**
- For per-process figures, an image built with utils `c17-lowmem-daemon`. Without it, the script
  falls back to the panel's `MEMMON` lines, which show the top 5 processes by name with no pid.
- Optionally, ports `c17-webkit-pressure`, to also see the `system level=` lines and to test the
  pressure events.

**Scripts:** `c17-gtk.sh`, `c17-wpe.sh` and `c17-inner.sh`, kept in
`/home/houp/.claude/jobs/c8f1289c/tmp/c17-repro/`; the full text of each is in section 6.1.

**Host:**
```
install -m 755 c17-gtk.sh c17-wpe.sh c17-inner.sh /srv/phoenix-rpi4-nfs-gcc16/usr/share/gate/
./scripts/netboot-server-up.sh
setsid nohup ./scripts/test-cycle-psh-interact.sh --label c17-gtk --inter-cmd-secs 8 --idle-secs 120 --max-cmd-secs 1100 -- "/bin/bash /usr/share/gate/c17-gtk.sh" > /tmp/c17-gtk.out 2>&1 < /dev/null & disown
# after it finishes: the same with c17-wpe / c17-wpe.sh
./scripts/uart-summary.sh c17-gtk
grep -a -E '^C17|LOWMEMD|MEMMON|WPEB-MEMPRESSURE|WKGB t=[0-9]+ (ui start|role=)|WPEB t=[0-9]+ (mem|sysmem)|no free block|SHMSRV|V3DA srv' artifacts/rpi4b-uart/*c17-gtk*.log
```
The complete logs are on the export under `root/c17/`. Keep in mind that the UART loses about 1.3 %
of lines.

**psh** (one line; the long part is in the scripts):
```
/bin/bash /usr/share/gate/c17-gtk.sh
```

**Knobs**, set with `export` lines in the outer script:
- `C17_URL` (default: the YouTube embed of Big Buck Bunny, autoplay and muted);
- `C17_PLAY=600`, `C17_AFTER=180`, `C17_STEP=15`;
- `C17_SAFETY=1`: lowmemd `-k 200 -m 256`, so the desktop survives. The kill itself is a result:
  which pid, and whether memory returned.

**How to read it:**
1. **Which process grows:**
   - For GTK, match the `WKGB t= role=web|network pid=` lines (and the UI's `ui start pid=`)
     against the `LOWMEMD … top=pid:name:MB` lines.
   - For WPE, the `WPEB mem role= pid= footprint_kb=` lines name the role directly.
2. **How fast:** the slope of that pid's MB across the 15 s reports, against `free_mb`.
3. **What is not anonymous:** `total − free − Σ(top)`, which is GPU, shm and kernel memory. Also
   look at the `shm=` field (`SHMSRV stats … bytes=`) and the `V3DA srv` lines.
4. **Whether memory comes back:**
   - `C17 … after` samples and `LOWMEMD after-kill` lines.
   - If free memory stays low after every browser pid is gone, it is held by the servers (V3D pool,
     shm pools) or by a stuck reaper. A browser pid that stays in `ps` means the reaper is stuck
     (look for `exit waits for tid`).
5. **Whether playback ran:** the `present-stats` fps, `load finished`, and the `rpi4-audio` lines.

**Unverified:**
- That the YouTube embed autoplays without a consent page. The fallback is
  `C17_URL=https://player.vimeo.com/video/1228694119?autoplay=1&muted=1` (`gate/mse-sites.txt`,
  which played in build 68).
- That busybox `kill -0`, `tail -f` and `grep --line-buffered` behave as written. survey.sh uses
  similar constructs.
- That the autostart item's environment carries `C17_*`. They are exported before
  `xfce-session`, as in the existing gate scripts.

### 6.1 Script text
`c17-gtk.sh` (`c17-wpe.sh` is the same, with `wpe`):
```
echo C17 begin browser=gtk
export HOLD=1200
export THUNAR_START=0
export C17_BROWSER=gtk
export XFCE_AUTOSTART=/usr/share/gate/c17-inner.sh
/bin/bash /bin/xfce-session
echo C17 end
```

`c17-inner.sh`:
```bash
#!/bin/bash
B=${C17_BROWSER:-gtk}
URL=${C17_URL:-https://www.youtube.com/embed/aqz-KE-bpKQ?autoplay=1&mute=1}
PLAY=${C17_PLAY:-600}
AFTER=${C17_AFTER:-180}
STEP=${C17_STEP:-15}
OUT=/root/c17
mkdir -p "$OUT"
RUN=c17-$B-$(date +%m%d-%H%M%S)
LOG=$OUT/$RUN.log
BLOG=$OUT/$RUN.browser.log

say() { echo "C17 run=$RUN t=$SECONDS $*" | tee -a "$LOG" > /dev/console; }
pause() { sleep "$1" & wait $!; }
sample() { say "$1" mem="$(/bin/mem 2>&1)" shm="$(/bin/shmsrv -s 2>&1)"; }

say start browser=$B url=$URL play=$PLAY after=$AFTER step=$STEP display=$WAYLAND_DISPLAY
LMD=""
if [ -x /bin/lowmemd ]; then
	K=""
	[ "${C17_SAFETY:-1}" = 1 ] && K="-k 200 -m 256 -x labwc -x xfce4-panel"
	/bin/lowmemd -r "$STEP" -t 8 $K -l /dev/console &
	LMD=$!
	say lowmemd pid=$LMD kill_args="$K"
else
	say lowmemd absent: per-process figures only from the panel MEMMON lines
fi

if [ "$B" = wpe ]; then
	CMD=(/usr/bin/wpe-browser --autoplay=allow --present-stats=30 --rss-secs="$STEP" "$URL")
else
	CMD=(/usr/bin/webkit-browser --autoplay=allow --present-stats=30 "$URL")
fi
: > "$BLOG"
"${CMD[@]}" >> "$BLOG" 2>&1 &
BPID=$!
say browser pid=$BPID cmd="${CMD[*]}"
tail -f "$BLOG" | grep --line-buffered -a -E 'WKGB t=[0-9]+ (ui start|role=)|role=(web|network) pid=|WPEB t=[0-9]+ (mem|sysmem) |WPEB-MEMPRESSURE|web-process-terminated|present-stats|WPEB t=[0-9]+ present|load (finished|failed)' > /dev/console &
TPID=$!

why=played
end=$((SECONDS + PLAY))
while [ $SECONDS -lt $end ]; do
	sample play
	kill -0 $BPID 2>/dev/null || { why=browser-exited; break; }
	pause "$STEP"
done
say stop why=$why
kill -TERM $BPID 2>/dev/null
for i in 1 2 3 4 5; do kill -0 $BPID 2>/dev/null || break; pause 2; done
kill -0 $BPID 2>/dev/null && { say kill -KILL $BPID; kill -KILL $BPID; }

end=$((SECONDS + AFTER))
while [ $SECONDS -lt $end ]; do
	sample after
	pause "$STEP"
done
/bin/ps 2>&1 | grep -a -E 'browser|PID' | while read -r l; do say ps $l; done
kill $TPID 2>/dev/null
[ -n "$LMD" ] && kill $LMD 2>/dev/null
say done log=$LOG browser_log=$BLOG
: > "${XFCE_LOGOUT_FLAG:-/tmp/xdg/xfce-logout}"
```

## 7. Decisions for the owner
1. Should lowmemd run in the XFCE session by default, and with which `-k` and `-x` values?
2. Should webkit-browser and wpe-browser get a default memory limit and kill threshold (e.g. limit
   = RAM, kill = 0.6)? Or should WTF get an `OS(PHOENIX)` RAM-aware active-process kill threshold
   (the 32-bit formula, min(3 GB, 0.9 × RAM), or lower)?
3. Kernel (b): a kernel-only reserve, a close message that cannot be lost, and a reaper that does
   not block on an uninterruptible close. Which of these, and when?
4. V3D BO pool: trim it under pressure, or cap it by bytes?
5. mimalloc decommit via `MAP_FIXED` remap: try it, after measuring the map-entry count?

Re-verify: the line numbers above are as of kernel/devices master on 2026-10-08.

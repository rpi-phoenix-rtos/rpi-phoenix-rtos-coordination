# ttyprobe — interactive tty read-path diagnostic

Minimal reproduction of GNU readline's interactive input path, used to
determine why bash gets EOF at its prompt on Phoenix-RTOS. Prints fd0
identity, whether `tcsetattr` applies raw VMIN=1/VTIME=0/ICANON-off, whether
`select()` spuriously reports readable, and what blocking + non-blocking
`read()` return on an empty fifo.

Build: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -static -o ttyprobe ttyprobe.c`
Run over netboot: stage into the NFS export `/bin/ttyprobe`, then
`test-cycle-psh-interact.sh -- "/bin/ttyprobe"`.

## Result (2026-08-25, gcc-16 rootfs, HW)
The Phoenix console tty fd0 read path is SOUND — NOT the cause of bash's EOF:
- `isatty=1`, tcsetattr applies raw VMIN=1/VTIME=0/ICANON=0 correctly;
- blocking read blocks on an empty fifo (select times out, no spurious readable);
- non-blocking read returns -1/EAGAIN on empty (not 0).
So bash's immediate EOF-at-prompt is a bash/readline-internal issue, not a
Phoenix tty-read bug. Next: instrument readline's rl_getc/rl_gather_tyi.

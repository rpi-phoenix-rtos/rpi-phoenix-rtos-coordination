#!/usr/bin/env bash
# Phoenix-RTOS Pi 4 QEMU debug harness.
#
# Launches the current build's plo.elf + loader.disk inside the
# phoenix-dev Lima VM using the new qemu-system-aarch64 11.0.0 (built
# with --enable-debug at /home/witoldbolt.guest/tools/qemu-11.0.0).
# Captures the serial console to a log file under
# artifacts/qemu/. Optionally exposes the QEMU gdb stub on TCP:1234
# inside the VM and runs an aarch64 gdb auto-script that prints state
# at known boot markers.
#
# Usage:
#   ./scripts/qemu-debug.sh                          # bare 60s run, qemu-11.0.0
#   ./scripts/qemu-debug.sh --timeout 120            # longer capture
#   ./scripts/qemu-debug.sh --qemu 10.2.2            # use older qemu for A/B
#   ./scripts/qemu-debug.sh --label firstrun         # tag the log filename
#   ./scripts/qemu-debug.sh --gdb                    # launch with gdb auto-investigate
#   ./scripts/qemu-debug.sh --print                  # dump UART to stdout too
#
# Output:
#   artifacts/qemu/qemu-rpi4b-<timestamp>-<label>.uart.log
#   artifacts/qemu/qemu-rpi4b-<timestamp>-<label>.qemu.stderr.log
#   artifacts/qemu/qemu-rpi4b-<timestamp>-<label>.gdb.log   (only with --gdb)

set -u
set -o pipefail

COORD="${PHOENIX_COORD:-/Users/witoldbolt/phoenix-rpi}"
VM="${PHOENIX_VM:-phoenix-dev}"
QEMU_VERSION="11.0.0"
TIMEOUT=60
LABEL="bare"
GDB=0
PRINT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --timeout)    TIMEOUT="$2"; shift 2 ;;
        --qemu)       QEMU_VERSION="$2"; shift 2 ;;
        --label)      LABEL="$2"; shift 2 ;;
        --gdb)        GDB=1; LABEL="gdb"; shift ;;
        --print)      PRINT=1; shift ;;
        -h|--help)
            sed -n '1,30p' "$0"
            exit 0
            ;;
        *)
            echo "qemu-debug: unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Build paths inside the VM.
VM_QEMU="/home/witoldbolt.guest/tools/qemu-${QEMU_VERSION}/bin/qemu-system-aarch64"
VM_BUILD="/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy"
VM_PLO_ELF="${VM_BUILD}/_boot/aarch64a72-generic-rpi4b/plo.elf"
VM_LOADER="${VM_BUILD}/_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk"

# Output paths on host.
mkdir -p "$COORD/artifacts/qemu"
ts="$(date -u +%Y%m%d-%H%M%SZ)"
STAGE_BASE="qemu-rpi4b-${ts}-${LABEL}"
UART_LOG="$COORD/artifacts/qemu/${STAGE_BASE}.uart.log"
STDERR_LOG="$COORD/artifacts/qemu/${STAGE_BASE}.qemu.stderr.log"
GDB_LOG="$COORD/artifacts/qemu/${STAGE_BASE}.gdb.log"
VM_TMP="/tmp/${STAGE_BASE}"

echo "qemu-debug: VM=$VM qemu=$QEMU_VERSION timeout=${TIMEOUT}s label=$LABEL gdb=$GDB"
echo "qemu-debug: host log: $UART_LOG"

# Build the remote invocation. Use a heredoc into bash -lc.
gdb_part=""
qemu_extra=""
if [ "$GDB" -eq 1 ]; then
    # -gdb tcp::1234 + -S (pause at startup; gdb 'continue' starts execution)
    qemu_extra="-gdb tcp::1234,server=on,wait=off -S"
fi

if [ "$GDB" -eq 1 ]; then
    # GDB mode: launch qemu paused, attach aarch64-elf-gdb with auto-script.
    # gdb script: connect, set breakpoints at known markers in plo.elf and the
    # kernel image, continue, print state at each break, halt after timeout.
    remote=$(cat <<EOF
set -u
set -o pipefail

mkdir -p "$VM_TMP"

if [ ! -x "$VM_QEMU" ]; then
    echo "qemu-debug: qemu binary not found at $VM_QEMU" >&2
    exit 11
fi
if [ ! -f "$VM_PLO_ELF" ]; then
    echo "qemu-debug: plo.elf not found at $VM_PLO_ELF" >&2
    exit 12
fi

# Locate the kernel ELF (un-stripped) so gdb has symbols for kernel _start.
KERNEL_ELF=""
for cand in \\
    "${VM_BUILD}/_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf" \\
    "${VM_BUILD}/_build/aarch64a72-generic-rpi4b/phoenix-aarch64a72-generic.elf"; do
    if [ -f "\$cand" ]; then KERNEL_ELF="\$cand"; break; fi
done
echo "qemu-debug(VM): KERNEL_ELF=\$KERNEL_ELF"

# Locate aarch64 gdb.
GDB=""
for cand in /usr/bin/gdb-multiarch /usr/bin/aarch64-linux-gnu-gdb /usr/bin/gdb; do
    if [ -x "\$cand" ]; then GDB="\$cand"; break; fi
done
if [ -z "\$GDB" ]; then
    echo "qemu-debug: no aarch64-capable gdb found in VM" >&2
    exit 14
fi
echo "qemu-debug(VM): GDB=\$GDB"

# Build gdb script.
# Compose the symbol-file directive separately so we can use a fully-quoted
# inner heredoc for the gdb commands (no shell expansion inside).
PLO_FILE_LINE="file $VM_PLO_ELF"
KSYM_LINE=""
if [ -n "\$KERNEL_ELF" ]; then
    KSYM_LINE="add-symbol-file \$KERNEL_ELF"
fi

{
    echo "\$PLO_FILE_LINE"
    if [ -n "\$KSYM_LINE" ]; then echo "\$KSYM_LINE"; fi
    # Quoted heredoc disables \$ expansion, so gdb \$pc etc. survive intact.
    cat <<'GDBEOF'
set pagination off
set confirm off
set print pretty on
target remote :1234

# Kernel image is loaded by plo at LOW PA (0x80000 on QEMU rpi4b per
# firmware "Kernel relocated to 0x80000"). Kernel runs from LOW PA before
# MMU is on, so symbolic break <high_va_symbol> at 0xffffffffc0...
# won't match. Set physical-address breakpoints explicitly.
break *0x80000
break *0x8007c
break *0x80100

# Also try symbol-based (some of these may exist via add-symbol-file).
python
syms = ["_start", "el1_entry"]
for s in syms:
    try:
        gdb.execute("break " + s)
        print("[gdb] symbol-break OK:", s)
    except Exception as e:
        print("[gdb] symbol-break FAIL:", s, e)
end

# Resume and watch. After WATCH_SECS, force-interrupt the CPU so we can
# dump state even if no breakpoint fires (CPU wedged in a tight loop).
python
import threading, time
def watch():
    time.sleep(20)
    try:
        gdb.execute("interrupt")
    except Exception as e:
        print("[gdb] interrupt failed:", e)
threading.Thread(target=watch, daemon=True).start()
end

continue

# Reached either a breakpoint or the watcher interrupt. Dump state.
echo \n\n=== POST-STOP STATE ===\n
info threads
# QEMU monitor passthrough — exposes full CPU state including all SP_ELx,
# ELR_ELx, ESR_ELx, VBAR_ELx, CurrentEL, etc.
echo \n=== QEMU CPU 0 full register dump ===\n
monitor info registers -a
python
for i in range(1, 5):
    try:
        gdb.execute("thread " + str(i))
        gdb.execute("echo \\\\n--- core " + str(i - 1) + " gdb view ---\\\\n")
        gdb.execute("info registers pc sp x0 x1 x2 x9 x30")
        gdb.execute("x/8i \$pc-16")
    except Exception as e:
        print("[gdb] thread", i, "dump failed:", e)
end
GDBEOF
} > ${VM_TMP}/gdb.script

# Launch qemu in background; attach gdb.
QEMU_CMD="$VM_QEMU \\
    -M raspi4b \\
    -cpu cortex-a72 \\
    -smp 4 \\
    -m 2G \\
    -nographic \\
    -monitor none \\
    -kernel $VM_PLO_ELF \\
    -device loader,file=$VM_LOADER,addr=0x08000000,force-raw=on \\
    -gdb tcp::1234,server=on,wait=on \\
    -S \\
    -serial file:${VM_TMP}/uart.log"

echo "qemu-debug(VM): launching qemu (paused, gdb stub @1234) ..."
\$QEMU_CMD > ${VM_TMP}/qemu.stdout 2> ${VM_TMP}/qemu.stderr.log &
QPID=\$!
echo "qemu-debug(VM): qemu pid=\$QPID"
sleep 2

echo "qemu-debug(VM): attaching gdb (timeout=${TIMEOUT}s)"
timeout --foreground --signal=TERM ${TIMEOUT}s \$GDB -batch -x ${VM_TMP}/gdb.script > ${VM_TMP}/gdb.stdout 2>&1 || true
gdbrc=\$?
echo "qemu-debug(VM): gdb rc=\$gdbrc"

# Kill qemu if still alive.
kill -TERM \$QPID 2>/dev/null || true
wait \$QPID 2>/dev/null || true

ls -la ${VM_TMP}/
echo
echo "=== qemu-debug(VM): GDB stdout tail ==="
tail -n 60 ${VM_TMP}/gdb.stdout || true
echo
echo "=== qemu-debug(VM): UART tail ==="
tail -n 20 ${VM_TMP}/uart.log || true
EOF
)
else
    # Bare mode: just run qemu under timeout, capture serial.
    remote=$(cat <<EOF
set -u
set -o pipefail

if [ ! -x "$VM_QEMU" ]; then
    echo "qemu-debug: qemu binary not found at $VM_QEMU" >&2
    exit 11
fi
if [ ! -f "$VM_PLO_ELF" ]; then
    echo "qemu-debug: plo.elf not found at $VM_PLO_ELF" >&2
    exit 12
fi
if [ ! -f "$VM_LOADER" ]; then
    echo "qemu-debug: loader.disk not found at $VM_LOADER" >&2
    exit 13
fi

mkdir -p "$VM_TMP"

# Build QEMU command.
QEMU_CMD="$VM_QEMU \\
    -M raspi4b \\
    -cpu cortex-a72 \\
    -smp 4 \\
    -m 2G \\
    -nographic \\
    -monitor none \\
    -kernel $VM_PLO_ELF \\
    -device loader,file=$VM_LOADER,addr=0x08000000,force-raw=on \\
    $qemu_extra \\
    -serial mon:stdio"

echo "qemu-debug(VM): launching qemu with timeout=${TIMEOUT}s..."

timeout --foreground --signal=TERM ${TIMEOUT}s bash -c "\$QEMU_CMD > ${VM_TMP}/uart.log 2> ${VM_TMP}/qemu.stderr.log" || true
rc=\$?
echo "qemu-debug(VM): qemu exit/kill rc=\$rc"

ls -la ${VM_TMP}/

echo
echo "=== qemu-debug(VM): UART tail ==="
tail -n 30 ${VM_TMP}/uart.log || echo "(no uart log)"
echo
echo "=== qemu-debug(VM): QEMU stderr tail ==="
tail -n 20 ${VM_TMP}/qemu.stderr.log || echo "(no stderr log)"
EOF
)
fi

# Run remote.
limactl shell -y "$VM" -- /bin/bash -lc "$remote"
remote_rc=$?
echo "qemu-debug: remote rc=$remote_rc"

# Copy artifacts back.
limactl shell -y "$VM" -- /bin/bash -lc "cat ${VM_TMP}/uart.log" > "$UART_LOG" 2>/dev/null || true
limactl shell -y "$VM" -- /bin/bash -lc "cat ${VM_TMP}/qemu.stderr.log" > "$STDERR_LOG" 2>/dev/null || true
if [ "$GDB" -eq 1 ]; then
    limactl shell -y "$VM" -- /bin/bash -lc "cat ${VM_TMP}/gdb.stdout" > "$GDB_LOG" 2>/dev/null || true
    if [ -s "$GDB_LOG" ]; then
        echo "qemu-debug: gdb log: $GDB_LOG ($(wc -l < "$GDB_LOG" | tr -d ' ') lines)"
    fi
fi

# Print summary.
if [ -s "$UART_LOG" ]; then
    lines=$(wc -l < "$UART_LOG" | tr -d ' ')
    bytes=$(wc -c < "$UART_LOG" | tr -d ' ')
    echo
    echo "=== qemu-debug: host UART summary ==="
    echo "log:   $UART_LOG"
    echo "lines: $lines"
    echo "bytes: $bytes"

    if [ "$PRINT" -eq 1 ]; then
        echo "=== qemu-debug: full UART ==="
        cat "$UART_LOG"
    fi
else
    echo "qemu-debug: WARNING UART log empty: $UART_LOG"
fi

echo "qemu-debug: stderr: $STDERR_LOG"
exit 0

#!/usr/bin/env bash
# Phoenix-RTOS Pi 4 QEMU debug harness (native Linux host).
#
# Launches the current build's plo.elf + loader.disk under
# qemu-system-aarch64 directly on this host (no VM). Captures the serial
# console to a log under artifacts/qemu/. Optionally exposes the QEMU gdb
# stub on TCP:1234 and runs an aarch64 gdb auto-script that prints CPU
# state at known boot markers.
#
# QEMU: defaults to 11.1.0, resolved from /opt/qemu-<major> or
# /opt/qemu-<version> (whichever exists), else the first
# qemu-system-aarch64 on PATH. Override with PHOENIX_QEMU_BIN=<path> or
# --qemu <version> (picks /opt/qemu-<version>). On this host 11.1.0 is
# installed at /opt/qemu-11.1 and 11.0.0 at /opt/qemu-11.
#
# Usage:
#   ./scripts/qemu-debug.sh                          # bare 60s run
#   ./scripts/qemu-debug.sh --timeout 120            # longer capture
#   ./scripts/qemu-debug.sh --qemu 11.0.0            # older qemu for A/B
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

_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
COORD="${PHOENIX_COORD:-$(cd "$_script_dir/.." && pwd)}"
QEMU_VERSION="11.1.0"
# QEMU's `raspi4b` machine model is hard-capped at 2 GiB ("Invalid RAM
# size, should be 2 GiB" if any other value is passed). Real Pi 4 has
# 4 GB so this is a fidelity gap, but it's structural to QEMU — chunk-2
# of DRAM (the `ddrh` map covering PA 0x40000000..0xfc000000) cannot be
# exercised under raspi4b. Use the `virt` machine model instead if you
# need >2 GiB; that loses Pi-specific peripheral fidelity though.
QEMU_MEM="${PHOENIX_QEMU_MEM:-2G}"
TIMEOUT=60
LABEL="bare"
GDB=0
PRINT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --timeout)    TIMEOUT="$2"; shift 2 ;;
        --qemu)       QEMU_VERSION="$2"; shift 2 ;;
        --label)      LABEL="$2"; shift 2 ;;
        --mem)        QEMU_MEM="$2"; shift 2 ;;
        --gdb)        GDB=1; LABEL="gdb"; shift ;;
        --print)      PRINT=1; shift ;;
        -h|--help)
            head -n 32 "$0"
            exit 0
            ;;
        *)
            echo "qemu-debug: unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Resolve the qemu-system-aarch64 binary. Preference order:
#   1. $PHOENIX_QEMU_BIN (explicit override)
#   2. /opt/qemu-<version>/bin  (e.g. /opt/qemu-11.1.0)
#   3. /opt/qemu-<major.minor>/bin  (e.g. /opt/qemu-11.1)
#   4. /opt/qemu-<major>/bin  (e.g. /opt/qemu-11 = the 11.0.0 install)
#   5. first qemu-system-aarch64 on PATH
QEMU_BIN=""
if [ -n "${PHOENIX_QEMU_BIN:-}" ]; then
    QEMU_BIN="$PHOENIX_QEMU_BIN"
else
    _vmajmin="${QEMU_VERSION%.*}"    # 11.1.0 -> 11.1
    _vmaj="${QEMU_VERSION%%.*}"       # 11.1.0 -> 11
    for cand in \
        "/opt/qemu-${QEMU_VERSION}/bin/qemu-system-aarch64" \
        "/opt/qemu-${_vmajmin}/bin/qemu-system-aarch64" \
        "/opt/qemu-${_vmaj}/bin/qemu-system-aarch64"; do
        if [ -x "$cand" ]; then QEMU_BIN="$cand"; break; fi
    done
    if [ -z "$QEMU_BIN" ]; then
        QEMU_BIN="$(command -v qemu-system-aarch64 || true)"
    fi
fi

if [ -z "$QEMU_BIN" ] || [ ! -x "$QEMU_BIN" ]; then
    echo "qemu-debug: qemu-system-aarch64 not found (wanted v$QEMU_VERSION)." >&2
    echo "qemu-debug: set PHOENIX_QEMU_BIN=<path> or install to /opt/qemu-<ver>." >&2
    exit 11
fi

# Native build artifacts (this repo's buildroot).
BUILD="${PHOENIX_BUILDROOT:-$COORD/.buildroot}"
BOOT="$BUILD/_boot/aarch64a72-generic-rpi4b"
PLO_ELF="$BOOT/plo.elf"
LOADER="$BOOT/rpi4b/loader.disk"
[ -f "$LOADER" ] || LOADER="$BOOT/rpi4b-bootfs/loader.disk"
KERNEL_ELF="$BUILD/_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf"

if [ ! -f "$PLO_ELF" ]; then
    echo "qemu-debug: plo.elf not found at $PLO_ELF (build first)" >&2
    exit 12
fi
if [ ! -f "$LOADER" ]; then
    echo "qemu-debug: loader.disk not found under $BOOT (build first)" >&2
    exit 13
fi

# Output paths.
mkdir -p "$COORD/artifacts/qemu"
ts="$(date -u +%Y%m%d-%H%M%SZ)"
STAGE_BASE="qemu-rpi4b-${ts}-${LABEL}"
UART_LOG="$COORD/artifacts/qemu/${STAGE_BASE}.uart.log"
STDERR_LOG="$COORD/artifacts/qemu/${STAGE_BASE}.qemu.stderr.log"
GDB_LOG="$COORD/artifacts/qemu/${STAGE_BASE}.gdb.log"

echo "qemu-debug: qemu=$QEMU_BIN ($("$QEMU_BIN" --version | head -1 | awk '{print $NF}'))"
echo "qemu-debug: timeout=${TIMEOUT}s label=$LABEL gdb=$GDB mem=$QEMU_MEM"
echo "qemu-debug: plo=$PLO_ELF"
echo "qemu-debug: host log: $UART_LOG"

QEMU_BASE=( "$QEMU_BIN"
    -M raspi4b
    -cpu cortex-a72
    -smp 4
    -m "$QEMU_MEM"
    -nographic
    -monitor none
    -kernel "$PLO_ELF"
    -device "loader,file=$LOADER,addr=0x08000000,force-raw=on" )

if [ "$GDB" -eq 1 ]; then
    # GDB mode: launch qemu paused with the gdb stub, attach gdb-multiarch
    # with an auto-script that breaks at low-PA kernel markers and dumps
    # CPU state (incl. QEMU monitor register passthrough) after a watchdog
    # interrupt or a breakpoint.
    GDBBIN=""
    for g in /usr/bin/gdb-multiarch /usr/bin/aarch64-linux-gnu-gdb /usr/bin/gdb; do
        [ -x "$g" ] && { GDBBIN="$g"; break; }
    done
    if [ -z "$GDBBIN" ]; then
        echo "qemu-debug: no aarch64-capable gdb found" >&2
        exit 14
    fi
    echo "qemu-debug: gdb=$GDBBIN kernel-elf=$KERNEL_ELF"

    GDB_SCRIPT="$(mktemp -t qemu-debug-gdb.XXXXXX.script)"
    {
        echo "file $PLO_ELF"
        [ -f "$KERNEL_ELF" ] && echo "add-symbol-file $KERNEL_ELF"
        cat <<'GDBEOF'
set pagination off
set confirm off
set print pretty on
target remote :1234

# Kernel image is loaded by plo at LOW PA (0x80000 on QEMU rpi4b). It runs
# from low PA before the MMU is on, so high-VA symbol breaks won't match;
# set physical-address breakpoints explicitly.
break *0x80000
break *0x8007c
break *0x80100

python
for s in ["_start", "el1_entry"]:
    try:
        gdb.execute("break " + s)
        print("[gdb] symbol-break OK:", s)
    except Exception as e:
        print("[gdb] symbol-break FAIL:", s, e)
end

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

echo \n\n=== POST-STOP STATE ===\n
info threads
echo \n=== QEMU CPU 0 full register dump ===\n
monitor info registers -a
python
for i in range(1, 5):
    try:
        gdb.execute("thread " + str(i))
        gdb.execute("echo \\n--- core " + str(i - 1) + " gdb view ---\\n")
        gdb.execute("info registers pc sp x0 x1 x2 x9 x30")
        gdb.execute("x/8i $pc-16")
    except Exception as e:
        print("[gdb] thread", i, "dump failed:", e)
end
GDBEOF
    } > "$GDB_SCRIPT"

    echo "qemu-debug: launching qemu (paused, gdb stub @1234) ..."
    "${QEMU_BASE[@]}" -gdb tcp::1234,server=on,wait=on -S \
        -serial "file:$UART_LOG" > /dev/null 2> "$STDERR_LOG" &
    QPID=$!
    echo "qemu-debug: qemu pid=$QPID"
    sleep 2

    echo "qemu-debug: attaching gdb (timeout=${TIMEOUT}s)"
    timeout --foreground --signal=TERM "${TIMEOUT}s" \
        "$GDBBIN" -batch -x "$GDB_SCRIPT" > "$GDB_LOG" 2>&1 || true
    echo "qemu-debug: gdb rc=$?"

    kill -TERM "$QPID" 2>/dev/null || true
    wait "$QPID" 2>/dev/null || true
    rm -f "$GDB_SCRIPT"

    if [ -s "$GDB_LOG" ]; then
        echo "qemu-debug: gdb log: $GDB_LOG ($(wc -l < "$GDB_LOG" | tr -d ' ') lines)"
    fi
else
    # Bare mode: run qemu under a timeout, capture serial to the UART log.
    echo "qemu-debug: launching qemu with timeout=${TIMEOUT}s..."
    timeout --foreground --signal=TERM "${TIMEOUT}s" \
        "${QEMU_BASE[@]}" -serial "file:$UART_LOG" > /dev/null 2> "$STDERR_LOG" || true
    echo "qemu-debug: qemu exit/kill rc=$?"
fi

# Summary.
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
if [ -s "$STDERR_LOG" ]; then
    echo "=== qemu-debug: QEMU stderr tail ==="
    tail -n 12 "$STDERR_LOG"
fi
exit 0

#!/usr/bin/env bash
# stage-lighttpd.sh — stage the HTTP-stress lighttpd config + docroot onto the
# NFS rootfs export so the Pi can serve them. Host-side, no Pi boot.
#
# Idempotent. Stages:
#   $NFS/etc/lighttpd-stress.conf      (separate from the validated lighttpd.conf)
#   $NFS/srv/stress-www/index.html     (small page — latency / RPS test)
#   $NFS/srv/stress-www/1mb.bin        (1 MiB deterministic file — throughput)
#   $NFS/var/log, $NFS/var/run         (lighttpd errorlog + pid-file targets)
#
# The 1 MiB file is GENERATED here (not committed) to keep the repo small.
#
# NOTE: the staged /usr/sbin/lighttpd binary has its module set baked in at
# build time (mod_indexfile, mod_dirlisting, mod_staticfile). The conf must not
# reference any other module. See lighttpd-stress.conf for the full warning.
set -euo pipefail

NFS=${NFS_EXPORT:-/srv/phoenix-rpi4-nfs}
HERE=$(cd "$(dirname "$0")" && pwd)

if [ ! -d "$NFS" ]; then
	echo "ERROR: NFS export $NFS not present" >&2
	exit 1
fi

mkdir -p "$NFS/etc" "$NFS/srv/stress-www" "$NFS/var/log" "$NFS/var/run"

cp "$HERE/lighttpd/lighttpd-stress.conf" "$NFS/etc/lighttpd-stress.conf"
cp "$HERE/lighttpd/www/index.html"       "$NFS/srv/stress-www/index.html"

# 1 MiB deterministic throughput file (repeating 64-byte ASCII pattern).
python3 - "$NFS/srv/stress-www/1mb.bin" <<'PY'
import sys
path = sys.argv[1]
block = b"PHOENIX-RTOS-RPI4-NET-STRESS-THROUGHPUT-PAYLOAD-0123456789ABCDEF"
assert len(block) == 64
with open(path, "wb") as f:
    for _ in range(1024 * 1024 // 64):
        f.write(block)
PY

echo "staged:"
echo "  $NFS/etc/lighttpd-stress.conf"
echo "  $NFS/srv/stress-www/index.html"
echo "  $NFS/srv/stress-www/1mb.bin ($(stat -c %s "$NFS/srv/stress-www/1mb.bin") bytes)"

if [ -x "$NFS/usr/sbin/lighttpd" ]; then
	echo "lighttpd binary present: $NFS/usr/sbin/lighttpd"
else
	echo "WARNING: $NFS/usr/sbin/lighttpd NOT present — build/stage the lighttpd port first" >&2
fi

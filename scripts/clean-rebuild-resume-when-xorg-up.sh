#!/usr/bin/env bash
#
# clean-rebuild-resume-when-xorg-up.sh — the full clean rebuild (session ~206, owner
# request) died in the X11 ports stage because x.org's tarball CDN was transiently
# serving broken 95-byte stubs (core + GPU/GL/GLQuake + most ports had already built
# CLEAN from the full-clean nuke). This waits for x.org to recover, then RESUMES the
# build (no re-nuke: --scope auto reuses the clean-built core; --with-ports/-showcase
# re-run and skip already-built ports, retrying only the failed xorg fetch onward) and
# finalizes the nfsroot rootfs.
#
# Cheap probe every 5 min (up to ~3 h); a real (>100 KB) libXau tarball = x.org is back.
# Detached (setsid) so it survives; a completion marker is appended for the monitor.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
LOG="artifacts/clean-rebuild-full.log"
PROBE_URL="https://www.x.org/releases/individual/lib/libXau-1.0.11.tar.gz"

log() { printf '[resume-wrapper %s] %s\n' "$(date +%H:%M:%S)" "$*" >> "$LOG"; }

log "waiting for x.org tarball CDN to recover (probe every 300s, max 36 tries)"
ok=0
for i in $(seq 1 36); do
	if timeout 60 curl -fsSL -o /tmp/xorg-probe.tgz "$PROBE_URL" 2>/dev/null && [ "$(wc -c < /tmp/xorg-probe.tgz 2>/dev/null || echo 0)" -gt 100000 ]; then
		log "x.org is back (probe $i: got $(wc -c </tmp/xorg-probe.tgz)b) — resuming the build"
		ok=1
		break
	fi
	log "probe $i/36: x.org still down; sleep 300"
	sleep 300
done

if [ "$ok" != 1 ]; then
	log "x.org did not recover within ~3h; giving up (retry manually later)"
	echo "CLEAN-REBUILD-RESUME-EXIT=99" >> "$LOG"
	exit 99
fi

log "resume build: rebuild-rpi4b-fast.sh --scope auto --variant nfsroot --with-showcase --with-ports --with-tests"
./scripts/rebuild-rpi4b-fast.sh --scope auto --variant nfsroot --with-showcase --with-ports --with-tests >> "$LOG" 2>&1
rc=$?
log "resume build finished rc=$rc"
echo "CLEAN-REBUILD-RESUME-EXIT=$rc" >> "$LOG"
exit "$rc"

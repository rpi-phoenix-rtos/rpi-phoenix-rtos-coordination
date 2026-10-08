#!/bin/bash
#
# stage.sh -- put the browser showcase's Pi side on the netboot export (the Pi reads it over NFS):
#   pi/*.sh, pi/gpu.html -> <export>/usr/share/browser-showcase/
# Usage: tools/browser/showcase/stage.sh [export root, default /srv/phoenix-rpi4-nfs-gcc16]
#
# The export is a hand-maintained superset (scripts/sync-netboot-tree.sh never deletes), so the
# staged files survive netboot-server-up.sh; re-stage after make-pristine-nfs-export.sh or a
# SYNC_DELETE=1 sync. docs/BROWSER-SHOWCASE-PLAN.md has the scenes.
#
# SPDX-License-Identifier: BSD-3-Clause

set -eu
here=$(dirname "$(readlink -f "$0")")
dst=${1:-/srv/phoenix-rpi4-nfs-gcc16}/usr/share/browser-showcase
mkdir -p "${dst}"
for f in "${here}"/pi/*.sh; do
	install -m 0755 "${f}" "${dst}/"
done
install -m 0644 "${here}/pi/gpu.html" "${dst}/gpu.html"
echo "staged: ${dst}: $(cd "${dst}" && echo *)"

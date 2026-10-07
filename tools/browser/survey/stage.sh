#!/bin/bash
#
# stage.sh -- put the site survey on the netboot export (the Pi reads it over NFS):
#   pi/survey.sh, pi/survey-sites.txt -> <export>/usr/share/wpe-browser/
# Usage: tools/browser/survey/stage.sh [export root, default /srv/phoenix-rpi4-nfs-gcc16]
#
# SPDX-License-Identifier: BSD-3-Clause

set -eu
here=$(dirname "$(readlink -f "$0")")
dst=${1:-/srv/phoenix-rpi4-nfs-gcc16}/usr/share/wpe-browser
mkdir -p "${dst}"
install -m 0755 "${here}/pi/survey.sh" "${dst}/survey.sh"
install -m 0644 "${here}/pi/survey-sites.txt" "${dst}/survey-sites.txt"
echo "staged: ${dst}/survey.sh ${dst}/survey-sites.txt"

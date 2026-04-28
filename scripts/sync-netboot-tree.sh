#!/usr/bin/env bash
#
# Deprecated: kept as a no-op for back-compat with older callers.
#
# In the current architecture (en7 bridged into the phoenix-dev VM via
# socket_vmnet, dnsmasq running inside the VM), there is no host-side
# TFTP mirror — dnsmasq serves the bootfs directly from the buildroot.
# Every rebuild is therefore live the moment it finishes, with no copy
# step needed.
#
# Older docs and scripts referenced this path, so we keep the entry
# point but make it a clean exit.
#

set -euo pipefail

printf 'sync-netboot-tree.sh: no-op (TFTP serves directly from buildroot in-VM)\n'
exit 0

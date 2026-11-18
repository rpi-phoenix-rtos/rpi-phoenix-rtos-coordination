#!/usr/bin/env bash
#
# rotate-lab-psk.sh — give the host soft-AP (NetworkManager connection `phoenix-ap`, SSID PhoenixNet)
# a new random WPA2 PSK and update the one copy the Pi reads, the NFS export's /etc/wifi.conf.off.
#
#   sudo -v && ./scripts/rotate-lab-psk.sh
#
# The PSK is never printed, logged or written into the repository; the script prints a short hash
# so two copies can be compared. Anything that joins PhoenixNet needs the new PSK afterwards:
#   * the Pi: picks it up from the export at the next boot (only if /etc/wifi.conf is enabled);
#   * the smart plug that switches the Pi's power: it must be re-paired in the vendor's phone app,
#     which needs someone at the plug. Until then nothing can power-cycle the Pi remotely.
# Old SD images (e.g. release #4) still carry the old PSK and will no longer join.

set -euo pipefail

con="${PHOENIX_AP_CON:-phoenix-ap}"
export_root="${RPI4B_NFS_EXPORT:-/srv/phoenix-rpi4-nfs-gcc16}"
conf="${export_root}/etc/wifi.conf.off"

nmcli -t -f NAME con show | grep -qx "${con}" || { echo "rotate-lab-psk: no NetworkManager connection '${con}'" >&2; exit 1; }
sudo -n test -f "${conf}" || { echo "rotate-lab-psk: ${conf} missing" >&2; exit 1; }

# 24 characters from [A-Za-z0-9]: ~143 bits, and no character any config parser could trip on
psk="$(LC_ALL=C tr -dc 'A-Za-z0-9' < /dev/urandom | head -c 24)"
[ "${#psk}" -eq 24 ] || { echo "rotate-lab-psk: PSK generation failed" >&2; exit 1; }

sudo -n nmcli con modify "${con}" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "${psk}"
sudo -n nmcli con up "${con}" > /dev/null

# Replace only the psk= line; keep the file's other lines, owner and mode
tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT
sudo -n awk -v k="${psk}" 'BEGIN{d=0} /^psk=/{print "psk=" k; d=1; next} {print} END{if(!d) print "psk=" k}' "${conf}" > "${tmp}"
sudo -n cp "${tmp}" "${conf}"

h_ap="$(sudo -n nmcli -s -g 802-11-wireless-security.psk con show "${con}" | tr -d '\n' | sha256sum | cut -c1-12)"
h_conf="$(sudo -n sed -n 's/^psk=//p' "${conf}" | tr -d '\n' | sha256sum | cut -c1-12)"
unset psk
echo "rotate-lab-psk: ${con} and ${conf} updated (psk sha256 ${h_ap} / ${h_conf})"
[ "${h_ap}" = "${h_conf}" ] || { echo "rotate-lab-psk: the two copies differ" >&2; exit 1; }

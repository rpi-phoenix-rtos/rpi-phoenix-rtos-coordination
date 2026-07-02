#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
host_os="$(uname -s)"

vm="${PHOENIX_VM:-phoenix-dev}"
# The Pi 4 DTB is FETCHED, never compiled. It comes from the
# raspberrypi/firmware blobs that bootstrap-linux-host.sh stages under
# .bootblobs/ (the same fetch that provides start4.elf/fixup4.dat). We do
# NOT compile from kernel .dts — that would need the multi-GB linux clone
# which the publication bootstrap intentionally drops.
#
# Source precedence: an explicit final-form DTB (RPI4B_SOURCE_DTB) →
# the firmware DTB → the in-repo project DTB.
firmware_tree="${RPI4B_FIRMWARE_TREE:-$repo_root/.bootblobs/.firmware-checkout}"
firmware_dtb="${RPI4B_FIRMWARE_DTB:-$repo_root/.bootblobs/bcm2711-rpi-4-b.dtb}"
project_dtb="${RPI4B_PROJECT_DTB:-$repo_root/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/bcm2711-rpi-4-b.dtb}"
source_dtb="${RPI4B_SOURCE_DTB:-}"
# A firmware checkout keeps the DTB under boot/; honour it as a fallback
# location for the firmware DTB if the staged copy is absent.
if [ ! -f "$firmware_dtb" ] && [ -f "$firmware_tree/boot/bcm2711-rpi-4-b.dtb" ]; then
	firmware_dtb="$firmware_tree/boot/bcm2711-rpi-4-b.dtb"
fi
out_dir="${RPI4B_DTB_DIR:-/tmp/rpi4b-dtb}"
out_dtb="${RPI4B_DTB_PATH:-$out_dir/bcm2711-rpi-4-b.dtb}"
allow_warnings="${RPI4B_DTB_ALLOW_WARNINGS:-0}"
lint_copied_dtb="${RPI4B_DTB_LINT:-0}"

# Run the inner script on the build host (Lima VM on macOS, host on Linux).
run_shell() {
	if [ "$host_os" = "Darwin" ]; then
		limactl shell -y "$vm" -- bash -lc "$1"
	else
		bash -lc "$1"
	fi
}

run_shell "
set -euo pipefail

firmware_dtb='$firmware_dtb'
project_dtb='$project_dtb'
source_dtb='$source_dtb'
out_dir='$out_dir'
out_dtb='$out_dtb'
allow_warnings='$allow_warnings'
lint_copied_dtb='$lint_copied_dtb'

mkdir -p \"\$out_dir\"
stderr_log=\"\$(mktemp)\"
mode=''
cleanup()
{
        rm -f \"\$stderr_log\"
}
trap cleanup EXIT

if [ -n \"\$source_dtb\" ] && [ -f \"\$source_dtb\" ]; then
        printf 'Using final DTB source: %s\n' \"\$source_dtb\"
        cp \"\$source_dtb\" \"\$out_dtb\"
        mode='copy'
elif [ -f \"\$firmware_dtb\" ]; then
        printf 'Using official firmware DTB source: %s\n' \"\$firmware_dtb\"
        cp \"\$firmware_dtb\" \"\$out_dtb\"
        mode='copy'
elif [ -f \"\$project_dtb\" ]; then
        printf 'Using project-local DTB source: %s\n' \"\$project_dtb\"
        cp \"\$project_dtb\" \"\$out_dtb\"
        mode='copy'
else
        printf 'missing Pi 4 DTB: no final DTB, no firmware DTB and no project DTB found\n' >&2
        printf 'The Pi 4 DTB is fetched from the raspberrypi/firmware blobs, not compiled.\n' >&2
        printf 'Run the host bootstrap to stage them:\n' >&2
        printf '  ./scripts/bootstrap-linux-host.sh\n' >&2
        printf 'or fetch the firmware repo manually (its boot/ ships bcm2711-rpi-4-b.dtb):\n' >&2
        printf '  https://github.com/raspberrypi/firmware\n' >&2
        printf 'checked final DTB:    %s\n' \"\$source_dtb\" >&2
        printf 'checked firmware DTB: %s\n' \"\$firmware_dtb\" >&2
        printf 'checked project DTB:  %s\n' \"\$project_dtb\" >&2
        exit 1
fi

if [ \"\$mode\" = 'copy' ] && [ \"\$lint_copied_dtb\" = '1' ]; then
        dtc -I dtb -O dts -o /dev/null \"\$out_dtb\" 2>>\"\$stderr_log\"
elif [ \"\$mode\" = 'copy' ]; then
        printf 'Copied final-form DTB without dtc decompile lint. Set RPI4B_DTB_LINT=1 to audit it.\n'
fi

if [ -s \"\$stderr_log\" ]; then
        printf 'DTB preparation warnings or errors:\n' >&2
        cat \"\$stderr_log\" >&2
        if [ \"\$allow_warnings\" != \"1\" ]; then
                printf 'DTB preparation aborted because warnings are treated as significant.\\n' >&2
                printf 'If this warning is understood and temporarily tolerated, rerun with RPI4B_DTB_ALLOW_WARNINGS=1 and document why.\\n' >&2
                exit 1
        fi
        printf 'Continuing because RPI4B_DTB_ALLOW_WARNINGS=1\\n' >&2
fi

printf 'Prepared: %s\n' \"\$out_dtb\"
wc -c < \"\$out_dtb\"
"

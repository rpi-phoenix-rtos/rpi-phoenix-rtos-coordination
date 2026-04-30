#!/usr/bin/env bash

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"
linux_tree="${RPI4B_LINUX_TREE:-/Users/witoldbolt/phoenix-rpi/external/raspberrypi-linux}"
firmware_tree="${RPI4B_FIRMWARE_TREE:-/Users/witoldbolt/phoenix-rpi/external/raspberrypi-firmware}"
firmware_dtb="${RPI4B_FIRMWARE_DTB:-$firmware_tree/boot/bcm2711-rpi-4-b.dtb}"
project_dtb="${RPI4B_PROJECT_DTB:-/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/bcm2711-rpi-4-b.dtb}"
source_dtb="${RPI4B_SOURCE_DTB:-}"
source_dts="${RPI4B_SOURCE_DTS:-$linux_tree/arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts}"
out_dir="${RPI4B_DTB_DIR:-/tmp/rpi4b-dtb}"
out_dtb="${RPI4B_DTB_PATH:-$out_dir/bcm2711-rpi-4-b.dtb}"
allow_warnings="${RPI4B_DTB_ALLOW_WARNINGS:-0}"
lint_copied_dtb="${RPI4B_DTB_LINT:-0}"

limactl shell -y "$vm" -- bash -lc "
set -euo pipefail

linux_tree='$linux_tree'
firmware_dtb='$firmware_dtb'
project_dtb='$project_dtb'
source_dtb='$source_dtb'
source_dts='$source_dts'
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
elif [ -f \"\$source_dts\" ]; then
        printf 'Compiling Pi 4 DTS source: %s\n' \"\$source_dts\"
        cd \"\$linux_tree\"
        cpp -nostdinc \
                -I arch/arm/boot/dts \
                -I arch/arm/boot/dts/broadcom \
                -I include \
                -undef \
                -x assembler-with-cpp \
                \"\$source_dts\" | dtc -I dts -O dtb -o \"\$out_dtb\" 2>\"\$stderr_log\"
        mode='compile'
else
        printf 'missing Pi 4 DT source: no final DTB, no firmware DTB and no DTS found\n' >&2
        printf 'To fix this, you can clone the official firmware repo:\n' >&2
        printf '  mkdir -p external && git clone --depth 1 https://github.com/raspberrypi/firmware external/raspberrypi-firmware\n' >&2
        printf 'checked DTB: %s\n' \"\$source_dtb\" >&2
        printf 'checked firmware DTB: %s\n' \"\$firmware_dtb\" >&2
        printf 'checked project DTB: %s\n' \"\$project_dtb\" >&2
        printf 'checked DTS: %s\n' \"\$source_dts\" >&2
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

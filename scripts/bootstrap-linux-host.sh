#!/usr/bin/env bash
#
# bootstrap-linux-host.sh — set up a fresh Ubuntu x86-64 host for
# Phoenix-RTOS Raspberry Pi 4 development.
#
# Idempotent: re-running is safe. Each step checks whether its target
# already exists before doing work.
#
# What this script does:
#   1. Installs system packages (build toolchain, dnsmasq, serial,
#      video-capture, Python venv tooling, GitHub CLI)
#   2. Clones the project layout into ~/phoenix-rpi/:
#        ~/phoenix-rpi/                          (coord repo)
#        ~/phoenix-rpi/sources/<repo>/           (each sibling)
#      Each sibling gets fork=houp/* + origin=phoenix-rtos/* remotes.
#   3. Stages the Raspberry Pi 4 boot blobs (start4.elf, fixup4.dat,
#      bcm2711-rpi-4-b.dtb, overlays/miniuart-bt.dtbo) under
#      ~/phoenix-rpi/.bootblobs/.
#   4. Creates a Python venv at ~/phoenix-rpi/.venv/ with pyserial.
#   5. Prints the next steps (NIC config, dnsmasq launch, first build).
#
# Author/contact: see https://github.com/houp/phoenix-rpi
# Generated 2026-05-19 during Phoenix-RTOS Pi 4 bring-up session.

set -euo pipefail

PROJECT_DIR="${PROJECT_DIR:-$HOME/phoenix-rpi}"
SOURCES_DIR="$PROJECT_DIR/sources"
BOOTBLOBS_DIR="$PROJECT_DIR/.bootblobs"
VENV_DIR="$PROJECT_DIR/.venv"

GH_USER="${GH_USER:-houp}"

# Branches each sibling repo should track when newly cloned.
# Keep in sync with the latest manifest in manifests/.
declare -A SIBLING_BRANCHES=(
	[phoenix-rtos-kernel]=agent/rpi4-program-reloc
	[phoenix-rtos-devices]=codex/upstream-sync-20260516
	[phoenix-rtos-usb]=master
	[phoenix-rtos-utils]=codex/upstream-sync-20260516
	[libphoenix]=codex/upstream-sync-20260516
	[plo]=codex/upstream-sync-20260516
	[phoenix-rtos-filesystems]=codex/upstream-sync-20260516
	[phoenix-rtos-corelibs]=master
	[phoenix-rtos-project]=codex/upstream-sync-20260516
	[phoenix-rtos-build]=codex/upstream-sync-20260516
)

# Raspberry Pi firmware blobs we need from raspberrypi/firmware boot
# tree. We pin to a known-good firmware date; bump deliberately.
PI_FW_REF="${PI_FW_REF:-master}"
PI_FW_REPO="https://github.com/raspberrypi/firmware.git"
PI_FW_FILES=(
	boot/start4.elf
	boot/fixup4.dat
	boot/bcm2711-rpi-4-b.dtb
	boot/overlays/miniuart-bt.dtbo
)

log()   { printf '[bootstrap] %s\n' "$*"; }
warn()  { printf '[bootstrap] WARN: %s\n' "$*" >&2; }
fatal() { printf '[bootstrap] FATAL: %s\n' "$*" >&2; exit 1; }

require_root_for() {
	if [ "$(id -u)" -ne 0 ]; then
		fatal "this step needs sudo: $*"
	fi
}

##############################################################################
# 1. System packages
##############################################################################

install_packages() {
	log "Installing system packages (sudo apt-get)..."
	sudo apt-get update
	sudo apt-get install -y --no-install-recommends \
		build-essential \
		gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
		cmake pkg-config make \
		device-tree-compiler \
		mtools dosfstools parted \
		dnsmasq \
		iproute2 \
		tio picocom \
		ffmpeg v4l-utils \
		python3 python3-pip python3-venv \
		git git-lfs curl jq \
		gh
	# uv is not in apt; install via the official installer script.
	if ! command -v uv >/dev/null 2>&1; then
		log "Installing uv (Python venv tool)..."
		curl -LsSf https://astral.sh/uv/install.sh | sh
		export PATH="$HOME/.local/bin:$PATH"
	fi
}

##############################################################################
# 2. Clone the project layout
##############################################################################

clone_repo() {
	local fork_url="$1"
	local upstream_url="$2"
	local dest="$3"
	local branch="$4"

	if [ -d "$dest/.git" ]; then
		log "  already cloned: $dest"
		# Make sure remotes are configured even on re-run.
		git -C "$dest" remote get-url fork    >/dev/null 2>&1 || git -C "$dest" remote add fork    "$fork_url"
		git -C "$dest" remote get-url origin  >/dev/null 2>&1 || git -C "$dest" remote add origin  "$upstream_url"
		return 0
	fi

	log "  cloning $fork_url → $dest (branch $branch)"
	# Use the fork as origin during clone (we want HEAD on fork's branch),
	# then rewire so origin=upstream, fork=fork to match the macOS layout.
	git clone --branch "$branch" "$fork_url" "$dest" || {
		warn "fork branch '$branch' not on fork; trying default branch then setting up"
		git clone "$fork_url" "$dest"
		(cd "$dest" && git checkout -b "$branch" || true)
	}
	# Rename remotes: clone's "origin" becomes our "fork"; add upstream as "origin"
	git -C "$dest" remote rename origin fork
	git -C "$dest" remote add origin "$upstream_url"
	git -C "$dest" fetch origin --no-tags || warn "couldn't fetch from upstream $upstream_url"
}

clone_layout() {
	log "Cloning project layout to $PROJECT_DIR/"
	mkdir -p "$SOURCES_DIR"

	# 2a. Coord repo
	if [ ! -d "$PROJECT_DIR/.git" ]; then
		# Only happens if the user ran this script from a fresh empty
		# directory. Normally they curl the script and run it inside the
		# coord clone.
		log "Cloning coord repo into $PROJECT_DIR/"
		git clone "https://github.com/$GH_USER/phoenix-rpi.git" "$PROJECT_DIR"
	else
		log "Coord repo already at $PROJECT_DIR/"
	fi

	# 2b. Sibling repos
	for repo in "${!SIBLING_BRANCHES[@]}"; do
		branch="${SIBLING_BRANCHES[$repo]}"
		clone_repo \
			"https://github.com/$GH_USER/$repo.git" \
			"https://github.com/phoenix-rtos/$repo.git" \
			"$SOURCES_DIR/$repo" \
			"$branch"
	done
}

##############################################################################
# 3. Pi firmware blobs
##############################################################################

stage_pi_firmware() {
	log "Staging Raspberry Pi firmware blobs ($PI_FW_REF) → $BOOTBLOBS_DIR/"
	mkdir -p "$BOOTBLOBS_DIR/overlays"
	local fw_tmp="$BOOTBLOBS_DIR/.firmware-checkout"
	if [ ! -d "$fw_tmp/.git" ]; then
		log "  doing sparse-checkout of $PI_FW_REPO ..."
		git clone --depth 1 --filter=blob:none --no-checkout \
			--branch "$PI_FW_REF" \
			"$PI_FW_REPO" "$fw_tmp"
		git -C "$fw_tmp" sparse-checkout init --cone
		git -C "$fw_tmp" sparse-checkout set boot
		git -C "$fw_tmp" checkout
	else
		log "  firmware checkout already present; pulling latest"
		git -C "$fw_tmp" pull --ff-only || true
	fi
	for f in "${PI_FW_FILES[@]}"; do
		local rel="${f#boot/}"
		local dest="$BOOTBLOBS_DIR/$rel"
		mkdir -p "$(dirname "$dest")"
		cp "$fw_tmp/$f" "$dest"
		log "  copied $rel"
	done
}

##############################################################################
# 4. Python venv for psh-interact.py
##############################################################################

setup_python_venv() {
	if [ -d "$VENV_DIR" ]; then
		log "Python venv already at $VENV_DIR"
		return 0
	fi
	log "Creating Python venv at $VENV_DIR with pyserial"
	uv venv "$VENV_DIR"
	"$VENV_DIR/bin/python" -m pip install --no-input pyserial
}

##############################################################################
# 5. Print next steps
##############################################################################

print_next_steps() {
	cat <<EOF

[bootstrap] DONE. Next steps:

  1. Confirm the dedicated NIC is up at 10.42.0.1/24. Example with
     NetworkManager:

        nmcli connection add type ethernet ifname eth1 con-name pi-netboot \\
            ipv4.method manual ipv4.addresses 10.42.0.1/24 ipv6.method ignore
        nmcli connection up pi-netboot

     (Replace 'eth1' with whatever your USB-Ethernet adapter shows in 'ip link'.)

  2. Wire up the Pi:
        - GPIO 14/15 → USB-UART (TX/RX) at 115200 8N1
        - HDMI0      → HDMI-USB grabber
        - ETH        → dedicated NIC (eth1)
        - USB-A      → optional USB keyboard

  3. Power-cycle scripts: edit scripts/pi_power_on.sh and pi_power_off.sh to
     control whatever smart plug or switch you have. The macOS version uses
     python-kasa; the equivalent works fine on Linux.

  4. First build (full-clean to be safe):

        cd $PROJECT_DIR
        RPI4B_DTB_ALLOW_WARNINGS=1 ./scripts/rebuild-rpi4b-fast.sh --scope full-clean

  5. Start the netboot server (foreground, for the first test):

        sudo dnsmasq --no-daemon \\
            --interface=eth1 --bind-interfaces \\
            --dhcp-range=10.42.0.10,10.42.0.20,255.255.255.0,12h \\
            --enable-tftp --tftp-root=$PROJECT_DIR/.netboot/rpi4b-bootfs

     (Replace eth1 with your dedicated interface. The build copies output
     into .netboot/rpi4b-bootfs along with the firmware blobs from .bootblobs/.)

  6. In another terminal, open the UART:

        tio -b 115200 /dev/ttyUSB0

  7. Power-cycle the Pi. Expect 'fbcon: ok' on HDMI ~50 s after power-on,
     then the '(psh)% ' prompt about 1 s later.

  8. Verification runbook (USB keyboard etc.):

        $PROJECT_DIR/docs/interactive-verification-runbook.md

  9. Read these in order:
        - AGENTS.md          (project rules)
        - docs/status.md     (current focus)
        - docs/build-cache-and-cold-boot-races.md   (build-cache lesson)
        - docs/console-architecture-and-fbcon.md    (Path A vs Path B)
        - tracking/current-step.md  (the active implementation step)
        - manifests/2026-05-19-td12-stable-plus-pm-sigint.md  (current baseline)

  Bootstrapped repo locations on this machine:

        $PROJECT_DIR             — coord repo
        $SOURCES_DIR/            — sibling Phoenix-RTOS repos
        $BOOTBLOBS_DIR/          — Raspberry Pi firmware blobs
        $VENV_DIR/               — Python venv (pyserial for psh-interact.py)

  GitHub remotes per sibling:
        origin   = https://github.com/phoenix-rtos/<repo>  (upstream)
        fork     = https://github.com/$GH_USER/<repo>      (work + pushes)

EOF
}

##############################################################################
# main
##############################################################################

main() {
	log "Starting Phoenix-RTOS Pi 4 dev-host bootstrap"
	log "  PROJECT_DIR = $PROJECT_DIR"
	log "  GH_USER     = $GH_USER"

	install_packages
	clone_layout
	stage_pi_firmware
	setup_python_venv
	print_next_steps
}

main "$@"

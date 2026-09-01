#!/usr/bin/env bash
# Publication-integrity audit.
#
# Answers one question, for the coordination repo and every sibling:
#
#   Is there any code that affects the build or is used in the tests which is
#   NOT published to the github.com/rpi-phoenix-rtos org?
#
# Four ways that can happen, each checked below:
#   A. commits that exist locally but were never pushed to `publish`
#   B. modified tracked files (the local build sees them, a fresh clone doesn't)
#   C. untracked files that a tracked source #includes or a script consumes
#   D. content staged into the image from a rootfs-overlay without being tracked
#
# Some unpublished payloads are DELIBERATE (third-party licences forbid
# redistribution). Those are listed in KNOWN_PAYLOADS and must each be
# reproducible by a staging/generator script, which this audit verifies exists.
#
# Read-only: reports, never fixes. Exit 0 = clean, 1 = findings.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 2
ROOT=$(pwd)
FINDINGS=0
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# Unpublished-by-design payloads: "<path glob>|<script that (re)creates it>"
KNOWN_PAYLOADS=(
	"*/rootfs-overlay/usr/share/quake/id1/pak0.pak|scripts/fetch-quake-shareware-pak.sh"
	"*wifi-fw-43455.*|scripts/gen-wifi-fw-c.sh"
	"*wifi-nvram-43455.*|scripts/gen-wifi-nvram-py.py"
	"*clm-43455.h|tools/wifi-probe/gen-clm.py"
	"*bt-hcd.h|tools/bt-probe/gen-bt-hcd.py"
	# libphoenix generates these from its errno tables during the build.
	"*errno.str.inc|sources/libphoenix/string/Makefile"
	"*errno.tab.inc|sources/libphoenix/string/Makefile"
	"*gaierr.str.inc|sources/libphoenix/string/Makefile"
	"*gaierr.tab.inc|sources/libphoenix/string/Makefile"
)

note()  { printf '  %-9s %s\n' "$1" "$2"; }
finding() { FINDINGS=$((FINDINGS + 1)); note "FINDING" "$1"; }

known_payload() {
	local f="$1" entry glob script
	for entry in "${KNOWN_PAYLOADS[@]}"; do
		glob=${entry%%|*}; script=${entry##*|}
		# shellcheck disable=SC2053
		if [[ $f == $glob ]]; then
			if [ -f "$ROOT/$script" ]; then
				note "by-design" "$f (staged by $script)"
			else
				finding "$f is excluded by design but its generator $script is MISSING"
			fi
			return 0
		fi
	done
	return 1
}

repos() {
	echo "$ROOT"
	local d
	for d in "$ROOT"/sources/*; do
		[ -d "$d/.git" ] && echo "$d"
	done
}

for repo in $(repos); do
	name=$(basename "$repo")
	[ "$repo" = "$ROOT" ] && name="phoenix-rpi (coord)"
	echo "== $name"

	# --- A. unpushed commits ---------------------------------------------
	if git -C "$repo" remote | grep -qx publish; then
		branch=$(git -C "$repo" rev-parse --abbrev-ref HEAD)
		for target in "publish/$branch" publish/main publish/master; do
			if git -C "$repo" rev-parse --verify -q "$target" >/dev/null; then
				n=$(git -C "$repo" rev-list --count "$target..HEAD" 2>/dev/null || echo 0)
				[ "$n" != 0 ] && finding "$n commit(s) on $branch not pushed to $target"
				break
			fi
		done
	else
		note "skip" "no 'publish' remote (not an org repo)"
	fi

	# --- B. modified tracked files ---------------------------------------
	while read -r f; do
		[ -z "$f" ] && continue
		case "$f" in
			artifacts/*|docs/*) note "dirt" "$f (not build-affecting)" ;;
			*) finding "modified tracked file affects the build: $f" ;;
		esac
	done < <(git -C "$repo" diff --name-only HEAD 2>/dev/null)

	# --- C. untracked files (gitignored ones included: they still build) --
	while read -r f; do
		[ -z "$f" ] && continue
		rel="$name/$f"
		known_payload "$rel" && continue
		# Is it referenced by tracked source (an #include or a script)?
		base=$(basename "$f")
		if git -C "$repo" grep -qI -- "$base" -- '*.c' '*.h' '*.sh' '*.mk' '*.yaml' Makefile 2>/dev/null; then
			finding "untracked but referenced by tracked source: $rel"
		else
			note "stray" "$rel (unreferenced; still: delete or track it)"
		fi
	done < <(git -C "$repo" status --porcelain --untracked-files=all 2>/dev/null | grep '^??' | cut -c4-)

	# --- C2. #includes that resolve to an on-disk but UNPUBLISHED header ---
	# `git status -uall` hides gitignored files, yet a gitignored header the
	# build #includes is the dangerous case: it compiles here and not in a fresh
	# clone. Only headers that exist ON DISK inside this repo count -- anything
	# resolved from the sysroot, another repo, or a port's fetched upstream
	# tarball is not this repo's business and would be pure noise.
	while IFS=: read -r src hdr; do
		[ -z "${hdr:-}" ] && continue
		for cand in "$(dirname "$src")/$hdr" "$hdr"; do
			[ -f "$repo/$cand" ] || continue
			# a `../..` path that leaves this repo is another repo's file
			case "$(realpath -m "$repo/$cand")" in "$repo"/*) ;; *) break ;; esac
			git -C "$repo" ls-files --error-unmatch -- "$cand" >/dev/null 2>&1 && break
			known_payload "${cand##*/}" && break
			finding "#included by $src but not published: $cand"
			break
		done
	done < <(git -C "$repo" grep -nI --no-color -E '^[[:space:]]*#include[[:space:]]*"' -- '*.c' '*.h' 2>/dev/null |
		sed -e 's/^\([^:]*\):[0-9]*:[[:space:]]*#include[[:space:]]*"/\1:/' -e 's/".*//' | sort -u)

	# --- C3. tracked build outputs (note only) ----------------------------
	# Committed ELF/binary build outputs are *over*-published: bloat, not an
	# integrity risk, so they never fail the audit. Only executable-mode files
	# without an extension are magic-checked, which keeps this cheap.
	while read -r f; do
		[ -f "$repo/$f" ] || continue
		# ELF magic is 0x7f 'E' 'L' 'F' -- 0x7f is DEL, outside \000-\037
		if [ "$(head -c4 "$repo/$f" 2>/dev/null | tr -d '\000-\037\177')" = "ELF" ]; then
			note "bloat" "$name/$f ($(stat -c%s "$repo/$f") B tracked binary; prefer building it)"
		fi
	done < <(git -C "$repo" ls-files -s | awk '$1 == "100755" { $1=$2=$3=""; sub(/^ +/,""); print }')

	# --- D. rootfs-overlay content staged into images ---------------------
	# phoenix-rtos-build/build.sh puts $PROJECT_PATH/rootfs-overlay first in
	# ROOTFS_OVERLAYS, so anything here lands in the image. Only the source
	# repos are authoritative; the coord repo's own hits are all inside
	# gitignored build trees (.buildroot*/, .toolchain*/) that are copies.
	[ "$repo" = "$ROOT" ] && { echo; continue; }
	while read -r f; do
		[ -z "$f" ] && continue
		relpath=${f#"$repo"/}
		git -C "$repo" ls-files --error-unmatch -- "$relpath" >/dev/null 2>&1 && continue
		known_payload "$name/$relpath" && continue
		finding "rootfs-overlay content enters images but is unpublished: $relpath"
	done < <(find "$repo" -path '*/rootfs-overlay/*' -type f 2>/dev/null)
done

# --- E. external/ dependency clones -----------------------------------------
# These are NOT siblings, but the build compiles them (mesa -> the V3D driver,
# quakespasm -> GLQuake, ...), so the same question applies: would a fresh clone
# build the same thing? Two ways it would not:
#   * local commits/edits that were never pushed to our fork
#   * a bootstrap PIN that no longer matches what is checked out here, so local
#     builds compile different sources than any clean build -- the exact reason a
#     Docker build can fail on code that "works locally"
echo "== external/ dependency clones"
BOOTSTRAP="$ROOT/scripts/bootstrap-linux-host.sh"
for d in "$ROOT"/external/*/; do
	[ -d "$d/.git" ] || continue
	name=$(basename "$d")

	# the pin, if bootstrap declares one: "<name>|<url>|<rev>[|patch]"
	pin_branch=""
	pin=$(grep -oE "\"$name\|[^\"]*\"" "$BOOTSTRAP" 2>/dev/null | head -1 |
		tr -d '"' | cut -d'|' -f3)
	# mesa's "pin" is an upstream TAG plus patches/mesa/, so its HEAD is
	# deliberately not the pin -- exempt it from the pin comparison.
	if [ "$name" = "mesa" ]; then
		pin=""
	fi
	if [ -n "${pin:-}" ]; then
		head_sha=$(git -C "$d" rev-parse HEAD 2>/dev/null)
		# A pin can be an exact sha OR a branch/tag name. For a NAME, what a
		# clean clone gets is that ref ON THE REMOTE, not whatever a stale local
		# ref points at -- resolving it locally reports phantom drift.
		case "$pin" in
			????????????????????????????????????????)
				# exactly 40 hex chars -> an exact commit
				pin_sha=$(git -C "$d" rev-parse "$pin^{commit}" 2>/dev/null || echo "")
				pin_branch=""
				;;
			*)
				# a branch or tag name
				pin_sha=$(git -C "$d" rev-parse "publish/$pin^{commit}" 2>/dev/null ||
					git -C "$d" rev-parse "origin/$pin^{commit}" 2>/dev/null ||
					git -C "$d" rev-parse "$pin^{commit}" 2>/dev/null || echo "")
				pin_branch="$pin"
				;;
		esac
		if [ -z "$pin_sha" ]; then
			note "pin" "$name pinned to '$pin' (not resolvable here; a ref a clean clone fetches)"
		elif [ "$pin_sha" != "$head_sha" ]; then
			behind=$(git -C "$d" rev-list --count "$pin_sha..HEAD" 2>/dev/null)
			[ -z "${behind:-}" ] && behind="?"
			finding "$name: checked out HEAD is $behind commit(s) off the bootstrap pin '$pin' -- local builds compile different sources than a clean build"
		else
			note "pin" "$name at its pin ($pin)"
		fi
	fi

	# unpushed work, only meaningful when we actually have a fork for it
	if git -C "$d" remote | grep -qx publish; then
		# Compare against the branch the BUILD uses. vkquake, for one, keeps its
		# fork's master mirroring upstream and the port on the pinned branch, so
		# comparing HEAD to publish/master would report the whole port as
		# "unpushed" when it is fully published.
		base="${pin_branch:-$(git -C "$d" branch --show-current 2>/dev/null)}"
		if [ -n "$base" ] && git -C "$d" rev-parse --verify -q "publish/$base" >/dev/null; then
			n=$(git -C "$d" rev-list --count "publish/$base..HEAD" 2>/dev/null || echo 0)
			[ "$n" != 0 ] && finding "$name: $n commit(s) at HEAD not pushed to publish/$base"
		fi
	fi

	# Local work in a clone with no org fork is unpublishable by construction --
	# it exists on this host only. Reference-only clones (linux, u-boot, ...) are
	# expected to be pristine, so the same check covers them for free.
	if ! git -C "$d" remote | grep -qx publish; then
		up=$(git -C "$d" rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null || echo "")
		if [ -n "$up" ]; then
			n=$(git -C "$d" rev-list --count "$up..HEAD" 2>/dev/null || echo 0)
			[ "$n" != 0 ] && finding "$name: $n local commit(s) and NO org fork (publish remote) -- this work exists on this host only"
		else
			note "detached" "$name has no upstream branch set (detached HEAD?) -- local commits cannot be compared"
		fi
	fi

	# a dirty build-consumed tree builds here and nowhere else. mesa is exempt:
	# its tree is derived by applying patches/mesa/ to an upstream tag.
	if [ "$name" = "mesa" ]; then
		if [ -d "$ROOT/patches/mesa" ]; then
			note "by-design" "mesa tree is patch-derived (patches/mesa/ is committed)"
		else
			finding "mesa is patch-derived but patches/mesa/ is missing"
		fi
	else
		while read -r f; do
			[ -z "$f" ] && continue
			finding "$name: modified tracked file affects the build: $f"
		done < <(git -C "$d" diff --name-only HEAD 2>/dev/null)
		while read -r f; do
			[ -z "$f" ] && continue
			note "stray" "$name/$f (untracked in a build-consumed clone)"
		done < <(git -C "$d" status --porcelain --untracked-files=all 2>/dev/null | grep '^??' | cut -c4-)
	fi
done

echo
if [ "$FINDINGS" -eq 0 ]; then
	echo "PUBLICATION-AUDIT: CLEAN — a fresh org clone builds from published sources only"
	echo "  (deliberate third-party payloads excepted; each has a staging script above)"
	exit 0
fi
echo "PUBLICATION-AUDIT: $FINDINGS finding(s) — see FINDING lines above"
exit 1

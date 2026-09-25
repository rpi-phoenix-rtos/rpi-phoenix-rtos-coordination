#!/bin/bash
# Extract and VALIDATE the C1 allocator evidence from a UART log.
#
# The point of this script is the validation. The UART capture mangles characters
# under load -- 4 of 202 hex literals in one 2026-09-23 log contained a character
# that is not even hex, and a digit swapped for another DIGIT leaves no trace at
# all. A `caller=` value read straight out of a log has twice symbolized to a
# plausible-looking function that turned out to have no call instruction before
# it. So malloc prints ~caller as `callerx=`, and nothing here symbolizes an
# address whose complement does not check out.
#
# Usage: scripts/c1-report.sh [label|path]   (default: newest log)
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART="$REPO/artifacts/rpi4b-uart"
SYMDIR="$REPO/artifacts/c1-symbols"
A2L="$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-addr2line"
OBJDUMP="$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-objdump"

arg="${1:-}"
if [ -n "$arg" ] && [ -f "$arg" ]; then
	log="$arg"
elif [ -n "$arg" ]; then
	log="$(ls -t "$ART"/*"$arg"*.log 2>/dev/null | head -1)"
else
	log="$(ls -t "$ART"/*.log 2>/dev/null | head -1)"
fi
[ -n "${log:-}" ] && [ -f "$log" ] || { echo "no log found for '${arg}'" >&2; exit 1; }

# ⚠ This picks the NEWEST archived ELF, which is only right when the log came
# from that same build. SuperTuxKart is relinked by every --scope core build and
# `caller=` is an absolute address in a no-ASLR static binary, so symbolizing an
# older log against a newer ELF yields a plausible-looking WRONG function. It did
# exactly that on 2026-09-25, resolving a 09-24 log against f2efc4b0, a binary
# that did not exist when that log was written. There is nothing in a UART log
# that identifies the binary, and the archived ELFs all carry copy-times rather
# than build-times, so this cannot be resolved automatically -- say so instead of
# quietly implying a match.
elf="$(ls -t "$SYMDIR"/*.elf 2>/dev/null | head -1)"
nelf="$(ls -1 "$SYMDIR"/*.elf 2>/dev/null | wc -l)"

echo "log:  $(basename "$log")"
if [ -n "$elf" ]; then
	echo "syms: $(basename "$elf")"
	if [ "$nelf" -gt 1 ]; then
		echo "      ⚠ newest of $nelf archived ELFs, NOT matched to this log. Any symbol below is"
		echo "        only valid if this log came from that exact binary; otherwise it is noise."
		echo "        Archived: $(ls -1 "$SYMDIR"/*.elf | xargs -n1 basename | tr '\n' ' ')"
	fi
else
	echo "syms: <none archived>"
fi
echo

plain() { sed 's/\x1b\[[0-9;]*m//g' "$log"; }

# --- how trustworthy is this log at all? -------------------------------------
tot=$(grep -aoE "0x[0-9a-fA-F]{4,}" "$log" | wc -l)
bad=$(grep -aoE "0x[0-9a-fA-F]*[g-zG-Z][0-9a-fA-F]*" "$log" | wc -l)
echo "== log integrity =="
echo "   hex literals: $tot ; visibly malformed: $bad  (digit->digit corruption is INVISIBLE)"
echo

# --- guard fires --------------------------------------------------------------
echo "== allocator guard fires =="
for pat in "free() of a corrupt chunk header" \
           "small-bin head is not a chunk" \
           "large-bin lookup returned" \
           "chunk handed out twice" \
           "mmap returned a region OVERLAPPING"; do
	n=$(grep -ac "$pat" "$log")
	[ "$n" -gt 0 ] && echo "   $n x  $pat"
done
echo

# --- the corrupted heap headers ----------------------------------------------
echo "== heap-header readings =="
plain | grep -aoE "h(size|lo32|hhi32|hi32|fixed) *= 0x[0-9a-f]*" | sort | uniq -c | head -20
echo

# --- callers, validated before symbolizing ------------------------------------
echo "== caller attribution (validated) =="
callers=$(plain | grep -aoE "caller= 0x[0-9a-f]+" | grep -aoE "0x[0-9a-f]+")
callerxs=$(plain | grep -aoE "callerx= 0x[0-9a-f]+" | grep -aoE "0x[0-9a-f]+")

if [ -z "$callers" ]; then
	echo "   (no caller= lines)"
else
	i=0
	while read -r c; do
		i=$((i + 1))
		x=$(echo "$callerxs" | sed -n "${i}p")
		if [ -z "$x" ]; then
			echo "   caller $c -- NO callerx (pre-checksum build): UNVALIDATED, do not symbolize"
			continue
		fi
		# caller ^ callerx must be all ones
		chk=$(python3 -c "print('%016x' % ((int('$c',16) ^ int('$x',16)) & 0xffffffffffffffff))" 2>/dev/null)
		if [ "$chk" != "ffffffffffffffff" ]; then
			echo "   caller $c / callerx $x -> XOR=$chk MANGLED, discarded"
			continue
		fi
		echo -n "   caller $c VALID -> "
		if [ -n "$elf" ]; then
			"$A2L" -f -C -e "$elf" "$c" 2>/dev/null | head -1
			# The check that caught two false attributions: a return address is
			# preceded by a branch-with-link. If it is not, the symbol is a lie.
			prev=$(printf '0x%x' $(( $(printf '%d' "$c") - 4 )))
			ins=$("$OBJDUMP" -d --start-address="$prev" --stop-address="$c" "$elf" 2>/dev/null | tail -1)
			case "$ins" in
				*bl*|*blr*) echo "        preceded by a call: OK" ;;
				*)          echo "        ⚠ NOT preceded by a call -- not a return address, symbol is meaningless" ;;
			esac
		else
			echo "(no symbols archived)"
		fi
	done <<< "$callers"
fi
echo

# --- hunt instruments ---------------------------------------------------------
echo "== instruments =="
# ⚠ 0 here does NOT mean STK created no victim heap. Since 2026-09-25 this trace
# is env-gated (C1_HEAP_TRACE=1) and OFF by default, because it was four blocking
# debug() writes per creation -- ~200 ms of UART inside heap creation, in every
# process -- and it was added after the last fire. On a default run 0 is expected;
# `export C1_HEAP_TRACE=1` before the app to arm it as a positive control.
c1_created=$(grep -ac 'C1-hunt: created' "$log")
if [ "$c1_created" = "0" ]; then
	echo "   victim-size heap creations: 0  (trace OFF by default -- export C1_HEAP_TRACE=1 to arm)"
else
	echo "   victim-size heap creations: $c1_created  (trace ran; pre-2026-09-25 builds counted 0xd000 ONLY, later ones 0xd000+0x2000)"
fi
echo "   implausible gc header: $(grep -ac 'implausible gc header' "$log")"
echo "   EL0/EL1 exceptions:    $(grep -ac '(EL0)' "$log") / $(grep -ac '(EL1)' "$log")"

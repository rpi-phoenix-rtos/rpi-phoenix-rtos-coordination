#!/bin/bash
# Report whether the RPI4_EARLY_MARKERS gate (D9) is actually applied in all
# three components of the current build.
#
# Why this exists: editing board_config.h does NOT invalidate plo's cached
# objects. The armstub and the kernel8-reloc trampoline are re-assembled from
# scratch every image stage, so they always track the macro -- plo does not. A
# --scope core rebuild after flipping the macro can therefore produce a
# HALF-APPLIED gate (armstub + TR markers back, plo's 'A' still missing) that
# grades green everywhere else. Touch plo/hal/aarch64/generic/_init.S and
# rebuild if this reports a mismatch.
#
# Detection keys on the TXFF spin that only uart_putc generates ("tst wN,
# #0x20" against UARTFR_TXFF). Do NOT count the store instead: the armstub's
# spin-table write "str w9,[x8]" is an unrelated instruction with the same
# register pair and gives a false positive.
set -u

cd "$(dirname "$0")/.." || exit 1

OD=.toolchain/aarch64-phoenix/bin/aarch64-phoenix-objdump
BB=.buildroot/_build/aarch64a72-generic-rpi4b
BOOT=.buildroot/_boot/aarch64a72-generic-rpi4b
HDR=sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h

if [ ! -x "$OD" ]; then
	printf 'error: objdump not found at %s\n' "$OD" >&2
	exit 1
fi

want=0
if grep -Eq '^[[:space:]]*#define[[:space:]]+RPI4_EARLY_MARKERS[[:space:]]+1\b' "$HDR" 2>/dev/null; then
	want=1
fi

armstub=$($OD -d "$BB/armstub/phoenix-armstub8-rpi4.elf" 2>/dev/null | grep -c 'tst	w9, #0x20')
reloc=$($OD -d "$BB/kernel8-reloc/phoenix-kernel8-reloc.elf" 2>/dev/null | grep -c 'tst	w4, #0x20')
plo=$($OD -d "$BOOT/plo.elf" 2>/dev/null | grep -c 'tst	w4, #0x20')

# Expected uart_putc site counts, from the sources:
#   armstub  9  = 4 progress markers + "AS0\r\n"
#   reloc   20  = 4 cores x 5 chars ("TR<n>\r\n")
#   plo     92  = _exc_dump and friends, NEVER gated
#          +8  = hal_exitToEL1 (2 at entry + 2 per EL branch x 3)
if [ "$want" = 1 ]; then
	exp_armstub=9; exp_reloc=20; exp_plo=100
else
	exp_armstub=0; exp_reloc=0; exp_plo=92
fi

printf 'board_config.h RPI4_EARLY_MARKERS = %s\n\n' "$want"
printf '%-22s %6s %8s\n' COMPONENT FOUND EXPECTED
rc=0
for row in "armstub:$armstub:$exp_armstub" "kernel8-reloc:$reloc:$exp_reloc" "plo:$plo:$exp_plo"; do
	name=${row%%:*}; rest=${row#*:}; got=${rest%%:*}; exp=${rest##*:}
	if [ "$got" = "$exp" ]; then
		printf '%-22s %6s %8s  [ok]\n' "$name" "$got" "$exp"
	else
		printf '%-22s %6s %8s  [MISMATCH]\n' "$name" "$got" "$exp"
		rc=1
	fi
done

if [ "$rc" != 0 ]; then
	printf '\nHALF-APPLIED GATE. If only plo disagrees, that is the cached-object\n'
	printf 'trap: touch sources/plo/hal/aarch64/generic/_init.S and rebuild\n'
	printf '(./scripts/rebuild-rpi4b-fast.sh --scope core).\n'
fi
exit $rc

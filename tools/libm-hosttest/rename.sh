#!/bin/sh
# rename.sh PREFIX OBJDIR OUT LD NM OBJCOPY MATHSYM_REGEX
#
# Link every object in OBJDIR into one relocatable OUT and give each defined
# global symbol the PREFIX_ prefix (internal references follow the rename).
# Fails if a libm function is still UNDEFINED afterwards: that would mean the
# library under test quietly called the host's implementation of it.
set -e
prefix=$1 objdir=$2 out=$3 ld=$4 nm=$5 objcopy=$6 mathre=$7

"$ld" -r -o "$out.tmp" "$objdir"/*.o
"$nm" --defined-only -g "$out.tmp" | awk -v p="$prefix" 'NF == 3 { print $3, p "_" $3 }' > "$out.syms"
"$objcopy" --redefine-syms="$out.syms" "$out.tmp" "$out"
rm -f "$out.tmp"

leak=$("$nm" -u "$out" | awk '{ print $2 }' | grep -E "$mathre" || true)
if [ -n "$leak" ]; then
	echo "rename.sh: $prefix calls the HOST libm for: $leak" >&2
	exit 1
fi

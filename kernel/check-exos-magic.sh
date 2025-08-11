#!/bin/sh
# Usage: ./check-exos-magic.sh <binary_file>

set -eu

[ $# -eq 1 ] || { echo "Usage: $0 <binary_file>" >&2; exit 2; }
file="$1"
[ -f "$file" ] || { echo "ERROR: file not found: $file" >&2; exit 2; }

size=$(stat -c %s "$file")
[ "$size" -ge 8 ] || { echo "ERROR: file too small (< 8 bytes): $file" >&2; exit 2; }

# Read little-endian u32 at offset
read_u32le() {
	LC_ALL=C od -An -t u4 --endian=little -N 4 -j "$2" "$1" | tr -d ' '
}

MAGIC_HEX=0x534F5845
MAGIC=$((MAGIC_HEX))

magic_2nd=$(read_u32le "$file" 4)
magic_last=$(read_u32le "$file" $((size-8)))
checksum_stored=$(read_u32le "$file" $((size-4)))

printf "Binary magic:              0x%08X\n" "$MAGIC"
printf "Binary 2nd u32:            0x%08X\n" "$magic_2nd"
printf "Binary 2nd to last u32:    0x%08X\n" "$magic_last"
printf "Binary checksum (stored):  0x%08X\n" "$checksum_stored"

# Recalculate checksum excluding the stored checksum (last 4 bytes)
script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
checksum_recalc_dec=$("$script_dir/checksum.sh" --dec --size-adjust -4 "$file" 2>/dev/null)

# Validate numeric
case "$checksum_recalc_dec" in
	*[!0-9]*|'') echo "ERROR: recalculated checksum is not a valid decimal: <$checksum_recalc_dec>" >&2; exit 1;;
esac
printf "Binary checksum (recalc):  0x%08X\n" "$checksum_recalc_dec"

# Magic check
if [ "$magic_2nd" -eq "$MAGIC" ] && [ "$magic_last" -eq "$MAGIC" ]; then
	echo "EXOS magic signature present at both positions."
else
	echo "ERROR: EXOS magic signature NOT found at both positions!" >&2
	exit 1
fi

# Checksum check
if [ "$checksum_stored" -ne "$checksum_recalc_dec" ]; then
	echo "ERROR: checksum mismatch!" >&2
	exit 1
else
	echo "Checksum matches."
fi

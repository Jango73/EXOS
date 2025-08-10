#!/bin/sh

# Usage: ./check-exos-magic.sh <binary_file>

if [ $# -ne 1 ]; then
	echo "Usage: $0 <binary_file>" >&2
	exit 2
fi

file="$1"
if [ ! -f "$file" ]; then
	echo "ERROR: file not found: $file" >&2
	exit 2
fi

size=$(stat -c %s "$file")
if [ "$size" -lt 8 ]; then
	echo "ERROR: file too small (< 8 bytes): $file" >&2
	exit 2
fi

# Little-endian uint32 at offset
read_u32le() {
	od -An -t u4 --endian=little -N 4 -j "$2" "$1" | tr -d ' '
}

MAGIC_HEX = 0x534F5845
MAGIC     = $((MAGIC_HEX))

magic_2nd=$(read_u32le "$file" 4)
magic_last=$(read_u32le "$file" $((size-8)))
checksum=$(read_u32le "$file" $((size-4)))

printf "[info] 2nd u32:  0x%08X\n" "$magic_2nd"
printf "[info] 2nd to last u32: 0x%08X\n" "$magic_last"
printf "[info] checksum:  0x%08X\n" "$checksum"

if [ "$magic_2nd" -eq "$MAGIC" ] && [ "$magic_last" -eq "$MAGIC" ]; then
	echo "EXOS magic signature present at both positions."
	exit 0
else
	echo "ERROR: EXOS magic signature NOT found at both positions!" >&2
	exit 1
fi

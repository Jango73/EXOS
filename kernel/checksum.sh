#!/bin/sh
# Compute byte-sum checksum (mod 2^32).
# Options:
#   --bin          : output 4 bytes LE on stdout (default, no newline)
#   --dec          : output decimal with newline
#   --hex          : output 8 lowercase hex chars with newline
#   --size-adjust N: add signed N to file size to define the number of bytes to read (clamped to [0, size])

set -eu

usage() {
	echo "Usage: $0 [--bin|--dec|--hex] [--size-adjust <+/-N>] <file>" >&2
	exit 1
}

mode="bin"
len_adj=0

# Parse options
while [ $# -gt 0 ]; do
	case "$1" in
		--bin|--dec|--hex)
			mode="${1#--}"
			shift
			;;
		--size-adjust)
			shift
			[ $# -gt 0 ] || usage
			len_adj="$1"
			# basic validation: signed integer
			# shellcheck disable=SC2039
			case "$len_adj" in
				''|*[!0-9+-]* ) echo "ERROR: --size-adjust expects a signed integer (e.g. -4, +2)" >&2; exit 1;;
			esac
			shift
			;;
		-- )
			shift; break;;
		-* )
			usage;;
		* )
			break;;
	esac
done

[ $# -eq 1 ] || usage
FILE="$1"
[ -f "$FILE" ] || { echo "ERROR: file not found: $FILE" >&2; exit 1; }

# File size
size=$(stat -c %s "$FILE")

# Compute effective length: clamp to [0, size]
# shellcheck disable=SC2003
len=$((size + len_adj))
[ "$len" -ge 0 ] || len=0
[ "$len" -le "$size" ] || len="$size"

# Sum bytes modulo 2^32 (do modulo in-loop for huge files)
cs=$(
    head -c "$len" "$FILE" \
    | od -An -v -tu1 \
    | awk '{for(i=1;i<=NF;i++){s+=$i; if(s>=4294967296)s-=4294967296}} END{printf "%u", s+0}'
)

# Log to stderr (informational)
# printf 'checksum (dec): %s\n' "$cs" >&2
# printf 'checksum (hex): %08x\n' "$cs" >&2
# printf 'bytes counted:  %s / %s\n' "$len" "$size" >&2

case "$mode" in
	bin)
		hex=$(printf '%08x' "$cs")
		b1=$(printf '%s' "$hex" | cut -c1-2)
		b2=$(printf '%s' "$hex" | cut -c3-4)
		b3=$(printf '%s' "$hex" | cut -c5-6)
		b4=$(printf '%s' "$hex" | cut -c7-8)
		# Little-endian bytes, no newline
		printf "\\x%s\\x%s\\x%s\\x%s" "$b4" "$b3" "$b2" "$b1"
		;;
	dec)
		printf '%s\n' "$cs"
		;;
	hex)
		printf '%08x\n' "$cs"
		;;
	*)
		echo "ERROR: unknown mode: $mode" >&2
		exit 1
		;;
esac

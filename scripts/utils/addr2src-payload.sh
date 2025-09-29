#!/bin/bash

# Script to convert payload address to source code line
# Usage: ./addr2src-payload.sh 0x8462

if [ $# -ne 1 ]; then
    echo "Usage: $0 <address>"
    echo "Example: $0 0x8462"
    exit 1
fi

ADDR=$1
PAYLOAD_ELF="boot-hd/bin/payload.elf"

if [ ! -f "$PAYLOAD_ELF" ]; then
    echo "Error: $PAYLOAD_ELF not found"
    exit 1
fi

echo "=== Payload Address $ADDR ==="

# Get function name and source file:line
RESULT=$(addr2line -e "$PAYLOAD_ELF" -f -C "$ADDR")
FUNCTION=$(echo "$RESULT" | head -n1)
LOCATION=$(echo "$RESULT" | tail -n1)

if [ "$LOCATION" = "??:0" ] || [ "$LOCATION" = "??:?" ]; then
    echo "No source info found for address $ADDR"
    exit 1
fi

echo "Function: $FUNCTION"
echo "Location: $LOCATION"
echo ""

# Extract file and line number
FILE=$(echo "$LOCATION" | cut -d: -f1)
LINE=$(echo "$LOCATION" | cut -d: -f2)

if [ -f "$FILE" ]; then
    echo "=== Source Code ==="
    # Show 5 lines before and after
    awk -v line="$LINE" '
        NR >= line-5 && NR <= line+5 {
            if (NR == line) 
                printf ">>> %4d: %s\n", NR, $0
            else 
                printf "    %4d: %s\n", NR, $0
        }' "$FILE"
else
    echo "Source file not found: $FILE"
fi
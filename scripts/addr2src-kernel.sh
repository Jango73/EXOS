#!/bin/bash

# Script to convert kernel address to source code line
# Usage: ./addr2src-kernel.sh 0xc0123456

if [ $# -ne 1 ]; then
    echo "Usage: $0 <address>"
    echo "Example: $0 0xc0123456"
    exit 1
fi

ADDR=$1
KERNEL_ELF="kernel/bin/exos.elf"

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: $KERNEL_ELF not found"
    exit 1
fi

echo "=== Kernel Address $ADDR ==="

# Get function name and source file:line
RESULT=$(addr2line -e "$KERNEL_ELF" -f -C "$ADDR")
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
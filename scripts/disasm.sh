#!/bin/bash

if [ $# -lt 3 ]; then
    echo "Usage: $0 <file> <origin> <offset>"
    echo "  <file>   : binary file to inspect"
    echo "  <origin> : origin of code (decimal or hex, ex: 31744 or 0x7c00)"
    echo "  <offset> : offset in file (decimal or hex)"
    echo "  <size> : size in bytes (decimal or hex)"
    exit 1
fi

FILE="$1"
ORIGIN="$2"
OFFSET="$3"
SIZE="$4"

# convert to decimal
ORIGIN_DEC=$((ORIGIN))
OFFSET_DEC=$((OFFSET))
SIZE_DEC=$((SIZE))

dd if="$FILE" bs=1 skip=$OFFSET_DEC count=$SIZE_DEC 2>/dev/null | ndisasm -b 16 -o $((ORIGIN_DEC + OFFSET_DEC)) -

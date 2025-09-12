#!/bin/bash

# Script to convert payload source line to address (16-bit code)
# Usage: ./src2addr-payload.sh boot-hd/source/vbr-payload-c.c:371

if [ $# -ne 1 ]; then
    echo "Usage: $0 <file:line>"
    echo "Example: $0 boot-hd/source/vbr-payload-c.c:371"
    exit 1
fi

# Load common functions
source "$(dirname "$0")/src2addr-common.sh"

SOURCE_LINE=$1
PAYLOAD_ELF="boot-hd/bin/payload.elf"

# Call common function with 16-bit flags (-M i8086)
src2addr_convert "$PAYLOAD_ELF" "$SOURCE_LINE" "-M i8086"
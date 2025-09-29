#!/bin/bash

# Script to convert kernel source line to address (32-bit code)
# Usage: ./src2addr-kernel.sh kernel/source/Schedule.c:123

if [ $# -ne 1 ]; then
    echo "Usage: $0 <file:line>"
    echo "Example: $0 kernel/source/Schedule.c:123"
    exit 1
fi

# Load common functions
source "$(dirname "$0")/src2addr-common.sh"

SOURCE_LINE=$1
KERNEL_ELF="kernel/bin/exos.elf"

# Call common function with 32-bit (no special arch flags)
src2addr_convert "$KERNEL_ELF" "$SOURCE_LINE" ""
#!/bin/bash

# Script to parse EIP values from qemu.log and disassemble each instruction

TRACE_LOG="log/qemu.log"
BUILD_CORE_NAME="${BUILD_CORE_NAME:-x86-32-mbr-debug}"
ELF_FILE="build/core/${BUILD_CORE_NAME}/kernel/exos.elf"
ADDR_TO_SRC="scripts/utils/addr2src-kernel-x86-32.sh"

if [ ! -f "$TRACE_LOG" ]; then
    echo "Error: $TRACE_LOG not found"
    exit 1
fi

if [ ! -f "$ELF_FILE" ]; then
    echo "Error: $ELF_FILE not found"
    exit 1
fi

echo "Parsing EIP values from $TRACE_LOG and disassembling each instruction..."
echo "======================================================================"

# Extract all EIP values (EIP=xxxx or EIP : xxxx) and disassemble each one
grep -E "EIP[[:space:]]*[=:]" "$TRACE_LOG" | \
    sed -n 's/.*EIP[[:space:]]*[=:][[:space:]]*\([0-9a-fA-F]*\).*/\1/p' | \
    while read -r eip; do
    if [ -n "$eip" ]; then
        $ADDR_TO_SRC 0x$eip
    fi
done

echo "Analysis complete."

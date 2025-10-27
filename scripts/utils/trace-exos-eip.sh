#!/bin/bash

# Script to parse EIP values from qemu.log and disassemble each instruction

TRACE_LOG="log/qemu.log"
ELF_FILE="build/i386/kernel/exos.elf"
ADDR_TO_SRC="scripts/utils/addr2src-kernel-i386.sh"

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

# Extract all EIP values and disassemble each one
grep "EIP=" "$TRACE_LOG" | sed 's/.*EIP=\([0-9a-fA-F]*\).*/\1/' | while read -r eip; do
    if [ -n "$eip" ]; then
        $ADDR_TO_SRC 0x$eip
    fi
done

echo "Analysis complete."

#!/bin/bash

# Script to parse EIP values from qemu.log and disassemble each instruction

TRACE_LOG="log/kernel.log"
ELF_FILE="build/x86-64/kernel/exos.elf"
ADDR_TO_SRC="scripts/utils/addr2src-kernel-x86-64.sh"

if [ ! -f "$TRACE_LOG" ]; then
    echo "Error: $TRACE_LOG not found"
    exit 1
fi

if [ ! -f "$ELF_FILE" ]; then
    echo "Error: $ELF_FILE not found"
    exit 1
fi

echo "Parsing RIP values from $TRACE_LOG and disassembling each instruction..."
echo "======================================================================"

# Extract all RIP values and disassemble each one
grep "RIP=" "$TRACE_LOG" | sed 's/.*RIP=\([0-9a-fA-F]*\).*/\1/' | while read -r rip; do
    if [ -n "$rip" ]; then
        $ADDR_TO_SRC 0x$rip
    fi
done

echo "Analysis complete."

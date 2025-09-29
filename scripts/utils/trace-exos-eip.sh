#!/bin/bash

# Script to parse EIP values from qemu.log and disassemble each instruction

TRACE_LOG="log/qemu.log"
ELF_FILE="kernel/bin/exos.elf"

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
        # Use objdump to find the instruction at this address
        instruction=$(objdump -d "$ELF_FILE" | grep -E "^\s*$eip:" | head -1)
        if [ -n "$instruction" ]; then
            echo "$instruction"
        else
            echo "EIP 0x$eip: <address not found in disassembly>"
        fi
    fi
done

echo "Analysis complete."

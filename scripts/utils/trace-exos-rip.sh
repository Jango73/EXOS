#!/bin/bash

# Script to parse RIP values from kernel.log and disassemble each instruction

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

# Extract all RIP values (RIP=xxxx or RIP : xxxx) and disassemble each one
grep -E "RIP[[:space:]]*[=:]" "$TRACE_LOG" | \
    sed -n 's/.*RIP[[:space:]]*[=:][[:space:]]*\([0-9a-fA-F]*\).*/\1/p' | \
    while read -r rip; do
    if [ -n "$rip" ]; then
        $ADDR_TO_SRC 0x$rip
    fi
done

echo "Analysis complete."

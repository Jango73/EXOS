#!/bin/bash

# Start Bochs with debugging enabled
# The debugger will automatically load symbols and set up breakpoints

echo "Starting Bochs with symbols loaded..."
echo "Available debugger commands:"
echo "  info symbols        - List all symbols"
echo "  b function_name     - Set breakpoint on function"
echo "  x /10i address      - Disassemble at address"
echo "  c                   - Continue execution"
echo ""

bochs -q -f scripts/bochs/bochs.txt -rc scripts/bochs/bochs_debug_commands.txt -unlock

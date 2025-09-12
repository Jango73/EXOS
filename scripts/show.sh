#!/bin/sh

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <address> [context_lines] (hex with 0x or decimal)"
    echo "  context_lines: number of lines to show before and after target (default: 20)"
    exit 1
fi

map="kernel/bin/exos.map"
elf="kernel/bin/exos.elf"
input_addr="$1"
context_lines="${2:-20}"

# Normalize address to hex (without decimal conversion)
if echo "$input_addr" | grep -q '^0x'; then
    hex_addr=$(echo "$input_addr" | tr '[:lower:]' '[:upper:]')
else
    hex_addr=$(printf "0x%X" "$input_addr")
fi
echo "Input address: $hex_addr"

# Extract symbols from map and find the right one in a single pass
found=$(awk -v target="$hex_addr" '
    /^[ \t]*0x[0-9a-fA-F]+[ \t]+[a-zA-Z_][a-zA-Z0-9_]*/ {
        addr = toupper($1)
        name = $2
        target_up = toupper(target)
        
        # Convert hex to decimal for comparison
        addr_dec = strtonum(addr)
        target_dec = strtonum(target_up)
        
        if (addr_dec <= target_dec) {
            best_addr = addr
            best_name = name
        }
    }
    END { 
        if (best_addr) print best_addr " " best_name 
    }
' "$map")

if [ -z "$found" ]; then
    echo "No symbol found."
    exit 1
fi

# Extract address and name of the found symbol
func_hex=$(echo "$found" | awk '{print $1}')
func_name=$(echo "$found" | awk '{print $2}')
echo "Symbol found: $func_name ($func_hex)"

# Find the next symbol in a single awk pass
next_hex=$(awk -v current="$func_hex" '
    /^[ \t]*0x[0-9a-fA-F]+[ \t]+[a-zA-Z_][a-zA-Z0-9_]*/ {
        addr_dec = strtonum(toupper($1))
        current_dec = strtonum(toupper(current))
        
        if (addr_dec > current_dec) {
            if (next_addr == "" || addr_dec < next_addr_dec) {
                next_addr = toupper($1)
                next_addr_dec = addr_dec
            }
        }
    }
    END { 
        if (next_addr) print next_addr 
    }
' "$map")

# Get BSS section start address from map file
bss_start=$(grep "^\.bss" "$map" | awk '{print $2}')
bss_start_dec=0
if [ -n "$bss_start" ]; then
    bss_start_dec=$(printf "%d" "$bss_start")
fi

# Check if address is within function range (numeric calculation)
target_dec=$(printf "%d" "$hex_addr")
func_dec=$(printf "%d" "$func_hex")

if [ -n "$next_hex" ]; then
    next_dec=$(printf "%d" "$next_hex")
    if [ $target_dec -ge $func_dec ] && [ $target_dec -lt $next_dec ]; then
        in_range=1
    else
        in_range=0
    fi
else
    # No next symbol, assume in function if >= func_hex
    if [ $target_dec -ge $func_dec ]; then
        in_range=1
    else
        in_range=0
    fi
fi

# Check if symbol is in BSS section or beyond
if [ $bss_start_dec -gt 0 ] && [ $func_dec -ge $bss_start_dec ]; then
    echo "Symbol $func_name ($func_hex) is in BSS section or beyond - no disassembly/dump performed"
    echo "BSS section starts at: $bss_start"
    exit 0
fi

# Action based on result
if [ "$in_range" = 1 ]; then
    echo "Within function $func_name ($func_hex)"
    if [ -n "$next_hex" ]; then
        echo "Disassembly from $func_hex to $next_hex"
        objdump -d "$elf" --start-address="$func_hex" --stop-address="$next_hex" | \
        awk -v target="$hex_addr" -v context="$context_lines" '
        {
            # Store all lines in array
            lines[NR] = $0
            
            # Extract address from objdump line (format: "  c0123456:	...")
            if (match($0, /^[ \t]*([0-9a-fA-F]+):/, arr)) {
                addr = "0X" toupper(arr[1])
                if (addr == target) {
                    target_line = NR
                }
            }
        }
        END {
            if (target_line > 0) {
                start_line = (target_line - context > 1) ? target_line - context : 1
                end_line = (target_line + context < NR) ? target_line + context : NR
                
                for (i = start_line; i <= end_line; i++) {
                    if (i == target_line) {
                        print ">>> " lines[i] " <<<"
                    } else {
                        print lines[i]
                    }
                }
            } else {
                # Target not found, print all lines (fallback)
                for (i = 1; i <= NR; i++) {
                    print lines[i]
                }
            }
        }'
    else
        echo "Disassembly from $func_hex (unknown size)"
        objdump -d "$elf" --start-address="$func_hex" | \
        awk -v target="$hex_addr" -v context="$context_lines" '
        {
            # Store all lines in array
            lines[NR] = $0
            
            # Extract address from objdump line (format: "  c0123456:	...")
            if (match($0, /^[ \t]*([0-9a-fA-F]+):/, arr)) {
                addr = "0X" toupper(arr[1])
                if (addr == target) {
                    target_line = NR
                }
            }
        }
        END {
            if (target_line > 0) {
                start_line = (target_line - context > 1) ? target_line - context : 1
                end_line = (target_line + context < NR) ? target_line + context : NR
                
                for (i = start_line; i <= end_line; i++) {
                    if (i == target_line) {
                        print ">>> " lines[i] " <<<"
                    } else {
                        print lines[i]
                    }
                }
            } else {
                # Target not found, print all lines (fallback)
                for (i = 1; i <= NR; i++) {
                    print lines[i]
                }
            }
        }'
    fi
else
    echo "No function found, closest symbol below: $func_name ($func_hex)"
    hexdump -C -n 256 -s "$func_hex" "$elf"
fi
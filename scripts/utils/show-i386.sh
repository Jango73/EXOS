#!/bin/sh

ARCH_DIR="i386"

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <address> [context_lines] (hex with 0x or decimal)"
    echo "  context_lines: number of lines to show before and after target (default: 20)"
    exit 1
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(dirname -- "$(dirname -- "$SCRIPT_DIR")")"
MAP_FILE="$ROOT_DIR/build/$ARCH_DIR/kernel/exos.map"
ELF_FILE="$ROOT_DIR/build/$ARCH_DIR/kernel/exos.elf"

if [ ! -f "$MAP_FILE" ]; then
    echo "Missing map file: $MAP_FILE"
    echo "Build the $ARCH_DIR kernel before using this tool."
    exit 1
fi

if [ ! -f "$ELF_FILE" ]; then
    echo "Missing ELF file: $ELF_FILE"
    echo "Build the $ARCH_DIR kernel before using this tool."
    exit 1
fi

input_addr="$1"
context_lines="${2:-20}"

if echo "$input_addr" | grep -q '^0x'; then
    hex_addr=$(echo "$input_addr" | tr '[:lower:]' '[:upper:]')
else
    hex_addr=$(printf "0x%X" "$input_addr")
fi
echo "Input address: $hex_addr"

found=$(awk -v target="$hex_addr" '
    /^[ \t]*0x[0-9a-fA-F]+[ \t]+[a-zA-Z_][a-zA-Z0-9_]*/ {
        addr = toupper($1)
        name = $2
        target_up = toupper(target)

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
' "$MAP_FILE")

if [ -z "$found" ]; then
    echo "No symbol found."
    exit 1
fi

func_hex=$(echo "$found" | awk '{print $1}')
func_name=$(echo "$found" | awk '{print $2}')
echo "Symbol found: $func_name ($func_hex)"

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
' "$MAP_FILE")

bss_start=$(grep "^\.bss" "$MAP_FILE" | awk '{print $2}')
bss_start_dec=0
if [ -n "$bss_start" ]; then
    bss_start_dec=$(printf "%d" "$bss_start")
fi

target_dec=$(printf "%d" "$hex_addr")
func_dec=$(printf "%d" "$func_hex")

in_range=0
if [ -n "$next_hex" ]; then
    next_dec=$(printf "%d" "$next_hex")
    if [ "$target_dec" -ge "$func_dec" ] && [ "$target_dec" -lt "$next_dec" ]; then
        in_range=1
    fi
else
    if [ "$target_dec" -ge "$func_dec" ]; then
        in_range=1
    fi
fi

if [ "$bss_start_dec" -gt 0 ] && [ "$func_dec" -ge "$bss_start_dec" ]; then
    echo "Symbol $func_name ($func_hex) is in BSS section or beyond - no disassembly/dump performed"
    echo "BSS section starts at: $bss_start"
    exit 0
fi

if [ "$in_range" -eq 1 ]; then
    echo "Within function $func_name ($func_hex)"
    if [ -n "$next_hex" ]; then
        echo "Disassembly from $func_hex to $next_hex"
        objdump -d "$ELF_FILE" --start-address="$func_hex" --stop-address="$next_hex" | \
        awk -v target="$hex_addr" -v context="$context_lines" '
        {
            lines[NR] = $0
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
                for (i = 1; i <= NR; i++) {
                    print lines[i]
                }
            }
        }'
    else
        echo "Disassembly from $func_hex (unknown size)"
        objdump -d "$ELF_FILE" --start-address="$func_hex" | \
        awk -v target="$hex_addr" -v context="$context_lines" '
        {
            lines[NR] = $0
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
                for (i = 1; i <= NR; i++) {
                    print lines[i]
                }
            }
        }'
    fi
else
    echo "No function found, closest symbol below: $func_name ($func_hex)"
    hexdump -C -n 256 -s "$func_hex" "$ELF_FILE"
fi

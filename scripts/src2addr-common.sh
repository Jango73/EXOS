#!/bin/bash

# Common function for source to address conversion
# Usage: src2addr_convert <ELF_FILE> <SOURCE_LINE> <OBJDUMP_ARCH_FLAGS>
# Example: src2addr_convert "kernel/bin/exos.elf" "kernel/source/Schedule.c:123" ""
# Example: src2addr_convert "boot-hd/bin/payload.elf" "boot-hd/source/vbr-payload-c.c:407" "-M i8086"

function src2addr_convert() {
    local ELF_FILE="$1"
    local SOURCE_LINE="$2" 
    local ARCH_FLAGS="$3"
    
    if [ ! -f "$ELF_FILE" ]; then
        echo "Error: $ELF_FILE not found"
        return 1
    fi
    
    echo "=== Source $SOURCE_LINE ===" 
    
    # Use objdump with architecture-specific flags
    objdump -d -l $ARCH_FLAGS "$ELF_FILE" | awk -v target="$SOURCE_LINE" '
    BEGIN { 
        found = 0; first_addr = ""; count = 0; 
        split(target, parts, ":")
        target_file = parts[1]
        target_line = int(parts[2])
        near_matches = ""
        near_count = 0
    }

    # Look for source line references (with absolute paths)
    /^\/.*:[0-9]+$/ { 
        current_source = $0
        gsub(/^\//, "", current_source)  # Remove leading /
        
        # Split current source
        split(current_source, curr_parts, ":")
        curr_file = curr_parts[1]
        curr_line = int(curr_parts[2])
        
        # Exact match
        if (current_source == target || index(current_source, target) > 0) {
            found = 1
            if (count == 0) {
                print "Found exact source line: " current_source
            }
            next
        } 
        # Check for same file and nearby lines (within 5 lines)
        else if (index(curr_file, target_file) > 0 && abs(curr_line - target_line) <= 5) {
            if (near_count < 3) {  # Limit to 3 nearby matches
                if (near_matches == "") {
                    near_matches = current_source ":" curr_line
                } else {
                    near_matches = near_matches "|" current_source ":" curr_line
                }
                near_count++
            }
            found = 0
        }
        else {
            found = 0
        }
    }

    # Capture assembly lines when we have a match
    found && /^[[:space:]]*[0-9a-f]+:/ {
        # Extract address (first field, remove colon)
        addr = $1
        gsub(/:/, "", addr)
        
        if (count == 0) {
            first_addr = addr
            print "First address: 0x" addr
            print "First ASM: " $0
            count++
        } else {
            count++
        }
    }

    # Absolute value function
    function abs(x) { return x < 0 ? -x : x }

    # Print summary at end
    END {
        if (count > 1) {
            print ""
            print "Total instructions for this source line: " count
            print "Range: 0x" first_addr " to 0x" addr
            print "Use ./scripts/show.sh 0x" first_addr " to see disassembly context"
        } else if (count == 1) {
            print ""
            print "Use ./scripts/show.sh 0x" first_addr " to see disassembly context"
        } else if (count == 0 && near_count > 0) {
            print ""
            print "EXACT line not found. Possible nearby lines due to compiler optimization:"
            split(near_matches, nearby_array, "|")
            for (i = 1; i <= near_count; i++) {
                print "  - " nearby_array[i]
            }
            print ""
            print "Try one of these nearby lines, or check if the code was optimized away."
        } else {
            print ""
            print "No matching source line found. Possible causes:"
            print "  - Line was optimized away by compiler (-O2)"
            print "  - Line contains only declarations/comments"
            print "  - Debug info not available for this line"
        }
    }
    '
}
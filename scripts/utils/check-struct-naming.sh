#!/bin/bash

# Script to find structures that don't follow the tag_SCREAMING_SNAKE_CASE format

echo "Checking struct naming conventions in .h and .c files..."
echo "Expected format: tag_SCREAMING_SNAKE_CASE"
echo "=================================================="

# Find all .h and .c files and search for struct definitions
find . -name "*.h" -o -name "*.c" | while read file; do
    # Extract struct definitions (both typedef struct and plain struct)
    # Look for patterns like "struct NAME" or "typedef struct NAME"
    # Handle __attribute__ between struct and name
    grep -n "^\s*\(typedef\s\+\)\?struct\s\+" "$file" | while read line; do
        line_num=$(echo "$line" | cut -d: -f1)
        content=$(echo "$line" | cut -d: -f2-)

        # Extract the struct name, skipping __attribute__ if present
        struct_name=$(echo "$content" | awk '{
            # Remove typedef if present
            gsub(/^[[:space:]]*typedef[[:space:]]+/, "")
            # Remove everything up to and including "struct "
            sub(/^.*struct[[:space:]]+/, "")
            # Remove __attribute__((packed)) if present (handle nested parentheses)
            if (match($0, /^__attribute__[[:space:]]*\(\(packed\)\)[[:space:]]+/)) {
                sub(/^__attribute__[[:space:]]*\(\(packed\)\)[[:space:]]+/, "")
            }
            # Extract first word as struct name
            if (match($0, /^[A-Za-z_][A-Za-z0-9_]*/)) {
                print substr($0, RSTART, RLENGTH)
            }
        }')

        if [ ! -z "$struct_name" ]; then
            # Check if it follows tag_SCREAMING_SNAKE_CASE pattern
            # Should start with "tag_" followed by uppercase letters, numbers and underscores
            if ! echo "$struct_name" | grep -q "^tag_[A-Z0-9_]\+$"; then
                echo "File: $file:$line_num"
                echo "  Struct: $struct_name"
                echo "  Line: $(echo "$content" | sed 's/^[[:space:]]*//')"
                echo ""
            fi
        fi
    done
done

echo "Done."

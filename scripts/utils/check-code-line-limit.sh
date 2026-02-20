#!/bin/sh

# Scan tracked .c/.h files and report files above a configurable line limit.
LIMIT="${1:-1000}"

if ! printf '%s' "$LIMIT" | grep -Eq '^[0-9]+$'; then
    echo "ERROR: limit must be an integer"
    echo "Usage: $0 [line_limit]"
    exit 2
fi

if command -v git >/dev/null 2>&1; then
    FILES=$(git ls-files '*.c' '*.h' ':(exclude)third/**')
    SCAN_MODE="tracked .c/.h files (excluding third/)"
else
    FILES=$(find . \
        -path './.git' -prune -o \
        -path './third' -prune -o \
        -type f \( -name '*.c' -o -name '*.h' \) -print | sed 's|^\./||')
    SCAN_MODE="filesystem .c/.h files (excluding .git/ and third/)"
fi

if [ -z "$FILES" ]; then
    echo "No .c or .h files found."
    exit 0
fi

TOTAL_FILES=0
EXCEEDED_COUNT=0

printf 'Line limit: %s\n' "$LIMIT"
printf 'Scanning %s...\n\n' "$SCAN_MODE"

# shellcheck disable=SC2039
for FILE in $FILES; do
    TOTAL_FILES=$((TOTAL_FILES + 1))
    LINE_COUNT=$(wc -l < "$FILE")
    LINE_COUNT=$(printf '%s' "$LINE_COUNT" | tr -d '[:space:]')

    if [ "$LINE_COUNT" -gt "$LIMIT" ]; then
        EXCEEDED_COUNT=$((EXCEEDED_COUNT + 1))
        printf '%s lines | %s\n' "$LINE_COUNT" "$FILE"
    fi
done

printf '\nScanned files: %s\n' "$TOTAL_FILES"
printf 'Files above %s lines: %s\n' "$LIMIT" "$EXCEEDED_COUNT"

if [ "$EXCEEDED_COUNT" -gt 0 ]; then
    exit 1
fi

exit 0

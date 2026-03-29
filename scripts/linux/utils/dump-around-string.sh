#!/bin/sh

if [ $# -lt 2 ]; then
    echo "Usage: $0 <file> <string> [context]"
    echo "  <file>   : binary file to inspect"
    echo "  <string> : string to search for (e.g. STAK)"
    echo "  [context]: bytes of context before/after (default 32)"
    exit 1
fi

FILE="$1"
PATTERN="$2"
CONTEXT="${3:-32}"

grep -abo -- "$PATTERN" "$FILE" | cut -d: -f1 | while read -r off; do
    start=$((off > CONTEXT ? off - CONTEXT : 0))
    hexdump -C -s $start -n $((CONTEXT * 2 + ${#PATTERN})) "$FILE"
done

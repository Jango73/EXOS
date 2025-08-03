#!/bin/sh

if [ $# -lt 2 ]; then
    echo "Usage: $0 <string> <file> [context]"
    echo "  <string> : chaîne à chercher (ex: STAK)"
    echo "  <file>   : fichier binaire à inspecter"
    echo "  [context]: octets de contexte avant/après (défaut 32)"
    exit 1
fi

PATTERN="$1"
FILE="$2"
CONTEXT="${3:-32}"

grep -abo -- "$PATTERN" "$FILE" | cut -d: -f1 | while read -r off; do
    start=$((off > CONTEXT ? off - CONTEXT : 0))
    hexdump -C -s $start -n $((CONTEXT * 2 + ${#PATTERN})) "$FILE"
done

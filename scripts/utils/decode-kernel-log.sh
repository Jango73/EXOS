#!/bin/bash
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $0 <log-file>"
    exit 1
fi

LOG_FILE="$1"

if [ ! -f "$LOG_FILE" ]; then
    echo "Error: file not found: $LOG_FILE"
    exit 1
fi

is_hex_dump() {
    local stripped
    stripped="$(LC_ALL=C tr -d '0-9a-fA-F \r\n\t' < "$LOG_FILE")"
    [ -z "$stripped" ]
}

decode_stream() {
    if is_hex_dump; then
        LC_ALL=C tr -d ' \r\n\t' < "$LOG_FILE" | xxd -r -p
    else
        cat "$LOG_FILE"
    fi
}

TMP_OUTPUT="${LOG_FILE}.decoded"

decode_stream | LC_ALL=C tr -c '\11\12\15\40-\176' '?' > "$TMP_OUTPUT"
mv "$TMP_OUTPUT" "$LOG_FILE"

#!/usr/bin/env sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <save-file>"
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "Save file not found: $1"
    exit 1
fi

node "$SCRIPT_DIR/dump-terminal-tactics-save.js" "$1"

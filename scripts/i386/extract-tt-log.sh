#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
EXTRACT="$SCRIPT_DIR/../utils/extract-exos-file.sh"
LOG_PATH="/exos/apps/terminal-tactics.log"
IMG="$SCRIPT_DIR/../../build/i386/boot-hd/exos.img"

if [ ! -x "$EXTRACT" ]; then
    echo "Missing helper: $EXTRACT"
    exit 1
fi

sh "$EXTRACT" "$IMG" "$LOG_PATH" "$SCRIPT_DIR/../../temp/terminal-tactics.log"

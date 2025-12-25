#!/bin/sh
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

sh "$SCRIPT_DIR/extract-exos-file.sh" \
    "$SCRIPT_DIR/../build/i386/boot-hd/exos.img" \
    "/exos/apps/terminal-tactics.sav" \
    "$SCRIPT_DIR/../terminal-tactics.sav"

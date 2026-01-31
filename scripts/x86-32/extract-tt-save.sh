#!/bin/sh
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

sh "$SCRIPT_DIR/../utils/extract-exos-file.sh" \
    "$SCRIPT_DIR/../../build/x86-32/boot-hd/exos.img" \
    "/exos/apps/terminal-tactics.sav" \
    "$SCRIPT_DIR/../../temp/terminal-tactics.sav"

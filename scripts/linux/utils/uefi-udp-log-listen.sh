#!/bin/bash
set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
node "$SCRIPT_DIR/uefi-udp-log-listen.js" "$@"


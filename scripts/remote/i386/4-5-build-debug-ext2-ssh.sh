#!/bin/sh
set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(dirname -- "$(dirname -- "$(dirname -- "$SCRIPT_DIR")")")"

"$ROOT_DIR/scripts/remote/run-ssh.sh" "scripts/i386/4-5-build-debug-ext2.sh"

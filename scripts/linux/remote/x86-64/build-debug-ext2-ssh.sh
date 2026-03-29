#!/bin/sh
set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(dirname -- "$(dirname -- "$(dirname -- "$(dirname -- "$SCRIPT_DIR")")")")"

"$ROOT_DIR/scripts/linux/remote/run-ssh.sh" "scripts/linux/build/build.sh" --arch x86-64 --fs ext2 --debug

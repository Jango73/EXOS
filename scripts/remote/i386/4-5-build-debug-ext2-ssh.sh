#!/bin/sh
set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(dirname -- "$(dirname -- "$(dirname -- "$SCRIPT_DIR")")")"

"$ROOT_DIR/scripts/remote/run-ssh.sh" "scripts/build.sh" --arch x86-32 --fs ext2 --debug

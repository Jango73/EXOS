#!/bin/sh
set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(dirname -- "$SCRIPT_DIR")"
CONFIG_FILE="$SCRIPT_DIR/ssh-config.sh"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Missing config: $CONFIG_FILE"
    exit 1
fi

. "$CONFIG_FILE"

if [ -z "$EXOS_REMOTE_REPO" ]; then
    echo "EXOS_REMOTE_REPO is not set in $CONFIG_FILE"
    exit 1
fi

REMOTE_SCRIPT="$1"
shift || true

if [ -z "$REMOTE_SCRIPT" ]; then
    echo "Usage: $0 <script_path> [args...]"
    exit 1
fi

SSH_TARGET="${EXOS_SSH_USER}@${EXOS_SSH_HOST}"
SSH_BASE_CMD="ssh -p $EXOS_SSH_PORT"

if [ -n "$EXOS_SSH_PASS" ]; then
    if command -v sshpass >/dev/null 2>&1; then
        SSH_BASE_CMD="sshpass -p \"$EXOS_SSH_PASS\" $SSH_BASE_CMD"
    else
        echo "sshpass is required when EXOS_SSH_PASS is set."
        echo "Install sshpass or configure key-based auth and clear EXOS_SSH_PASS."
        exit 1
    fi
fi

REMOTE_ARGS="$*"
if [ -n "$REMOTE_ARGS" ]; then
    REMOTE_ARGS=" $REMOTE_ARGS"
fi

$SSH_BASE_CMD "$SSH_TARGET" "cd \"$EXOS_REMOTE_REPO\" && \"$REMOTE_SCRIPT\"$REMOTE_ARGS"

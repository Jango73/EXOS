#!/usr/bin/env bash
set -euo pipefail

MONITOR_HOST="${MONITOR_HOST:-127.0.0.1}"
MONITOR_PORT="${MONITOR_PORT:-4444}"

if ! command -v nc >/dev/null 2>&1; then
    echo "nc not found. Install netcat or set up another monitor client."
    exit 1
fi

printf "device_del usbmsd0\n" | nc -w 1 "$MONITOR_HOST" "$MONITOR_PORT" >/dev/null

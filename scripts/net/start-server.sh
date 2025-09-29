#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_HOST="0.0.0.0"
SERVER_PORT="8081"

echo "[start-server.sh] Killing any existing HTTP servers on port $SERVER_PORT"
pkill -f "python.*http.server.*$SERVER_PORT" 2>/dev/null || true

echo "[start-server.sh] Starting HTTP server on $SERVER_HOST:$SERVER_PORT"
cd "$SCRIPT_DIR"
python3 -m http.server "$SERVER_PORT" --bind "$SERVER_HOST" &
SERVER_PID=$!

echo "[start-server.sh] HTTP server started with PID $SERVER_PID"
echo "[start-server.sh] Serving files from: $SCRIPT_DIR"
echo "[start-server.sh] Test URL: http://$SERVER_HOST:$SERVER_PORT/"

# Give server time to start
sleep 1
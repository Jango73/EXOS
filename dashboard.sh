#!/bin/bash
set -e

# Path to the dashboard script
SCRIPT_FILE="dashboard.mjs"

# Required dependencies
DEPS=("blessed" "tail" "kill-port")

# Initialize npm project if missing
if [ ! -f package.json ]; then
    echo "[setup] Initializing npm project..."
    npm init -y >/dev/null
fi

# Install missing dependencies
for dep in "${DEPS[@]}"; do
    if ! npm list "$dep" >/dev/null 2>&1; then
        echo "[setup] Installing $dep..."
        npm install "$dep"
    fi
done

# Run the dashboard
if [ ! -f "$SCRIPT_FILE" ]; then
    echo "[error] Script '$SCRIPT_FILE' not found."
    exit 1
fi

echo "[run] Starting dashboard..."
exec node "$SCRIPT_FILE"

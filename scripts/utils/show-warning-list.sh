#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(dirname -- "$(dirname -- "$SCRIPT_DIR")")"

FS="${1:-ext2}"
DO_CLEAN="${2:---clean}"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

BUILD_FAILED=0

run_build() {
    local Arch="$1"
    local LogFile="$TMP_DIR/build-${Arch}.log"

    echo "[${Arch}] Running debug build (fs=${FS})..." >&2

    if ! LC_ALL=C LANG=C "$ROOT_DIR/scripts/build" --arch "$Arch" --fs "$FS" --debug "$DO_CLEAN" >"$LogFile" 2>&1; then
        echo "[${Arch}] Build failed" >&2
        BUILD_FAILED=1
    fi
}

print_warnings() {
    local Arch="$1"
    local LogFile="$TMP_DIR/build-${Arch}.log"
    local NormalizedLog="$TMP_DIR/build-${Arch}.normalized.log"
    local Found=0

    # Normalize line endings and remove ANSI color sequences before matching.
    sed -E 's/\x1B\[[0-9;]*[A-Za-z]//g; s/\r$//' "$LogFile" > "$NormalizedLog"

    while IFS= read -r Line; do
        printf '[%s] %s\n' "$Arch" "$Line"
        Found=1
    done < <(
        grep -Ei "warning|avertissement" "$NormalizedLog" \
            | grep -Eiv '(^|[[:space:]])(\.\./)?third/' \
            || true
    )

    if [ "$Found" -eq 0 ]; then
        if grep -qi "A build is already running" "$NormalizedLog"; then
            echo "[${Arch}] <build lock detected: no warning scan>"
            BUILD_FAILED=1
            return
        fi
        echo "[${Arch}] <no warnings>"
    fi
}

run_build "x86-32"
run_build "x86-64"

print_warnings "x86-32"
print_warnings "x86-64"

if [ "$BUILD_FAILED" -ne 0 ]; then
    exit 1
fi

#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SMOKE_TEST_SCRIPT_NAME="$0"
SMOKE_TEST_DEFAULT_COMMANDS_FILE="$ROOT_DIR/scripts/smoke-test-xhci-commands.txt"
SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER=1

# shellcheck source=/dev/null
source "$ROOT_DIR/scripts/utils/smoke-test-common.sh"
SmokeTestMain "$@"

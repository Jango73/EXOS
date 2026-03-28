#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
SCRIPT_DIR="$ROOT_DIR/scripts/linux"
SMOKE_TEST_SCRIPT_NAME="$0"
SMOKE_TEST_DEFAULT_COMMANDS_FILE="$ROOT_DIR/scripts/common/smoke-test-network-commands.txt"
SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER=1

# shellcheck source=/dev/null
source "$SCRIPT_DIR/utils/smoke-test-common.sh"
SmokeTestMain "$@"

#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
SMOKE_TEST_SCRIPT_NAME="$0"
SMOKE_TEST_DEFAULT_COMMANDS_FILE="$ROOT_DIR/scripts/common/smoke-test-global-commands.txt"
SMOKE_TEST_X86_32_RTL8139_COMMANDS_FILE="$ROOT_DIR/scripts/common/smoke-test-global-rtl8139-commands.txt"
SMOKE_TEST_REQUIRE_LOCAL_HTTP_SERVER=1

# shellcheck source=/dev/null
source "$ROOT_DIR/scripts/linux/utils/smoke-test-common.sh"
SmokeTestMain "$@"

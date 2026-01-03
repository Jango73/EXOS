#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$ROOT_DIR/log/kernel.log"
COMMANDS_FILE="$ROOT_DIR/scripts/test-commands.txt"
MONITOR_HOST="127.0.0.1"
MONITOR_PORT="4444"
DEFAULT_TIMEOUT_SECONDS=15

FAULT_PATTERN="#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC"
TEST_KO_PATTERN="TEST > .* : KO"

RG_BIN="$(command -v rg || true)"
GREP_BIN="$(command -v grep || true)"

if [ -z "$GREP_BIN" ]; then
    echo "Missing grep. Aborting."
    exit 1
fi

function SearchRegex() {
    if [ -n "$RG_BIN" ]; then
        rg -n "$1"
    else
        grep -n -E "$1"
    fi
}

function SearchFixed() {
    if [ -n "$RG_BIN" ]; then
        rg -n -F "$1"
    else
        grep -n -F "$1"
    fi
}

function GetLogSize() {
    if [ -f "$LOG_FILE" ]; then
        wc -c < "$LOG_FILE" | tr -d ' '
    else
        echo 0
    fi
}

function TailFromOffset() {
    local Offset="$1"
    if [ -f "$LOG_FILE" ]; then
        tail -c "+$((Offset + 1))" "$LOG_FILE"
    fi
}

function MonitorCommand() {
    local Cmd="$1"

    if ! exec 3<>"/dev/tcp/$MONITOR_HOST/$MONITOR_PORT"; then
        echo "Failed to connect to QEMU monitor at $MONITOR_HOST:$MONITOR_PORT"
        return 1
    fi

    printf "%s\r\n" "$Cmd" >&3
    exec 3<&-
    exec 3>&-
}

function WaitForMonitor() {
    local Index=0

    while [ "$Index" -lt 50 ]; do
        if exec 3<>"/dev/tcp/$MONITOR_HOST/$MONITOR_PORT"; then
            exec 3<&-
            exec 3>&-
            return 0
        fi
        Index=$((Index + 1))
        sleep 0.2
    done

    return 1
}

function KeyForChar() {
    local Char="$1"
    case "$Char" in
        [a-z0-9]) echo "$Char" ;;
        " ") echo "spc" ;;
        "/") echo "slash" ;;
        "-") echo "minus" ;;
        ".") echo "dot" ;;
        "_") echo "underscore" ;;
        *) echo "" ;;
    esac
}

function SendKey() {
    local Key="$1"
    if [ -z "$Key" ]; then
        echo "Unsupported key in command string."
        return 1
    fi
    MonitorCommand "sendkey $Key"
    sleep 0.05
}

function SendCommand() {
    local Cmd="$1"
    local Index=0
    local Length=${#Cmd}
    local Char
    local Key

    while [ "$Index" -lt "$Length" ]; do
        Char="${Cmd:$Index:1}"
        Key="$(KeyForChar "$Char")"
        SendKey "$Key"
        Index=$((Index + 1))
    done

    SendKey "ret"
}

function WaitForExpectedLog() {
    local Expected="$1"
    local Offset="$2"
    local StartTime="$SECONDS"

    while [ $((SECONDS - StartTime)) -lt "$DEFAULT_TIMEOUT_SECONDS" ]; do
        if TailFromOffset "$Offset" | SearchRegex "$FAULT_PATTERN" >/dev/null; then
            echo "Fault detected in kernel log."
            TailFromOffset "$Offset" | SearchRegex "$FAULT_PATTERN" || true
            return 1
        fi

        if TailFromOffset "$Offset" | SearchRegex "$TEST_KO_PATTERN" >/dev/null; then
            echo "Test reported KO in kernel log."
            TailFromOffset "$Offset" | SearchRegex "$TEST_KO_PATTERN" || true
            return 1
        fi

        if [ -n "$Expected" ] && TailFromOffset "$Offset" | SearchFixed "$Expected" >/dev/null; then
            return 0
        fi

        sleep 0.2
    done

    echo "Timed out waiting for expected log: $Expected"
    return 1
}

function AssertNoFailures() {
    local Offset="$1"

    if TailFromOffset "$Offset" | SearchRegex "$FAULT_PATTERN" >/dev/null; then
        echo "Fault detected in kernel log."
        TailFromOffset "$Offset" | SearchRegex "$FAULT_PATTERN" || true
        return 1
    fi

    if TailFromOffset "$Offset" | SearchRegex "$TEST_KO_PATTERN" >/dev/null; then
        echo "Test reported KO in kernel log."
        TailFromOffset "$Offset" | SearchRegex "$TEST_KO_PATTERN" || true
        return 1
    fi
}

function RunCommandList() {
    local Line
    local CommandText
    local ExpectedText

    if [ ! -f "$COMMANDS_FILE" ]; then
        echo "Commands file not found: $COMMANDS_FILE"
        exit 1
    fi

    while IFS= read -r Line || [ -n "$Line" ]; do
        Line="${Line%%$'\r'}"
        if [ -z "$Line" ]; then
            continue
        fi
        if [[ "$Line" == \#* ]]; then
            continue
        fi

        CommandText="${Line%%|*}"
        ExpectedText="${Line#*|}"

        if [ "$CommandText" = "$ExpectedText" ]; then
            echo "Invalid test line, missing expected text: $Line"
            exit 1
        fi

        echo "Running command: $CommandText"
        local Offset
        Offset="$(GetLogSize)"
        SendCommand "$CommandText"
        WaitForExpectedLog "$ExpectedText" "$Offset"
        sleep 0.2
        AssertNoFailures "$Offset"
    done < "$COMMANDS_FILE"
}

function StopQemu() {
    MonitorCommand "quit" || true
}

function RunArchitecture() {
    local Name="$1"
    local BuildScript="$2"
    local QemuScript="$3"

#    echo "Building $Name..."
#    "$ROOT_DIR/$BuildScript"

    echo "Starting QEMU for $Name..."
    mkdir -p "$ROOT_DIR/log"
    : > "$LOG_FILE"

    bash -c "cd \"$ROOT_DIR\" && $QemuScript" &
    local QemuPid=$!

    if ! WaitForMonitor; then
        echo "QEMU monitor did not start."
        kill "$QemuPid" || true
        exit 1
    fi

    sleep 2
    AssertNoFailures 0
    RunCommandList
    AssertNoFailures 0
    StopQemu

    wait "$QemuPid" || true
}

RunArchitecture "i386" "scripts/build.sh --arch i386 --fs ext2 --debug --clean" "scripts/run.sh --arch i386"
RunArchitecture "x86-64" "scripts/build.sh --arch x86-64 --fs ext2 --debug --clean" "scripts/run.sh --arch x86-64"

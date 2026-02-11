#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$ROOT_DIR/log/kernel.log"
COMMANDS_FILE="$ROOT_DIR/scripts/test-commands.txt"
CONFIG_FILES=(
    "$ROOT_DIR/kernel/configuration/exos.ext2.toml"
    "$ROOT_DIR/kernel/configuration/exos.fat32.toml"
)
MONITOR_HOST="127.0.0.1"
MONITOR_PORT="${MONITOR_PORT:-4444}"
DEFAULT_TIMEOUT_SECONDS=15
BOOT_READY_TIMEOUT_SECONDS=45
KEY_DELAY_SECONDS=0.12
COMMAND_DELAY_SECONDS=0.25
BOOT_INPUT_DELAY_SECONDS=1.0
TEST_KEYBOARD_LAYOUT="en-US"
CONFIG_BACKUP_DIR=""
BOOT_READY_PATTERN="[InitializeKernel] Shell task created"

FAULT_PATTERN="#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC"
TEST_KO_PATTERN="TEST > .* : KO"
ERROR_PATTERN="ERROR >"

RG_BIN="$(command -v rg || true)"
GREP_BIN="$(command -v grep || true)"
RUN_X86_32=1
RUN_X86_64=1
RUN_X86_64_UEFI=1

function Usage() {
    echo "Usage: $0 [--only <x86-32|x86-64|x86-64-uefi>] [--help]"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --only)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --only"
                Usage
                exit 1
            fi
            RUN_X86_32=0
            RUN_X86_64=0
            RUN_X86_64_UEFI=0
            case "$1" in
                x86-32) RUN_X86_32=1 ;;
                x86-64) RUN_X86_64=1 ;;
                x86-64-uefi) RUN_X86_64_UEFI=1 ;;
                *)
                    echo "Invalid --only target: $1"
                    Usage
                    exit 1
                    ;;
            esac
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            Usage
            exit 1
            ;;
    esac
    shift
done

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

function ForceTestKeyboardLayout() {
    CONFIG_BACKUP_DIR="$(mktemp -d)"

    for ConfigPath in "${CONFIG_FILES[@]}"; do
        if [ ! -f "$ConfigPath" ]; then
            continue
        fi

        cp "$ConfigPath" "$CONFIG_BACKUP_DIR/$(basename "$ConfigPath")"

        awk -v layout="$TEST_KEYBOARD_LAYOUT" '
        BEGIN {
            in_keyboard = 0;
            layout_set = 0;
        }
        {
            if ($0 ~ /^\[Keyboard\]/) {
                in_keyboard = 1;
                print $0;
                next;
            }

            if ($0 ~ /^\[/ && in_keyboard == 1) {
                if (layout_set == 0) {
                    print "Layout=\"" layout "\"";
                    layout_set = 1;
                }
                in_keyboard = 0;
                print $0;
                next;
            }

            if (in_keyboard == 1 && $0 ~ /^Layout[[:space:]]*=/) {
                if (layout_set == 0) {
                    print "Layout=\"" layout "\"";
                    layout_set = 1;
                }
                next;
            }

            print $0;
        }
        END {
            if (in_keyboard == 1 && layout_set == 0) {
                print "Layout=\"" layout "\"";
            }
        }
    ' "$ConfigPath" > "$ConfigPath.tmp"

        mv "$ConfigPath.tmp" "$ConfigPath"
    done
}

function RestoreConfigFile() {
    if [ -n "$CONFIG_BACKUP_DIR" ] && [ -d "$CONFIG_BACKUP_DIR" ]; then
        for ConfigPath in "${CONFIG_FILES[@]}"; do
            local BackupPath="$CONFIG_BACKUP_DIR/$(basename "$ConfigPath")"
            if [ -f "$BackupPath" ] && [ -f "$ConfigPath" ]; then
                cp "$BackupPath" "$ConfigPath"
            fi
        done
        rm -rf "$CONFIG_BACKUP_DIR"
        CONFIG_BACKUP_DIR=""
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
        [A-Z]) echo "shift-${Char,,}" ;;
        [a-z0-9]) echo "$Char" ;;
        " ") echo "spc" ;;
        "/") echo "kp_divide" ;;
        "-") echo "minus" ;;
        ".") echo "dot" ;;
        "_") echo "shift-minus" ;;
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
    sleep "$KEY_DELAY_SECONDS"
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
    sleep "$COMMAND_DELAY_SECONDS"
}

function WaitForExpectedLog() {
    local Expected="$1"
    local Offset="$2"
    local TimeoutSeconds="${3:-$DEFAULT_TIMEOUT_SECONDS}"
    local StartTime="$SECONDS"

    while [ $((SECONDS - StartTime)) -lt "$TimeoutSeconds" ]; do
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

function WarnLogErrors() {
    local Offset="$1"
    local ContextLabel="${2:-run}"

    if TailFromOffset "$Offset" | SearchFixed "$ERROR_PATTERN" >/dev/null; then
        echo "WARNING: kernel errors detected in $ContextLabel:"
        TailFromOffset "$Offset" | SearchFixed "$ERROR_PATTERN" || true
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
        WarnLogErrors "$Offset" "command '$CommandText'"
    done < "$COMMANDS_FILE"
}

function StopQemu() {
    MonitorCommand "quit" || true
}

function RunArchitecture() {
    local Name="$1"
    local BuildScript="$2"
    local QemuScript="$3"
    local KernelLogRelativePath="$4"

    echo "Building $Name..."
    bash -c "cd \"$ROOT_DIR\" && $BuildScript"

    echo "Starting QEMU for $Name..."
    mkdir -p "$ROOT_DIR/log"
    LOG_FILE="$ROOT_DIR/$KernelLogRelativePath"
    : > "$LOG_FILE"

    bash -c "cd \"$ROOT_DIR\" && $QemuScript" &
    local QemuPid=$!
    trap 'StopQemu || true; if kill -0 "$QemuPid" 2>/dev/null; then kill "$QemuPid" || true; fi' RETURN

    if ! WaitForMonitor; then
        echo "QEMU monitor did not start."
        kill "$QemuPid" || true
        exit 1
    fi

    sleep 2
    sleep "$BOOT_INPUT_DELAY_SECONDS"
    AssertNoFailures 0
    WaitForExpectedLog "$BOOT_READY_PATTERN" 0 "$BOOT_READY_TIMEOUT_SECONDS"
    WarnLogErrors 0 "boot phase"
    RunCommandList
    AssertNoFailures 0
    WarnLogErrors 0 "$Name full run"
    StopQemu

    wait "$QemuPid" || true
}

trap 'RestoreConfigFile' EXIT
ForceTestKeyboardLayout

if [ "$RUN_X86_32" -eq 1 ]; then
    RunArchitecture "x86-32" "scripts/build.sh --arch x86-32 --fs ext2 --debug --clean" "scripts/run.sh --arch x86-32" "log/kernel-x86-32-mbr.log"
fi
if [ "$RUN_X86_64" -eq 1 ]; then
    RunArchitecture "x86-64" "scripts/build.sh --arch x86-64 --fs ext2 --debug --clean" "scripts/run.sh --arch x86-64" "log/kernel-x86-64-mbr.log"
fi
if [ "$RUN_X86_64_UEFI" -eq 1 ]; then
    RunArchitecture "x86-64 UEFI" "scripts/build.sh --arch x86-64 --fs ext2 --debug --clean --uefi" "scripts/run.sh --arch x86-64 --uefi" "log/kernel-x86-64-uefi.log"
fi

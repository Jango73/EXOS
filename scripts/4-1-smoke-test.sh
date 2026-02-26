#!/bin/bash
set -euo pipefail

# Smoke test orchestrator:
# 1) Optional build(s)
# 2) Boot QEMU and wait until shell is ready
# 3) Inject deterministic keyboard input through QEMU monitor
# 4) Validate expected logs and fail on faults/KO/fatal errors
# 5) Optionally compare downloaded file size against host source file

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$ROOT_DIR/log/kernel.log"
COMMANDS_FILE="$ROOT_DIR/scripts/smoke-test-commands.txt"
LOCAL_HTTP_SERVER_SCRIPT="$ROOT_DIR/scripts/net/start-server.sh"
LOCAL_HTTP_SERVER_PORT="${LOCAL_HTTP_SERVER_PORT:-8081}"
SKIP_LOCAL_HTTP_SERVER="${SKIP_LOCAL_HTTP_SERVER:-0}"
MONITOR_HOST="127.0.0.1"
MONITOR_PORT="${MONITOR_PORT:-4444}"
MONITOR_CONNECT_MAX_ATTEMPTS=50
DEFAULT_TIMEOUT_SECONDS=15
BOOT_READY_TIMEOUT_SECONDS=45
KEY_DELAY_SECONDS=0.12
COMMAND_DELAY_SECONDS=0.25
BOOT_INPUT_DELAY_SECONDS=1.0
TEST_KEYBOARD_LAYOUT="en-US"
PATCH_KEYBOARD_LAYOUT=1
LOCAL_HTTP_SERVER_PID=""
BOOT_READY_PATTERN="[InitializeKernel] Shell task created"

FAULT_PATTERN="#PF|#GP|#UD|#SS|#NP|#TS|#DE|#DF|#MF|#AC|#MC"
TEST_KO_PATTERN="TEST > .* : KO"
ERROR_PATTERN="ERROR >"
NON_FATAL_ERROR_PATTERN="ERROR > \\[NVMeAttach\\] Failed to allocate admin queues"
AUTOTEST_ERROR_SCOPE_BEGIN="AUTOTEST_ERROR_SCOPE_BEGIN"
AUTOTEST_ERROR_SCOPE_END="AUTOTEST_ERROR_SCOPE_END"

RG_BIN="$(command -v rg || true)"
GREP_BIN="$(command -v grep || true)"
RUN_X86_32=1
RUN_X86_64=1
RUN_X86_64_UEFI=1
SKIP_BUILD=0
STOP_AFTER_SHELL_READY=0
CURRENT_IMAGE_PATH=""
CURRENT_FS_OFFSET=0
CURRENT_ARCHIVE_NAME=""
CURRENT_KERNEL_LOG_PATH=""
CURRENT_COM1_LOG_PATH=""
CURRENT_LOGS_ARCHIVED=0

function Usage() {
    echo "Usage: $0 [--only <x86-32|x86-64|x86-64-uefi>] [--commands-file <path>] [--no-build] [--stop-after-shell] [--no-keyboard-layout-patch] [--key-delay <seconds>] [--command-delay <seconds>] [--boot-input-delay <seconds>] [--help]"
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
        --commands-file)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --commands-file"
                Usage
                exit 1
            fi
            COMMANDS_FILE="$1"
            ;;
        --key-delay)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --key-delay"
                Usage
                exit 1
            fi
            KEY_DELAY_SECONDS="$1"
            ;;
        --command-delay)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --command-delay"
                Usage
                exit 1
            fi
            COMMAND_DELAY_SECONDS="$1"
            ;;
        --boot-input-delay)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --boot-input-delay"
                Usage
                exit 1
            fi
            BOOT_INPUT_DELAY_SECONDS="$1"
            ;;
        --no-build)
            SKIP_BUILD=1
            ;;
        --stop-after-shell)
            STOP_AFTER_SHELL_READY=1
            ;;
        --no-keyboard-layout-patch)
            PATCH_KEYBOARD_LAYOUT=0
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
if ! command -v debugfs >/dev/null 2>&1; then
    echo "Missing debugfs. Aborting."
    exit 1
fi
if ! command -v mdir >/dev/null 2>&1; then
    echo "Missing mtools (mdir). Aborting."
    exit 1
fi

function SearchRegex() {
    # Prefer ripgrep when available for speed and clearer output.
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

function Trim() {
    local Value="$1"
    Value="${Value#"${Value%%[![:space:]]*}"}"
    Value="${Value%"${Value##*[![:space:]]}"}"
    echo "$Value"
}

function SetImageKeyboardLayout() {
    # Force a deterministic keyboard layout directly in exos.toml.
    # This avoids flaky input when host/guest layouts differ.
    local ImagePath="$1"
    local FileSystemOffset="$2"
    local Layout="$3"
    local PartitionImage
    local ConfigFile
    local PatchedConfigFile
    local OffsetMegabytes
    local OffsetRemainder

    if [ ! -f "$ImagePath" ]; then
        echo "Image not found for keyboard layout patch: $ImagePath"
        return 1
    fi

    PartitionImage="$(mktemp)"
    ConfigFile="$(mktemp)"
    PatchedConfigFile="$(mktemp)"

    OffsetMegabytes=$((FileSystemOffset / 1048576))
    OffsetRemainder=$((FileSystemOffset % 1048576))
    if ! dd if="$ImagePath" of="$PartitionImage" iflag=skip_bytes skip="$FileSystemOffset" bs=1M status=none 2>/dev/null; then
        if [ "$OffsetRemainder" -eq 0 ]; then
            dd if="$ImagePath" of="$PartitionImage" bs=1M skip="$OffsetMegabytes" status=none
        else
            dd if="$ImagePath" of="$PartitionImage" bs=1 skip="$FileSystemOffset" status=none
        fi
    fi
    if ! debugfs -R "cat /exos.toml" "$PartitionImage" > "$ConfigFile" 2>/dev/null; then
        rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
        echo "Could not read /exos.toml from image: $ImagePath"
        return 1
    fi

    awk -v layout="$Layout" '
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
    ' "$ConfigFile" > "$PatchedConfigFile"

    debugfs -w -R "rm /exos.toml" "$PartitionImage" >/dev/null 2>&1 || true
    if ! debugfs -w -R "write $PatchedConfigFile /exos.toml" "$PartitionImage" >/dev/null 2>&1; then
        rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
        echo "Could not write patched /exos.toml into image: $ImagePath"
        return 1
    fi

    if [ "$OffsetRemainder" -eq 0 ]; then
        dd if="$PartitionImage" of="$ImagePath" bs=1M seek="$OffsetMegabytes" conv=notrunc status=none
    else
        dd if="$PartitionImage" of="$ImagePath" bs=1 seek="$FileSystemOffset" conv=notrunc status=none
    fi

    if ! debugfs -R "cat /exos.toml" "$PartitionImage" 2>/dev/null | SearchRegex '^Layout="' >/dev/null; then
        rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
        echo "Keyboard layout verification failed for image: $ImagePath"
        return 1
    fi

    rm -f "$PartitionImage" "$ConfigFile" "$PatchedConfigFile"
}

function EnsureLocalHttpServer() {
    # netget smoke command expects a local HTTP endpoint.
    local Index=0

    if [ "$SKIP_LOCAL_HTTP_SERVER" = "1" ]; then
        return 0
    fi

    if [ ! -x "$LOCAL_HTTP_SERVER_SCRIPT" ]; then
        echo "Missing local HTTP server script: $LOCAL_HTTP_SERVER_SCRIPT"
        exit 1
    fi

    echo "Starting local HTTP server for netget test..."
    bash "$LOCAL_HTTP_SERVER_SCRIPT" >/dev/null

    while [ "$Index" -lt 30 ]; do
        if exec 4<>"/dev/tcp/127.0.0.1/$LOCAL_HTTP_SERVER_PORT" 2>/dev/null; then
            exec 4<&-
            exec 4>&-
            break
        fi
        Index=$((Index + 1))
        sleep 0.1
    done

    if [ "$Index" -ge 30 ]; then
        echo "Local HTTP server did not start on port $LOCAL_HTTP_SERVER_PORT"
        exit 1
    fi

    LOCAL_HTTP_SERVER_PID="$(pgrep -f "http.server $LOCAL_HTTP_SERVER_PORT" | head -n 1 || true)"
}

function StopLocalHttpServer() {
    if [ "$SKIP_LOCAL_HTTP_SERVER" = "1" ]; then
        return 0
    fi

    if [ -n "$LOCAL_HTTP_SERVER_PID" ] && kill -0 "$LOCAL_HTTP_SERVER_PID" 2>/dev/null; then
        kill "$LOCAL_HTTP_SERVER_PID" || true
    fi
}

function ArchiveCurrentRunLogs() {
    local Status="$1"
    local Timestamp=""
    local ArchiveDir=""
    local SafeName=""
    local KernelArchivePath=""
    local Com1ArchivePath=""

    if [ "$CURRENT_LOGS_ARCHIVED" -eq 1 ]; then
        return 0
    fi
    if [ -z "$CURRENT_ARCHIVE_NAME" ] || [ -z "$CURRENT_KERNEL_LOG_PATH" ]; then
        return 0
    fi

    Timestamp="$(date +%Y%m%d-%H%M%S)"
    ArchiveDir="$ROOT_DIR/log/archive"
    SafeName="$(echo "$CURRENT_ARCHIVE_NAME" | tr ' ' '-')"
    mkdir -p "$ArchiveDir"

    if [ -f "$CURRENT_KERNEL_LOG_PATH" ]; then
        KernelArchivePath="$ArchiveDir/${Timestamp}-${SafeName}-${Status}-kernel.log"
        cp "$CURRENT_KERNEL_LOG_PATH" "$KernelArchivePath"
        echo "Archived kernel log: $KernelArchivePath"
    fi
    if [ -n "$CURRENT_COM1_LOG_PATH" ] && [ -f "$CURRENT_COM1_LOG_PATH" ]; then
        Com1ArchivePath="$ArchiveDir/${Timestamp}-${SafeName}-${Status}-com1.log"
        cp "$CURRENT_COM1_LOG_PATH" "$Com1ArchivePath"
        echo "Archived com1 log: $Com1ArchivePath"
    fi

    CURRENT_LOGS_ARCHIVED=1
}

function OnScriptExit() {
    local ExitCode="$1"
    if [ "$ExitCode" -ne 0 ]; then
        ArchiveCurrentRunLogs "fail"
    fi
    StopLocalHttpServer
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

function TailFromOffsetForErrorCheck() {
    # Ignore ERROR lines emitted while autotests are running.
    local Offset="$1"
    TailFromOffset "$Offset" | awk \
        -v ScopeBegin="$AUTOTEST_ERROR_SCOPE_BEGIN" \
        -v ScopeEnd="$AUTOTEST_ERROR_SCOPE_END" \
        -v ErrorPrefix="$ERROR_PATTERN" '
        {
            if (index($0, ScopeBegin) > 0) {
                InAutotestScope = 1;
                print $0;
                next;
            }

            if (index($0, ScopeEnd) > 0) {
                InAutotestScope = 0;
                print $0;
                next;
            }

            if (InAutotestScope == 1 && index($0, ErrorPrefix) > 0) {
                next;
            }

            print $0;
        }'
}

function MonitorCommand() {
    # Send one command to QEMU monitor (telnet) with retry/backoff.
    # Uses a short-lived socket per command for robustness.
    local Cmd="$1"
    local MaxAttempts="${2:-$MONITOR_CONNECT_MAX_ATTEMPTS}"
    local Quiet="${3:-0}"
    local Attempt=0
    local Delay=0.05

    while [ "$Attempt" -lt "$MaxAttempts" ]; do
        if exec 3<>"/dev/tcp/$MONITOR_HOST/$MONITOR_PORT" 2>/dev/null && printf "%s\r\n" "$Cmd" >&3 2>/dev/null; then
            exec 3<&- || true
            exec 3>&- || true
            return 0
        fi

        exec 3<&- || true
        exec 3>&- || true
        Attempt=$((Attempt + 1))
        if [ "$Attempt" -ge 10 ] && [ "$Attempt" -lt 30 ]; then
            Delay=0.1
        elif [ "$Attempt" -ge 30 ]; then
            Delay=0.2
        fi
        sleep "$Delay"
    done

    if [ "$Quiet" != "1" ]; then
        echo "Failed to connect to QEMU monitor at $MONITOR_HOST:$MONITOR_PORT after $MaxAttempts attempts"
    fi
    return 1
}

function WaitForMonitor() {
    local Index=0
    local Delay=0.05

    while [ "$Index" -lt "$MONITOR_CONNECT_MAX_ATTEMPTS" ]; do
        if exec 3<>"/dev/tcp/$MONITOR_HOST/$MONITOR_PORT"; then
            exec 3<&-
            exec 3>&-
            return 0
        fi
        Index=$((Index + 1))
        if [ "$Index" -ge 10 ] && [ "$Index" -lt 30 ]; then
            Delay=0.1
        elif [ "$Index" -ge 30 ]; then
            Delay=0.2
        fi
        sleep "$Delay"
    done

    return 1
}

function KeyForChar() {
    # Map command characters to QEMU sendkey names.
    local Char="$1"
    case "$Char" in
        [A-Z]) echo "shift-${Char,,}" ;;
        [a-z0-9]) echo "$Char" ;;
        " ") echo "spc" ;;
        "/") echo "slash" ;;
        "-") echo "minus" ;;
        ".") echo "dot" ;;
        ":") echo "shift-semicolon" ;;
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
    # Type a full shell command as key events, then press Enter.
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
    # Poll newly appended log data for success/failure conditions.
    local Expected="$1"
    local Offset="$2"
    local TimeoutSeconds="${3:-$DEFAULT_TIMEOUT_SECONDS}"
    local StartTime="$SECONDS"
    local ErrorLines=""
    local FatalErrorLines=""

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

        if TailFromOffsetForErrorCheck "$Offset" | SearchFixed "$ERROR_PATTERN" >/dev/null; then
            ErrorLines="$(TailFromOffsetForErrorCheck "$Offset" | SearchFixed "$ERROR_PATTERN" || true)"
            FatalErrorLines="$(echo "$ErrorLines" | "$GREP_BIN" -E -v "$NON_FATAL_ERROR_PATTERN" || true)"

            if [ -n "$FatalErrorLines" ]; then
                echo "Kernel fatal error detected in log."
                echo "$FatalErrorLines"
                return 1
            fi
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
    # Validate there is no fault/KO/fatal error in the selected log slice.
    local Offset="$1"
    local ErrorLines=""
    local FatalErrorLines=""

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

    if TailFromOffsetForErrorCheck "$Offset" | SearchFixed "$ERROR_PATTERN" >/dev/null; then
        ErrorLines="$(TailFromOffsetForErrorCheck "$Offset" | SearchFixed "$ERROR_PATTERN" || true)"
        FatalErrorLines="$(echo "$ErrorLines" | "$GREP_BIN" -E -v "$NON_FATAL_ERROR_PATTERN" || true)"

        if [ -n "$FatalErrorLines" ]; then
            echo "Kernel fatal error detected in kernel log."
            echo "$FatalErrorLines"
            return 1
        fi

        if [ -n "$ErrorLines" ]; then
            echo "Kernel non-fatal errors detected in kernel log."
            echo "$ErrorLines"
        fi
    fi
}

function AssertDownloadedFileSize() {
    # Read guest file metadata from the disk image and compare with host source.
    local Offset="$1"
    local SourcePath="$2"
    local DownloadedName="$3"
    local SourceSize
    local DownloadedSize
    local ResolvedSourcePath
    local DownloadedPath
    local PartitionImage
    local FsOffset

    if [[ "$SourcePath" = /* ]]; then
        ResolvedSourcePath="$SourcePath"
    else
        ResolvedSourcePath="$ROOT_DIR/$SourcePath"
    fi

    if [ ! -f "$ResolvedSourcePath" ]; then
        echo "Source file not found for size compare: $ResolvedSourcePath"
        return 1
    fi

    if [ -z "$CURRENT_IMAGE_PATH" ] || [ ! -f "$CURRENT_IMAGE_PATH" ]; then
        echo "Guest disk image not available for size compare: $CURRENT_IMAGE_PATH"
        return 1
    fi
    if [ -z "$DownloadedName" ]; then
        echo "Missing downloaded file name in file-size-compare."
        return 1
    fi

    if [[ "$DownloadedName" = /* ]]; then
        DownloadedPath="$DownloadedName"
    else
        DownloadedPath="/$DownloadedName"
    fi

    SourceSize="$(wc -c < "$ResolvedSourcePath" | tr -d ' ')"

    FsOffset="$CURRENT_FS_OFFSET"
    PartitionImage="$(mktemp)"
    if ! dd if="$CURRENT_IMAGE_PATH" of="$PartitionImage" iflag=skip_bytes skip="$FsOffset" bs=1M status=none 2>/dev/null; then
        dd if="$CURRENT_IMAGE_PATH" of="$PartitionImage" bs=1 skip="$FsOffset" status=none
    fi
    DownloadedSize="$(debugfs -R "stat $DownloadedPath" "$PartitionImage" 2>/dev/null | sed -n 's/.*Size:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -n 1)"
    rm -f "$PartitionImage"

    if [ -z "$DownloadedSize" ]; then
        echo "Could not read downloaded file size from guest image: $DownloadedPath"
        return 1
    fi

    if [ "$SourceSize" != "$DownloadedSize" ]; then
        echo "Downloaded size mismatch for $DownloadedName: expected $SourceSize got $DownloadedSize"
        return 1
    fi
}

function RunCommandSpec() {
    # Execute one command specification line:
    # command + expected log + optional file-size-compare check.
    local CommandText="$1"
    local ExpectedText="$2"
    local CompareSource="$3"
    local CompareDownloaded="$4"
    local TimeoutSeconds="${5:-$DEFAULT_TIMEOUT_SECONDS}"
    local Offset

    if [ -z "$CommandText" ]; then
        echo "Invalid empty command in command specification."
        return 1
    fi

    echo "Running command: $CommandText"
    Offset="$(GetLogSize)"
    SendCommand "$CommandText"
    if [ -n "$ExpectedText" ]; then
        WaitForExpectedLog "$ExpectedText" "$Offset" "$TimeoutSeconds"
    else
        sleep 0.5
    fi
    sleep 0.2
    AssertNoFailures "$Offset"
    if [ -n "$CompareSource" ] || [ -n "$CompareDownloaded" ]; then
        AssertDownloadedFileSize "$Offset" "$CompareSource" "$CompareDownloaded"
    fi
}

function RunCommandList() {
    # Command file grammar (one spec per line):
    # command: "..." | [log: "..."] | [file-size-compare: "host/path" "/guest/path"]
    local Line
    local Part
    local CommandText=""
    local ExpectedText=""
    local CompareSource=""
    local CompareDownloaded=""

    local ResolvedCommandsFile="$COMMANDS_FILE"

    if [ ! -f "$ResolvedCommandsFile" ] && [[ "$ResolvedCommandsFile" != /* ]]; then
        ResolvedCommandsFile="$ROOT_DIR/$COMMANDS_FILE"
    fi

    if [ ! -f "$ResolvedCommandsFile" ]; then
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
        CommandText=""
        ExpectedText=""
        CompareSource=""
        CompareDownloaded=""

        while IFS= read -r Part; do
            Part="$(Trim "$Part")"
            if [[ "$Part" =~ ^command:[[:space:]]*\"([^\"]*)\"$ ]]; then
                CommandText="${BASH_REMATCH[1]}"
            elif [[ "$Part" =~ ^log:[[:space:]]*\"([^\"]*)\"$ ]]; then
                ExpectedText="${BASH_REMATCH[1]}"
            elif [[ "$Part" =~ ^file-size-compare:[[:space:]]*\"([^\"]*)\"[[:space:]]+\"([^\"]*)\"$ ]]; then
                CompareSource="${BASH_REMATCH[1]}"
                CompareDownloaded="${BASH_REMATCH[2]}"
            else
                echo "Invalid command spec segment: $Part"
                exit 1
            fi
        done < <(echo "$Line" | tr '|' '\n')

        if [ -z "$CommandText" ]; then
            echo "Invalid command spec, missing command: $Line"
            exit 1
        fi

        RunCommandSpec "$CommandText" "$ExpectedText" "$CompareSource" "$CompareDownloaded"
    done < "$ResolvedCommandsFile"
}

function StopQemu() {
    MonitorCommand "quit" 1 1 || true
    exec 3<&- || true
    exec 3>&- || true
}

function RunArchitecture() {
    # End-to-end flow for one target (build, run, drive shell, validate, shutdown).
    local Name="$1"
    local BuildScript="$2"
    local QemuScript="$3"
    local KernelLogRelativePath="$4"
    local ImageRelativePath="$5"
    local FileSystemOffset="$6"

    if [ "$SKIP_BUILD" -eq 0 ]; then
        echo "Building $Name..."
        bash -c "cd \"$ROOT_DIR\" && $BuildScript"
    else
        echo "Skipping build for $Name (--no-build)"
    fi

    echo "Starting QEMU for $Name..."
    mkdir -p "$ROOT_DIR/log"
    LOG_FILE="$ROOT_DIR/$KernelLogRelativePath"
    CURRENT_IMAGE_PATH="$ROOT_DIR/$ImageRelativePath"
    CURRENT_FS_OFFSET="$FileSystemOffset"
    CURRENT_ARCHIVE_NAME="$Name"
    CURRENT_KERNEL_LOG_PATH="$ROOT_DIR/$KernelLogRelativePath"
    CURRENT_COM1_LOG_PATH="$ROOT_DIR/${KernelLogRelativePath/log\/kernel-/log\/debug-com1-}"
    CURRENT_LOGS_ARCHIVED=0
    if [ "$PATCH_KEYBOARD_LAYOUT" -eq 1 ]; then
        SetImageKeyboardLayout "$CURRENT_IMAGE_PATH" "$CURRENT_FS_OFFSET" "$TEST_KEYBOARD_LAYOUT"
    fi
    : > "$LOG_FILE"

    local ShutdownWaitStart
    local ShutdownWaitTimeout=20

    bash -c "cd \"$ROOT_DIR\" && $QemuScript" &
    local QemuPid=$!
    trap 'if kill -0 "$QemuPid" 2>/dev/null; then kill "$QemuPid" || true; fi' RETURN

    if ! WaitForMonitor; then
        echo "QEMU monitor did not start."
        kill "$QemuPid" || true
        exit 1
    fi

    sleep 2
    sleep "$BOOT_INPUT_DELAY_SECONDS"
    AssertNoFailures 0
    WaitForExpectedLog "$BOOT_READY_PATTERN" 0 "$BOOT_READY_TIMEOUT_SECONDS"
    if [ "$STOP_AFTER_SHELL_READY" -eq 1 ]; then
        echo "Shell ready detected, stopping early (--stop-after-shell)."
        StopQemu
        ShutdownWaitStart="$SECONDS"
        while kill -0 "$QemuPid" 2>/dev/null; do
            if [ $((SECONDS - ShutdownWaitStart)) -ge "$ShutdownWaitTimeout" ]; then
                echo "Timed out waiting for QEMU shutdown after shell-ready stop."
                kill "$QemuPid" || true
                exit 1
            fi
            sleep 0.2
        done
        wait "$QemuPid" || true
        ArchiveCurrentRunLogs "pass"
        return 0
    fi
    RunCommandList
    AssertNoFailures 0

    ShutdownWaitStart="$SECONDS"
    while kill -0 "$QemuPid" 2>/dev/null; do
        if [ $((SECONDS - ShutdownWaitStart)) -ge "$ShutdownWaitTimeout" ]; then
            echo "Timed out waiting for QEMU shutdown from shell command."
            kill "$QemuPid" || true
            exit 1
        fi
        sleep 0.2
    done

    wait "$QemuPid" || true
    ArchiveCurrentRunLogs "pass"
}

trap 'OnScriptExit $?' EXIT
EnsureLocalHttpServer

if [ "$RUN_X86_32" -eq 1 ]; then
    RunArchitecture "x86-32" "scripts/build.sh --arch x86-32 --fs ext2 --debug --clean --kernel-log-tag-filter ''" "scripts/run.sh --arch x86-32 --fs ext2 --debug" "log/kernel-x86-32-mbr-debug.log" "build/image/x86-32-mbr-debug-ext2/boot-hd/exos.img" "1048576"
fi
if [ "$RUN_X86_64" -eq 1 ]; then
    RunArchitecture "x86-64" "scripts/build.sh --arch x86-64 --fs ext2 --debug --clean --kernel-log-tag-filter ''" "scripts/run.sh --arch x86-64 --fs ext2 --debug" "log/kernel-x86-64-mbr-debug.log" "build/image/x86-64-mbr-debug-ext2/boot-hd/exos.img" "1048576"
fi
if [ "$RUN_X86_64_UEFI" -eq 1 ]; then
    RunArchitecture "x86-64 UEFI" "scripts/build.sh --arch x86-64 --fs ext2 --debug --clean --uefi --kernel-log-tag-filter ''" "scripts/run.sh --arch x86-64 --fs ext2 --debug --uefi" "log/kernel-x86-64-uefi-debug.log" "build/image/x86-64-uefi-debug-ext2/boot-uefi/exos-uefi.img" "4194304"
fi

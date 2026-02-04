#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 --arch <x86-32|x86-64> --fs <ext2|fat32> [--bare-metal] [--clean] [--debug|--release] [--force-pic] [--log-udp-dest <ip:port>] [--log-udp-source <ip:port>] [--profiling] [--scheduling-debug] [--split] [--system-data-view] [--uefi] [--use-log-udp] [--use-syscall]"
}

function ParseIpPort() {
    local Value="$1"
    local Prefix="$2"
    local Address Port
    local Ip0 Ip1 Ip2 Ip3 Extra

    Address="${Value%:*}"
    Port="${Value##*:}"
    if [ "$Address" = "$Value" ] || [ -z "$Address" ] || [ -z "$Port" ]; then
        echo "Invalid value for $Prefix: $Value (expected ip:port)"
        exit 1
    fi

    IFS='.' read -r Ip0 Ip1 Ip2 Ip3 Extra <<< "$Address"
    if [ -n "$Extra" ] || [ -z "$Ip0" ] || [ -z "$Ip1" ] || [ -z "$Ip2" ] || [ -z "$Ip3" ]; then
        echo "Invalid IP for $Prefix: $Address"
        exit 1
    fi

    for Part in "$Ip0" "$Ip1" "$Ip2" "$Ip3"; do
        if ! [[ "$Part" =~ ^[0-9]+$ ]] || [ "$Part" -lt 0 ] || [ "$Part" -gt 255 ]; then
            echo "Invalid IP for $Prefix: $Address"
            exit 1
        fi
    done

    if ! [[ "$Port" =~ ^[0-9]+$ ]] || [ "$Port" -lt 1 ] || [ "$Port" -gt 65535 ]; then
        echo "Invalid port for $Prefix: $Port"
        exit 1
    fi

    eval "${Prefix}_IP_0=$Ip0"
    eval "${Prefix}_IP_1=$Ip1"
    eval "${Prefix}_IP_2=$Ip2"
    eval "${Prefix}_IP_3=$Ip3"
    eval "${Prefix}_PORT=$Port"
}

ARCH="x86-32"
BARE_METAL=0
BUILD_UEFI=0
CLEAN=0
DEBUG_OUTPUT=0
DEBUG_SPLIT=0
FILE_SYSTEM="ext2"
FORCE_PIC=0
UEFI_LOG_UDP_DEST_IP_0=192
UEFI_LOG_UDP_DEST_IP_1=168
UEFI_LOG_UDP_DEST_IP_2=50
UEFI_LOG_UDP_DEST_IP_3=1
UEFI_LOG_UDP_DEST_PORT=18194
UEFI_LOG_UDP_SOURCE_IP_0=192
UEFI_LOG_UDP_SOURCE_IP_1=168
UEFI_LOG_UDP_SOURCE_IP_2=50
UEFI_LOG_UDP_SOURCE_IP_3=2
UEFI_LOG_UDP_SOURCE_PORT=18195
UEFI_LOG_USE_UDP=0
PROFILING=0
SCHEDULING_DEBUG=0
SYSTEM_DATA_VIEW=0
USE_SYSCALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --arch"
                Usage
                exit 1
            fi
            ARCH="$1"
            ;;
        --bare-metal)
            BARE_METAL=1
            ;;
        --clean)
            CLEAN=1
            ;;
        --debug)
            DEBUG_OUTPUT=1
            ;;
        --force-pic)
            FORCE_PIC=1
            ;;
        --fs)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --fs"
                Usage
                exit 1
            fi
            FILE_SYSTEM="$1"
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        --log-udp-dest)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --log-udp-dest"
                Usage
                exit 1
            fi
            ParseIpPort "$1" "UEFI_LOG_UDP_DEST"
            ;;
        --log-udp-source)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --log-udp-source"
                Usage
                exit 1
            fi
            ParseIpPort "$1" "UEFI_LOG_UDP_SOURCE"
            ;;
        --profiling)
            PROFILING=1
            ;;
        --release)
            ;;
        --scheduling-debug)
            SCHEDULING_DEBUG=1
            ;;
        --split)
            DEBUG_SPLIT=1
            ;;
        --system-data-view)
            SYSTEM_DATA_VIEW=1
            ;;
        --uefi)
            BUILD_UEFI=1
            ;;
        --use-log-udp)
            UEFI_LOG_USE_UDP=1
            ;;
        --use-syscall)
            USE_SYSCALL=1
            ;;
        *)
            echo "Unknown option: $1"
            Usage
            exit 1
            ;;
    esac
    shift
done

case "$ARCH" in
    x86-32|x86-64)
        ;;
    *)
        echo "Unknown architecture: $ARCH"
        Usage
        exit 1
        ;;
esac

case "$FILE_SYSTEM" in
    ext2|fat32)
        ;;
    *)
        echo "Unknown file system: $FILE_SYSTEM"
        Usage
        exit 1
        ;;
esac

SCHEDULING_DEBUG_OUTPUT=0
TRACE_STACK_USAGE=0

if [ "$SCHEDULING_DEBUG" -eq 1 ]; then
    PROFILING=1
    DEBUG_OUTPUT=1
    SCHEDULING_DEBUG_OUTPUT=1
    TRACE_STACK_USAGE=1
fi

export BARE_METAL
export DEBUG_OUTPUT
export DEBUG_SPLIT
export FORCE_PIC
export PROFILING
export SCHEDULING_DEBUG_OUTPUT
export SYSTEM_DATA_VIEW
export TRACE_STACK_USAGE
export UEFI_LOG_UDP_DEST_IP_0
export UEFI_LOG_UDP_DEST_IP_1
export UEFI_LOG_UDP_DEST_IP_2
export UEFI_LOG_UDP_DEST_IP_3
export UEFI_LOG_UDP_DEST_PORT
export UEFI_LOG_UDP_SOURCE_IP_0
export UEFI_LOG_UDP_SOURCE_IP_1
export UEFI_LOG_UDP_SOURCE_IP_2
export UEFI_LOG_UDP_SOURCE_IP_3
export UEFI_LOG_UDP_SOURCE_PORT
export UEFI_LOG_USE_UDP
export USE_SYSCALL
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM="$FILE_SYSTEM"

if [ "$CLEAN" -eq 1 ]; then
    make ARCH="$ARCH" clean
fi

make ARCH="$ARCH" -j"$(nproc)"

if [ "$BUILD_UEFI" -eq 1 ]; then
    make ARCH="$ARCH" -C boot-uefi
fi

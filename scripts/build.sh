#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 --arch <x86-32|x86-64> --fs <ext2|fat32> [--bare-metal] [--clean] [--debug|--release] [--force-pic] [--profiling] [--scheduling-debug] [--split] [--system-data-view] [--uefi] [--use-syscall]"
}

ARCH="x86-32"
BARE_METAL=0
BUILD_UEFI=0
CLEAN=0
DEBUG_OUTPUT=0
DEBUG_SPLIT=0
FILE_SYSTEM="ext2"
FORCE_PIC=0
PROFILING=0
SCHEDULING_DEBUG=0
SYSTEM_DATA_VIEW=0
USE_SYSCALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
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
            FILE_SYSTEM="$1"
            ;;
        --help|-h)
            Usage
            exit 0
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

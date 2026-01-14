#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 --arch <i386|x86-64> --fs <ext2|fat32> [--debug|--release] [--scheduling-debug] [--clean] [--force-pic] [--system-data-view] [--uefi]"
}

ARCH="i386"
FILE_SYSTEM="ext2"
DEBUG_OUTPUT=0
CLEAN=0
SCHEDULING_DEBUG=0
FORCE_PIC=0
SYSTEM_DATA_VIEW=0
BUILD_UEFI=0

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
            ARCH="$1"
            ;;
        --fs)
            shift
            FILE_SYSTEM="$1"
            ;;
        --debug)
            DEBUG_OUTPUT=1
            ;;
        --release)
            ;;
        --scheduling-debug)
            SCHEDULING_DEBUG=1
            ;;
        --clean)
            CLEAN=1
            ;;
        --force-pic)
            FORCE_PIC=1
            ;;
        --system-data-view)
            SYSTEM_DATA_VIEW=1
            ;;
        --uefi)
            BUILD_UEFI=1
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

case "$ARCH" in
    i386|x86-64)
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

PROFILING=0
SCHEDULING_DEBUG_OUTPUT=0
TRACE_STACK_USAGE=0

if [ "$SCHEDULING_DEBUG" -eq 1 ]; then
    PROFILING=1
    DEBUG_OUTPUT=1
    SCHEDULING_DEBUG_OUTPUT=1
    TRACE_STACK_USAGE=1
fi

export PROFILING
export DEBUG_OUTPUT
export SCHEDULING_DEBUG_OUTPUT
export TRACE_STACK_USAGE
export FORCE_PIC
export SYSTEM_DATA_VIEW
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM="$FILE_SYSTEM"

if [ "$CLEAN" -eq 1 ]; then
    make ARCH="$ARCH" clean
fi

make ARCH="$ARCH" -j"$(nproc)"

if [ "$BUILD_UEFI" -eq 1 ]; then
    make ARCH="$ARCH" -C boot-uefi
fi

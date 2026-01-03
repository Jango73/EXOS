#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 --arch <i386|x86-64> --fs <ext2|fat32> [--debug|--release] [--clean] [--scheduling-debug]"
}

ARCH="i386"
FILE_SYSTEM="ext2"
BUILD_MODE="release"
CLEAN=0
SCHEDULING_DEBUG=0

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
            BUILD_MODE="debug"
            ;;
        --release)
            BUILD_MODE="release"
            ;;
        --clean)
            CLEAN=1
            ;;
        --scheduling-debug)
            SCHEDULING_DEBUG=1
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
DEBUG_OUTPUT=0
SCHEDULING_DEBUG_OUTPUT=0
TRACE_STACK_USAGE=0

if [ "$BUILD_MODE" = "debug" ]; then
    DEBUG_OUTPUT=1
fi

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
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM="$FILE_SYSTEM"

if [ "$CLEAN" -eq 1 ]; then
    make ARCH="$ARCH" clean
fi

make ARCH="$ARCH" -j"$(nproc)"

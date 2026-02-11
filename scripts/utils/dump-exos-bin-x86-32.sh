#!/bin/sh

BUILD_CORE_NAME="${BUILD_CORE_NAME:-x86-32-mbr-debug}"
BIN_PATH="./build/core/${BUILD_CORE_NAME}/kernel/exos.bin"
ELF_PATH="./build/core/${BUILD_CORE_NAME}/kernel/exos.elf"

if [ ! -f "$BIN_PATH" ] || [ ! -f "$ELF_PATH" ]; then
    echo "Error: x86-32 build artifacts not found. Run ./scripts/build.sh --arch x86-32 --fs ext2 --debug first."
    exit 1
fi

hexdump -C "$BIN_PATH" | head -n 20
hexdump -C "$BIN_PATH" | tail -n 20
xxd -g1 -s +0x0098 -l 32 "$BIN_PATH"
readelf -S "$ELF_PATH"
nm -n "$ELF_PATH" | grep -E 'Start|stub|_start'

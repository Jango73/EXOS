#!/bin/sh

BIN_PATH="./build/i386/kernel/exos.bin"
ELF_PATH="./build/i386/kernel/exos.elf"

if [ ! -f "$BIN_PATH" ] || [ ! -f "$ELF_PATH" ]; then
    echo "Error: i386 build artifacts not found. Run ./scripts/i386/4-5-build-debug.sh first."
    exit 1
fi

hexdump -C "$BIN_PATH" | head -n 20
hexdump -C "$BIN_PATH" | tail -n 20
xxd -g1 -s +0x0098 -l 32 "$BIN_PATH"
readelf -S "$ELF_PATH"
nm -n "$ELF_PATH" | grep -E 'Start|stub|_start'

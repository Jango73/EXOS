#!/bin/bash
set -e
cd boot-qemu-hd

IMG_PATH="bin/exos.img"

if [ ! -f "$IMG_PATH" ]; then
    echo "❌ Image not found: $IMG_PATH"
    exit 1
fi

echo "Starting QEMU with image: $IMG_PATH"
qemu-system-i386 -drive format=raw,file="$IMG_PATH" -serial file:"../log/debug-com1.log" -serial file:"../log/debug-com2.log" -no-reboot -nographic -boot d

#!/bin/bash
set -e

IMG_1_PATH="boot-hd/bin/exos.img"
IMG_2_PATH="boot-hd/bin/src.img"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

if [ ! -f "$IMG_2_PATH" ]; then
    echo "Image not found: $IMG_2_PATH"
    exit 1
fi

echo "Starting QEMU with image: $IMG_PATH"
qemu-system-i386 -drive format=raw,file="$IMG_1_PATH" -drive format=raw,file="$IMG_2_PATH" \
-serial file:"log/debug-com1.log" -serial file:"log/debug-com2.log" \
-no-reboot -nographic -boot d -s -S

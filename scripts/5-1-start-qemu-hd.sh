#!/bin/bash
set -e
cd boot-qemu-hd

IMG_1_PATH="bin/exos.img"
IMG_2_PATH="bin/src.img"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

if [ ! -f "$IMG_2_PATH" ]; then
    echo "Image not found: $IMG_2_PATH"
    exit 1
fi

echo "Starting QEMU with images: $IMG_1_PATH and $IMG_2_PATH"
qemu-system-i386 -drive format=raw,file="$IMG_1_PATH" -drive format=raw,file="$IMG_2_PATH" -monitor stdio -serial file:"../log/debug-com1.log" -serial file:"../log/debug-com2.log" -no-reboot

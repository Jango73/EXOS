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

echo "Starting QEMU with images: $IMG_1_PATH and $IMG_2_PATH"

qemu-system-i386 -drive format=raw,file="$IMG_1_PATH" -drive format=raw,file="$IMG_2_PATH" \
-serial file:"log/debug-com1.log" -serial file:"log/debug-com2.log" \
-no-reboot -monitor stdio \

# -d cpu,int,guest_errors,cpu_reset,pcall 2>&1 | tools/bin/cycle -o log/qemu-trace.log -s 40000 -S "check_exception" -c 2

# -d int,in_asm,exec,nochain,cpu,guest_errors,pcall,cpu_reset 2>&1 | tools/bin/cycle log/qemu-trace.log 8192

#!/bin/bash
set -e
cd boot-qemu-hd

IMG_PATH="bin/exos.img"

if [ ! -f "$IMG_PATH" ]; then
    echo "❌ Image not found: $IMG_PATH"
    exit 1
fi

echo "Starting QEMU with image: $IMG_PATH"
qemu-system-i386 -drive format=raw,file="$IMG_PATH" -boot d -serial file:"../log/debug-com1.log" -serial file:"../log/debug-com2.log" -s -S &
sleep 1
cgdb ../kernel/bin/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234" -ex "break *0x8116"

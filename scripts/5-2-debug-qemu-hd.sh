#!/bin/bash
set -e

IMG_PATH="boot-qemu-hd/bin/exos.img"

if [ ! -f "$IMG_PATH" ]; then
    echo "❌ Image not found: $IMG_PATH"
    exit 1
fi

echo "Starting QEMU with image: $IMG_PATH"

qemu-system-i386 -drive format=raw,file="$IMG_PATH" -boot d -serial file:"log/debug-com1.log" -serial file:"log/debug-com2.log" -s -S &
sleep 2
# ddd --gdb --args gdb -ex 'symbol-file ../kernel/bin/exos.elf' -ex 'target remote localhost:1234'
# ddd --eval-command="symbol-file kernel/bin/exos.elf" --eval-command="dir kernel/source" --eval-command="dir kernel/source/asm" --eval-command="target remote localhost:1234"
# gdbgui -r localhost:1234 --args -ex symbol-file\ ../kernel/bin/exos.elf
# gdb ../kernel/bin/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234"
gdb kernel/bin/exos.elf -ex "set architecture i386" -ex "target remote localhost:1234" -ex "break RestoreFromInterruptFrame" -ex "break BuildInterruptFrame" -ex "break Scheduler" -ex "continue"
# gdb-multiarch ./kernel/bin/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234"

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

echo "Starting QEMU with images : $IMG_1_PATH & $IMG_2_PATH"

qemu-system-i386 -drive format=raw,file="$IMG_1_PATH" -drive format=raw,file="$IMG_2_PATH" \
-boot d -no-reboot -serial file:"log/debug-com1.log" -serial file:"log/debug-com2.log" -s -S &

sleep 2
# ddd --gdb --args gdb -ex 'symbol-file ../kernel/bin/exos.elf' -ex 'target remote localhost:1234'
# ddd --eval-command="symbol-file kernel/bin/exos.elf" --eval-command="dir kernel/source" --eval-command="dir kernel/source/asm" --eval-command="target remote localhost:1234"
# gdbgui -r localhost:1234 --args -ex symbol-file\ ../kernel/bin/exos.elf
# gdb ../kernel/bin/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234"
cgdb kernel/bin/exos.elf -ex "set architecture i386" -ex "target remote localhost:1234" \
-ex "break *0x00400000" -ex "break *0x00400020" -ex "break *0x9FFFF000"  -ex "break *0x9FFFF00C" \
-ex "display/x \$cr3" -ex "display/x \$eax" -ex "display/x \$ebx" -ex "display/x \$ecx" -ex "display/x \$edx"
# gdb-multiarch ./kernel/bin/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234"

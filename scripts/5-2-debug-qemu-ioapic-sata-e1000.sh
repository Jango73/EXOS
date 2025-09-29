#!/bin/bash
set -e

IMG_1_PATH="build/boot-hd/exos.img"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

echo "Starting QEMU with $IMG_1_PATH"

/usr/local/bin/qemu-system-i386 \
-machine q35,acpi=on,kernel-irqchip=split \
-smp cpus=1,cores=1,threads=1 \
-device ahci,id=ahci \
-drive format=raw,file="$IMG_1_PATH",if=none,id=drive0 \
-device ide-hd,drive=drive0,bus=ahci.0 \
-netdev user,id=net0 \
-device e1000,netdev=net0,mac=52:54:00:12:34:56 \
-serial file:"log/debug-com1.log" \
-serial file:"log/kernel.log" \
-vga std -no-reboot \
-s -S &

sleep 2
# ddd --gdb --args gdb -ex 'symbol-file build/kernel/exos.elf' -ex 'target remote localhost:1234'
# ddd --eval-command="symbol-file build/kernel/exos.elf" --eval-command="dir kernel/source" --eval-command="dir kernel/source/asm" --eval-command="target remote localhost:1234"
# gdbgui -r localhost:1234 --args -ex symbol-file\ build/kernel/exos.elf
# gdb build/kernel/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234"
cgdb build/kernel/exos.elf -ex "set architecture i386" -ex "target remote localhost:1234" \
-ex "break EnableInterrupts"

# -ex "break *0x00400000" -ex "break *0x00400020" -ex "break *0x9FFFF000"  -ex "break *0x9FFFF00C" \
# -ex "display/x \$cr3" -ex "display/x \$eax" -ex "display/x \$ebx" -ex "display/x \$ecx" -ex "display/x \$edx"
# gdb-multiarch ./build/kernel/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234"

#!/bin/bash
set -e

IMG_1_PATH="build/x86-64/boot-hd/exos.img"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

echo "Starting QEMU with $IMG_1_PATH"

qemu-system-x86_64 \
-machine q35,acpi=on,kernel-irqchip=split \
-smp cpus=1,cores=1,threads=1 \
-device ahci,id=ahci \
-drive format=raw,file="$IMG_1_PATH",if=none,id=drive0 \
-device ide-hd,drive=drive0,bus=ahci.0 \
-netdev user,id=net0 \
-device e1000,netdev=net0 \
-object filter-dump,id=dump0,netdev=net0,file=log/kernel-net.pcap \
-serial file:"log/debug-com1.log" \
-serial file:"log/kernel.log" \
-vga std -no-reboot -s -S &

sleep 2
cgdb build/x86-64/kernel/exos.elf -ex "set architecture x86-64" -ex "target remote localhost:1234" \
-ex "break KernelMain" -ex "break InitializeKernel"

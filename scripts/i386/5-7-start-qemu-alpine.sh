#!/bin/bash
set -e

# wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86/alpine-virt-3.20.3-x86.iso

IMG_1_PATH="temp/alpine-virt-3.20.3-x86.iso"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

echo "Starting QEMU with $IMG_1_PATH"

/usr/local/bin/qemu-system-i386 \
-machine q35,acpi=on,kernel-irqchip=split \
-smp cpus=1,cores=1,threads=1 \
-device ahci,id=ahci \
-cdrom $IMG_1_PATH \
-netdev user,id=net0 \
-device e1000,netdev=net0 \
-object filter-dump,id=dump0,netdev=net0,file=log/linux-wget.pcap \
-serial file:"log/linux-1.log" \
-serial file:"log/linux-2.log" \
-vga std \
-no-reboot \
-nographic &

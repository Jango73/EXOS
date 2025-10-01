#!/bin/bash
set -e

IMG_1_PATH="build/boot-hd/exos.img"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

echo "Starting QEMU with $IMG_1_PATH"
echo "ACPI support enabled for IOAPIC testing with kernel-irqchip=split"

# Start HTTP server for NETGET testing
# ./scripts/net/start-server.sh

qemu-system-i386 \
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
-no-reboot \
-nographic &

# -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
# -d int 2>&1 | build/tools/cycle -o log/qemu.log -s 20000

sleep 60
killall qemu-system-i386

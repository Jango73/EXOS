#!/bin/bash
set -e

IMG_1_PATH="build/x86-64/boot-hd/exos.img"
USB_3_PATH="build/x86-64/boot-hd/usb-3.img"
CYCLE_BIN="build/x86-64/tools/cycle"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

if [ ! -f "$USB_3_PATH" ]; then
    echo "Image not found: $USB_3_PATH"
    exit 1
fi

if [ ! -x "$CYCLE_BIN" ]; then
    echo "Cycle tool not found or not executable: $CYCLE_BIN"
    exit 1
fi

echo "Starting QEMU with $IMG_1_PATH"
echo "ACPI support enabled for IOAPIC testing with kernel-irqchip=split"

# Start HTTP server for NETGET testing
# ./scripts/net/start-server.sh

qemu-system-x86_64 \
-machine q35,acpi=on,kernel-irqchip=split \
-nodefaults \
-smp cpus=1,cores=1,threads=1 \
-device qemu-xhci,id=xhci \
-device usb-kbd,bus=xhci.0 \
-device usb-mouse,bus=xhci.0 \
-drive format=raw,file="$USB_3_PATH",if=none,id=usbdrive0 \
-device usb-storage,drive=usbdrive0,bus=xhci.0 \
-audiodev pa,id=audio0 \
-device intel-hda,id=hda \
-device hda-duplex,bus=hda.0,audiodev=audio0 \
-device ahci,id=ahci \
-drive format=raw,file="$IMG_1_PATH",if=none,id=drive0 \
-device ide-hd,drive=drive0,bus=ahci.0 \
-netdev user,id=net0 \
-device e1000,netdev=net0 \
-object filter-dump,id=dump0,netdev=net0,file=log/kernel-net.pcap \
-monitor telnet:127.0.0.1:4444,server,nowait \
-serial file:"log/debug-com1.log" \
-serial stdio \
-vga std \
-no-reboot \
2>&1 | "$CYCLE_BIN" -o log/kernel.log -s 200000

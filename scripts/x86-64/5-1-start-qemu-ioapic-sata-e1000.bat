@echo off
setlocal enabledelayedexpansion

set "QEMU=c:\program files\qemu\qemu-system-x86_64
set "IMG_1_PATH=build/x86-64/boot-hd/exos.img"

if not exist "%IMG_1_PATH%" (
    echo Image not found: %IMG_1_PATH%
    exit /b 1
)

"%QEMU%" --version

echo Starting QEMU with images : %IMG_1_PATH%
echo ACPI support enabled for IOAPIC testing with kernel-irqchip=split
"%QEMU%" ^
-machine q35,acpi=on,kernel-irqchip=split ^
-smp cpus=1,cores=1,threads=1 ^
-device ahci,id=ahci ^
-drive format=raw,file="%IMG_1_PATH%",if=none,id=drive0 ^
-device ide-hd,drive=drive0,bus=ahci.0 ^
-netdev user,id=net0 ^
-device e1000,netdev=net0 ^
-object filter-dump,id=dump0,netdev=net0,file=log/kernel-net.pcap ^
-monitor telnet:127.0.0.1:4444,server,nowait ^
-serial file:"log/debug-com1.log" ^
-serial file:"log/kernel.log" ^
-vga std ^
-no-reboot ^
-monitor stdio

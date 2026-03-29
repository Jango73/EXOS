@echo off
setlocal enabledelayedexpansion

set "QEMU=c:\program files\qemu\qemu-system-i386"
set "IMG_1_PATH=temp/alpine-virt-3.20.3-x86.iso"

if not exist "%IMG_1_PATH%" (
    echo Image not found: %IMG_1_PATH%
    exit /b 1
)

echo Starting QEMU with images : %IMG_1_PATH%

"%QEMU%" ^
-machine q35,acpi=on,kernel-irqchip=split ^
-smp cpus=1,cores=1,threads=1 ^
-device ahci,id=ahci ^
-cdrom %IMG_1_PATH% ^
-netdev user,id=net0 ^
-device e1000,netdev=net0 ^
-object filter-dump,id=dump0,netdev=net0,file=log/linux-wget.pcap ^
-monitor telnet:127.0.0.1:4444,server,nowait ^
-serial file:"log/linux-1.log" ^
-serial file:"log/linux-2.log" ^
-vga std ^
-no-reboot ^
-monitor stdio

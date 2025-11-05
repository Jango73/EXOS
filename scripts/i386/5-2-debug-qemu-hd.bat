@echo off
setlocal enabledelayedexpansion

cd /d boot-hd

set "QEMU=c:\program files\qemu\qemu-system-i386"
set "IMG_1_PATH=..\\build\\i386\\boot-hd\\exos.img"

if not exist "%IMG_1_PATH%" (
    echo Image not found: %IMG_1_PATH%
    exit /b 1
)

echo Starting QEMU with %IMG_1_PATH%
"%QEMU%" -drive format=raw,file="%IMG_1_PATH%" -machine q35,acpi=on,kernel-irqchip=split -m 128 -smp cpus=1,cores=1,threads=1 -device ahci,id=ahci -drive format=raw,file="$IMG_1_PATH",if=none,id=drive0 -device ide-hd,drive=drive0,bus=ahci.0 -drive format=raw,file="$IMG_2_PATH",if=none,id=drive1 -device ide-hd,drive=drive1,bus=ahci.1 -serial file:"log/debug-com1.log" -serial file:"log/kernel.log" -vga std -no-reboot -monitor stdio -d int 2>&1 | ..\\build\\i386\\tools\\cycle -o log/qemu.log -s 20000

timeout /t 2 /nobreak >nul

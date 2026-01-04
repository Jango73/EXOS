@echo off
setlocal enabledelayedexpansion

set "ARCH=i386"
set "USE_GDB=0"
set "USB3_ENABLED=1"

:parse
if "%~1"=="" goto done
if "%~1"=="--arch" (
    set "ARCH=%~2"
    shift
    shift
    goto parse
)
if "%~1"=="--gdb" (
    set "USE_GDB=1"
    shift
    goto parse
)
if "%~1"=="--usb3" (
    set "USB3_ENABLED=1"
    shift
    goto parse
)
if "%~1"=="--no-usb3" (
    set "USB3_ENABLED=0"
    shift
    goto parse
)
if "%~1"=="--help" goto usage
if "%~1"=="-h" goto usage

echo Unknown option: %~1
goto usage

:usage
echo Usage: %~nx0 --arch ^<i386^|x86-64^> [--gdb] [--usb3^|--no-usb3]
exit /b 1

:done
if "%ARCH%"=="i386" (
    set "QEMU_BIN_DEFAULT=c:\program files\qemu\qemu-system-i386"
    set "IMG_PATH=build\i386\boot-hd\exos.img"
    set "USB_3_PATH=build\i386\boot-hd\usb-3.img"
    set "DEBUG_ELF=build\i386\kernel\exos.elf"
) else if "%ARCH%"=="x86-64" (
    set "QEMU_BIN_DEFAULT=c:\program files\qemu\qemu-system-x86_64"
    set "IMG_PATH=build\x86-64\boot-hd\exos.img"
    set "USB_3_PATH=build\x86-64\boot-hd\usb-3.img"
    set "DEBUG_ELF=build\x86-64\kernel\exos.elf"
) else (
    echo Unknown architecture: %ARCH%
    goto usage
)

if defined QEMU_BIN (
    set "QEMU_BIN=%QEMU_BIN%"
) else (
    set "QEMU_BIN=%QEMU_BIN_DEFAULT%"
)

if not exist "%IMG_PATH%" (
    echo Image not found: %IMG_PATH%
    exit /b 1
)

if "%USB3_ENABLED%"=="1" (
    if not exist "%USB_3_PATH%" (
        echo Image not found: %USB_3_PATH%
        exit /b 1
    )
)

if not exist log mkdir log

set "USB_ARGS="
if "%USB3_ENABLED%"=="1" (
    set "USB_ARGS=-drive format=raw,file=%USB_3_PATH%,if=none,id=usbdrive0 -device usb-storage,drive=usbdrive0,bus=xhci.0,id=usbmsd0"
)

set "GDB_ARGS="
if "%USE_GDB%"=="1" (
    set "GDB_ARGS=-s -S"
    if not exist "%DEBUG_ELF%" (
        echo Debug symbol file not found: %DEBUG_ELF%
    )
    echo GDB server ready on localhost:1234
)

"%QEMU_BIN%" --version

echo Starting QEMU for %ARCH%
"%QEMU_BIN%" ^
-machine q35,acpi=on,kernel-irqchip=split ^
-nodefaults ^
-smp cpus=1,cores=1,threads=1 ^
-device qemu-xhci,id=xhci ^
-device usb-kbd,bus=xhci.0 ^
-device usb-mouse,bus=xhci.0 ^
%USB_ARGS% ^
-audiodev dsound,id=audio0 ^
-device intel-hda,id=hda ^
-device hda-output,bus=hda.0,audiodev=audio0 ^
-device ahci,id=ahci ^
-drive format=raw,file="%IMG_PATH%",if=none,id=drive0 ^
-device ide-hd,drive=drive0,bus=ahci.0 ^
-netdev user,id=net0 ^
-device e1000,netdev=net0 ^
-object filter-dump,id=dump0,netdev=net0,file=log/kernel-net.pcap ^
-monitor telnet:127.0.0.1:4444,server,nowait ^
-serial file:"log/debug-com1.log" ^
-serial file:"log/kernel.log" ^
-vga std ^
-no-reboot ^
%GDB_ARGS% ^
-monitor stdio

exit /b %errorlevel%

@echo off
setlocal enabledelayedexpansion

cd /d boot-qemu-hd

set "QEMU=c:\program files\qemu\qemu-system-i386"
set "IMG_PATH=bin/exos.img"

if not exist "%IMG_PATH%" (
    echo Image not found: %IMG_PATH%
    exit /b 1
)

echo Starting QEMU with image: %IMG_PATH%
"%QEMU%" -drive format=raw,file="%IMG_PATH%" -monitor telnet:127.0.0.1:4444,server,nowait -serial file:"../log/debug-com1.log" -serial file:"../log/debug-com2.log"

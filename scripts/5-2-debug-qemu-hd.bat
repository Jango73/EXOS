@echo off
setlocal enabledelayedexpansion

cd /d boot-qemu-hd

set "QEMU=c:\program files\qemu\qemu-system-i386"
set "IMG_1_PATH=bin/exos.img"
set "IMG_2_PATH=bin/src.img"

if not exist "%IMG_1_PATH%" (
    echo Image not found: %IMG_1_PATH%
    exit /b 1
)

if not exist "%IMG_2_PATH%" (
    echo Image not found: %IMG_2_PATH%
    exit /b 1
)

echo Starting QEMU with image: %IMG_1_PATH%
"%QEMU%" -drive format=raw,file="%IMG_1_PATH%" -drive format=raw,file="%IMG_2_PATH%" -monitor telnet:127.0.0.1:4444,server,nowait -serial file:"../log/debug-com1.log" -serial file:"../log/debug-com2.log" -s -S
timeout /t 2 /nobreak >nul

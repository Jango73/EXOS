@echo off
setlocal enabledelayedexpansion

set "BOCHS=c:\program files\bochs-3.0\bochs.exe"
set "IMG_1_PATH=build/i386/boot-hd/exos.img"

if not exist "%IMG_1_PATH%" (
    echo Image not found: %IMG_1_PATH%
    exit /b 1
)

echo Starting Bochs with image : %IMG_1_PATH%

"%BOCHS%" -q -f scripts/bochs/bochs.windows.txt -rc scripts/bochs/bochs_debug_commands.txt -unlock

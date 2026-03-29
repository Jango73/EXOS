@echo off
setlocal enabledelayedexpansion

set "ARCH=x86-32"
set "FILE_SYSTEM=ext2"
set "BUILD_CONFIGURATION=release"
set "DEBUG_SPLIT=0"
set "BUILD_CORE_NAME="
set "BUILD_IMAGE_NAME="
set "LOG_CONFIGURATION="
set "CORE_BUILD_DIR="
set "IMAGE_BUILD_DIR="
set "USE_GDB=0"
set "USE_UEFI=0"
set "USB3_ENABLED=1"
set "NVME_ENABLED=1"
set "BOOT_MODE=mbr"
set "UEFI_ARGS="
set "UEFI_VARS_COPY="

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
if "%~1"=="--fs" (
    set "FILE_SYSTEM=%~2"
    shift
    shift
    goto parse
)
if "%~1"=="--debug" (
    set "BUILD_CONFIGURATION=debug"
    shift
    goto parse
)
if "%~1"=="--release" (
    set "BUILD_CONFIGURATION=release"
    shift
    goto parse
)
if "%~1"=="--split" (
    set "DEBUG_SPLIT=1"
    shift
    goto parse
)
if "%~1"=="--build-core-name" (
    set "BUILD_CORE_NAME=%~2"
    shift
    shift
    goto parse
)
if "%~1"=="--build-image-name" (
    set "BUILD_IMAGE_NAME=%~2"
    shift
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
if "%~1"=="--uefi" (
    set "USE_UEFI=1"
    set "BOOT_MODE=uefi"
    shift
    goto parse
)
if "%~1"=="--nvme" (
    set "NVME_ENABLED=1"
    shift
    goto parse
)
if "%~1"=="--help" goto usage
if "%~1"=="-h" goto usage

echo Unknown option: %~1
goto usage

:usage
echo Usage: %~nx0 --arch ^<x86-32^|x86-64^> [--fs ^<ext2^|fat32^>] [--debug^|--release] [--split] [--build-core-name NAME] [--build-image-name NAME] [--gdb] [--usb3^|--no-usb3] [--uefi] [--nvme]
exit /b 1

:done
if not "%FILE_SYSTEM%"=="ext2" if not "%FILE_SYSTEM%"=="fat32" (
    echo Unknown file system: %FILE_SYSTEM%
    goto usage
)

if "%BUILD_CORE_NAME%"=="" if not "%BUILD_IMAGE_NAME%"=="" (
    if /I "%BUILD_IMAGE_NAME:~-5%"=="-ext2" (
        set "BUILD_CORE_NAME=%BUILD_IMAGE_NAME:~0,-5%"
        set "FILE_SYSTEM=ext2"
    ) else if /I "%BUILD_IMAGE_NAME:~-6%"=="-fat32" (
        set "BUILD_CORE_NAME=%BUILD_IMAGE_NAME:~0,-6%"
        set "FILE_SYSTEM=fat32"
    ) else (
        echo Cannot derive build core name from image name: %BUILD_IMAGE_NAME%
        echo Use --build-core-name explicitly.
        exit /b 1
    )
)

set "BUILD_SPLIT_SUFFIX="
if "%DEBUG_SPLIT%"=="1" set "BUILD_SPLIT_SUFFIX=-split"
if "%BUILD_CORE_NAME%"=="" set "BUILD_CORE_NAME=%ARCH%-%BOOT_MODE%-%BUILD_CONFIGURATION%%BUILD_SPLIT_SUFFIX%"
if "%BUILD_IMAGE_NAME%"=="" set "BUILD_IMAGE_NAME=%BUILD_CORE_NAME%-%FILE_SYSTEM%"
set "LOG_CONFIGURATION=%BUILD_CONFIGURATION%"
echo %BUILD_CORE_NAME% | findstr /I /C:"-debug" >nul && set "LOG_CONFIGURATION=debug"
echo %BUILD_CORE_NAME% | findstr /I /C:"-release" >nul && set "LOG_CONFIGURATION=release"
set "CORE_BUILD_DIR=build\core\%BUILD_CORE_NAME%"
set "IMAGE_BUILD_DIR=build\image\%BUILD_IMAGE_NAME%"

if "%ARCH%"=="x86-32" (
    set "QEMU_BIN_DEFAULT=c:\program files\qemu\qemu-system-i386"
    set "DEBUG_ELF=%CORE_BUILD_DIR%\kernel\exos.elf"
    set "OVMF_CODE_DEFAULT=c:\program files\qemu\share\qemu\OVMF32_CODE.fd"
    set "OVMF_VARS_DEFAULT=c:\program files\qemu\share\qemu\OVMF32_VARS.fd"
) else if "%ARCH%"=="x86-64" (
    set "QEMU_BIN_DEFAULT=c:\program files\qemu\qemu-system-x86_64"
    set "DEBUG_ELF=%CORE_BUILD_DIR%\kernel\exos.elf"
    set "OVMF_CODE_DEFAULT=c:\program files\qemu\share\qemu\OVMF_CODE.fd"
    set "OVMF_VARS_DEFAULT=c:\program files\qemu\share\qemu\OVMF_VARS.fd"
) else (
    echo Unknown architecture: %ARCH%
    goto usage
)

set "IMG_PATH=%IMAGE_BUILD_DIR%\boot-hd\exos.img"
set "USB_3_PATH=%IMAGE_BUILD_DIR%\boot-hd\usb-3.img"
set "FS_TEST_EXT2_IMG_PATH=%IMAGE_BUILD_DIR%\boot-hd\fs-test-ext2.img"
set "FS_TEST_FAT32_IMG_PATH=%IMAGE_BUILD_DIR%\boot-hd\fs-test-fat32.img"
set "FS_TEST_NTFS_IMG_PATH=%IMAGE_BUILD_DIR%\boot-hd\fs-test-ntfs.img"
set "NTFS_LIVE_IMG_PATH=build\test-images\ntfs-live.img"

if defined QEMU_BIN (
    set "QEMU_BIN=%QEMU_BIN%"
) else (
    set "QEMU_BIN=%QEMU_BIN_DEFAULT%"
)

if "%USE_UEFI%"=="1" (
    set "IMG_PATH=%IMAGE_BUILD_DIR%\boot-uefi\exos-uefi.img"
)

if not exist "%IMG_PATH%" (
    echo Image not found: %IMG_PATH%
    exit /b 1
)

if "%USE_UEFI%"=="0" if "%USB3_ENABLED%"=="1" (
    if not exist "%USB_3_PATH%" (
        echo Image not found: %USB_3_PATH%
        exit /b 1
    )
)

if not exist log mkdir log
set "LOG_DEBUG_COM1=log/debug-com1-%ARCH%-%BOOT_MODE%-%LOG_CONFIGURATION%.log"
set "LOG_KERNEL=log/kernel-%ARCH%-%BOOT_MODE%-%LOG_CONFIGURATION%.log"
set "LOG_NET=log/kernel-net-%ARCH%-%BOOT_MODE%-%LOG_CONFIGURATION%.pcap"

set "USB_ARGS="
if "%USE_UEFI%"=="0" if "%USB3_ENABLED%"=="1" (
    set "USB_ARGS=-drive format=raw,file=%USB_3_PATH%,if=none,id=usbdrive0 -device usb-storage,drive=usbdrive0,bus=xhci.0,id=usbmsd0"
)

set "NVME_ARGS="
if "%NVME_ENABLED%"=="1" (
    set "NTFS_NVME_IMG_PATH=%FS_TEST_NTFS_IMG_PATH%"
    if exist "%NTFS_LIVE_IMG_PATH%" (
        set "NTFS_NVME_IMG_PATH=%NTFS_LIVE_IMG_PATH%"
        echo Using NTFS live image: %NTFS_NVME_IMG_PATH%
    )

    if not exist "%FS_TEST_EXT2_IMG_PATH%" (
        echo Image not found: %FS_TEST_EXT2_IMG_PATH%
        echo Build it with: scripts\build.bat --arch %ARCH% --fs ext2 --debug
        exit /b 1
    )
    if not exist "%FS_TEST_FAT32_IMG_PATH%" (
        echo Image not found: %FS_TEST_FAT32_IMG_PATH%
        echo Build it with: scripts\build.bat --arch %ARCH% --fs ext2 --debug
        exit /b 1
    )
    if not exist "%NTFS_NVME_IMG_PATH%" (
        echo Image not found: %NTFS_NVME_IMG_PATH%
        echo Build it with: scripts\build.bat --arch %ARCH% --fs ext2 --debug
        exit /b 1
    )
    set "NVME_ARGS=-drive format=raw,file=%FS_TEST_EXT2_IMG_PATH%,if=none,id=fsxt0 -device nvme,drive=fsxt0,serial=exosfs0 -drive format=raw,file=%FS_TEST_FAT32_IMG_PATH%,if=none,id=fsxt1 -device nvme,drive=fsxt1,serial=exosfs1 -drive format=raw,file=%NTFS_NVME_IMG_PATH%,if=none,id=fsxt2 -device nvme,drive=fsxt2,serial=exosfs2"
)

if "%USE_UEFI%"=="1" (
    if defined OVMF_CODE (
        set "OVMF_CODE=%OVMF_CODE%"
    ) else (
        set "OVMF_CODE=%OVMF_CODE_DEFAULT%"
    )

    if defined OVMF_VARS (
        set "OVMF_VARS=%OVMF_VARS%"
    ) else (
        set "OVMF_VARS=%OVMF_VARS_DEFAULT%"
    )

    if not exist "%OVMF_CODE%" (
        echo OVMF code firmware not found: %OVMF_CODE%
        exit /b 1
    )

    if not exist "%OVMF_VARS%" (
        echo OVMF variables firmware not found: %OVMF_VARS%
        exit /b 1
    )

    if not exist "%IMAGE_BUILD_DIR%\boot-uefi" mkdir "%IMAGE_BUILD_DIR%\boot-uefi"
    set "UEFI_VARS_COPY=%IMAGE_BUILD_DIR%\boot-uefi\ovmf-vars.fd"
    copy /y "%OVMF_VARS%" "%UEFI_VARS_COPY%" >nul
    set "UEFI_ARGS=-drive if=pflash,format=raw,readonly=on,file=%OVMF_CODE% -drive if=pflash,format=raw,file=%UEFI_VARS_COPY%"
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
%NVME_ARGS% ^
%UEFI_ARGS% ^
-audiodev dsound,id=audio0 ^
-device intel-hda,id=hda ^
-device hda-output,bus=hda.0,audiodev=audio0 ^
-device ahci,id=ahci ^
-drive format=raw,file="%IMG_PATH%",if=none,id=drive0 ^
-device ide-hd,drive=drive0,bus=ahci.0 ^
-netdev user,id=net0 ^
-device e1000,netdev=net0 ^
-object filter-dump,id=dump0,netdev=net0,file=%LOG_NET% ^
-monitor telnet:127.0.0.1:4444,server,nowait ^
-serial file:"%LOG_DEBUG_COM1%" ^
-serial file:"%LOG_KERNEL%" ^
-vga std ^
-no-reboot ^
%GDB_ARGS% ^
-monitor stdio

exit /b %errorlevel%

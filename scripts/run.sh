#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 --arch <i386|x86-64> [--gdb] [--usb3|--no-usb3] [--uefi]"
}

ARCH="i386"
USE_GDB=0
USB3_ENABLED=1
USE_UEFI=0

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
            ARCH="$1"
            ;;
        --gdb)
            USE_GDB=1
            ;;
        --usb3)
            USB3_ENABLED=1
            ;;
        --no-usb3)
            USB3_ENABLED=0
            ;;
        --uefi)
            USE_UEFI=1
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            Usage
            exit 1
            ;;
    esac
    shift
done

case "$ARCH" in
    i386)
        QEMU_BIN_DEFAULT="qemu-system-i386"
        IMG_PATH="build/i386/boot-hd/exos.img"
        USB_3_PATH="build/i386/boot-hd/usb-3.img"
        CYCLE_BIN="build/i386/tools/cycle"
        DEBUG_ELF="build/i386/kernel/exos.elf"
        OVMF_CODE_CANDIDATES=(
            "/usr/share/OVMF/OVMF32_CODE.fd"
            "/usr/share/edk2/ovmf/OVMF32_CODE.fd"
            "/usr/share/qemu/OVMF32_CODE.fd"
        )
        OVMF_VARS_CANDIDATES=(
            "/usr/share/OVMF/OVMF32_VARS.fd"
            "/usr/share/edk2/ovmf/OVMF32_VARS.fd"
            "/usr/share/qemu/OVMF32_VARS.fd"
        )
        ;;
    x86-64)
        QEMU_BIN_DEFAULT="qemu-system-x86_64"
        IMG_PATH="build/x86-64/boot-hd/exos.img"
        USB_3_PATH="build/x86-64/boot-hd/usb-3.img"
        CYCLE_BIN="build/x86-64/tools/cycle"
        DEBUG_ELF="build/x86-64/kernel/exos.elf"
        DEBUG_GDB="scripts/x86-64/debug.gdb"
        OVMF_CODE_CANDIDATES=(
            "/usr/share/OVMF/OVMF_CODE.fd"
            "/usr/share/edk2/ovmf/OVMF_CODE.fd"
            "/usr/share/qemu/OVMF_CODE.fd"
        )
        OVMF_VARS_CANDIDATES=(
            "/usr/share/OVMF/OVMF_VARS.fd"
            "/usr/share/edk2/ovmf/OVMF_VARS.fd"
            "/usr/share/qemu/OVMF_VARS.fd"
        )
        ;;
    *)
        echo "Unknown architecture: $ARCH"
        Usage
        exit 1
        ;;
esac

QEMU_BIN="${QEMU_BIN:-$QEMU_BIN_DEFAULT}"

if [ "$USE_UEFI" -eq 1 ]; then
    IMG_PATH="build/$ARCH/boot-uefi/exos-uefi.img"
fi

if [ ! -f "$IMG_PATH" ]; then
    echo "Image not found: $IMG_PATH"
    exit 1
fi

if [ "$USB3_ENABLED" -eq 1 ] && [ ! -f "$USB_3_PATH" ]; then
    echo "Image not found: $USB_3_PATH"
    exit 1
fi

mkdir -p log

USB_ARGUMENTS=()

function BuildUsbArguments() {
    USB_ARGUMENTS=()
    if [ "$USB3_ENABLED" -eq 1 ]; then
        USB_ARGUMENTS=(-drive format=raw,file="$USB_3_PATH",if=none,id=usbdrive0 -device usb-storage,drive=usbdrive0,bus=xhci.0,id=usbmsd0)
    fi
}

function FindFirmwareFile() {
    local env_value="$1"
    shift
    if [ -n "$env_value" ] && [ -f "$env_value" ]; then
        echo "$env_value"
        return 0
    fi
    for candidate in "$@"; do
        if [ -f "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

function BuildUefiArguments() {
    if [ "$USE_UEFI" -eq 0 ]; then
        return 0
    fi

    OVMF_CODE_PATH="$(FindFirmwareFile "${OVMF_CODE:-}" "${OVMF_CODE_CANDIDATES[@]}")" || {
        echo "OVMF code firmware not found. Set OVMF_CODE to a valid file."
        exit 1
    }

    OVMF_VARS_PATH="$(FindFirmwareFile "${OVMF_VARS:-}" "${OVMF_VARS_CANDIDATES[@]}")" || {
        echo "OVMF variables firmware not found. Set OVMF_VARS to a valid file."
        exit 1
    }

    OVMF_VARS_COPY="build/$ARCH/boot-uefi/ovmf-vars.fd"
    mkdir -p "build/$ARCH/boot-uefi"
    cp -f "$OVMF_VARS_PATH" "$OVMF_VARS_COPY"

    UEFI_ARGUMENTS=(
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE_PATH"
        -drive if=pflash,format=raw,file="$OVMF_VARS_COPY"
    )
}

function RunStandardQemu() {
    BuildUsbArguments
    BuildUefiArguments

    if [ ! -x "$CYCLE_BIN" ]; then
        echo "Cycle tool not found or not executable: $CYCLE_BIN"
        exit 1
    fi

    "$QEMU_BIN" \
    -machine q35,acpi=on,kernel-irqchip=split \
    -nodefaults \
    -smp cpus=1,cores=1,threads=1 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    "${USB_ARGUMENTS[@]}" \
    "${UEFI_ARGUMENTS[@]}" \
    -audiodev pa,id=audio0 \
    -device intel-hda,id=hda \
    -device hda-duplex,bus=hda.0,audiodev=audio0 \
    -device ahci,id=ahci \
    -drive format=raw,file="$IMG_PATH",if=none,id=drive0 \
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
}

function RunGdbQemu() {
    BuildUsbArguments
    BuildUefiArguments

    if [ ! -f "$DEBUG_ELF" ]; then
        echo "Debug symbol file not found: $DEBUG_ELF"
        exit 1
    fi

    if [ "$ARCH" = "x86-64" ] && [ ! -f "$DEBUG_GDB" ]; then
        echo "Debug configuration file not found: $DEBUG_GDB"
        exit 1
    fi

    "$QEMU_BIN" \
    -machine q35,acpi=on,kernel-irqchip=split \
    -smp cpus=1,cores=1,threads=1 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    "${USB_ARGUMENTS[@]}" \
    "${UEFI_ARGUMENTS[@]}" \
    -device ahci,id=ahci \
    -drive format=raw,file="$IMG_PATH",if=none,id=drive0 \
    -device ide-hd,drive=drive0,bus=ahci.0 \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -serial file:"log/debug-com1.log" \
    -serial file:"log/kernel.log" \
    -vga std \
    -no-reboot \
    -s -S &

    sleep 2

    if [ "$ARCH" = "x86-64" ]; then
        cgdb "$DEBUG_ELF" -ex "set architecture x86-64" -ex "target remote localhost:1234" \
        -ex "break SwitchToNextTask" -ex "source $DEBUG_GDB"
    else
        cgdb "$DEBUG_ELF" -ex "set architecture i386" -ex "target remote localhost:1234" \
        -ex "break EnableInterrupts"
    fi
}

if [ "$USE_GDB" -eq 1 ]; then
    echo "Starting QEMU with GDB for $ARCH"
    RunGdbQemu
else
    echo "Starting QEMU for $ARCH"
    RunStandardQemu
fi

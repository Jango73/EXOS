#!/bin/bash
set -euo pipefail

ARCH=x86-32

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UTILS_DIR="$SCRIPT_DIR/../utils"

source "$UTILS_DIR/create-usb-common.sh"

if parse_usb_flash_args "$ARCH" "mbr" "$@"; then
    :
else
    Status=$?
    echo "Usage: $0 [--debug|--release] [--fs <ext2|fat32>] [--split] [--build-image-name <name>] /dev/sdX"
    exit $([ "$Status" -eq 2 ] && echo 0 || echo 1)
fi

DEVICE_PATH="$USB_DEVICE_PATH"
IMAGE_PATH="$USB_IMAGE_PATH"

check_device "$DEVICE_PATH"

[[ -f "$IMAGE_PATH" ]] || {
    echo "ERROR: Image not found:"
    echo "   $IMAGE_PATH"
    echo "Build the image first."
    exit 1
}

confirm_flash "$ARCH" "$IMAGE_PATH" "$DEVICE_PATH"
flash_image "$IMAGE_PATH" "$DEVICE_PATH"
if eject_device "$DEVICE_PATH"; then
    show_success "$DEVICE_PATH" "OK"
else
    show_success "$DEVICE_PATH" "FAILED"
fi

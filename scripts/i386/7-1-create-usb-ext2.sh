#!/bin/bash
set -euo pipefail

ARCH=i386

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UTILS_DIR="$SCRIPT_DIR/../utils"

# Source common functions
source "$UTILS_DIR/create-usb-common.sh"

[[ $# -eq 1 ]] || { echo "Usage: $0 /dev/sdX"; exit 1; }

DEVICE_PATH="$1"

check_device "$DEVICE_PATH"

IMAGE_PATH="build/$ARCH/boot-hd/exos.img"

[[ -f "$IMAGE_PATH" ]] || {
    echo "ERROR: Image not found:"
    echo "   $IMAGE_PATH"
    echo "Build the image first."
    exit 1
}

confirm_flash "$ARCH" "$IMAGE_PATH" "$DEVICE_PATH"
flash_image "$IMAGE_PATH" "$DEVICE_PATH"
show_success "$DEVICE_PATH"

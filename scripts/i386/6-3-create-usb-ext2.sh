#!/bin/bash
set -euo pipefail

ARCH=i386

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: This script must be run as root."
    exit 1
fi

if [ $# -lt 1 ]; then
    echo "Usage: $0 /dev/<usb_device>"
    exit 1
fi

DEVICE_PATH="$1"

if [ ! -b "${DEVICE_PATH}" ]; then
    echo "ERROR: ${DEVICE_PATH} is not a block device."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
IMAGE_PATH="${REPO_ROOT}/build/${ARCH}/boot-hd/exos.img"

if [ ! -f "${IMAGE_PATH}" ]; then
    echo "ERROR: ${IMAGE_PATH} does not exist. Build the image first."
    exit 1
fi

if command -v lsblk >/dev/null 2>&1; then
    PARENT_DEVICE="$(lsblk -no PKNAME "${DEVICE_PATH}" 2>/dev/null || true)"
    if [ -n "${PARENT_DEVICE}" ]; then
        echo "ERROR: ${DEVICE_PATH} appears to be a partition. Please provide the whole device (for example, /dev/sdb)."
        exit 1
    fi
fi

cat <<EOF
Ready to write bootable EXOS image to USB.
Architecture: ${ARCH}
Image source: ${IMAGE_PATH}
Target device: ${DEVICE_PATH}

The operation will:
 - Overwrite the target device with the disk image (MBR + EXT2 filesystem)
 - Format the device as defined in the image
 - Copy the EXOS system files contained in the image
EOF

read -r -p "Type YES to continue: " CONFIRMATION
if [ "${CONFIRMATION}" != "YES" ]; then
    echo "Aborted."
    exit 0
fi

echo "Writing ${IMAGE_PATH} to ${DEVICE_PATH}..."
dd if="${IMAGE_PATH}" of="${DEVICE_PATH}" bs=4M status=progress conv=fsync
sync

if command -v partprobe >/dev/null 2>&1; then
    partprobe "${DEVICE_PATH}" || true
fi

echo "USB device ${DEVICE_PATH} is ready."
echo "You may need to replug the device for the kernel to refresh the partition table."


#!/bin/bash
set -euo pipefail

# Common USB creation functions

check_device() {
    local DEVICE_PATH="$1"
    
    (( $(id -u) == 0 )) || { echo "ERROR: Run as root (sudo)."; exit 1; }

    [[ -b "$DEVICE_PATH" ]] || {
        echo "ERROR: $DEVICE_PATH is not a block device."
        echo "USB drives detected:"
        lsblk -dpo NAME,SIZE,MODEL,TRAN | grep -E 'sd|nvme'
        exit 1
    }

    if udevadm info -q property -n "$DEVICE_PATH" | grep -q '^ID_BUS=usb$'; then
        :  # c’est une clé USB → OK
    else
        echo "ERROR: $DEVICE_PATH is an internal drive. Refused."
        exit 1
    fi

    if lsblk -no TYPE "$DEVICE_PATH" 2>/dev/null | grep -q partition; then
        echo "ERROR: $DEVICE_PATH is a partition."
        echo "Give the whole device, e.g. /dev/sdb (not /dev/sdb1)."
        exit 1
    fi
}

confirm_flash() {
    local ARCH="$1"
    local IMAGE_PATH="$2"
    local DEVICE_PATH="$3"
    
    cat <<EOF

Ready to flash the kernel
Architecture : $ARCH
Image        : $IMAGE_PATH
Target       : $DEVICE_PATH  ($(lsblk -no SIZE "$DEVICE_PATH" | head -1))

/!\ THIS WILL ERASE EVERYTHING ON $DEVICE_PATH /!\\

EOF

    read -r -p "Type YES to continue: " REPLY
    [[ "$REPLY" == "YES" ]] || { echo "Aborted."; exit 0; }
}

flash_image() {
    local IMAGE_PATH="$1"
    local DEVICE_PATH="$2"
    
    echo "Writing image (this takes 10–30 seconds)..."
    dd if="$IMAGE_PATH" of="$DEVICE_PATH" bs=4M conv=fsync status=progress oflag=direct
    sync

    partprobe "$DEVICE_PATH" 2>/dev/null || true
    sleep 1
}

show_success() {
    local DEVICE_PATH="$1"
    
    cat <<EOF

SUCCESS! $DEVICE_PATH is now bootable.

1. Safely remove the USB key
2. Plug it into the target machine
3. Boot → select USB in BIOS/UEFI

EOF
}

#!/bin/bash
set -e

IMG_1_PATH="boot-hd/bin/exos.img"
IMG_2_PATH="boot-hd/bin/src.img"

if [ ! -f "$IMG_1_PATH" ]; then
    echo "Image not found: $IMG_1_PATH"
    exit 1
fi

if [ ! -f "$IMG_2_PATH" ]; then
    echo "Image not found: $IMG_2_PATH"
    exit 1
fi

echo "Starting DOSBOX with images: $IMG_1_PATH and $IMG_2_PATH"

dosbox -c "BOOT $IMG_1_PATH"

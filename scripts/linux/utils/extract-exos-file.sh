#!/bin/sh
set -eu

if [ "$#" -lt 3 ]; then
    echo "Usage: $0 <exos.img> <file-inside-image> <output-path>"
    exit 1
fi

IMG="$1"
IN_PATH="$2"
OUT_PATH="$3"

if [ ! -f "$IMG" ]; then
    echo "Image not found: $IMG"
    exit 1
fi

TMPDIR="$(mktemp -d)"
MNT="$TMPDIR/mnt"
LOOPDEV=""
PARTOFF=""

cleanup() {
    if [ -n "$MNT" ] && mountpoint -q "$MNT"; then
        umount "$MNT"
    fi
    if [ -n "$LOOPDEV" ]; then
        losetup -d "$LOOPDEV" >/dev/null 2>&1 || true
    fi
    rm -rf "$TMPDIR"
}
trap cleanup EXIT INT TERM

mkdir -p "$MNT"

PARTOFF="$(parted -s "$IMG" unit B print | awk '/^ 1/ { gsub("B","",$2); print $2; exit }')"
if [ -z "$PARTOFF" ]; then
    echo "Cannot find partition offset"
    exit 1
fi

LOOPDEV="$(losetup -f --show -o "$PARTOFF" "$IMG")"
if [ -z "$LOOPDEV" ]; then
    echo "losetup failed"
    exit 1
fi

FSTYPE="$(blkid -o value -s TYPE "$LOOPDEV" 2>/dev/null || true)"
if [ -z "$FSTYPE" ]; then
    echo "Cannot detect filesystem type"
    exit 1
fi

mount -o ro -t "$FSTYPE" "$LOOPDEV" "$MNT"

if [ ! -f "$MNT$IN_PATH" ]; then
    echo "File not found in image: $IN_PATH"
    exit 1
fi

cp -f "$MNT$IN_PATH" "$OUT_PATH"
echo "Extracted to: $OUT_PATH"

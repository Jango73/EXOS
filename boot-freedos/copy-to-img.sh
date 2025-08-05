#!/bin/sh
# Usage: ./copy-to-img.sh <img> <src> <dst>
# Example: ./copy-to-img.sh bin/exos_dos.img freedos/uhex.com UHEX.COM

if [ $# -ne 3 ]; then
    echo "Usage: $0 <image> <file_to_copy> <dest_in_img>"
    exit 1
fi

IMG="$1"
SRC="$2"
DST="$3"

# Find the offset of the first partition (in bytes)
PART_OFFSET=$(parted -s "$IMG" unit B print | awk '/^ 1/ { gsub("B","",$2); print $2; exit }')
if [ -z "$PART_OFFSET" ]; then
    echo "Unable to find partition offset in $IMG"
    exit 2
fi

# Generate a temporary mtools config file
MTOOLS_CONF="./mtools_$$.conf"
echo "drive z: file=\"$IMG\" offset=$PART_OFFSET" > "$MTOOLS_CONF"

# Copy the file into the image using mcopy (syntax: z:DEST)
MTOOLSRC="$MTOOLS_CONF" mcopy -o "$SRC" "z:$DST"

rm -f "$MTOOLS_CONF"

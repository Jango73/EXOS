#!/bin/sh

cd ../boot-freedos

# Makefile variables (not all used, but kept for context)
LOADER_SRC="source/loader.asm"
LOADER_COM="bin/loader.com"
KERNEL_BIN="../kernel/bin/exos.bin"
BASE_IMG="freedos/FD14FULL.img"
FINAL_IMG="bin/exos_dos.img"
AUTOEXEC="bin/AUTOEXEC.BAT"
FD_AUTO_TMP="./FDAUTO_TMP.BAT"
MTOOLS_CONF=".mtoolsrc.tmp"

# --- Partition offset auto-detection ---
PART_OFFSET=$(parted -s "$FINAL_IMG" unit B print | awk '/^ 1/ { gsub("B","",$2); print $2; exit }')
if [ -z "$PART_OFFSET" ]; then
    echo "ERROR: Could not find a valid partition offset in $FINAL_IMG."
    exit 1
fi

echo ">>> Using partition offset: $PART_OFFSET"

# --- Generate mtools temporary config pointing to the correct partition offset ---
echo ">>> Generating temporary mtools config file for FDAUTO.BAT update..."
echo "drive z: file=\"$FINAL_IMG\" offset=$PART_OFFSET" > "$MTOOLS_CONF"

# --- Extract FDAUTO.BAT from the image ---
echo ">>> Extracting FDAUTO.BAT from FINAL_IMG..."
if MTOOLSRC="$MTOOLS_CONF" mcopy -o z:FDAUTO.BAT "$FD_AUTO_TMP"; then
	echo ">>> FDAUTO.BAT extracted successfully."
else
	echo ">>> FDAUTO.BAT not found, creating empty file."
	echo "" > "$FD_AUTO_TMP"
fi

# --- Remove all lines containing LOADER.COM (with or without 'call', case-insensitive) ---
echo ">>> Removing all references to LOADER.COM from FDAUTO.BAT..."
grep -vi '^[[:space:]]*\(call[[:space:]]\+\)\?LOADER\.COM' "$FD_AUTO_TMP" > "${FD_AUTO_TMP}.filtered"
mv "${FD_AUTO_TMP}.filtered" "$FD_AUTO_TMP"

# --- Write the cleaned FDAUTO.BAT back into the image ---
echo ">>> Writing updated FDAUTO.BAT to FINAL_IMG..."
MTOOLSRC="$MTOOLS_CONF" mcopy -o "$FD_AUTO_TMP" z:FDAUTO.BAT

# --- Clean up temporary files ---
echo ">>> Cleaning up temporary files..."
rm -f "$FD_AUTO_TMP" "$MTOOLS_CONF"

echo ">>> Done."

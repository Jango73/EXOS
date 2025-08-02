#!/bin/bash
set -e

cd boot-freedos

EXTRA_IMG="bin/exos_extra.img"
if [ ! -f "$EXTRA_IMG" ]; then
	echo "[Creating EXOS extra image (10M, FAT32)]"
	qemu-img create -f raw "$EXTRA_IMG" 10M
	mkfs.fat -F32 "$EXTRA_IMG"
else
	echo "→ exos_extra.img déjà présent."
fi

DOS_IMG="bin/exos_dos.img"
if [ -f "$DOS_IMG" ]; then
	echo "→ exos_dos.img déjà présent, rien à faire pour FreeDOS."
	exit 0
fi

echo "[Downloading FD13-LiveCD.zip]"
wget -nc https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/previews/1.3-rc4/FD13-LiveCD.zip -O freedos/FD13-LiveCD.zip

echo "[Unzipping FD13-LiveCD.zip]"
unzip -o freedos/FD13-LiveCD.zip -d freedos/

echo "[Creating DOS image (25M)]"
qemu-img create -f raw "$DOS_IMG" 25M

echo "[Launching DOS install]"
qemu-system-i386 \
	-drive format=raw,file="$DOS_IMG" \
	-cdrom freedos/FD13LIVE.iso \
	-boot d

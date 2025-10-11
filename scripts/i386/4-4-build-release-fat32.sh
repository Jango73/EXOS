#!/bin/bash
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=fat32
make ARCH=i386 -j$(nproc)

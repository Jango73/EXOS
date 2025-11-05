#!/bin/bash
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=ext2
make ARCH=i386 -j$(nproc)

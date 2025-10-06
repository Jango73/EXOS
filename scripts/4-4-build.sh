#!/bin/bash
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=ext2
make -j$(nproc)

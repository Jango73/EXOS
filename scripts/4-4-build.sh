#!/bin/bash
export KERNEL_FILE="exos.bin"
make -j$(nproc)

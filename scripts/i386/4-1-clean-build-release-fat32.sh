#!/bin/bash
export DEBUG_OUTPUT=0
export SCHEDULING_DEBUG_OUTPUT=0
export TRACE_STACK_USAGE=0
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=fat32
make ARCH=i386 clean
make ARCH=i386 -j$(nproc)

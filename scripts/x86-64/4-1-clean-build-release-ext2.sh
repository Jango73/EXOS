#!/bin/bash
export ARCH=x86-64
export DEBUG_OUTPUT=0
export SCHEDULING_DEBUG_OUTPUT=0
export TRACE_STACK_USAGE=0
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=ext2
make ARCH=x86-64 clean
make ARCH=x86-64 -j$(nproc)

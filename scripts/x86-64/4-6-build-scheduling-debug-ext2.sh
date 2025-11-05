#!/bin/bash
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=1
export TRACE_STACK_USAGE=1
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=ext2
make ARCH=x86-64 -j$(nproc)

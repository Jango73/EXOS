#!/bin/bash
export PROFILING=0
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=0
export TRACE_STACK_USAGE=0
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=ext2
make ARCH=i386 clean
make ARCH=i386 -j$(nproc)

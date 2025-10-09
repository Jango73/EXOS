#!/bin/bash
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=0
export TRACE_STACK_USAGE=0
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=fat32
make clean
make -j$(nproc)

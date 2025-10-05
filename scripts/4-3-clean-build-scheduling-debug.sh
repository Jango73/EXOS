#!/bin/bash
export KERNEL_FILE="exos.bin"
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=1
export TRACE_STACK_USAGE=1
make clean
make -j$(nproc)

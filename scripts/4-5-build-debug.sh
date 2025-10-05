#!/bin/bash
export KERNEL_FILE="exos.bin"
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=0
export TRACE_STACK_USAGE=0
make -j$(nproc)

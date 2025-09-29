#!/bin/bash
export KERNEL_FILE="EXOS    BIN"
export DEBUG_OUTPUT=0
export SCHEDULING_DEBUG_OUTPUT=0
export TRACE_STACK_USAGE=0
make clean
make -j$(nproc)

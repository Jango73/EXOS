#!/bin/bash
export KERNEL_FILE="EXOS    BIN"
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=1
export TRACE_STACK_USAGE=1
make -j$(nproc)

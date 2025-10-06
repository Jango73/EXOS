#!/bin/bash
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=0
export TRACE_STACK_USAGE=0
export KERNEL_FILE="exos.bin"
export FILE_SYSTEM=ext2
export PAYLOAD_SOURCE="test-payloads/vesa-test.c"
make -j$(nproc)

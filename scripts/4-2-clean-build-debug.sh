#!/bin/bash
export DEBUG_OUTPUT=1
export SCHEDULING_DEBUG_OUTPUT=0
make clean > log/make.log 2>&1
make >> log/make.log 2>&1

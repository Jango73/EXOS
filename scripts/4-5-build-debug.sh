#!/bin/bash
export DEBUG_OUTPUT=1
export CRITICAL_DEBUG_OUTPUT=0
make > log/make.log 2>&1

#!/bin/bash
export DEBUG_OUTPUT=1
make clean > log/make.log 2>&1
make >> log/make.log 2>&1

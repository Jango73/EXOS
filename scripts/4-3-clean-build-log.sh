#!/bin/bash
make clean
make LOG=1 > log/make.log 2>&1

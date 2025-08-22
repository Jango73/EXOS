#!/bin/bash
make clean > log/make.log 2>&1
make >> log/make.log 2>&1

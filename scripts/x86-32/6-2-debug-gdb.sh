#!/bin/bash

gdb-multiarch ./build/x86-32/kernel/exos.elf -ex "set architecture x86-32" -ex "show architecture" -ex "target remote localhost:1234" -ex "break RAMDisk.c:467"

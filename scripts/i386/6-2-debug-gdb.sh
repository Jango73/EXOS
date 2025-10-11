#!/bin/bash

gdb-multiarch ./build/i386/kernel/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234" -ex "break RAMDisk.c:467"

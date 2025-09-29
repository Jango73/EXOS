#!/bin/bash

gdb-multiarch ./kernel/bin/exos.elf -ex "set architecture i386" -ex "show architecture" -ex "target remote localhost:1234" -ex "break RAMDisk.c:467"

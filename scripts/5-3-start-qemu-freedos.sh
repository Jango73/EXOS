cd boot-freedos
qemu-system-i386 -drive format=raw,file=bin/exos_dos.img -drive format=raw,file=bin/exos_extra.img -serial file:debug.log

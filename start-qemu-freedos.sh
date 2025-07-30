cd boot-freedos
make clean
make
qemu-system-i386 -drive format=raw,file=bin/exos_dos.img
